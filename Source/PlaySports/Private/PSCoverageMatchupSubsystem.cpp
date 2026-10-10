#include "PSCoverageMatchupSubsystem.h"
#include "PSAIFieldSnapshot.h"
#include "PSDataIngestion.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSFieldDimensions.h"
#include "PSHealthComponent.h"
#include "PSOffenseController.h"
#include "PSPlatformTiers.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayOrchestrator.h"
#include "PSPlaybookData.h"
#include "PSPlayerPawn.h"
#include "PSRouteRunnerComponent.h"
#include "PSRouteRunning.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

DECLARE_CYCLE_STAT(TEXT("Coverage matchups"), STAT_PSAICoverageMatchups, STATGROUP_PSAI);

namespace PSCoverageMatchupPrivate
{
    bool IsStanding(const APSPlayerPawn* Pawn)
    {
        const UPSHealthComponent* Health = Pawn ? Pawn->GetHealthComponent() : nullptr;
        return Pawn && !(Health && Health->IsDowned());
    }

    /** A receiver the coverage plays: a standing wide receiver, tight end or back of the offense
     *  without the ball. Role is the pawn's role from the field snapshot. */
    bool IsEligible(const APSPlayerPawn* Pawn, EPlayerRole Role)
    {
        return Pawn && Pawn->TeamSide == EPSTeamSide::Offense && !Pawn->HasPossession() && IsStanding(Pawn)
            && (Role == EPlayerRole::WideReceiver || Role == EPlayerRole::TightEnd || Role == EPlayerRole::RunningBack);
    }

    /** The route-running tuning Receiver's release and breaks are judged by (Epic 68). */
    FRouteRunningTuningRow RouteTuningOf(const APSPlayerPawn* Receiver)
    {
        const APSOffenseController* Controller = Receiver ? Cast<APSOffenseController>(Receiver->GetController()) : nullptr;
        UPSRouteRunnerComponent* Runner = Controller ? Controller->GetRouteRunner() : nullptr;
        return Runner ? Runner->GetTuning() : FRouteRunningTuningRow();
    }

    FName FreeRoleName(EPSFreeRole Role)
    {
        return Role == EPSFreeRole::DeepMiddle ? FName(TEXT("DeepMiddle")) : FName(TEXT("Robber"));
    }
}

void UPSCoverageMatchupSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>();
    if (Bus)
    {
        Bus->OnSnapMC.AddUObject(this, &UPSCoverageMatchupSubsystem::HandleSnap);
        Bus->OnThrowMC.AddUObject(this, &UPSCoverageMatchupSubsystem::HandleThrow);
        Bus->OnCatchMC.AddUObject(this, &UPSCoverageMatchupSubsystem::HandleCatch);
        Bus->OnPhaseChangeMC.AddUObject(this, &UPSCoverageMatchupSubsystem::HandlePhaseChange);
        Bus->OnRouteRunningMC.AddUObject(this, &UPSCoverageMatchupSubsystem::HandleRouteRunning);
        BoundBus = Bus;
    }
}

void UPSCoverageMatchupSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnThrowMC.RemoveAll(this);
        Bus->OnCatchMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
        Bus->OnRouteRunningMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

bool UPSCoverageMatchupSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPSCoverageMatchupSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSCoverageMatchupSubsystem, STATGROUP_Tickables);
}

void UPSCoverageMatchupSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    // Decide at the AI's rate (the platform tier's, Epic 129). Before the snap the walk up to
    // press steers every frame: movement input is used up each frame.
    SinceUpdate += DeltaTime;
    if (SinceUpdate >= PSPlatformTiers::GetActiveTier().AIDecisionInterval)
    {
        UpdateCoverage(SinceUpdate);
        SinceUpdate = 0.f;
    }
    else if (!bLive)
    {
        WalkToPress();
    }
}

FString UPSCoverageMatchupSubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/coverage_matchups.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSCoverageMatchupTuning& UPSCoverageMatchupSubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSCoverageMatchupSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSCoverageMatchupTuning Loaded;
    if (!Ingestion->LoadCoverageMatchupTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCoverageMatchupSubsystem: Could not load coverage tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    const TArray<FString> Problems = ValidateTuning(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCoverageMatchupSubsystem: %s"), *Problem);
    }
    Tuning = Loaded;
    return Problems.Num() == 0;
}

void UPSCoverageMatchupSubsystem::SetTuning(const FPSCoverageMatchupTuning& InTuning)
{
    Tuning = InTuning;
    bTuningLoaded = true;
}

