#include "PSPreSnapSubsystem.h"
#include "PSDataIngestion.h"
#include "PSDefenderPreSnapSubsystem.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayOrchestrator.h"
#include "PSPlayerPawn.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"

namespace PSPreSnapPrivate
{
    /** A player the pre-snap calls can move: an offensive receiver, tight end or back. */
    bool IsEligible(const APSPlayerPawn* Pawn)
    {
        if (!Pawn || Pawn->TeamSide != EPSTeamSide::Offense)
        {
            return false;
        }
        const EPlayerRole Role = Pawn->GetAttributes().Role;
        return Role == EPlayerRole::WideReceiver || Role == EPlayerRole::TightEnd || Role == EPlayerRole::RunningBack;
    }

    bool IsRun(const FPSPlayDefinition& Play)
    {
        return Play.PlayCategory == TEXT("Run");
    }

    /** Unit direction from From to To on the ground; zero when they coincide. */
    FVector GroundDirection(const FVector& From, const FVector& To)
    {
        FVector Direction = To - From;
        Direction.Z = 0.f;
        return Direction.GetSafeNormal();
    }

    FName SlideName(EPSSlideDirection Direction)
    {
        switch (Direction)
        {
        case EPSSlideDirection::Left:  return TEXT("SlideLeft");
        case EPSSlideDirection::Right: return TEXT("SlideRight");
        default:                       return TEXT("NoSlide");
        }
    }

    FName ProtectionName(EPSProtectionCall Call)
    {
        switch (Call)
        {
        case EPSProtectionCall::Block:   return TEXT("Block");
        case EPSProtectionCall::Release: return TEXT("Release");
        default:                         return TEXT("AsCalled");
        }
    }
}

void UPSPreSnapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // The call authority first: its snap handler hands out the call, reading this.
    Collection.InitializeDependency<UPSPlayCallSubsystem>();
    UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>();
    if (Bus)
    {
        Bus->OnPlayCallMC.AddUObject(this, &UPSPreSnapSubsystem::HandlePlayCall);
        Bus->OnSnapMC.AddUObject(this, &UPSPreSnapSubsystem::HandleSnap);
        Bus->OnPhaseChangeMC.AddUObject(this, &UPSPreSnapSubsystem::HandlePhaseChange);
        BoundBus = Bus;
    }
}

void UPSPreSnapSubsystem::Deinitialize()
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

bool UPSPreSnapSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSPreSnapSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    TickMotion(DeltaTime);
}

TStatId UPSPreSnapSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSPreSnapSubsystem, STATGROUP_Tickables);
}

FString UPSPreSnapSubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/presnap_tuning.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPreSnapTuningRow& UPSPreSnapSubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSPreSnapSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPreSnapTuningRow Loaded;
    if (!Ingestion->LoadPreSnapTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPreSnapSubsystem: Could not load pre-snap tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    Tuning = Loaded;
    return true;
}

UPSPlayCallSubsystem* UPSPreSnapSubsystem::GetPlayCall() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
}

bool UPSPreSnapSubsystem::GetOffensePlay(FPSPlayDefinition& OutPlay) const
{
    UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    return PlayCall && PlayCall->GetCall(true).IsSet() && PlayCall->FindPlay(PlayCall->GetCall(true).PlayId, OutPlay);
}

TArray<APSPlayerPawn*> UPSPreSnapSubsystem::GetFieldPawns() const
{
    TArray<APSPlayerPawn*> Pawns;
    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        Pawns.Add(*It);
    }
    return Pawns;
}

APSPlayerPawn* UPSPreSnapSubsystem::FindAIQuarterback() const
{
    for (APSPlayerPawn* Pawn : GetFieldPawns())
    {
        if (Pawn->TeamSide == EPSTeamSide::Offense && Pawn->GetAttributes().Role == EPlayerRole::Quarterback && !Pawn->IsUserControlled())
        {
            return Pawn;
        }
    }
    return nullptr;
}

bool UPSPreSnapSubsystem::IsAdjustable() const
{
    const UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    return PlayCall && PlayCall->IsCallWindowOpen() && PlayCall->GetCall(true).IsSet();
}

void UPSPreSnapSubsystem::ResetAdjustments()
{
    Adjustments.Reset();
    Slide = EPSSlideDirection::None;
}

