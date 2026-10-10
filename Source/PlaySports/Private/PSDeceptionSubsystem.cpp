#include "PSDeceptionSubsystem.h"
#include "PSAIFieldSnapshot.h"
#include "PSDataIngestion.h"
#include "PSDefenseController.h"
#include "PSHealthComponent.h"
#include "PSOffenseController.h"
#include "PSPlatformTiers.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayRecognitionSubsystem.h"
#include "PSPlayerPawn.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

DECLARE_CYCLE_STAT(TEXT("Deception"), STAT_PSAIDeception, STATGROUP_PSAI);

namespace PSDeceptionPrivate
{
    bool IsStanding(const APSPlayerPawn* Pawn)
    {
        const UPSHealthComponent* Health = Pawn ? Pawn->GetHealthComponent() : nullptr;
        return Pawn && !(Health && Health->IsDowned());
    }

    /** A scrimmage call the offense's tendency counts: runs and passes, not kicks or the clock. */
    bool IsScrimmageCategory(const FString& Category)
    {
        return Category == TEXT("Run") || Category == TEXT("ShortPass") || Category == TEXT("DeepPass") || Category == TEXT("PlayAction") || Category == TEXT("Screen");
    }

    bool IsRunOption(EPSDeception Type)
    {
        return Type == EPSDeception::RPO || Type == EPSDeception::ZoneRead || Type == EPSDeception::TripleOption;
    }

    FName ChoiceName(EPSOptionChoice Choice)
    {
        switch (Choice)
        {
        case EPSOptionChoice::Keep:
            return FName(TEXT("Keep"));
        case EPSOptionChoice::Throw:
            return FName(TEXT("Throw"));
        case EPSOptionChoice::Ride:
            return FName(TEXT("Ride"));
        default:
            return FName(TEXT("Give"));
        }
    }
}

void UPSDeceptionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>();
    if (Bus)
    {
        Bus->OnSnapMC.AddUObject(this, &UPSDeceptionSubsystem::HandleSnap);
        Bus->OnPlayCallMC.AddUObject(this, &UPSDeceptionSubsystem::HandlePlayCall);
        Bus->OnPhaseChangeMC.AddUObject(this, &UPSDeceptionSubsystem::HandlePhaseChange);
        BoundBus = Bus;
    }
}

void UPSDeceptionSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnPlayCallMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

bool UPSDeceptionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPSDeceptionSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSDeceptionSubsystem, STATGROUP_Tickables);
}

void UPSDeceptionSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!bLive || bRecognized)
    {
        return;
    }
    // The defense looks for the mesh at the AI's decision rate (the platform tier's, Epic 129).
    SinceUpdate += DeltaTime;
    if (SinceUpdate >= PSPlatformTiers::GetActiveTier().AIDecisionInterval)
    {
        SinceUpdate = 0.f;
        UpdateDefense();
    }
}

FString UPSDeceptionSubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/deception.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSDeceptionTuning& UPSDeceptionSubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSDeceptionSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSDeceptionTuning Loaded;
    if (!Ingestion->LoadDeceptionTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSDeceptionSubsystem: Could not load deception tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    const TArray<FString> Problems = ValidateTuning(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSDeceptionSubsystem: %s"), *Problem);
    }
    Tuning = Loaded;
    return Problems.Num() == 0;
}

void UPSDeceptionSubsystem::SetTuning(const FPSDeceptionTuning& InTuning)
{
    Tuning = InTuning;
    bTuningLoaded = true;
}

TArray<FString> UPSDeceptionSubsystem::ValidateTuning(const FPSDeceptionTuning& InTuning)
{
    TArray<FString> Problems;
    if (InTuning.FakeSeconds < 0.f || InTuning.MeshRideSeconds < 0.f || InTuning.ReadMinSpeed < 0.f
        || InTuning.KeyLineDepth < 0.f || InTuning.PitchReadRadius < 0.f
        || InTuning.PitchWindowDepth < 0.f || InTuning.MeshRecognizeRadius < 0.f || InTuning.ScrapeRadius < 0.f)
    {
        Problems.Add(TEXT("Times and distances must be 0 or more"));
    }
    if (InTuning.DisciplineAwareness < 0.f || InTuning.DisciplineAwareness > 100.f)
    {
        Problems.Add(TEXT("DisciplineAwareness is a rating, 0-100"));
    }
    if (InTuning.TendencyWindow < 1)
    {
        Problems.Add(TEXT("TendencyWindow must be 1 or more"));
    }
    return Problems;
}