TArray<FString> UPSCoverageMatchupSubsystem::ValidateTuning(const FPSCoverageMatchupTuning& InTuning)
{
    struct FNamedTuning
    {
        const TCHAR* Name;
        float Value;
    };
    TArray<FString> Problems;
    const FNamedTuning Distances[] = {
        { TEXT("PressDepth"), InTuning.PressDepth }, { TEXT("PressShade"), InTuning.PressShade }, { TEXT("PressAlignWidth"), InTuning.PressAlignWidth },
        { TEXT("PreSnapArrivalRadius"), InTuning.PreSnapArrivalRadius }, { TEXT("PressCushion"), InTuning.PressCushion },
        { TEXT("PressBeatenSeconds"), InTuning.PressBeatenSeconds }, { TEXT("LeverageShade"), InTuning.LeverageShade },
        { TEXT("LeverageLostMargin"), InTuning.LeverageLostMargin }, { TEXT("LeverageRegainMargin"), InTuning.LeverageRegainMargin },
        { TEXT("AwayFromLeverageBonus"), InTuning.AwayFromLeverageBonus }, { TEXT("MaxOutOfPhaseSeconds"), InTuning.MaxOutOfPhaseSeconds },
        { TEXT("CarryMargin"), InTuning.CarryMargin }, { TEXT("ZoneCarryCushion"), InTuning.ZoneCarryCushion },
        { TEXT("VerticalCarryDepth"), InTuning.VerticalCarryDepth }, { TEXT("DeepZoneDepth"), InTuning.DeepZoneDepth },
        { TEXT("OverTopCushion"), InTuning.OverTopCushion }, { TEXT("DeepHelpWidth"), InTuning.DeepHelpWidth },
        { TEXT("FreeDeepDepth"), InTuning.FreeDeepDepth }, { TEXT("RobberDepth"), InTuning.RobberDepth },
        { TEXT("RobberRadius"), InTuning.RobberRadius }, { TEXT("ContactRadius"), InTuning.ContactRadius }, { TEXT("TrailMargin"), InTuning.TrailMargin }
    };
    for (const FNamedTuning& Entry : Distances)
    {
        if (Entry.Value < 0.f)
        {
            Problems.Add(FString::Printf(TEXT("%s is %.2f; it must be 0 or more"), Entry.Name, Entry.Value));
        }
    }
    const FNamedTuning Shares[] = {
        { TEXT("PressMinJamChance"), InTuning.PressMinJamChance }, { TEXT("LeverageBiteBonus"), InTuning.LeverageBiteBonus },
        { TEXT("BreakMinLateral"), InTuning.BreakMinLateral }, { TEXT("IntoLeverageSeparationScale"), InTuning.IntoLeverageSeparationScale },
        { TEXT("DeepShadeWeight"), InTuning.DeepShadeWeight }, { TEXT("RobberJumpWeight"), InTuning.RobberJumpWeight },
        { TEXT("FlagChance"), InTuning.FlagChance }
    };
    for (const FNamedTuning& Entry : Shares)
    {
        if (Entry.Value < 0.f || Entry.Value > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("%s is %.2f; it must be from 0 to 1"), Entry.Name, Entry.Value));
        }
    }
    if (InTuning.SeparationRecoverySpeed <= 0.f)
    {
        Problems.Add(TEXT("SeparationRecoverySpeed must be above 0"));
    }
    if (InTuning.FieldWidth <= 0.f)
    {
        Problems.Add(TEXT("FieldWidth must be above 0"));
    }
    TSet<FString> Names;
    for (const FPSCoverageShellRule& Rule : InTuning.Shells)
    {
        if (Rule.Shell.IsEmpty())
        {
            Problems.Add(TEXT("A shell rule has no Shell"));
        }
        else if (Names.Contains(Rule.Shell))
        {
            Problems.Add(FString::Printf(TEXT("Shell %s is listed twice"), *Rule.Shell));
        }
        Names.Add(Rule.Shell);
        if (Rule.FreeRoles.Contains(EPSFreeRole::None))
        {
            Problems.Add(FString::Printf(TEXT("Shell %s lists None as a free role"), *Rule.Shell));
        }
    }
    return Problems;
}

const FPSCoverageShellRule& UPSCoverageMatchupSubsystem::GetShellRule(const FString& Shell)
{
    const FPSCoverageMatchupTuning& Settings = GetTuning();
    const FPSCoverageShellRule* Rule = Settings.Shells.FindByPredicate([&Shell](const FPSCoverageShellRule& Candidate) { return Candidate.Shell == Shell; });
    return Rule ? *Rule : Settings.DefaultShell;
}

FString UPSCoverageMatchupSubsystem::GetCalledShell() const
{
    UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    FPSPlayDefinition Play;
    return PlayCall && PlayCall->GetCall(false).IsSet() && PlayCall->GetDefensivePlayToRun(Play) ? Play.CoverageShell : FString();
}

const FPSCoverageShellRule& UPSCoverageMatchupSubsystem::GetPlayedShellRule()
{
    // The defense's call is settled by the snap; read it once, after the snap's own handlers.
    if (!bShellRead)
    {
        PlayedShell = GetCalledShell();
        bShellRead = true;
    }
    return GetShellRule(PlayedShell);
}

UPSPlayCallSubsystem* UPSCoverageMatchupSubsystem::GetPlayCall() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
}

const TArray<APSPlayerPawn*>& UPSCoverageMatchupSubsystem::GetFieldPawns() const
{
    return UPSAIFieldSnapshot::GetFieldPawns(GetWorld());
}

APSPlayerPawn* UPSCoverageMatchupSubsystem::FindPawnByName(const FString& DisplayName, bool bOffense) const
{
    const EPSTeamSide Side = bOffense ? EPSTeamSide::Offense : EPSTeamSide::Defense;
    for (APSPlayerPawn* Pawn : GetFieldPawns())
    {
        if (Pawn && Pawn->TeamSide == Side && Pawn->GetAttributes().DisplayName == DisplayName)
        {
            return Pawn;
        }
    }
    return nullptr;
}

UPSDefenderAIComponent* UPSCoverageMatchupSubsystem::DefenderAIOf(const APSPlayerPawn* Pawn) const
{
    const APSDefenseController* Controller = Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr;
    return Controller ? Controller->GetDefenderAI() : nullptr;
}

float UPSCoverageMatchupSubsystem::LeverageSide(EPSLeverage Leverage, float ReceiverY, float MiddleY)
{
    const float Inside = ReceiverY > MiddleY ? -1.f : 1.f;
    return Leverage == EPSLeverage::Inside ? Inside : -Inside;
}

void UPSCoverageMatchupSubsystem::UpdateCoverage(float DeltaSeconds)
{
    SCOPE_CYCLE_COUNTER(STAT_PSAICoverageMatchups);
    GetTuning();
    if (!bLive)
    {
        PlanPress();
        WalkToPress();
        return;
    }
    if (!IsCoverageLive())
    {
        return;
    }
    UpdateManMatchups();
    UpdateFreeRoles();
    UpdateZones();
    CheckInterference();
}

