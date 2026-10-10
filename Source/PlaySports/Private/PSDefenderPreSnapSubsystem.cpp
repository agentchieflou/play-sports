#include "PSDefenderPreSnapSubsystem.h"
#include "PSDataIngestion.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayOrchestrator.h"
#include "PSPlayerPawn.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"

namespace PSDefenderPreSnapPrivate
{
    /** A job that sends the defender after the passer. */
    bool IsRushKind(EPSAssignmentKind Kind)
    {
        return Kind == EPSAssignmentKind::Blitz || Kind == EPSAssignmentKind::PassRush;
    }

    /** A second-level defender (linebacker or back) the offense watches for a blitz. */
    bool IsSecondLevel(const APSPlayerPawn* Pawn)
    {
        const EPlayerRole Role = Pawn->GetAttributes().Role;
        return Role == EPlayerRole::Linebacker || Role == EPlayerRole::DefensiveBack;
    }

    float AverageAwareness(const TArray<APSPlayerPawn*>& Players)
    {
        if (Players.Num() == 0)
        {
            return 0.f;
        }
        float Total = 0.f;
        for (const APSPlayerPawn* Player : Players)
        {
            Total += FMath::Clamp(Player->GetAttributes().Awareness, 0.f, 100.f);
        }
        return Total / Players.Num();
    }

    /** How good a cover man (or a receiver) is, for the CPU's shadow matchup. */
    float CoverRating(const APSPlayerPawn* Pawn)
    {
        const FPlayerAttributes Attributes = Pawn->GetAttributes();
        return Attributes.Speed + Attributes.Agility + Attributes.Awareness;
    }

    float ReceiverRating(const APSPlayerPawn* Pawn)
    {
        const FPlayerAttributes Attributes = Pawn->GetAttributes();
        return Attributes.Speed + Attributes.Agility;
    }

    FVector GroundDirection(const FVector& From, const FVector& To)
    {
        FVector Direction = To - From;
        Direction.Z = 0.f;
        return Direction.GetSafeNormal();
    }
}

DECLARE_CYCLE_STAT(TEXT("Defensive pre-snap"), STAT_PSDefenderPreSnap, STATGROUP_Tickables);

void UPSDefenderPreSnapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>();
    if (Bus)
    {
        Bus->OnPlayCallMC.AddUObject(this, &UPSDefenderPreSnapSubsystem::HandlePlayCall);
        Bus->OnSnapMC.AddUObject(this, &UPSDefenderPreSnapSubsystem::HandleSnap);
        Bus->OnPhaseChangeMC.AddUObject(this, &UPSDefenderPreSnapSubsystem::HandlePhaseChange);
        BoundBus = Bus;
    }
}

void UPSDefenderPreSnapSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnPlayCallMC.RemoveAll(this);
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

bool UPSDefenderPreSnapSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSDefenderPreSnapSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    TickPreSnap(DeltaTime);
}

TStatId UPSDefenderPreSnapSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSDefenderPreSnapSubsystem, STATGROUP_Tickables);
}

FString UPSDefenderPreSnapSubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/defensive_presnap.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSDefensivePreSnapTuning& UPSDefenderPreSnapSubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSDefenderPreSnapSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSDefensivePreSnapTuning Loaded;
    if (!Ingestion->LoadDefensivePreSnapTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSDefenderPreSnapSubsystem: Could not load the tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSDefenderPreSnapSubsystem: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

void UPSDefenderPreSnapSubsystem::SetTuning(const FPSDefensivePreSnapTuning& InTuning)
{
    Tuning = InTuning;
    bTuningLoaded = true;
    bAlignDirty = true;
}

TArray<FString> UPSDefenderPreSnapSubsystem::ValidateTuning(const FPSDefensivePreSnapTuning& InTuning)
{
    TArray<FString> Problems;
    TSet<FString> Shells;
    for (const FPSShellSafeties& Entry : InTuning.ShellSafeties)
    {
        if (Entry.Shell.IsEmpty() || Shells.Contains(Entry.Shell) || Entry.DeepSafeties < 0 || Entry.DeepSafeties > 2)
        {
            Problems.Add(FString::Printf(TEXT("ShellSafeties: '%s' is empty, listed twice, or not 0-2 deep safeties"), *Entry.Shell));
        }
        Shells.Add(Entry.Shell);
    }
    const float Chances[] = { InTuning.MaxDisguiseLeak, InTuning.DisguiseChanceConservative, InTuning.DisguiseChanceAggressive, InTuning.ShowBlitzChanceConservative,
        InTuning.ShowBlitzChanceAggressive, InTuning.CreepChanceConservative, InTuning.CreepChanceAggressive, InTuning.CreepSpeedScale };
    for (const float Chance : Chances)
    {
        if (Chance < 0.f || Chance > 1.f)
        {
            Problems.Add(TEXT("a chance, leak or speed scale is outside 0-1"));
            break;
        }
    }
    if (InTuning.TwoHighDepth < 0.f || InTuning.TwoHighWidth < 0.f || InTuning.SingleHighDepth < 0.f || InTuning.RobberDepth < 0.f || InTuning.RobberWidth < 0.f
        || InTuning.ShowBlitzDepth < 0.f || InTuning.BlitzLookDepth < 0.f || InTuning.BlitzLookWidth < 0.f || InTuning.CreepDelaySeconds < 0.f || InTuning.ShowBlitzCount < 0)
    {
        Problems.Add(TEXT("a distance, time or count is negative"));
    }
    if (InTuning.DeepSafetyDepth <= InTuning.RobberDepth || InTuning.DeepSafetyDepth > FMath::Min(InTuning.TwoHighDepth, InTuning.SingleHighDepth))
    {
        Problems.Add(TEXT("DeepSafetyDepth must lie past RobberDepth and no deeper than the deep safeties' spots"));
    }
    if (InTuning.ShowBlitzDepth > InTuning.BlitzLookDepth)
    {
        Problems.Add(TEXT("ShowBlitzDepth must be within BlitzLookDepth, or a shown blitz can't be seen"));
    }
    return Problems;
}

UPSPlayCallSubsystem* UPSDefenderPreSnapSubsystem::GetPlayCall() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
}

bool UPSDefenderPreSnapSubsystem::GetDefensePlay(FPSPlayDefinition& OutPlay) const
{
    UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    return PlayCall && PlayCall->GetCall(false).IsSet() && PlayCall->GetDefensivePlayToRun(OutPlay);
}

bool UPSDefenderPreSnapSubsystem::IsAdjustable() const
{
    const UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    return PlayCall && PlayCall->IsCallWindowOpen() && PlayCall->GetCall(false).IsSet();
}

TArray<APSPlayerPawn*> UPSDefenderPreSnapSubsystem::GetFieldPawns() const
{
    TArray<APSPlayerPawn*> Pawns;
    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        Pawns.Add(*It);
    }
    return Pawns;
}

bool UPSDefenderPreSnapSubsystem::ReadLine(float& OutLineX, float& OutCentreY) const
{
    // The front of the offensive line and its centre across the field, as the offense reads
    // the box (UPSPreSnapSubsystem::GetDefensiveLook).
    OutLineX = -TNumericLimits<float>::Max();
    OutCentreY = 0.f;
    int32 Linemen = 0;
    for (const APSPlayerPawn* Pawn : GetFieldPawns())
    {
        if (Pawn->TeamSide == EPSTeamSide::Offense && Pawn->GetAttributes().Role == EPlayerRole::OffensiveLineman)
        {
            OutLineX = FMath::Max(OutLineX, Pawn->GetActorLocation().X);
            OutCentreY += Pawn->GetActorLocation().Y;
            ++Linemen;
        }
    }
    if (Linemen == 0)
    {
        return false;
    }
    OutCentreY /= Linemen;
    return true;
}

int32 UPSDefenderPreSnapSubsystem::GetDeepSafetiesForShell(const FString& Shell)
{
    const FPSShellSafeties* Entry = GetTuning().ShellSafeties.FindByPredicate([&Shell](const FPSShellSafeties& Candidate) { return Candidate.Shell == Shell; });
    return Entry ? Entry->DeepSafeties : 1;
}