void UPSPreSnapSubsystem::HandlePlayCall(const FPSTelemetryPlayCallEvent& Event)
{
    if (Event.bOffense)
    {
        // A new call (an audible too): the old call's routes and protection go with it.
        ResetAdjustments();
    }

    // Once both calls are in, a CPU offense's quarterback reads the defense.
    const UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    if (!bCpuReadDone && IsAdjustable() && PlayCall->GetCall(false).IsSet()
        && PlayCall->GetCall(true).Caller == EPSPlayCaller::CPU && !PlayCall->IsHumanSide(true))
    {
        RunCpuRead();
    }
}

void UPSPreSnapSubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    // Motion ends at the snap: the man in motion runs his route from where he is. The
    // changes themselves stay until the next down; the play-out reads them now.
    bMotionActive = false;
    bTravelActive = false;
    bCpuReadDone = false;
}

void UPSPreSnapSubsystem::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("PreSnap"))
    {
        ResetAdjustments();
        MotionPlayer.Reset();
        TravellingDefender.Reset();
        bMotionActive = false;
        bTravelActive = false;
        bCpuReadDone = false;
    }
}

// --- Audibles ---

TArray<FPSPlayDefinition> UPSPreSnapSubsystem::GetAudibles() const
{
    TArray<FPSPlayDefinition> Audibles;
    FPSPlayDefinition Current;
    UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    if (!IsAdjustable() || !GetOffensePlay(Current))
    {
        return Audibles;
    }
    for (const FPSPlayDefinition& Play : PlayCall->GetPlaysInFormation(Current.Formation, true))
    {
        if (Play.PlayId != Current.PlayId)
        {
            Audibles.Add(Play);
        }
    }
    return Audibles;
}

bool UPSPreSnapSubsystem::Audible(FName PlayId, bool bHuman)
{
    UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    const bool bCompatible = GetAudibles().ContainsByPredicate([PlayId](const FPSPlayDefinition& Play) { return Play.PlayId == PlayId; });
    if (!bCompatible || !PlayCall->CallPlay(PlayId, bHuman ? EPSPlayCaller::Human : EPSPlayCaller::CPU))
    {
        return false;
    }
    Publish(EPSPreSnapAction::Audible, nullptr, PlayId, bHuman);
    return true;
}

bool UPSPreSnapSubsystem::AudibleToNext(bool bHuman)
{
    FPSPlayDefinition Current;
    if (!IsAdjustable() || !GetOffensePlay(Current))
    {
        return false;
    }
    const TArray<FPSPlayDefinition> Formation = GetPlayCall()->GetPlaysInFormation(Current.Formation, true);
    const int32 Index = Formation.IndexOfByPredicate([&Current](const FPSPlayDefinition& Play) { return Play.PlayId == Current.PlayId; });
    if (Formation.Num() < 2 || Index == INDEX_NONE)
    {
        return false;
    }
    return Audible(Formation[(Index + 1) % Formation.Num()].PlayId, bHuman);
}

// --- Hot routes ---

EPSReceiverAlignment UPSPreSnapSubsystem::GetAlignment(const APSPlayerPawn* Player)
{
    const EPlayerRole Role = Player ? Player->GetAttributes().Role : EPlayerRole::WideReceiver;
    if (Role == EPlayerRole::TightEnd)
    {
        return EPSReceiverAlignment::Tight;
    }
    if (Role == EPlayerRole::RunningBack)
    {
        return EPSReceiverAlignment::Backfield;
    }
    const float Split = Player ? FMath::Abs(Player->GetActorLocation().Y) : 0.f;
    return Split <= GetTuning().SlotMaxSplit ? EPSReceiverAlignment::Slot : EPSReceiverAlignment::Wide;
}

TArray<FName> UPSPreSnapSubsystem::GetAllowedHotRoutes(const APSPlayerPawn* Player)
{
    if (!PSPreSnapPrivate::IsEligible(Player))
    {
        return TArray<FName>();
    }
    const EPSReceiverAlignment Alignment = GetAlignment(Player);
    const FPSHotRouteSet* Set = GetTuning().HotRouteSets.FindByPredicate([Alignment](const FPSHotRouteSet& Candidate) { return Candidate.Alignment == Alignment; });
    return Set ? Set->Routes : TArray<FName>();
}