bool UPSCoverageMatchupSubsystem::IsCoverageLive() const
{
    if (!bLive)
    {
        return false;
    }
    if (bBallInAir)
    {
        return true;
    }
    // Once a back or receiver has the ball, or the quarterback is past the line, it is a chase.
    UPSAIFieldSnapshot* Field = GetWorld() ? GetWorld()->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    const APSPlayerPawn* Carrier = Field ? Field->FindBallCarrier() : nullptr;
    return Carrier && Carrier->TeamSide == EPSTeamSide::Offense && Carrier->GetAttributes().Role == EPlayerRole::Quarterback
        && Carrier->GetActorLocation().X <= LineOfScrimmage.X;
}

// --- Press ---

void UPSCoverageMatchupSubsystem::PlanPress()
{
    PressPlans.Reset();
    UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    FPSPlayDefinition Play;
    if (!PlayCall || !PlayCall->IsCallWindowOpen() || !PlayCall->GetCall(false).IsSet() || !PlayCall->GetDefensivePlayToRun(Play))
    {
        return;
    }
    const FPSCoverageShellRule& Rule = GetShellRule(Play.CoverageShell);
    UPSAIFieldSnapshot* Field = GetWorld()->GetSubsystem<UPSAIFieldSnapshot>();
    if (!Rule.bPress || !Field)
    {
        return;
    }
    const FPSCoverageMatchupTuning& Settings = GetTuning();
    const TArray<APSPlayerPawn*>& Pawns = Field->GetPawns();
    const TArray<EPlayerRole>& Roles = Field->GetRoles();
    const APSPlayerPawn* Ball = Field->FindBallCarrier();
    const float MiddleY = Ball ? Ball->GetActorLocation().Y : 0.f;

    TMap<EPlayerRole, int32> RoleCounts;
    TSet<const APSPlayerPawn*> Taken;
    for (int32 Index = 0; Index < Pawns.Num(); ++Index)
    {
        APSPlayerPawn* Defender = Pawns[Index];
        if (!Defender || Defender->TeamSide != EPSTeamSide::Defense)
        {
            continue;
        }
        // The slot UPSPlayOrchestrator will hand him at the snap: the field's order, role by role.
        int32& RoleIndex = RoleCounts.FindOrAdd(Roles[Index]);
        const FPSPlayAssignment* Slot = UPSPlayOrchestrator::FindAssignmentSlot(Play, Roles[Index], RoleIndex);
        ++RoleIndex;
        if (!Slot || Slot->Kind != EPSAssignmentKind::ManCoverage || Roles[Index] != EPlayerRole::DefensiveBack
            || Defender->IsUserControlled() || !PSCoverageMatchupPrivate::IsStanding(Defender))
        {
            continue;
        }

        // The receiver he is lined up over: the nearest across the field.
        APSPlayerPawn* Receiver = nullptr;
        float Nearest = Settings.PressAlignWidth;
        for (int32 Other = 0; Other < Pawns.Num(); ++Other)
        {
            APSPlayerPawn* Candidate = Pawns[Other];
            const bool bSplitOut = Roles[Other] == EPlayerRole::WideReceiver || Roles[Other] == EPlayerRole::TightEnd;
            if (!Candidate || Candidate->TeamSide != EPSTeamSide::Offense || !bSplitOut || Taken.Contains(Candidate) || !PSCoverageMatchupPrivate::IsStanding(Candidate))
            {
                continue;
            }
            const float Across = FMath::Abs(Candidate->GetActorLocation().Y - Defender->GetActorLocation().Y);
            if (Across <= Nearest)
            {
                Nearest = Across;
                Receiver = Candidate;
            }
        }
        if (!Receiver)
        {
            continue;
        }

        // He presses only a release he is likely to win: Epic 68's model, from his side.
        const float JamChance = 1.f - PSRouteRunning::ReleaseWinChance(Receiver->GetAttributes(), Defender->GetAttributes(), PSCoverageMatchupPrivate::RouteTuningOf(Receiver));
        if (JamChance < Settings.PressMinJamChance)
        {
            continue;
        }
        Taken.Add(Receiver);
        FPressPlan Plan;
        Plan.Defender = Defender;
        Plan.Receiver = Receiver;
        Plan.Side = LeverageSide(Rule.Leverage, Receiver->GetActorLocation().Y, MiddleY);
        PressPlans.Add(FObjectKey(Defender), Plan);
    }
}

void UPSCoverageMatchupSubsystem::WalkToPress()
{
    const FPSCoverageMatchupTuning& Settings = GetTuning();
    for (const TPair<FObjectKey, FPressPlan>& Pair : PressPlans)
    {
        APSPlayerPawn* Defender = Pair.Value.Defender.Get();
        APSPlayerPawn* Receiver = Pair.Value.Receiver.Get();
        FVector Spot;
        if (!Defender || Defender->IsUserControlled() || !GetPressPlan(Defender, Receiver, Spot))
        {
            continue;
        }
        FVector ToSpot = Spot - Defender->GetActorLocation();
        ToSpot.Z = 0.f;
        if (ToSpot.Size() > Settings.PreSnapArrivalRadius)
        {
            Defender->AddMovementInput(ToSpot.GetSafeNormal(), 1.f);
        }
    }
}

bool UPSCoverageMatchupSubsystem::GetPressPlan(const APSPlayerPawn* Defender, APSPlayerPawn*& OutReceiver, FVector& OutSpot) const
{
    const FPressPlan* Plan = Defender ? PressPlans.Find(FObjectKey(Defender)) : nullptr;
    APSPlayerPawn* Receiver = Plan ? Plan->Receiver.Get() : nullptr;
    if (!Receiver)
    {
        return false;
    }
    // In front of him, shaded to the leverage side, wherever he stands now (a receiver in
    // motion takes his presser with him).
    OutReceiver = Receiver;
    OutSpot = Receiver->GetActorLocation() + FVector(Tuning.PressDepth, Plan->Side * Tuning.PressShade, 0.f);
    OutSpot.Z = Defender->GetActorLocation().Z;
    return true;
}