bool UPSDefenderPreSnapSubsystem::GetCalledKind(const APSPlayerPawn* Defender, const FPSPlayDefinition& Play, EPSAssignmentKind& OutKind) const
{
    // The orchestrator hands slots out in the field's actor order, role by role.
    const EPlayerRole Role = Defender->GetAttributes().Role;
    int32 RoleIndex = 0;
    for (const APSPlayerPawn* Pawn : GetFieldPawns())
    {
        if (Pawn == Defender)
        {
            break;
        }
        RoleIndex += Pawn->GetAttributes().Role == Role ? 1 : 0;
    }
    const FPSPlayAssignment* Slot = UPSPlayOrchestrator::FindAssignmentSlot(Play, Role, RoleIndex);
    if (!Slot)
    {
        return false;
    }
    OutKind = Slot->Kind;
    return true;
}

void UPSDefenderPreSnapSubsystem::HandlePlayCall(const FPSTelemetryPlayCallEvent& Event)
{
    if (!Event.bOffense)
    {
        bAlignDirty = true;
        EnsureAligned();
    }
}

void UPSDefenderPreSnapSubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    // The defense plays its real call from where it stands; the look is spent. Shadows stay:
    // the orchestrator reads them as it hands the call out on this same snap.
    Disguise = FPSDefensiveDisguise();
    BaseSpots.Reset();
    Creepers.Reset();
    AlignedPlayId = NAME_None;
    CreepClock = 0.f;
    bAlignDirty = false;
}

void UPSDefenderPreSnapSubsystem::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("PreSnap"))
    {
        ResetDown();
    }
}

void UPSDefenderPreSnapSubsystem::ResetDown()
{
    // A new down: a new look. Shadow matchups stay until cleared; the CPU's go with its call.
    Disguise = FPSDefensiveDisguise();
    BaseSpots.Reset();
    Creepers.Reset();
    AlignedPlayId = NAME_None;
    CreepClock = 0.f;
    bAlignDirty = false;
    for (auto It = Shadows.CreateIterator(); It; ++It)
    {
        if (It->Value.bCpu)
        {
            It.RemoveCurrent();
        }
    }
}

// --- Alignment and disguise ---

void UPSDefenderPreSnapSubsystem::EnsureAligned()
{
    FPSPlayDefinition Play;
    if (!IsAdjustable() || !GetDefensePlay(Play))
    {
        return;
    }
    if (Play.PlayId != AlignedPlayId)
    {
        // A new call: the CPU decides its look for it; a human keeps the look he set.
        AlignedPlayId = Play.PlayId;
        const UPSPlayCallSubsystem* PlayCall = GetPlayCall();
        if (PlayCall->GetCall(false).Caller == EPSPlayCaller::CPU && !PlayCall->IsHumanSide(false))
        {
            PlanCpu(Play);
        }
        bAlignDirty = true;
    }
    if (bAlignDirty)
    {
        bAlignDirty = false;
        Align();
    }
}

bool UPSDefenderPreSnapSubsystem::SetDisguise(const FPSDefensiveDisguise& InDisguise, bool bHuman)
{
    if (!IsAdjustable())
    {
        return false;
    }
    EnsureAligned();
    if (InDisguise.bDisguiseShell != Disguise.bDisguiseShell)
    {
        Publish(TEXT("DisguiseShell"), nullptr, InDisguise.bDisguiseShell ? FName(TEXT("On")) : FName(TEXT("Off")), bHuman);
    }
    if (InDisguise.bShowBlitz != Disguise.bShowBlitz)
    {
        Publish(TEXT("ShowBlitz"), nullptr, InDisguise.bShowBlitz ? FName(TEXT("On")) : FName(TEXT("Off")), bHuman);
    }
    if (InDisguise.bCreep != Disguise.bCreep)
    {
        Publish(TEXT("Creep"), nullptr, InDisguise.bCreep ? FName(TEXT("On")) : FName(TEXT("Off")), bHuman);
    }
    Disguise = InDisguise;
    bAlignDirty = true;
    EnsureAligned();
    return true;
}

bool UPSDefenderPreSnapSubsystem::ToggleShellDisguise(bool bHuman)
{
    FPSDefensiveDisguise Changed = Disguise;
    Changed.bDisguiseShell = !Changed.bDisguiseShell;
    return SetDisguise(Changed, bHuman);
}