float UPSDeceptionSubsystem::GetRunShare() const
{
    if (RecentRuns.Num() == 0)
    {
        return 0.5f;
    }
    int32 Runs = 0;
    for (const bool bRun : RecentRuns)
    {
        Runs += bRun ? 1 : 0;
    }
    return static_cast<float>(Runs) / RecentRuns.Num();
}

UPSPlayCallSubsystem* UPSDeceptionSubsystem::GetPlayCall() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
}

APSPlayerPawn* UPSDeceptionSubsystem::FindOffense(EPlayerRole Role, bool bOnRoute) const
{
    UPSAIFieldSnapshot* Field = GetWorld() ? GetWorld()->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    if (!Field)
    {
        return nullptr;
    }
    const TArray<APSPlayerPawn*>& Pawns = Field->GetPawns();
    const TArray<EPlayerRole>& Roles = Field->GetRoles();
    for (int32 Index = 0; Index < Pawns.Num(); ++Index)
    {
        APSPlayerPawn* Candidate = Pawns[Index];
        if (!Candidate || Candidate->TeamSide != EPSTeamSide::Offense || Roles[Index] != Role || !PSDeceptionPrivate::IsStanding(Candidate))
        {
            continue;
        }
        const APSOffenseController* Controller = Cast<APSOffenseController>(Candidate->GetController());
        if (bOnRoute && !(Controller && Controller->GetRouteWaypointCount() > 0))
        {
            continue;
        }
        return Candidate;
    }
    return nullptr;
}

APSPlayerPawn* UPSDeceptionSubsystem::FindEndManOnLine() const
{
    // The outermost defender on the line on the play side.
    const float Side = PlayDeception.PlaySide < 0 ? -1.f : 1.f;
    APSPlayerPawn* EndMan = nullptr;
    float Widest = 0.f;
    for (APSPlayerPawn* Defender : UPSAIFieldSnapshot::GetFieldPawns(GetWorld()))
    {
        if (!Defender || Defender->TeamSide != EPSTeamSide::Defense || !PSDeceptionPrivate::IsStanding(Defender)
            || FMath::Abs(Defender->GetActorLocation().X - LineOfScrimmage.X) > Tuning.KeyLineDepth)
        {
            continue;
        }
        const float Width = (Defender->GetActorLocation().Y - LineOfScrimmage.Y) * Side;
        if (Width > Widest)
        {
            Widest = Width;
            EndMan = Defender;
        }
    }
    return EndMan;
}

bool UPSDeceptionSubsystem::IsPlayingTheBack(const APSPlayerPawn* Defender, const APSPlayerPawn* Back, const APSPlayerPawn* Passer, float MinSpeed)
{
    if (!Defender || !Back || !Passer)
    {
        return false;
    }
    const FVector Here = Defender->GetActorLocation();
    FVector Heading = Defender->GetVelocity();
    Heading.Z = 0.f;
    if (!Heading.IsNearlyZero() && Heading.Size() >= MinSpeed)
    {
        // On the move: whom he is heading for.
        const FVector Direction = Heading.GetSafeNormal();
        const float TowardBack = FVector::DotProduct(Direction, (Back->GetActorLocation() - Here).GetSafeNormal2D());
        const float TowardPasser = FVector::DotProduct(Direction, (Passer->GetActorLocation() - Here).GetSafeNormal2D());
        return TowardBack > TowardPasser;
    }
    return FVector::Dist2D(Here, Back->GetActorLocation()) < FVector::Dist2D(Here, Passer->GetActorLocation());
}

// --- The quarterback ---