APSPlayerPawn* UPSCoverageMatchupSubsystem::FindPlannedPresser(const APSPlayerPawn* Receiver) const
{
    for (const TPair<FObjectKey, FPressPlan>& Pair : PressPlans)
    {
        if (Receiver && Pair.Value.Receiver.Get() == Receiver)
        {
            return Pair.Value.Defender.Get();
        }
    }
    return nullptr;
}

APSPlayerPawn* UPSCoverageMatchupSubsystem::GetPlannedReceiver(const APSPlayerPawn* Defender) const
{
    const FPressPlan* Plan = Defender ? PressPlans.Find(FObjectKey(Defender)) : nullptr;
    return Plan ? Plan->Receiver.Get() : nullptr;
}

bool UPSCoverageMatchupSubsystem::WonJam(const APSPlayerPawn* Defender, const APSPlayerPawn* Receiver) const
{
    const FObjectKey* Presser = Receiver ? WonJams.Find(FObjectKey(Receiver)) : nullptr;
    return Presser && Defender && *Presser == FObjectKey(Defender);
}

void UPSCoverageMatchupSubsystem::ResolveRelease(const FPSTelemetryRouteEvent& Event)
{
    // The release contest is the receiver's route runner's (Epic 68); this is the presser's
    // side of it. A presser who loses is out of phase; one who wins trails his man tight.
    APSPlayerPawn* Receiver = FindPawnByName(Event.ReceiverName, true);
    APSPlayerPawn* Presser = Event.DefenderName.IsEmpty() ? nullptr : FindPawnByName(Event.DefenderName, false);
    const APSDefenseController* Controller = Presser ? Cast<APSDefenseController>(Presser->GetController()) : nullptr;
    if (!Receiver || !Controller || Controller->GetAssignment() != EPSDefensiveAssignmentType::ManCoverage)
    {
        return;
    }
    if (Event.Outcome == FName(TEXT("Win")))
    {
        Publish(EPSCoverageEventKind::Press, Presser, Receiver, TEXT("Beaten"), GetTuning().PressBeatenSeconds, Receiver->GetActorLocation());
        return;
    }
    WonJams.Add(FObjectKey(Receiver), FObjectKey(Presser));
    const FName Outcome = Event.Outcome == FName(TEXT("Reroute")) ? FName(TEXT("Rerouted")) : FName(TEXT("Jammed"));
    Publish(EPSCoverageEventKind::Press, Presser, Receiver, Outcome, 0.f, Receiver->GetActorLocation());
}

// --- Leverage ---

void UPSCoverageMatchupSubsystem::UpdateManMatchups()
{
    const FPSCoverageMatchupTuning& Settings = GetTuning();
    const FPSCoverageShellRule& Rule = GetPlayedShellRule();
    TSet<FObjectKey> Seen;
    for (APSPlayerPawn* Defender : GetFieldPawns())
    {
        if (!Defender || Defender->TeamSide != EPSTeamSide::Defense)
        {
            continue;
        }
        // Who covers whom is the defense AI's; how he plays him is this.
        const UPSDefenderAIComponent* AI = DefenderAIOf(Defender);
        APSPlayerPawn* Receiver = AI && AI->GetAction() == EPSDefenderAction::Cover ? AI->GetCoveredReceiver() : nullptr;
        if (!Receiver)
        {
            continue;
        }
        const FObjectKey Key(Defender);
        Seen.Add(Key);
        FManMatchup* Matchup = ManMatchups.Find(Key);
        if (!Matchup || Matchup->Receiver.Get() != Receiver)
        {
            FManMatchup Fresh;
            Fresh.Defender = Defender;
            Fresh.Receiver = Receiver;
            Fresh.Leverage = Rule.Leverage;
            const FPressPlan* Plan = PressPlans.Find(Key);
            Fresh.Side = Plan && Plan->Receiver.Get() == Receiver ? Plan->Side : LeverageSide(Rule.Leverage, Receiver->GetActorLocation().Y, LineOfScrimmage.Y);
            Matchup = &ManMatchups.Add(Key, Fresh);
        }

        // Held until the receiver crosses his face to that side; his again once he is back.
        const float Shade = (Defender->GetActorLocation().Y - Receiver->GetActorLocation().Y) * Matchup->Side;
        if (Matchup->bHeld && Shade < -Settings.LeverageLostMargin)
        {
            Matchup->bHeld = false;
            Publish(EPSCoverageEventKind::Leverage, Defender, Receiver, TEXT("Lost"), 0.f, Receiver->GetActorLocation());
        }
        else if (!Matchup->bHeld && Shade > Settings.LeverageRegainMargin)
        {
            Matchup->bHeld = true;
            Publish(EPSCoverageEventKind::Leverage, Defender, Receiver, TEXT("Regained"), 0.f, Receiver->GetActorLocation());
        }
    }
    for (auto It = ManMatchups.CreateIterator(); It; ++It)
    {
        if (!Seen.Contains(It.Key()))
        {
            It.RemoveCurrent();
        }
    }
}

bool UPSCoverageMatchupSubsystem::GetLeverage(const APSPlayerPawn* Defender, const APSPlayerPawn* Receiver, EPSLeverage& OutLeverage, float& OutSide, bool& bOutHeld) const
{
    const FManMatchup* Matchup = Defender ? ManMatchups.Find(FObjectKey(Defender)) : nullptr;
    if (!bLive || !Matchup || !Receiver || Matchup->Receiver.Get() != Receiver)
    {
        return false;
    }
    OutLeverage = Matchup->Leverage;
    OutSide = Matchup->Side;
    bOutHeld = Matchup->bHeld;
    return true;
}