bool UPSDefenderPreSnapSubsystem::ToggleShowBlitz(bool bHuman)
{
    FPSDefensiveDisguise Changed = Disguise;
    Changed.bShowBlitz = !Changed.bShowBlitz;
    return SetDisguise(Changed, bHuman);
}

bool UPSDefenderPreSnapSubsystem::ToggleCreep(bool bHuman)
{
    FPSDefensiveDisguise Changed = Disguise;
    Changed.bCreep = !Changed.bCreep;
    return SetDisguise(Changed, bHuman);
}

int32 UPSDefenderPreSnapSubsystem::GetCalledDeepSafeties()
{
    FPSPlayDefinition Play;
    return GetDefensePlay(Play) ? GetDeepSafetiesForShell(Play.CoverageShell) : 1;
}

int32 UPSDefenderPreSnapSubsystem::GetShownDeepSafeties()
{
    EnsureAligned();
    return ShownDeepSafeties;
}

FString UPSDefenderPreSnapSubsystem::DescribeStructure(int32 DeepSafeties)
{
    return DeepSafeties >= 2 ? TEXT("TwoHigh") : (DeepSafeties == 1 ? TEXT("SingleHigh") : TEXT("ZeroHigh"));
}

void UPSDefenderPreSnapSubsystem::Align()
{
    FPSPlayDefinition Play;
    if (!GetDefensePlay(Play) || !ReadLine(LineX, CentreY))
    {
        return;
    }
    const FPSDefensivePreSnapTuning& Settings = GetTuning();

    // Everyone starts from where he stood before the defense lined up this down.
    TArray<APSPlayerPawn*> Defenders;
    for (APSPlayerPawn* Pawn : GetFieldPawns())
    {
        if (Pawn->TeamSide == EPSTeamSide::Defense)
        {
            Defenders.Add(Pawn);
            if (!BaseSpots.Contains(FObjectKey(Pawn)))
            {
                BaseSpots.Add(FObjectKey(Pawn), Pawn->GetActorLocation());
            }
        }
    }
    TMap<const APSPlayerPawn*, FVector> Spots;
    for (const APSPlayerPawn* Defender : Defenders)
    {
        Spots.Add(Defender, BaseSpots[FObjectKey(Defender)]);
    }

    // The safeties: the backs inside the two outermost (none with two or fewer backs).
    TArray<APSPlayerPawn*> Backs = Defenders.FilterByPredicate([](const APSPlayerPawn* Pawn) { return Pawn->GetAttributes().Role == EPlayerRole::DefensiveBack; });
    Backs.Sort([this](const APSPlayerPawn& A, const APSPlayerPawn& B)
    {
        return FMath::Abs(BaseSpots[FObjectKey(&A)].Y - CentreY) < FMath::Abs(BaseSpots[FObjectKey(&B)].Y - CentreY);
    });
    TArray<APSPlayerPawn*> Safeties;
    for (int32 Index = 0; Index < Backs.Num() - 2; ++Index)
    {
        Safeties.Add(Backs[Index]);
    }
    Safeties.Sort([this](const APSPlayerPawn& A, const APSPlayerPawn& B)
    {
        return BaseSpots[FObjectKey(&A)].Y < BaseSpots[FObjectKey(&B)].Y;
    });

    // The structure the call plays, and the one the defense shows.
    const int32 Called = GetDeepSafetiesForShell(Play.CoverageShell);
    ShownDeepSafeties = Disguise.bDisguiseShell && Safeties.Num() > 0 ? (Called >= 2 ? 1 : 2) : Called;
    auto StructureSpot = [this, &Settings, &Safeties](int32 Deep, int32 SafetyIndex) -> FVector
    {
        const APSPlayerPawn* Safety = Safeties[SafetyIndex];
        const float BaseY = BaseSpots[FObjectKey(Safety)].Y;
        const float Side = BaseY >= CentreY ? 1.f : -1.f;
        const float Z = Safety->GetActorLocation().Z;
        if (Deep >= 2 && Safeties.Num() >= 2)
        {
            return FVector(LineX + Settings.TwoHighDepth, CentreY + Side * Settings.TwoHighWidth, Z);
        }
        // One deep: the first safety in the middle, the rest rolled down to their sides.
        if (Deep >= 1 && SafetyIndex == 0)
        {
            return FVector(LineX + Settings.SingleHighDepth, CentreY, Z);
        }
        return FVector(LineX + Settings.RobberDepth, CentreY + Side * Settings.RobberWidth, Z);
    };
    for (int32 Index = 0; Index < Safeties.Num(); ++Index)
    {
        const FVector Real = StructureSpot(Called, Index);
        FVector Spot = Real;
        if (ShownDeepSafeties != Called)
        {
            // A safety who can't hold a disguise cheats part-way toward his real spot.
            const float Awareness = FMath::Clamp(Safeties[Index]->GetAttributes().Awareness, 0.f, 100.f);
            const float Leak = Settings.MaxDisguiseLeak * (1.f - Awareness / 100.f);
            Spot = FMath::Lerp(StructureSpot(ShownDeepSafeties, Index), Real, Leak);
        }
        Spots.Add(Safeties[Index], Spot);
    }

    // Blitzers walk up, unless they creep; a shown blitz walks up linebackers who drop.
    Creepers.Reset();
    CreepClock = 0.f;
    TArray<APSPlayerPawn*> Droppers;
    for (APSPlayerPawn* Defender : Defenders)
    {
        if (!PSDefenderPreSnapPrivate::IsSecondLevel(Defender))
        {
            continue;
        }
        EPSAssignmentKind Kind = EPSAssignmentKind::RunFit;
        const bool bCalled = GetCalledKind(Defender, Play, Kind);
        const FVector Base = BaseSpots[FObjectKey(Defender)];
        if (bCalled && PSDefenderPreSnapPrivate::IsRushKind(Kind))
        {
            if (Disguise.bCreep)
            {
                Spots.Add(Defender, Base);
                Creepers.Add(Defender);
            }
            else
            {
                Spots.Add(Defender, FVector(LineX + Settings.ShowBlitzDepth, Base.Y, Base.Z));
            }
        }
        else if (Defender->GetAttributes().Role == EPlayerRole::Linebacker)
        {
            Droppers.Add(Defender);
        }
    }
    if (Disguise.bShowBlitz)
    {
        Droppers.Sort([this](const APSPlayerPawn& A, const APSPlayerPawn& B)
        {
            return FMath::Abs(BaseSpots[FObjectKey(&A)].Y - CentreY) < FMath::Abs(BaseSpots[FObjectKey(&B)].Y - CentreY);
        });
        for (int32 Index = 0; Index < Droppers.Num() && Index < Settings.ShowBlitzCount; ++Index)
        {
            const FVector Base = BaseSpots[FObjectKey(Droppers[Index])];
            Spots.Add(Droppers[Index], FVector(LineX + Settings.ShowBlitzDepth, Base.Y, Base.Z));
        }
    }

    // A shadow lines up over his man.
    for (APSPlayerPawn* Defender : Defenders)
    {
        if (const APSPlayerPawn* Receiver = GetShadow(Defender))
        {
            const FVector Current = Spots[Defender];
            Spots.Add(Defender, FVector(Current.X, Receiver->GetActorLocation().Y, Current.Z));
        }
    }

    // Line up. A defender a person controls lines himself up.
    for (APSPlayerPawn* Defender : Defenders)
    {
        if (Defender->IsUserControlled())
        {
            continue;
        }
        Defender->SetActorLocation(Spots[Defender], false, nullptr, ETeleportType::TeleportPhysics);
        if (UFloatingPawnMovement* Movement = Defender->GetFloatingMovementComponent())
        {
            Movement->Velocity = FVector::ZeroVector;
        }
    }
}