bool UPSPreSnapSubsystem::GetCalledAssignment(const APSPlayerPawn* Player, const FPSPlayDefinition& Play, FPSPlayAssignment& OutAssignment) const
{
    if (!Player)
    {
        return false;
    }
    // The orchestrator hands slots out in the field's actor order, role by role.
    const EPlayerRole Role = Player->GetAttributes().Role;
    int32 RoleIndex = 0;
    for (const APSPlayerPawn* Pawn : GetFieldPawns())
    {
        if (Pawn == Player)
        {
            break;
        }
        if (Pawn->GetAttributes().Role == Role)
        {
            ++RoleIndex;
        }
    }
    const FPSPlayAssignment* Slot = UPSPlayOrchestrator::FindAssignmentSlot(Play, Role, RoleIndex);
    if (!Slot)
    {
        return false;
    }
    OutAssignment = *Slot;
    return true;
}

FName UPSPreSnapSubsystem::GetCurrentRouteId(const APSPlayerPawn* Player)
{
    FPSPlayDefinition Play;
    FPSPlayAssignment Assignment;
    if (!GetOffensePlay(Play) || !GetCalledAssignment(Player, Play, Assignment))
    {
        return NAME_None;
    }
    ApplyAdjustment(Player, Assignment);
    return Assignment.Kind == EPSAssignmentKind::Route ? Assignment.RouteId : NAME_None;
}

bool UPSPreSnapSubsystem::RunsRoute(const APSPlayerPawn* Player)
{
    // A route with no RouteId is a spot (the QB's drop, the back's mesh), not a pattern.
    return !GetCurrentRouteId(Player).IsNone();
}

bool UPSPreSnapSubsystem::HotRoute(APSPlayerPawn* Player, FName RouteId, bool bHuman)
{
    if (!IsAdjustable() || !PSPreSnapPrivate::IsEligible(Player) || RouteId.IsNone() || !GetAllowedHotRoutes(Player).Contains(RouteId))
    {
        return false;
    }
    const UDataTable* Routes = GetPlayCall()->GetRouteLibrary();
    if (!Routes || !Routes->FindRow<FPSRoute>(RouteId, TEXT("UPSPreSnapSubsystem"), false) || !RunsRoute(Player))
    {
        return false;
    }
    Adjustments.FindOrAdd(FObjectKey(Player)).HotRouteId = RouteId;
    Publish(EPSPreSnapAction::HotRoute, Player, RouteId, bHuman);
    return true;
}

bool UPSPreSnapSubsystem::CycleHotRoute(APSPlayerPawn* Player, bool bHuman)
{
    const TArray<FName> Allowed = GetAllowedHotRoutes(Player);
    if (!IsAdjustable() || Allowed.Num() == 0)
    {
        return false;
    }
    const int32 Current = Allowed.IndexOfByKey(GetCurrentRouteId(Player));
    for (int32 Step = 1; Step <= Allowed.Num(); ++Step)
    {
        const int32 Next = (Current + Step) % Allowed.Num();
        if (Next != Current && HotRoute(Player, Allowed[Next], bHuman))
        {
            return true;
        }
    }
    return false;
}

// --- Motion ---

APSPlayerPawn* UPSPreSnapSubsystem::FindManDefenderOver(const APSPlayerPawn* Player)
{
    UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    FPSPlayDefinition Defense;
    if (!PlayCall || !PlayCall->GetDefensivePlayToRun(Defense))
    {
        return nullptr;
    }

    // The AI defender in man coverage lined up over him, across the line.
    APSPlayerPawn* Over = nullptr;
    float NearestLateral = GetTuning().ManTravelLateralRadius;
    const FVector Location = Player->GetActorLocation();
    for (APSPlayerPawn* Candidate : GetFieldPawns())
    {
        if (Candidate->TeamSide != EPSTeamSide::Defense || Candidate->IsUserControlled() || Candidate->GetActorLocation().X <= Location.X)
        {
            continue;
        }
        const float Lateral = FMath::Abs(Candidate->GetActorLocation().Y - Location.Y);
        FPSPlayAssignment Assignment;
        if (Lateral <= NearestLateral && GetCalledAssignment(Candidate, Defense, Assignment) && Assignment.Kind == EPSAssignmentKind::ManCoverage)
        {
            NearestLateral = Lateral;
            Over = Candidate;
        }
    }
    return Over;
}