bool UPSCoverageMatchupSubsystem::GetCoverTarget(const APSPlayerPawn* Defender, const APSPlayerPawn* Receiver, const FVector& ReceiverLead, float OffCushion, FVector& OutTarget) const
{
    EPSLeverage Leverage = EPSLeverage::Inside;
    float Side = 1.f;
    bool bHeld = true;
    if (!GetLeverage(Defender, Receiver, Leverage, Side, bHeld))
    {
        return false;
    }
    const float Cushion = WonJam(Defender, Receiver) ? Tuning.PressCushion : OffCushion;
    OutTarget = Receiver->GetActorLocation() + ReceiverLead + FVector(Cushion, Side * Tuning.LeverageShade, 0.f);
    return true;
}

float UPSCoverageMatchupSubsystem::GetLeverageBiteBonus(const APSPlayerPawn* Defender, const APSPlayerPawn* Receiver, const FVector& FakeDirection) const
{
    EPSLeverage Leverage = EPSLeverage::Inside;
    float Side = 1.f;
    bool bHeld = false;
    const FVector Fake = FakeDirection.GetSafeNormal2D();
    if (!GetLeverage(Defender, Receiver, Leverage, Side, bHeld) || !bHeld || FMath::Abs(Fake.Y) < Tuning.BreakMinLateral)
    {
        return 0.f;
    }
    // A fake toward the side he plays is the break he is sitting on.
    return Fake.Y * Side > 0.f ? Tuning.LeverageBiteBonus : -Tuning.LeverageBiteBonus;
}

void UPSCoverageMatchupSubsystem::ResolveBreak(const FPSTelemetryRouteEvent& Event)
{
    const FManMatchup* Found = nullptr;
    for (const TPair<FObjectKey, FManMatchup>& Pair : ManMatchups)
    {
        const APSPlayerPawn* Receiver = Pair.Value.Receiver.Get();
        if (Receiver && Receiver->GetAttributes().DisplayName == Event.ReceiverName)
        {
            Found = &Pair.Value;
            break;
        }
    }
    APSPlayerPawn* Defender = Found ? Found->Defender.Get() : nullptr;
    APSPlayerPawn* Receiver = Found ? Found->Receiver.Get() : nullptr;
    if (!Defender || !Receiver)
    {
        return;
    }

    // The break's separation (Epic 68), then the leverage: into it he is sitting on the break,
    // away from it he has further to go. He is out of phase while he makes it up.
    const FPSCoverageMatchupTuning& Settings = GetTuning();
    float Gain = PSRouteRunning::BreakSeparationGain(Receiver->GetAttributes().Agility, Defender->GetAttributes().Agility, PSCoverageMatchupPrivate::RouteTuningOf(Receiver));
    const FVector Direction = Event.Direction.GetSafeNormal2D();
    FName Outcome = TEXT("Straight");
    if (Found->bHeld && FMath::Abs(Direction.Y) >= Settings.BreakMinLateral)
    {
        if (Direction.Y * Found->Side > 0.f)
        {
            Gain *= Settings.IntoLeverageSeparationScale;
            Outcome = TEXT("IntoLeverage");
        }
        else
        {
            Gain += Settings.AwayFromLeverageBonus;
            Outcome = TEXT("AwayFromLeverage");
        }
    }
    const float Seconds = FMath::Min(Settings.MaxOutOfPhaseSeconds, Gain / FMath::Max(KINDA_SMALL_NUMBER, Settings.SeparationRecoverySpeed));
    Publish(EPSCoverageEventKind::Break, Defender, Receiver, Outcome, Seconds, Receiver->GetActorLocation());
}

// --- Zones and safety help ---

void UPSCoverageMatchupSubsystem::UpdateFreeRoles()
{
    const FPSCoverageShellRule& Rule = GetPlayedShellRule();
    if (FreeRolesTaken >= Rule.FreeRoles.Num())
    {
        return;
    }
    // The man defenders with nobody left to cover (the defense AI has them on their spots) who
    // have no free role yet, deepest first: the deepest takes the shell's first role.
    TArray<APSPlayerPawn*> LeftOver;
    for (APSPlayerPawn* Defender : GetFieldPawns())
    {
        const APSDefenseController* Controller = Defender && Defender->TeamSide == EPSTeamSide::Defense ? Cast<APSDefenseController>(Defender->GetController()) : nullptr;
        const UPSDefenderAIComponent* AI = Controller ? Controller->GetDefenderAI() : nullptr;
        const FZoneState* Existing = AI ? Zones.Find(FObjectKey(Defender)) : nullptr;
        if (AI && Controller->GetAssignment() == EPSDefensiveAssignmentType::ManCoverage && AI->GetAction() == EPSDefenderAction::Zone && !AI->GetCoveredReceiver()
            && !(Existing && Existing->FreeRole != EPSFreeRole::None))
        {
            LeftOver.Add(Defender);
        }
    }
    const float MiddleY = LineOfScrimmage.Y;
    LeftOver.Sort([MiddleY](const APSPlayerPawn& A, const APSPlayerPawn& B)
    {
        const FVector AtA = A.GetActorLocation();
        const FVector AtB = B.GetActorLocation();
        return AtA.X != AtB.X ? AtA.X > AtB.X : FMath::Abs(AtA.Y - MiddleY) < FMath::Abs(AtB.Y - MiddleY);
    });

    const FPSCoverageMatchupTuning& Settings = GetTuning();
    for (APSPlayerPawn* Defender : LeftOver)
    {
        if (FreeRolesTaken >= Rule.FreeRoles.Num())
        {
            return;
        }
        FZoneState& Zone = Zones.FindOrAdd(FObjectKey(Defender));
        Zone.Defender = Defender;
        Zone.FreeRole = Rule.FreeRoles[FreeRolesTaken++];
        const float Depth = Zone.FreeRole == EPSFreeRole::DeepMiddle ? Settings.FreeDeepDepth : Settings.RobberDepth;
        Zone.BaseSpot = FVector(LineOfScrimmage.X + Depth, LineOfScrimmage.Y, Defender->GetActorLocation().Z);
        Zone.Spot = Zone.BaseSpot;
        Publish(EPSCoverageEventKind::FreeRole, Defender, nullptr, PSCoverageMatchupPrivate::FreeRoleName(Zone.FreeRole), 0.f, Zone.BaseSpot);
    }
}