bool UPSDefenderPreSnapSubsystem::ReadShownLook(int32& OutDeepSafeties, bool& bOutShowsBlitz)
{
    OutDeepSafeties = 0;
    bOutShowsBlitz = false;
    EnsureAligned();
    float FrontX = 0.f;
    float MiddleY = 0.f;
    if (!ReadLine(FrontX, MiddleY))
    {
        return false;
    }
    const FPSDefensivePreSnapTuning& Settings = GetTuning();
    for (const APSPlayerPawn* Pawn : GetFieldPawns())
    {
        if (Pawn->TeamSide != EPSTeamSide::Defense)
        {
            continue;
        }
        const FVector Location = Pawn->GetActorLocation();
        const float Off = Location.X - FrontX;
        OutDeepSafeties += Off >= Settings.DeepSafetyDepth ? 1 : 0;
        if (PSDefenderPreSnapPrivate::IsSecondLevel(Pawn) && Off >= 0.f && Off <= Settings.BlitzLookDepth && FMath::Abs(Location.Y - MiddleY) <= Settings.BlitzLookWidth)
        {
            bOutShowsBlitz = true;
        }
    }
    return true;
}

bool UPSDefenderPreSnapSubsystem::IsCreeper(const APSPlayerPawn* Defender) const
{
    return Creepers.ContainsByPredicate([Defender](const TWeakObjectPtr<APSPlayerPawn>& Creeper) { return Creeper.Get() == Defender; });
}