bool UPSDeceptionSubsystem::UpdateFake(APSPlayerPawn* Passer, float TimeSinceSnap)
{
    if (!bLive || bFakeSold || GetPlayDeception().Type != EPSDeception::PlayAction)
    {
        return false;
    }
    if (TimeSinceSnap < GetTuning().FakeSeconds)
    {
        return true;
    }
    SellFake(Passer);
    return false;
}

void UPSDeceptionSubsystem::SellFake(APSPlayerPawn* Passer)
{
    bFakeSold = true;
    const FPSDeceptionTuning& Settings = GetTuning();
    Publish(EPSDeceptionEventKind::Fake, Passer, nullptr, TEXT("Fake"));

    // A run-fit defender who hasn't seen through the fake by now bites (the recognition model,
    // Epic 80: his Awareness and style, against what he expected), and holds on it instead of
    // dropping until he does.
    UPSPlayRecognitionSubsystem* Recognition = UPSPlayRecognitionSubsystem::Get(GetWorld());
    if (!Recognition)
    {
        return;
    }
    for (APSPlayerPawn* Defender : UPSAIFieldSnapshot::GetFieldPawns(GetWorld()))
    {
        const APSDefenseController* Controller = Defender && Defender->TeamSide == EPSTeamSide::Defense ? Cast<APSDefenseController>(Defender->GetController()) : nullptr;
        if (!Controller || Controller->GetAssignment() != EPSDefensiveAssignmentType::RunFit || !PSDeceptionPrivate::IsStanding(Defender))
        {
            continue;
        }
        const float Hold = Recognition->GetBiteSeconds(Defender, Settings.FakeSeconds);
        if (Hold > 0.f)
        {
            Publish(EPSDeceptionEventKind::Bite, Defender, Passer, TEXT("Bit"), Hold);
        }
    }
}

EPSOptionChoice UPSDeceptionSubsystem::ReadMesh(APSPlayerPawn* Passer, APSPlayerPawn* Back, float TimeSinceSnap)
{
    if (bMeshRead)
    {
        return MeshChoice;
    }
    const FPSDeceptionTuning& Settings = GetTuning();
    const FPSDeceptionDef& Def = GetPlayDeception();
    if (!bLive || !Passer || !Back || !PSDeceptionPrivate::IsRunOption(Def.Type))
    {
        return EPSOptionChoice::Give;
    }
    // He rides the mesh first: the key shows what he is doing.
    if (TimeSinceSnap < Settings.MeshRideSeconds)
    {
        return EPSOptionChoice::Ride;
    }
    bMeshRead = true;
    MeshChoice = EPSOptionChoice::Give;

    const APSPlayerPawn* Key = nullptr;
    if (Def.Type == EPSDeception::RPO)
    {
        // The conflict defender: the linebacker nearest the pass option (a defensive back when
        // there is no linebacker). Playing the back rather than the pass option, he is playing
        // the run: throw it behind him.
        const APSPlayerPawn* Option = GetPassOption();
        UPSAIFieldSnapshot* Field = GetWorld() ? GetWorld()->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
        const TArray<APSPlayerPawn*> Pawns = Field ? Field->GetPawns() : TArray<APSPlayerPawn*>();
        const TArray<EPlayerRole> Roles = Field ? Field->GetRoles() : TArray<EPlayerRole>();
        for (const EPlayerRole ConflictRole : { EPlayerRole::Linebacker, EPlayerRole::DefensiveBack })
        {
            float Nearest = TNumericLimits<float>::Max();
            for (int32 Index = 0; Option && Index < Pawns.Num() && Index < Roles.Num(); ++Index)
            {
                const APSPlayerPawn* Candidate = Pawns[Index];
                if (!Candidate || Candidate->TeamSide != EPSTeamSide::Defense || Roles[Index] != ConflictRole || !PSDeceptionPrivate::IsStanding(Candidate))
                {
                    continue;
                }
                const float Distance = FVector::Dist2D(Candidate->GetActorLocation(), Option->GetActorLocation());
                if (Distance < Nearest)
                {
                    Nearest = Distance;
                    Key = Candidate;
                }
            }
            if (Key)
            {
                break;
            }
        }
        if (Key && IsPlayingTheBack(Key, Back, Option, Settings.ReadMinSpeed))
        {
            MeshChoice = EPSOptionChoice::Throw;
        }
    }
    else
    {
        // The end man on the line: playing the back, the quarterback keeps it; staying home on
        // the quarterback, he gives.
        Key = FindEndManOnLine();
        if (Key && IsPlayingTheBack(Key, Back, Passer, Settings.ReadMinSpeed))
        {
            MeshChoice = EPSOptionChoice::Keep;
        }
    }
    Publish(EPSDeceptionEventKind::Read, Passer, Key, PSDeceptionPrivate::ChoiceName(MeshChoice));
    return MeshChoice;
}