void UPSCoverageMatchupSubsystem::UpdateZones()
{
    const FPSCoverageMatchupTuning& Settings = GetTuning();
    UPSAIFieldSnapshot* Field = GetWorld()->GetSubsystem<UPSAIFieldSnapshot>();
    if (!Field)
    {
        return;
    }
    const TArray<APSPlayerPawn*>& Pawns = Field->GetPawns();
    const TArray<EPlayerRole>& Roles = Field->GetRoles();

    // Who plays a zone now, and where.
    for (TPair<FObjectKey, FZoneState>& Pair : Zones)
    {
        Pair.Value.bActive = false;
    }
    for (APSPlayerPawn* Defender : Pawns)
    {
        UPSDefenderAIComponent* AI = Defender && Defender->TeamSide == EPSTeamSide::Defense ? DefenderAIOf(Defender) : nullptr;
        if (!AI || AI->GetAction() != EPSDefenderAction::Zone || !PSCoverageMatchupPrivate::IsStanding(Defender))
        {
            continue;
        }
        const FObjectKey Key(Defender);
        FZoneState& Zone = Zones.FindOrAdd(Key);
        Zone.Defender = Defender;
        Zone.bActive = true;
        Zone.Radius = AI->GetTuning().ZoneRadius;
        if (Zone.FreeRole == EPSFreeRole::None)
        {
            Zone.BaseSpot = AI->GetZoneSpot();
        }
        Zone.bDeep = Zone.FreeRole == EPSFreeRole::DeepMiddle
            || (Zone.FreeRole == EPSFreeRole::None && Zone.BaseSpot.X - LineOfScrimmage.X >= Settings.DeepZoneDepth);
        if (Zone.bDeep)
        {
            DeepRoster.Add(Key);
        }
        if (!Zone.bRotated)
        {
            Zone.Spot = Zone.BaseSpot;
        }
    }

    // The receivers someone already has: carried in a zone, or in man coverage.
    TSet<const APSPlayerPawn*> Covered;
    for (TPair<FObjectKey, FZoneState>& Pair : Zones)
    {
        if (!Pair.Value.bActive)
        {
            // He left his zone (to a man, to the ball): he carries nobody.
            Pair.Value.Carried.Reset();
            Pair.Value.bVertical = false;
        }
        else if (const APSPlayerPawn* Carried = Pair.Value.Carried.Get())
        {
            Covered.Add(Carried);
        }
    }
    for (const TPair<FObjectKey, FManMatchup>& Pair : ManMatchups)
    {
        if (const APSPlayerPawn* Man = Pair.Value.Receiver.Get())
        {
            Covered.Add(Man);
        }
    }

    // A carried receiver who leaves the zone: to the zone he runs into, on with him if he goes
    // vertical with nobody there, or let go underneath.
    for (TPair<FObjectKey, FZoneState>& Pair : Zones)
    {
        FZoneState& Zone = Pair.Value;
        APSPlayerPawn* Receiver = Zone.Carried.Get();
        if (!Zone.bActive || !Receiver)
        {
            continue;
        }
        if (!PSCoverageMatchupPrivate::IsStanding(Receiver) || Receiver->HasPossession())
        {
            Zone.Carried.Reset();
            Zone.bVertical = false;
            Covered.Remove(Receiver);
            continue;
        }
        const FVector At = Receiver->GetActorLocation();
        if (FVector::Dist2D(At, Zone.Spot) <= Zone.Radius + Settings.CarryMargin)
        {
            continue;
        }
        FZoneState* Next = nullptr;
        float NextDistance = TNumericLimits<float>::Max();
        for (TPair<FObjectKey, FZoneState>& Other : Zones)
        {
            FZoneState& Candidate = Other.Value;
            if (&Candidate == &Zone || !Candidate.bActive || Candidate.Carried.IsValid() || Candidate.FreeRole == EPSFreeRole::Robber)
            {
                continue;
            }
            const float Distance = FVector::Dist2D(At, Candidate.Spot);
            if (Distance <= Candidate.Radius && Distance < NextDistance)
            {
                NextDistance = Distance;
                Next = &Candidate;
            }
        }
        if (Next)
        {
            Next->Carried = Receiver;
            Next->bVertical = false;
            Zone.Carried.Reset();
            Zone.bVertical = false;
            Publish(EPSCoverageEventKind::HandOff, Zone.Defender.Get(), Receiver, NAME_None, 0.f, At, Next->Defender.Get());
        }
        else if (At.X > Zone.Spot.X + Settings.VerticalCarryDepth)
        {
            if (!Zone.bVertical)
            {
                Zone.bVertical = true;
                Publish(EPSCoverageEventKind::Carry, Zone.Defender.Get(), Receiver, TEXT("Vertical"), 0.f, At);
            }
        }
        else
        {
            Zone.Carried.Reset();
            Zone.bVertical = false;
            Covered.Remove(Receiver);
            Publish(EPSCoverageEventKind::PassOff, Zone.Defender.Get(), Receiver, TEXT("Underneath"), 0.f, At);
        }
    }

    // A zone defender with nobody picks up the receiver who comes into his zone. A robber reads
    // instead (GetZoneTarget).
    for (TPair<FObjectKey, FZoneState>& Pair : Zones)
    {
        FZoneState& Zone = Pair.Value;
        if (!Zone.bActive || Zone.Carried.IsValid() || Zone.FreeRole == EPSFreeRole::Robber)
        {
            continue;
        }
        APSPlayerPawn* Nearest = nullptr;
        float NearestDistance = Zone.Radius;
        for (int32 Index = 0; Index < Pawns.Num(); ++Index)
        {
            APSPlayerPawn* Candidate = Pawns[Index];
            if (!PSCoverageMatchupPrivate::IsEligible(Candidate, Roles[Index]) || Covered.Contains(Candidate))
            {
                continue;
            }
            const float Distance = FVector::Dist2D(Candidate->GetActorLocation(), Zone.Spot);
            if (Distance <= NearestDistance)
            {
                NearestDistance = Distance;
                Nearest = Candidate;
            }
        }
        if (Nearest)
        {
            Zone.Carried = Nearest;
            Zone.bVertical = false;
            Covered.Add(Nearest);
            Publish(EPSCoverageEventKind::Carry, Zone.Defender.Get(), Nearest, TEXT("Zone"), 0.f, Nearest->GetActorLocation());
        }
    }

    UpdateRotation();
}