void UPSDefenderPreSnapSubsystem::TickPreSnap(float DeltaSeconds)
{
    SCOPE_CYCLE_COUNTER(STAT_PSDefenderPreSnap);
    if (Creepers.Num() == 0 || !IsAdjustable())
    {
        return;
    }
    const FPSDefensivePreSnapTuning& Settings = GetTuning();
    CreepClock += DeltaSeconds;
    if (CreepClock < Settings.CreepDelaySeconds)
    {
        return;
    }
    for (const TWeakObjectPtr<APSPlayerPawn>& Weak : Creepers)
    {
        APSPlayerPawn* Creeper = Weak.Get();
        if (!Creeper || Creeper->IsUserControlled())
        {
            continue;
        }
        const FVector Location = Creeper->GetActorLocation();
        const FVector Spot(LineX + Settings.ShowBlitzDepth, Location.Y, Location.Z);
        if (Location.X - Spot.X > 1.f)
        {
            Creeper->AddMovementInput(PSDefenderPreSnapPrivate::GroundDirection(Location, Spot), Settings.CreepSpeedScale);
        }
    }
}

// --- Audibles ---

TArray<FPSPlayDefinition> UPSDefenderPreSnapSubsystem::GetAudibles() const
{
    TArray<FPSPlayDefinition> Audibles;
    FPSPlayDefinition Current;
    if (!IsAdjustable() || !GetDefensePlay(Current))
    {
        return Audibles;
    }
    for (const FPSPlayDefinition& Play : GetPlayCall()->GetPlays(false))
    {
        if (Play.Front == Current.Front && Play.PlayId != Current.PlayId)
        {
            Audibles.Add(Play);
        }
    }
    return Audibles;
}

bool UPSDefenderPreSnapSubsystem::Audible(FName PlayId, bool bHuman)
{
    const bool bCompatible = GetAudibles().ContainsByPredicate([PlayId](const FPSPlayDefinition& Play) { return Play.PlayId == PlayId; });
    if (!bCompatible || !GetPlayCall()->CallPlay(PlayId, bHuman ? EPSPlayCaller::Human : EPSPlayCaller::CPU))
    {
        return false;
    }
    Publish(TEXT("Audible"), nullptr, PlayId, bHuman);
    return true;
}

bool UPSDefenderPreSnapSubsystem::AudibleToNext(bool bHuman)
{
    FPSPlayDefinition Current;
    if (!IsAdjustable() || !GetDefensePlay(Current))
    {
        return false;
    }
    TArray<FPSPlayDefinition> Front;
    for (const FPSPlayDefinition& Play : GetPlayCall()->GetPlays(false))
    {
        if (Play.Front == Current.Front)
        {
            Front.Add(Play);
        }
    }
    const int32 Index = Front.IndexOfByPredicate([&Current](const FPSPlayDefinition& Play) { return Play.PlayId == Current.PlayId; });
    if (Front.Num() < 2 || Index == INDEX_NONE)
    {
        return false;
    }
    return Audible(Front[(Index + 1) % Front.Num()].PlayId, bHuman);
}

// --- Matchups ---