bool UPSPreSnapSubsystem::StartMotion(APSPlayerPawn* Player, bool bHuman)
{
    if (!IsAdjustable() || bMotionActive || !PSPreSnapPrivate::IsEligible(Player) || Player->IsUserControlled())
    {
        return false;
    }

    // Across the formation, to the other side of the ball.
    const FVector Location = Player->GetActorLocation();
    const float Side = Location.Y >= 0.f ? 1.f : -1.f;
    MotionTarget = FVector(Location.X, -Side * GetTuning().MotionEndSplit, Location.Z);
    MotionPlayer = Player;
    bMotionActive = true;

    // Man coverage shows itself: the defender over him goes with him.
    APSPlayerPawn* Defender = FindManDefenderOver(Player);
    TravellingDefender = Defender;
    bTravelActive = Defender != nullptr;
    Publish(EPSPreSnapAction::Motion, Player, TEXT("Motion"), bHuman, Defender);
    return true;
}

void UPSPreSnapSubsystem::TickMotion(float DeltaSeconds)
{
    APSPlayerPawn* Mover = MotionPlayer.Get();
    if (!Mover)
    {
        bMotionActive = false;
        bTravelActive = false;
        return;
    }
    const float Arrival = GetTuning().MotionArrivalRadius;

    if (bMotionActive)
    {
        if (FVector::Dist2D(Mover->GetActorLocation(), MotionTarget) <= Arrival)
        {
            bMotionActive = false;
        }
        else
        {
            Mover->AddMovementInput(PSPreSnapPrivate::GroundDirection(Mover->GetActorLocation(), MotionTarget), 1.f);
        }
    }

    // The travelling defender stays over him, at his own depth, until the snap.
    APSPlayerPawn* Defender = TravellingDefender.Get();
    if (bTravelActive && Defender)
    {
        const FVector Spot(Defender->GetActorLocation().X, Mover->GetActorLocation().Y, Defender->GetActorLocation().Z);
        if (FVector::Dist2D(Defender->GetActorLocation(), Spot) > Arrival)
        {
            Defender->AddMovementInput(PSPreSnapPrivate::GroundDirection(Defender->GetActorLocation(), Spot), 1.f);
        }
    }
}

// --- Protection ---

bool UPSPreSnapSubsystem::SetProtection(APSPlayerPawn* Player, EPSProtectionCall Call, bool bHuman)
{
    FPSPlayDefinition Play;
    FPSPlayAssignment Called;
    if (!IsAdjustable() || !Player || Player->TeamSide != EPSTeamSide::Offense || !GetOffensePlay(Play) || PSPreSnapPrivate::IsRun(Play)
        || !GetCalledAssignment(Player, Play, Called))
    {
        return false;
    }
    const EPlayerRole Role = Player->GetAttributes().Role;
    if (Role != EPlayerRole::RunningBack && Role != EPlayerRole::TightEnd)
    {
        return false;
    }
    const bool bCalledRoute = Called.Kind == EPSAssignmentKind::Route && !Called.RouteId.IsNone();
    const bool bCalledBlock = Called.Kind == EPSAssignmentKind::PassBlock || Called.Kind == EPSAssignmentKind::RunBlock;
    if ((Call == EPSProtectionCall::Block && !bCalledRoute) || (Call == EPSProtectionCall::Release && !bCalledBlock))
    {
        return false;
    }

    const FObjectKey Key(Player);
    FPSPreSnapPlayerAdjustment& Adjustment = Adjustments.FindOrAdd(Key);
    Adjustment.Protection = Call;
    if (!RunsRoute(Player))
    {
        // A player kept in runs no hot route.
        Adjustment.HotRouteId = NAME_None;
    }
    if (Adjustment.Protection == EPSProtectionCall::AsCalled && Adjustment.HotRouteId.IsNone())
    {
        Adjustments.Remove(Key);
    }
    Publish(EPSPreSnapAction::Protection, Player, PSPreSnapPrivate::ProtectionName(Call), bHuman);
    return true;
}