APSPlayerPawn* UPSDeceptionSubsystem::GetPassOption() const
{
    return PlayDeception.Type == EPSDeception::RPO ? FindOffense(PlayDeception.PassRole, true) : nullptr;
}

APSPlayerPawn* UPSDeceptionSubsystem::ReadPitch(APSPlayerPawn* Passer)
{
    const FPSDeceptionDef& Def = GetPlayDeception();
    if (!bLive || !Passer || Def.Type != EPSDeception::TripleOption || !bMeshRead || MeshChoice != EPSOptionChoice::Keep || bPitched
        || Passer->GetActorLocation().X > LineOfScrimmage.X + GetTuning().PitchWindowDepth)
    {
        return nullptr;
    }
    APSPlayerPawn* PitchMan = FindOffense(Def.PitchRole);
    if (!PitchMan)
    {
        return nullptr;
    }
    // The pitch key: the defender the defense put on the pitch phase, else the nearest to the
    // pitch man other than the dive key the quarterback already read.
    APSPlayerPawn* Key = PitchKey.Get();
    if (!Key)
    {
        const APSPlayerPawn* DiveKey = FindEndManOnLine();
        float Nearest = TNumericLimits<float>::Max();
        for (APSPlayerPawn* Candidate : UPSAIFieldSnapshot::GetFieldPawns(GetWorld()))
        {
            const float Distance = Candidate && Candidate != DiveKey && Candidate->TeamSide == EPSTeamSide::Defense && PSDeceptionPrivate::IsStanding(Candidate)
                ? FVector::Dist2D(Candidate->GetActorLocation(), PitchMan->GetActorLocation()) : TNumericLimits<float>::Max();
            if (Distance < Nearest)
            {
                Nearest = Distance;
                Key = Candidate;
            }
        }
        PitchKey = Key;
    }
    // He has taken the quarterback: pitch it.
    if (Key && FVector::Dist2D(Key->GetActorLocation(), Passer->GetActorLocation()) <= GetTuning().PitchReadRadius
        && IsPlayingTheBack(Key, Passer, PitchMan, GetTuning().ReadMinSpeed))
    {
        bPitched = true;
        Publish(EPSDeceptionEventKind::Read, Passer, Key, TEXT("Pitch"));
        return PitchMan;
    }
    return nullptr;
}

// --- The defense ---