bool UPSDefenderPreSnapSubsystem::SetShadow(APSPlayerPawn* Defender, APSPlayerPawn* Receiver, bool bHuman)
{
    if (!Defender || !Receiver || Defender->TeamSide != EPSTeamSide::Defense || Receiver->TeamSide != EPSTeamSide::Offense || Defender->IsUserControlled())
    {
        return false;
    }
    // One shadow per receiver: whoever had him lets him go.
    for (auto It = Shadows.CreateIterator(); It; ++It)
    {
        if (It->Value.Receiver.Get() == Receiver)
        {
            It.RemoveCurrent();
        }
    }
    FShadow Shadow;
    Shadow.Receiver = Receiver;
    Shadow.bCpu = !bHuman;
    Shadows.Add(FObjectKey(Defender), Shadow);
    bAlignDirty = true;
    EnsureAligned();
    Publish(TEXT("Shadow"), Defender, FName(*Receiver->GetAttributes().DisplayName), bHuman);
    return true;
}

void UPSDefenderPreSnapSubsystem::ClearShadow(const APSPlayerPawn* Defender)
{
    if (Defender && Shadows.Remove(FObjectKey(Defender)) > 0)
    {
        bAlignDirty = true;
    }
}

APSPlayerPawn* UPSDefenderPreSnapSubsystem::GetShadow(const APSPlayerPawn* Defender) const
{
    const FShadow* Shadow = Defender ? Shadows.Find(FObjectKey(Defender)) : nullptr;
    return Shadow ? Shadow->Receiver.Get() : nullptr;
}

APSPlayerPawn* UPSDefenderPreSnapSubsystem::GetShadowingDefender(const APSPlayerPawn* Receiver) const
{
    for (const TPair<FObjectKey, FShadow>& Pair : Shadows)
    {
        if (Receiver && Pair.Value.Receiver.Get() == Receiver)
        {
            return Cast<APSPlayerPawn>(Pair.Key.ResolveObjectPtr());
        }
    }
    return nullptr;
}

void UPSDefenderPreSnapSubsystem::ApplyMatchup(const APSPlayerPawn* Defender, EPSDefensiveAssignmentType& InOutType, AActor*& InOutTarget) const
{
    if (APSPlayerPawn* Receiver = GetShadow(Defender))
    {
        InOutType = EPSDefensiveAssignmentType::ManCoverage;
        InOutTarget = Receiver;
    }
}

// --- The CPU ---