void UPSCoverageMatchupSubsystem::UpdateRotation()
{
    // Rotation integrity: when a deep defender leaves the deep zones, those still in them split
    // the field's width between them, at the deepest of their depths.
    TArray<FZoneState*> OnStation;
    for (TPair<FObjectKey, FZoneState>& Pair : Zones)
    {
        if (Pair.Value.bActive && Pair.Value.bDeep)
        {
            OnStation.Add(&Pair.Value);
        }
    }
    if (OnStation.Num() == 0 || OnStation.Num() >= DeepRoster.Num() || OnStation.Num() == RotatedFor)
    {
        return;
    }
    RotatedFor = OnStation.Num();
    OnStation.Sort([](const FZoneState& A, const FZoneState& B) { return A.BaseSpot.Y < B.BaseSpot.Y; });
    double Depth = 0.0;
    for (const FZoneState* Zone : OnStation)
    {
        Depth = FMath::Max(Depth, Zone->BaseSpot.X);
    }
    const FPSCoverageMatchupTuning& Settings = GetTuning();
    const float Band = Settings.FieldWidth / OnStation.Num();
    for (int32 Index = 0; Index < OnStation.Num(); ++Index)
    {
        FZoneState* Zone = OnStation[Index];
        Zone->Spot = FVector(Depth, LineOfScrimmage.Y - Settings.FieldWidth * 0.5f + Band * (Index + 0.5f), Zone->BaseSpot.Z);
        Zone->bRotated = true;
        Publish(EPSCoverageEventKind::Rotate, Zone->Defender.Get(), nullptr, NAME_None, 0.f, Zone->Spot);
    }
}

bool UPSCoverageMatchupSubsystem::GetZoneTarget(const APSPlayerPawn* Defender, FVector& OutTarget) const
{
    const FZoneState* Zone = Defender ? Zones.Find(FObjectKey(Defender)) : nullptr;
    UPSAIFieldSnapshot* Field = GetWorld() ? GetWorld()->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    if (!bLive || !Zone || !Zone->bActive || !Field)
    {
        return false;
    }
    const TArray<APSPlayerPawn*>& Pawns = Field->GetPawns();
    const TArray<EPlayerRole>& Roles = Field->GetRoles();
    FVector Target = Zone->Spot;

    if (Zone->FreeRole == EPSFreeRole::Robber)
    {
        // He jumps the receiver nearest his spot that crosses his window.
        const APSPlayerPawn* Threat = nullptr;
        float ThreatDistance = Tuning.RobberRadius;
        for (int32 Index = 0; Index < Pawns.Num(); ++Index)
        {
            const float Distance = PSCoverageMatchupPrivate::IsEligible(Pawns[Index], Roles[Index]) ? FVector::Dist2D(Pawns[Index]->GetActorLocation(), Zone->Spot) : TNumericLimits<float>::Max();
            if (Distance <= ThreatDistance)
            {
                ThreatDistance = Distance;
                Threat = Pawns[Index];
            }
        }
        if (Threat)
        {
            Target = FMath::Lerp(Zone->Spot, Threat->GetActorLocation(), Tuning.RobberJumpWeight);
        }
    }
    else if (Zone->bDeep)
    {
        // Over the top: deeper than the deepest receiver in his area, shaded across to him.
        const APSPlayerPawn* Deepest = nullptr;
        for (int32 Index = 0; Index < Pawns.Num(); ++Index)
        {
            const APSPlayerPawn* Candidate = Pawns[Index];
            if (!PSCoverageMatchupPrivate::IsEligible(Candidate, Roles[Index]) || Candidate->GetActorLocation().X <= LineOfScrimmage.X
                || FMath::Abs(Candidate->GetActorLocation().Y - Zone->Spot.Y) > Tuning.DeepHelpWidth)
            {
                continue;
            }
            if (!Deepest || Candidate->GetActorLocation().X > Deepest->GetActorLocation().X)
            {
                Deepest = Candidate;
            }
        }
        if (Deepest)
        {
            const FVector Threat = Deepest->GetActorLocation();
            Target.X = FMath::Max(Zone->Spot.X, Threat.X + Tuning.OverTopCushion);
            Target.Y = FMath::Lerp(Zone->Spot.Y, Threat.Y, Tuning.DeepShadeWeight);
        }
    }
    else if (const APSPlayerPawn* Receiver = Zone->Carried.Get())
    {
        // On the receiver he carries, downfield of him; underneath, no further than his zone lets him.
        Target = Receiver->GetActorLocation() + FVector(Tuning.ZoneCarryCushion, 0.f, 0.f);
        if (!Zone->bVertical)
        {
            FVector Offset = Target - Zone->Spot;
            Offset.Z = 0.f;
            const float Leash = Zone->Radius + Tuning.CarryMargin;
            if (Offset.Size() > Leash)
            {
                Target = Zone->Spot + Offset.GetSafeNormal() * Leash;
            }
        }
    }
    Target.Z = Zone->Spot.Z;
    OutTarget = Target;
    return true;
}

APSPlayerPawn* UPSCoverageMatchupSubsystem::GetCarriedReceiver(const APSPlayerPawn* Defender) const
{
    const FZoneState* Zone = Defender ? Zones.Find(FObjectKey(Defender)) : nullptr;
    return Zone && Zone->bActive ? Zone->Carried.Get() : nullptr;
}