void UPSDeceptionSubsystem::UpdateDefense()
{
    SCOPE_CYCLE_COUNTER(STAT_PSAIDeception);
    const FPSDeceptionDef& Def = GetPlayDeception();
    if (!bLive || bRecognized || (Def.Type != EPSDeception::ZoneRead && Def.Type != EPSDeception::TripleOption))
    {
        return;
    }
    UPSAIFieldSnapshot* Field = GetWorld() ? GetWorld()->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    APSPlayerPawn* Passer = Field ? Field->FindBallCarrier() : nullptr;
    APSPlayerPawn* Back = FindOffense(EPlayerRole::RunningBack);
    if (!Passer || !Back || Passer == Back || Passer->TeamSide != EPSTeamSide::Offense || Passer->GetActorLocation().X > LineOfScrimmage.X
        || FVector::Dist2D(Passer->GetActorLocation(), Back->GetActorLocation()) > GetTuning().MeshRecognizeRadius)
    {
        return;
    }

    // The mesh: the defense sees the option and plays it with assignments.
    bRecognized = true;
    const FPSDeceptionTuning& Settings = GetTuning();
    const TArray<APSPlayerPawn*>& Pawns = Field->GetPawns();
    const TArray<EPlayerRole>& Roles = Field->GetRoles();
    auto IsFree = [this](const APSPlayerPawn* Candidate)
    {
        return Candidate && Candidate->TeamSide == EPSTeamSide::Defense && PSDeceptionPrivate::IsStanding(Candidate) && !OptionJobs.Contains(FObjectKey(Candidate));
    };

    // The read key: disciplined, he takes the quarterback; otherwise he crashes on the dive and
    // an aware linebacker scrapes over to the quarterback.
    APSPlayerPawn* EndMan = FindEndManOnLine();
    if (EndMan)
    {
        if (EndMan->GetAttributes().Awareness >= Settings.DisciplineAwareness)
        {
            Assign(EndMan, Passer, TEXT("Quarterback"));
        }
        else
        {
            Assign(EndMan, Back, TEXT("Dive"));
            APSPlayerPawn* Scraper = nullptr;
            float Nearest = Settings.ScrapeRadius;
            for (int32 Index = 0; Index < Pawns.Num(); ++Index)
            {
                APSPlayerPawn* Candidate = Pawns[Index];
                if (!IsFree(Candidate) || Roles[Index] != EPlayerRole::Linebacker || Candidate->GetAttributes().Awareness < Settings.DisciplineAwareness)
                {
                    continue;
                }
                const float Distance = FVector::Dist2D(Candidate->GetActorLocation(), EndMan->GetActorLocation());
                if (Distance <= Nearest)
                {
                    Nearest = Distance;
                    Scraper = Candidate;
                }
            }
            if (Scraper)
            {
                Assign(Scraper, Passer, TEXT("Quarterback"));
            }
        }
    }

    // The dive: the nearest lineman to the back, unless someone has him already.
    bool bDiveTaken = false;
    for (const TPair<FObjectKey, TWeakObjectPtr<APSPlayerPawn>>& Job : OptionJobs)
    {
        bDiveTaken |= Job.Value.Get() == Back;
    }
    if (!bDiveTaken)
    {
        APSPlayerPawn* Diver = nullptr;
        float Nearest = TNumericLimits<float>::Max();
        for (int32 Index = 0; Index < Pawns.Num(); ++Index)
        {
            APSPlayerPawn* Candidate = Pawns[Index];
            const float Distance = IsFree(Candidate) && Roles[Index] == EPlayerRole::DefensiveLineman
                ? FVector::Dist2D(Candidate->GetActorLocation(), Back->GetActorLocation()) : TNumericLimits<float>::Max();
            if (Distance < Nearest)
            {
                Nearest = Distance;
                Diver = Candidate;
            }
        }
        if (Diver)
        {
            Assign(Diver, Back, TEXT("Dive"));
        }
    }

    // The pitch: the defender nearest the pitch man takes him if he is disciplined, otherwise
    // he goes for the quarterback.
    APSPlayerPawn* PitchMan = Def.Type == EPSDeception::TripleOption ? FindOffense(Def.PitchRole) : nullptr;
    if (PitchMan)
    {
        APSPlayerPawn* Key = nullptr;
        float Nearest = TNumericLimits<float>::Max();
        for (APSPlayerPawn* Candidate : Pawns)
        {
            const float Distance = IsFree(Candidate) ? FVector::Dist2D(Candidate->GetActorLocation(), PitchMan->GetActorLocation()) : TNumericLimits<float>::Max();
            if (Distance < Nearest)
            {
                Nearest = Distance;
                Key = Candidate;
            }
        }
        if (Key)
        {
            PitchKey = Key;
            const bool bDisciplined = Key->GetAttributes().Awareness >= Settings.DisciplineAwareness;
            Assign(Key, bDisciplined ? PitchMan : Passer, bDisciplined ? FName(TEXT("Pitch")) : FName(TEXT("Quarterback")));
        }
    }
}

void UPSDeceptionSubsystem::Assign(APSPlayerPawn* Defender, APSPlayerPawn* Man, FName Job)
{
    OptionJobs.Add(FObjectKey(Defender), Man);
    Publish(EPSDeceptionEventKind::Assignment, Defender, Man, Job);
}