void UPSDefenderPreSnapSubsystem::PlanCpu(const FPSPlayDefinition& Play)
{
    const FPSDefensivePreSnapTuning& Settings = GetTuning();
    const float Aggression = FMath::Clamp(Tendency.AggressionScore, 0.f, 1.f);

    // Who would carry each disguise: the safeties, the blitzers, the linebackers who drop.
    TArray<APSPlayerPawn*> Backs;
    TArray<APSPlayerPawn*> Blitzers;
    TArray<APSPlayerPawn*> Linebackers;
    TArray<APSPlayerPawn*> ManBacks;
    TArray<APSPlayerPawn*> Receivers;
    for (APSPlayerPawn* Pawn : GetFieldPawns())
    {
        const EPlayerRole Role = Pawn->GetAttributes().Role;
        if (Pawn->TeamSide == EPSTeamSide::Offense)
        {
            if (Role == EPlayerRole::WideReceiver || Role == EPlayerRole::TightEnd)
            {
                Receivers.Add(Pawn);
            }
            continue;
        }
        EPSAssignmentKind Kind = EPSAssignmentKind::RunFit;
        const bool bCalled = GetCalledKind(Pawn, Play, Kind);
        if (bCalled && PSDefenderPreSnapPrivate::IsSecondLevel(Pawn) && PSDefenderPreSnapPrivate::IsRushKind(Kind))
        {
            Blitzers.Add(Pawn);
        }
        else if (Role == EPlayerRole::Linebacker)
        {
            Linebackers.Add(Pawn);
        }
        if (Role == EPlayerRole::DefensiveBack)
        {
            Backs.Add(Pawn);
            if (bCalled && Kind == EPSAssignmentKind::ManCoverage && !Pawn->IsUserControlled())
            {
                ManBacks.Add(Pawn);
            }
        }
    }

    // Each disguise: as likely as the coach's aggression allows, scaled by how well the men
    // carrying it read the game. Three rolls, always made, so a seed replays the same look.
    const float ShellRoll = DecisionStream.FRand();
    const float BlitzRoll = DecisionStream.FRand();
    const float CreepRoll = DecisionStream.FRand();
    const float ShellChance = FMath::Lerp(Settings.DisguiseChanceConservative, Settings.DisguiseChanceAggressive, Aggression)
        * PSDefenderPreSnapPrivate::AverageAwareness(Backs) / 100.f;
    const float ShowChance = FMath::Lerp(Settings.ShowBlitzChanceConservative, Settings.ShowBlitzChanceAggressive, Aggression)
        * PSDefenderPreSnapPrivate::AverageAwareness(Linebackers) / 100.f;
    const float CreepChance = FMath::Lerp(Settings.CreepChanceConservative, Settings.CreepChanceAggressive, Aggression)
        * PSDefenderPreSnapPrivate::AverageAwareness(Blitzers) / 100.f;
    FPSDefensiveDisguise Planned;
    Planned.bDisguiseShell = Backs.Num() > 2 && ShellRoll < ShellChance;
    Planned.bShowBlitz = Blitzers.Num() == 0 && Linebackers.Num() > 0 && BlitzRoll < ShowChance;
    Planned.bCreep = Blitzers.Num() > 0 && CreepRoll < CreepChance;
    if (Planned.bDisguiseShell)
    {
        Publish(TEXT("DisguiseShell"), nullptr, TEXT("On"), false);
    }
    if (Planned.bShowBlitz)
    {
        Publish(TEXT("ShowBlitz"), nullptr, TEXT("On"), false);
    }
    if (Planned.bCreep)
    {
        Publish(TEXT("Creep"), nullptr, TEXT("On"), false);
    }
    Disguise = Planned;

    // On a man call, the best cover man shadows the best receiver (this call only).
    for (auto It = Shadows.CreateIterator(); It; ++It)
    {
        if (It->Value.bCpu)
        {
            It.RemoveCurrent();
        }
    }
    if (Settings.bCpuShadowsTopReceiver && ManBacks.Num() > 0 && Receivers.Num() > 0)
    {
        APSPlayerPawn* BestBack = ManBacks[0];
        for (APSPlayerPawn* Back : ManBacks)
        {
            BestBack = PSDefenderPreSnapPrivate::CoverRating(Back) > PSDefenderPreSnapPrivate::CoverRating(BestBack) ? Back : BestBack;
        }
        APSPlayerPawn* BestReceiver = Receivers[0];
        for (APSPlayerPawn* Receiver : Receivers)
        {
            BestReceiver = PSDefenderPreSnapPrivate::ReceiverRating(Receiver) > PSDefenderPreSnapPrivate::ReceiverRating(BestReceiver) ? Receiver : BestReceiver;
        }
        if (!GetShadow(BestBack) && !GetShadowingDefender(BestReceiver))
        {
            FShadow Shadow;
            Shadow.Receiver = BestReceiver;
            Shadow.bCpu = true;
            Shadows.Add(FObjectKey(BestBack), Shadow);
            Publish(TEXT("Shadow"), BestBack, FName(*BestReceiver->GetAttributes().DisplayName), false);
        }
    }
}

void UPSDefenderPreSnapSubsystem::Publish(FName Action, const APSPlayerPawn* Player, FName Detail, bool bHuman)
{
    UE_LOG(LogTemp, Display, TEXT("UPSDefenderPreSnapSubsystem: %s %s %s (%s)."), *Action.ToString(),
        Player ? *Player->GetAttributes().DisplayName : TEXT(""), *Detail.ToString(), bHuman ? TEXT("human") : TEXT("CPU"));
    UPSTelemetryBus* Bus = BoundBus.Get();
    if (!Bus)
    {
        return;
    }
    FPSTelemetryDefensivePreSnapEvent Event;
    Event.Action = Action;
    Event.PlayerName = Player ? Player->GetAttributes().DisplayName : FString();
    Event.Detail = Detail;
    Event.bHumanCall = bHuman;
    Bus->PublishDefensivePreSnap(Event);
}