bool UPSPreSnapSubsystem::ToggleProtection(APSPlayerPawn* Player, bool bHuman)
{
    const FPSPreSnapPlayerAdjustment* Current = FindAdjustment(Player);
    if (Current && Current->Protection != EPSProtectionCall::AsCalled)
    {
        return SetProtection(Player, EPSProtectionCall::AsCalled, bHuman);
    }
    FPSPlayDefinition Play;
    FPSPlayAssignment Called;
    if (!GetOffensePlay(Play) || !GetCalledAssignment(Player, Play, Called))
    {
        return false;
    }
    return SetProtection(Player, Called.Kind == EPSAssignmentKind::Route ? EPSProtectionCall::Block : EPSProtectionCall::Release, bHuman);
}

bool UPSPreSnapSubsystem::SetSlide(EPSSlideDirection Direction, bool bHuman)
{
    FPSPlayDefinition Play;
    if (!IsAdjustable() || !GetOffensePlay(Play) || PSPreSnapPrivate::IsRun(Play))
    {
        return false;
    }
    Slide = Direction;
    Publish(EPSPreSnapAction::Protection, nullptr, PSPreSnapPrivate::SlideName(Direction), bHuman);
    return true;
}

bool UPSPreSnapSubsystem::CycleSlide(bool bHuman)
{
    const EPSSlideDirection Next = Slide == EPSSlideDirection::None ? EPSSlideDirection::Left
        : (Slide == EPSSlideDirection::Left ? EPSSlideDirection::Right : EPSSlideDirection::None);
    return SetSlide(Next, bHuman);
}

FVector UPSPreSnapSubsystem::GetSlideAimOffset()
{
    switch (Slide)
    {
    case EPSSlideDirection::Left:  return FVector(0.f, -GetTuning().SlideAimOffset, 0.f);
    case EPSSlideDirection::Right: return FVector(0.f, GetTuning().SlideAimOffset, 0.f);
    default:                       return FVector::ZeroVector;
    }
}

TArray<TPair<APSPlayerPawn*, APSPlayerPawn*>> UPSPreSnapSubsystem::ComputeBlockingPairs(const TArray<APSPlayerPawn*>& Linemen, const TArray<APSPlayerPawn*>& Rushers, const FVector& AimOffset)
{
    // On a slide the linemen on the slide side pick first, so each takes the man in his
    // slide gap and the backside rusher is the one left over.
    TArray<APSPlayerPawn*> Ordered = Linemen.FilterByPredicate([](const APSPlayerPawn* Lineman) { return Lineman != nullptr; });
    const float Toward = FMath::Sign(AimOffset.Y);
    if (Toward != 0.f)
    {
        Ordered.StableSort([Toward](const APSPlayerPawn& A, const APSPlayerPawn& B)
        {
            return A.GetActorLocation().Y * Toward > B.GetActorLocation().Y * Toward;
        });
    }

    TArray<TPair<APSPlayerPawn*, APSPlayerPawn*>> Pairs;
    TSet<const APSPlayerPawn*> Claimed;
    for (APSPlayerPawn* Lineman : Ordered)
    {
        const FVector Aim = Lineman->GetActorLocation() + AimOffset;
        APSPlayerPawn* Best = nullptr;
        float BestDistance = TNumericLimits<float>::Max();
        for (APSPlayerPawn* Rusher : Rushers)
        {
            if (!Rusher || Claimed.Contains(Rusher))
            {
                continue;
            }
            const float Distance = FVector::Dist(Aim, Rusher->GetActorLocation());
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                Best = Rusher;
            }
        }
        if (Best)
        {
            Claimed.Add(Best);
            Pairs.Emplace(Lineman, Best);
        }
    }
    return Pairs;
}

// --- What the play-out reads ---

const FPSPreSnapPlayerAdjustment* UPSPreSnapSubsystem::FindAdjustment(const APSPlayerPawn* Player) const
{
    return Player ? Adjustments.Find(FObjectKey(Player)) : nullptr;
}