APSPlayerPawn* UPSDeceptionSubsystem::GetOptionMan(const APSPlayerPawn* Defender) const
{
    const TWeakObjectPtr<APSPlayerPawn>* Man = Defender ? OptionJobs.Find(FObjectKey(Defender)) : nullptr;
    return Man ? Man->Get() : nullptr;
}

bool UPSDeceptionSubsystem::GetOptionTarget(const APSPlayerPawn* Defender, FVector& OutTarget) const
{
    const APSPlayerPawn* Man = bRecognized ? GetOptionMan(Defender) : nullptr;
    if (!Man)
    {
        return false;
    }
    // While the quarterback has the ball near the line; once it is given or pitched, or he is
    // through, everyone pursues.
    UPSAIFieldSnapshot* Field = GetWorld() ? GetWorld()->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    const APSPlayerPawn* Carrier = Field ? Field->FindBallCarrier() : nullptr;
    if (!Carrier || Carrier->TeamSide != EPSTeamSide::Offense || Carrier->GetAttributes().Role != EPlayerRole::Quarterback
        || Carrier->GetActorLocation().X > LineOfScrimmage.X + Tuning.PitchWindowDepth)
    {
        return false;
    }
    OutTarget = Man->GetActorLocation();
    return true;
}

// --- The bus ---

void UPSDeceptionSubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    // The play's deception is its call's, which comes before the snap (or with it, a CPU call
    // outside a call window), so it is kept.
    bLive = true;
    bCallSinceSnap = false;
    bFakeSold = false;
    bMeshRead = false;
    MeshChoice = EPSOptionChoice::Give;
    bPitched = false;
    bRecognized = false;
    OptionJobs.Reset();
    PitchKey.Reset();
    LineOfScrimmage = Event.LineOfScrimmage;
    SinceUpdate = 0.f;
}

void UPSDeceptionSubsystem::HandlePlayCall(const FPSTelemetryPlayCallEvent& Event)
{
    if (!Event.bOffense)
    {
        return;
    }
    // The play's deception: the offense's latest call, the one its quarterback runs.
    PlayDeception = FPSDeceptionDef();
    FPSPlayDefinition Play;
    UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    if (PlayCall && !Event.PlayId.IsNone() && PlayCall->FindPlay(Event.PlayId, Play))
    {
        PlayDeception = Play.Deception;
    }

    // The offense's tendency, one entry per snap: an audible replaces the call it changes.
    if (!PSDeceptionPrivate::IsScrimmageCategory(Event.PlayCategory))
    {
        return;
    }
    const bool bRun = Event.PlayCategory == TEXT("Run");
    if (bCallSinceSnap && RecentRuns.Num() > 0)
    {
        RecentRuns.Last() = bRun;
    }
    else
    {
        RecentRuns.Add(bRun);
    }
    bCallSinceSnap = true;
    const int32 Window = FMath::Max(1, GetTuning().TendencyWindow);
    while (RecentRuns.Num() > Window)
    {
        RecentRuns.RemoveAt(0);
    }
}

void UPSDeceptionSubsystem::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("Scoring") || Event.NewPhase == TEXT("PreSnap"))
    {
        bLive = false;
        bRecognized = false;
        OptionJobs.Reset();
        PitchKey.Reset();
    }
}

void UPSDeceptionSubsystem::Publish(EPSDeceptionEventKind Kind, const APSPlayerPawn* Player, const APSPlayerPawn* Other, FName Outcome, float Seconds)
{
    UPSTelemetryBus* Bus = BoundBus.Get();
    if (!Bus)
    {
        return;
    }
    FPSTelemetryDeceptionEvent Event;
    Event.Kind = Kind;
    Event.PlayerName = Player ? Player->GetAttributes().DisplayName : FString();
    Event.OtherName = Other ? Other->GetAttributes().DisplayName : FString();
    Event.Outcome = Outcome;
    Event.Seconds = Seconds;
    Bus->PublishDeception(Event);
}