EPSFreeRole UPSCoverageMatchupSubsystem::GetFreeRole(const APSPlayerPawn* Defender) const
{
    const FZoneState* Zone = Defender ? Zones.Find(FObjectKey(Defender)) : nullptr;
    return Zone ? Zone->FreeRole : EPSFreeRole::None;
}

bool UPSCoverageMatchupSubsystem::IsDeepDefender(const APSPlayerPawn* Defender) const
{
    const FZoneState* Zone = Defender ? Zones.Find(FObjectKey(Defender)) : nullptr;
    return Zone && Zone->bActive && Zone->bDeep;
}

bool UPSCoverageMatchupSubsystem::HasRotated(const APSPlayerPawn* Defender) const
{
    const FZoneState* Zone = Defender ? Zones.Find(FObjectKey(Defender)) : nullptr;
    return Zone && Zone->bRotated;
}

// --- Pass interference ---

void UPSCoverageMatchupSubsystem::CheckInterference()
{
    if (!bBallInAir || bInterferenceSeen || TargetReceiverName.IsEmpty())
    {
        return;
    }
    APSPlayerPawn* Receiver = FindPawnByName(TargetReceiverName, true);
    if (!Receiver || !PSCoverageMatchupPrivate::IsStanding(Receiver) || Receiver->GetActorLocation().X <= LineOfScrimmage.X)
    {
        return;
    }
    const FPSCoverageMatchupTuning& Settings = GetTuning();
    const FVector At = Receiver->GetActorLocation();
    const float ReceiverToBall = FVector::Dist2D(At, LandingSpot);
    for (APSPlayerPawn* Defender : GetFieldPawns())
    {
        if (!Defender || Defender->TeamSide != EPSTeamSide::Defense || !PSCoverageMatchupPrivate::IsStanding(Defender))
        {
            continue;
        }
        // In contact with him, and further from the ball than he is: playing through the man,
        // not the ball.
        const FVector DefenderAt = Defender->GetActorLocation();
        if (FVector::Dist2D(DefenderAt, At) > Settings.ContactRadius || FVector::Dist2D(DefenderAt, LandingSpot) <= ReceiverToBall + Settings.TrailMargin)
        {
            continue;
        }
        bInterferenceSeen = true;
        if (Rolls.FRand() < Settings.FlagChance)
        {
            const int32 Yards = FMath::Max(1, FMath::RoundToInt(PSField::CentimetresToYards(At.X - LineOfScrimmage.X)));
            Publish(EPSCoverageEventKind::PassInterference, Defender, Receiver, TEXT("Flag"), 0.f, At, nullptr, Yards);
        }
        return;
    }
}

// --- The bus ---

void UPSCoverageMatchupSubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    // The press plans stay: the receivers' release contests read them on the first step.
    bLive = true;
    bShellRead = false;
    bBallInAir = false;
    bInterferenceSeen = false;
    LineOfScrimmage = Event.LineOfScrimmage;
    LandingSpot = FVector::ZeroVector;
    TargetReceiverName.Reset();
    ManMatchups.Reset();
    Zones.Reset();
    WonJams.Reset();
    DeepRoster.Reset();
    FreeRolesTaken = 0;
    RotatedFor = 0;
    SinceUpdate = 0.f;
    // The officials' calls are seeded from the snap's situation, so a replayed snap calls the same.
    Rolls.Initialize(static_cast<int32>(HashCombine(HashCombine(GetTypeHash(Event.YardLine), GetTypeHash(Event.Down)), GetTypeHash(Event.GameClockSeconds))));
}

void UPSCoverageMatchupSubsystem::HandleThrow(const FPSTelemetryThrowEvent& Event)
{
    bBallInAir = true;
    TargetReceiverName = Event.TargetReceiverName;
    LandingSpot = Event.LandingLocation.IsZero() ? Event.TargetLocation : Event.LandingLocation;
}

void UPSCoverageMatchupSubsystem::HandleCatch(const FPSTelemetryCatchEvent& Event)
{
    bBallInAir = false;
}

void UPSCoverageMatchupSubsystem::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("Scoring") || Event.NewPhase == TEXT("PreSnap"))
    {
        bLive = false;
        bBallInAir = false;
        PressPlans.Reset();
        ManMatchups.Reset();
        Zones.Reset();
        WonJams.Reset();
        DeepRoster.Reset();
    }
}

void UPSCoverageMatchupSubsystem::HandleRouteRunning(const FPSTelemetryRouteEvent& Event)
{
    if (!bLive)
    {
        return;
    }
    if (Event.Kind == EPSRouteEventKind::Release)
    {
        ResolveRelease(Event);
    }
    else if (Event.Kind == EPSRouteEventKind::Break || (Event.Kind == EPSRouteEventKind::OptionRead && Event.Outcome == FName(TEXT("Man"))))
    {
        // An option route's man read is its break.
        ResolveBreak(Event);
    }
}

void UPSCoverageMatchupSubsystem::Publish(EPSCoverageEventKind Kind, const APSPlayerPawn* Defender, const APSPlayerPawn* Receiver, FName Outcome, float Seconds,
    const FVector& Location, const APSPlayerPawn* OtherDefender, int32 YardsPastLine)
{
    UPSTelemetryBus* Bus = BoundBus.Get();
    if (!Bus)
    {
        return;
    }
    FPSTelemetryCoverageEvent Event;
    Event.Kind = Kind;
    Event.DefenderName = Defender ? Defender->GetAttributes().DisplayName : FString();
    Event.ReceiverName = Receiver ? Receiver->GetAttributes().DisplayName : FString();
    Event.OtherDefenderName = OtherDefender ? OtherDefender->GetAttributes().DisplayName : FString();
    Event.Outcome = Outcome;
    Event.Seconds = Seconds;
    Event.Location = Location;
    Event.YardsPastLine = YardsPastLine;
    Bus->PublishCoverage(Event);
}