void UPSPreSnapSubsystem::ApplyAdjustment(const APSPlayerPawn* Player, FPSPlayAssignment& InOutAssignment)
{
    const FPSPreSnapPlayerAdjustment* Adjustment = FindAdjustment(Player);
    if (!Adjustment)
    {
        return;
    }
    if (Adjustment->Protection == EPSProtectionCall::Block)
    {
        InOutAssignment.Kind = EPSAssignmentKind::PassBlock;
        InOutAssignment.RouteId = NAME_None;
        return;
    }
    if (Adjustment->Protection == EPSProtectionCall::Release)
    {
        const EPSReceiverAlignment Alignment = GetAlignment(Player);
        const FPSHotRouteSet* Set = GetTuning().HotRouteSets.FindByPredicate([Alignment](const FPSHotRouteSet& Candidate) { return Candidate.Alignment == Alignment; });
        InOutAssignment.Kind = EPSAssignmentKind::Route;
        InOutAssignment.RouteId = Set ? Set->ReleaseRoute : NAME_None;
        InOutAssignment.FormationOffset = FVector::ZeroVector;
    }
    if (!Adjustment->HotRouteId.IsNone() && InOutAssignment.Kind == EPSAssignmentKind::Route)
    {
        InOutAssignment.RouteId = Adjustment->HotRouteId;
    }
}

FPSDefensiveLook UPSPreSnapSubsystem::GetDefensiveLook()
{
    FPSDefensiveLook Look;
    // The defense lines up for its call before the offense reads it (Epic 67).
    UPSDefenderPreSnapSubsystem* DefensePreSnap = GetWorld() ? GetWorld()->GetSubsystem<UPSDefenderPreSnapSubsystem>() : nullptr;
    if (DefensePreSnap)
    {
        DefensePreSnap->EnsureAligned();
    }
    UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    FPSPlayDefinition Defense;
    if (PlayCall && PlayCall->GetDefensivePlayToRun(Defense))
    {
        Look.Front = Defense.Front;
        Look.CoverageShell = Defense.CoverageShell;
        Look.bShowsBlitz = Defense.PlayCategory == TEXT("Blitz")
            || Defense.Assignments.ContainsByPredicate([](const FPSPlayAssignment& Assignment) { return Assignment.Kind == EPSAssignmentKind::Blitz; });
    }

    // The box, measured from the front of the offensive line, centred on it.
    const TArray<APSPlayerPawn*> Pawns = GetFieldPawns();
    float LineX = -TNumericLimits<float>::Max();
    float CentreY = 0.f;
    int32 Linemen = 0;
    for (const APSPlayerPawn* Pawn : Pawns)
    {
        if (Pawn->TeamSide == EPSTeamSide::Offense && Pawn->GetAttributes().Role == EPlayerRole::OffensiveLineman)
        {
            LineX = FMath::Max(LineX, Pawn->GetActorLocation().X);
            CentreY += Pawn->GetActorLocation().Y;
            ++Linemen;
        }
    }
    if (Linemen == 0)
    {
        return Look;
    }
    CentreY /= Linemen;

    // The shell and the blitz as the defense shows them, not as it called them (Epic 67).
    int32 DeepSafeties = 0;
    bool bShowsBlitz = false;
    if (DefensePreSnap && DefensePreSnap->ReadShownLook(DeepSafeties, bShowsBlitz))
    {
        Look.CoverageShell = UPSDefenderPreSnapSubsystem::DescribeStructure(DeepSafeties);
        Look.bShowsBlitz = bShowsBlitz;
    }

    const FPreSnapTuningRow& Settings = GetTuning();
    for (const APSPlayerPawn* Pawn : Pawns)
    {
        if (Pawn->TeamSide != EPSTeamSide::Defense)
        {
            continue;
        }
        const FVector Location = Pawn->GetActorLocation();
        const float Across = Location.Y - CentreY;
        const float Off = Location.X - LineX;
        if (Off >= 0.f && Off <= Settings.BoxDepth && FMath::Abs(Across) <= Settings.BoxWidth)
        {
            ++Look.BoxCount;
            Look.BoxLeft += Across < 0.f ? 1 : 0;
            Look.BoxRight += Across > 0.f ? 1 : 0;
        }
    }
    return Look;
}

void UPSPreSnapSubsystem::RunCpuRead()
{
    bCpuReadDone = true;
    const FPreSnapTuningRow& Settings = GetTuning();
    const APSPlayerPawn* Quarterback = FindAIQuarterback();
    FPSPlayDefinition Play;
    if (!Quarterback || Quarterback->GetAttributes().Awareness < Settings.CpuReadMinAwareness || !GetOffensePlay(Play))
    {
        return;
    }
    const FPSDefensiveLook Look = GetDefensiveLook();

    // 1. Check out of a bad call: a run into a blitz or a heavy box goes to a pass, quickest
    //    first; a pass against a light box with no blitz goes to a run. Unless he is kept to it.
    const TArray<FPSPlayDefinition> Audibles = bCpuAudiblesAllowed ? GetAudibles() : TArray<FPSPlayDefinition>();
    const FPSPlayDefinition* CheckTo = nullptr;
    if (PSPreSnapPrivate::IsRun(Play) && (Look.bShowsBlitz || Look.BoxCount >= Settings.HeavyBoxCount))
    {
        for (const TCHAR* Category : { TEXT("ShortPass"), TEXT("Screen"), TEXT("PlayAction"), TEXT("DeepPass") })
        {
            CheckTo = Audibles.FindByPredicate([Category](const FPSPlayDefinition& Candidate) { return Candidate.PlayCategory == Category; });
            if (CheckTo)
            {
                break;
            }
        }
    }
    else if (!PSPreSnapPrivate::IsRun(Play) && !Look.bShowsBlitz && Look.BoxCount <= Settings.LightBoxCount)
    {
        CheckTo = Audibles.FindByPredicate([](const FPSPlayDefinition& Candidate) { return PSPreSnapPrivate::IsRun(Candidate); });
    }
    if (CheckTo && Audible(CheckTo->PlayId, false))
    {
        GetOffensePlay(Play);
    }
    if (PSPreSnapPrivate::IsRun(Play))
    {
        return;
    }

    // The slot receiver: the AI wide receiver nearest the ball who runs a route.
    APSPlayerPawn* SlotReceiver = nullptr;
    APSPlayerPawn* Back = nullptr;
    for (APSPlayerPawn* Pawn : GetFieldPawns())
    {
        if (!PSPreSnapPrivate::IsEligible(Pawn) || Pawn->IsUserControlled() || !RunsRoute(Pawn))
        {
            continue;
        }
        const EPlayerRole Role = Pawn->GetAttributes().Role;
        if (Role == EPlayerRole::WideReceiver && (!SlotReceiver || FMath::Abs(Pawn->GetActorLocation().Y) < FMath::Abs(SlotReceiver->GetActorLocation().Y)))
        {
            SlotReceiver = Pawn;
        }
        else if (Role == EPlayerRole::RunningBack && !Back)
        {
            Back = Pawn;
        }
    }

    // 2. Beat the blitz: the slot runs the quick route, the back stays in.
    if (Look.bShowsBlitz)
    {
        if (SlotReceiver && !Settings.BlitzHotRoute.IsNone())
        {
            HotRoute(SlotReceiver, Settings.BlitzHotRoute, false);
        }
        if (Back && Settings.bCpuKeepsBackInVsBlitz)
        {
            SetProtection(Back, EPSProtectionCall::Block, false);
        }
    }

    // 3. Slide the line toward the side with more men in the box.
    if (Look.BoxLeft != Look.BoxRight)
    {
        SetSlide(Look.BoxLeft > Look.BoxRight ? EPSSlideDirection::Left : EPSSlideDirection::Right, false);
    }

    // 4. Motion the slot across to see man or zone.
    if (Settings.bCpuMotionOnPass && SlotReceiver)
    {
        StartMotion(SlotReceiver, false);
    }
}

void UPSPreSnapSubsystem::Publish(EPSPreSnapAction Action, const APSPlayerPawn* Player, FName Detail, bool bHuman, const APSPlayerPawn* Defender)
{
    UE_LOG(LogTemp, Display, TEXT("UPSPreSnapSubsystem: %s %s %s (%s)."), *UEnum::GetValueAsString(Action),
        Player ? *Player->GetAttributes().DisplayName : TEXT(""), *Detail.ToString(), bHuman ? TEXT("human") : TEXT("CPU"));

    UPSTelemetryBus* Bus = BoundBus.Get();
    if (!Bus)
    {
        return;
    }
    FPSTelemetryPreSnapEvent Event;
    Event.Action = Action;
    Event.PlayerName = Player ? Player->GetAttributes().DisplayName : FString();
    Event.Detail = Detail;
    Event.bHumanCall = bHuman;
    Event.DefenderName = Defender ? Defender->GetAttributes().DisplayName : FString();
    Event.bManIndicator = Defender != nullptr;
    Bus->PublishPreSnap(Event);
}
