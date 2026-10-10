#include "PSRouteRunnerComponent.h"
#include "PSDataIngestion.h"
#include "PSHealthComponent.h"
#include "PSOffenseController.h"
#include "PSPlayerPawn.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace PSRouteRunnerPrivate
{
    /** Unit direction from From to To on the ground; zero when they coincide. */
    FVector GroundDirection(const FVector& From, const FVector& To)
    {
        FVector Direction = To - From;
        Direction.Z = 0.f;
        return Direction.GetSafeNormal();
    }

    /** The nearest standing opponent of Self within Radius, or null. */
    APSPlayerPawn* NearestOpponentWithin(const TArray<APSPlayerPawn*>& Pawns, const APSPlayerPawn* Self, float Radius, bool bInFrontOnly)
    {
        APSPlayerPawn* Nearest = nullptr;
        float NearestDistance = Radius;
        const FVector Location = Self->GetActorLocation();
        for (APSPlayerPawn* Candidate : Pawns)
        {
            if (!Candidate || Candidate->TeamSide == Self->TeamSide || (bInFrontOnly && Candidate->GetActorLocation().X <= Location.X))
            {
                continue;
            }
            const UPSHealthComponent* Health = Candidate->GetHealthComponent();
            if (Health && Health->IsDowned())
            {
                continue;
            }
            const float Distance = FVector::Dist2D(Candidate->GetActorLocation(), Location);
            if (Distance <= NearestDistance)
            {
                NearestDistance = Distance;
                Nearest = Candidate;
            }
        }
        return Nearest;
    }
}

UPSRouteRunnerComponent::UPSRouteRunnerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

FString UPSRouteRunnerComponent::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/route_running.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FRouteRunningTuningRow& UPSRouteRunnerComponent::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSRouteRunnerComponent::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FRouteRunningTuningRow Loaded;
    if (!Ingestion->LoadRouteRunningTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSRouteRunnerComponent: Could not load route-running tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    Tuning = Loaded;
    return true;
}

void UPSRouteRunnerComponent::SetTuning(const FRouteRunningTuningRow& InTuning)
{
    Tuning = InTuning;
    bTuningLoaded = true;
}

void UPSRouteRunnerComponent::SetRoutePlan(const FPSRoute& Route, const TArray<FVector>& WorldWaypoints, float InMirror, const UDataTable* RouteLibrary, int32 Seed)
{
    const FRouteRunningTuningRow& Settings = GetTuning();
    bHasPlan = WorldWaypoints.Num() == Route.Waypoints.Num() && WorldWaypoints.Num() > 0;
    PlannedWaypoints = WorldWaypoints;
    FakeWaypoints.Reset();
    for (const FPSRouteWaypoint& Waypoint : Route.Waypoints)
    {
        FakeWaypoints.Add(Waypoint.bFake);
    }
    Library = RouteLibrary;
    VsManBranch = Route.VsManBranch;
    VsZoneBranch = Route.VsZoneBranch;
    Mirror = InMirror < 0.f ? -1.f : 1.f;
    OptionReadIndex = Route.Waypoints.IsValidIndex(Route.OptionReadWaypoint) ? Route.OptionReadWaypoint : INDEX_NONE;
    BreakIndex = OptionReadIndex == INDEX_NONE ? PSRouteRunning::FindBreakWaypoint(Route, Settings) : INDEX_NONE;
    ReadTime = PSRouteRunning::ReadTime(Route, RouteLibrary, Settings);
    Rolls.Initialize(Seed);
    ResetRun();
}

void UPSRouteRunnerComponent::ClearRoutePlan()
{
    bHasPlan = false;
    PlannedWaypoints.Reset();
    FakeWaypoints.Reset();
    Library.Reset();
    BreakIndex = INDEX_NONE;
    OptionReadIndex = INDEX_NONE;
    ReadTime = 0.f;
    ResetRun();
}

void UPSRouteRunnerComponent::ResetRun()
{
    bStarted = false;
    bBroken = false;
    bRead = false;
    HoldUntil = -1.f;
    ReleaseOutcome = EPSReleaseOutcome::Unpressed;
    CoverageRead = EPSCoverageRead::Zone;
}

bool UPSRouteRunnerComponent::GetCoverageRead(EPSCoverageRead& OutRead) const
{
    OutRead = CoverageRead;
    return bRead;
}

bool UPSRouteRunnerComponent::HasPlan() const
{
    return IsPlanFor(Cast<APSOffenseController>(GetOwner()));
}

bool UPSRouteRunnerComponent::IsPlanFor(const APSOffenseController* Controller) const
{
    // The plan describes the route it was made for; a route replaced since runs plainly.
    return bHasPlan && Controller && Controller->GetRouteWaypoints() == PlannedWaypoints;
}

FVector UPSRouteRunnerComponent::Steer(APSPlayerPawn* Self, APSOffenseController* Controller, const TArray<APSPlayerPawn*>& Pawns, float TimeSinceSnap, float ArrivalRadius, bool& bOutFinished)
{
    bOutFinished = false;
    if (!Self || !Controller || Controller->GetRouteWaypointCount() == 0)
    {
        return FVector::ZeroVector;
    }
    const FRouteRunningTuningRow& Settings = GetTuning();

    if (!bStarted)
    {
        bStarted = true;
        StartLocation = Self->GetActorLocation();
        if (IsPlanFor(Controller))
        {
            ContestRelease(Self, Controller, Pawns, TimeSinceSnap);
        }
    }
    if (TimeSinceSnap < HoldUntil)
    {
        return FVector::ZeroVector;
    }

    const bool bPlanned = IsPlanFor(Controller);
    const TArray<FVector>& Waypoints = Controller->GetRouteWaypoints();
    const int32 Index = Controller->GetRouteWaypointIndex();
    const bool bLast = Index + 1 >= Waypoints.Num();
    const bool bFake = bPlanned && FakeWaypoints.IsValidIndex(Index) && FakeWaypoints[Index];
    const bool bOptionRead = bPlanned && Index == OptionReadIndex;
    const FVector Target = Controller->GetCurrentTargetLocation();

    // A break is rounded by how stiff he is: he turns for the next leg before the corner. A
    // fake is sold at the spot, and a read is made there.
    float Arrival = ArrivalRadius;
    if (!bLast && !bFake && !bOptionRead)
    {
        const FVector Previous = Index > 0 ? Waypoints[Index - 1] : StartLocation;
        if (PSRouteRunning::TurnAngleDegrees(Previous, Target, Waypoints[Index + 1]) >= Settings.BreakMinAngleDegrees)
        {
            Arrival = FMath::Max(Arrival, PSRouteRunning::BreakRounding(Self->GetAttributes().Agility, Settings));
        }
    }
    if (FVector::Dist2D(Self->GetActorLocation(), Target) > Arrival)
    {
        return PSRouteRunnerPrivate::GroundDirection(Self->GetActorLocation(), Target);
    }

    // At the waypoint.
    if (bOptionRead)
    {
        ReadOption(Self, Controller, Pawns);
        return PSRouteRunnerPrivate::GroundDirection(Self->GetActorLocation(), Controller->GetCurrentTargetLocation());
    }
    if (bPlanned && Index == BreakIndex)
    {
        bBroken = true;
    }
    if (bFake)
    {
        SellFake(Self, Pawns, TimeSinceSnap);
    }
    if (bLast)
    {
        bOutFinished = true;
        return FVector::ZeroVector;
    }
    // The plan follows the controller's route: only the index moves.
    Controller->AdvanceToNextWaypoint();
    if (TimeSinceSnap < HoldUntil)
    {
        return FVector::ZeroVector;
    }
    return PSRouteRunnerPrivate::GroundDirection(Self->GetActorLocation(), Controller->GetCurrentTargetLocation());
}

void UPSRouteRunnerComponent::ContestRelease(APSPlayerPawn* Self, APSOffenseController* Controller, const TArray<APSPlayerPawn*>& Pawns, float TimeSinceSnap)
{
    const FRouteRunningTuningRow& Settings = GetTuning();
    const APSPlayerPawn* Presser = PSRouteRunnerPrivate::NearestOpponentWithin(Pawns, Self, Settings.PressRadius, true);
    if (!Presser)
    {
        ReleaseOutcome = EPSReleaseOutcome::Unpressed;
        return;
    }

    ReleaseOutcome = PSRouteRunning::ResolveRelease(Self->GetAttributes(), Presser->GetAttributes(), Rolls.FRand(), Settings);
    float Held = 0.f;
    FName Outcome = TEXT("Win");
    if (ReleaseOutcome == EPSReleaseOutcome::Delay)
    {
        Held = Settings.DelaySeconds;
        Outcome = TEXT("Delay");
    }
    else if (ReleaseOutcome == EPSReleaseOutcome::Reroute)
    {
        // Pushed off his stem toward his sideline: the whole route moves over.
        Held = Settings.RerouteDelaySeconds;
        Outcome = TEXT("Reroute");
        const FVector Push(0.f, Mirror * Settings.RerouteOffset, 0.f);
        TArray<FVector> Rerouted = Controller->GetRouteWaypoints();
        for (FVector& Waypoint : Rerouted)
        {
            Waypoint += Push;
        }
        Controller->SetAssignedRoute(Rerouted);
        PlannedWaypoints = Rerouted;
    }
    HoldUntil = TimeSinceSnap + Held;
    Publish(EPSRouteEventKind::Release, Self, Presser, Outcome, Held);
}

void UPSRouteRunnerComponent::SellFake(APSPlayerPawn* Self, const TArray<APSPlayerPawn*>& Pawns, float TimeSinceSnap)
{
    const FRouteRunningTuningRow& Settings = GetTuning();
    HoldUntil = TimeSinceSnap + Settings.FakeSellSeconds;
    const APSPlayerPawn* Defender = PSRouteRunnerPrivate::NearestOpponentWithin(Pawns, Self, Settings.BiteRadius, false);
    if (!Defender)
    {
        return;
    }
    // The bite is decided here, once: the fake's one authority. A defender who bit freezes.
    const float Chance = PSRouteRunning::BiteChance(Self->GetAttributes().Agility, Defender->GetAttributes().Awareness, Settings);
    const bool bBit = Rolls.FRand() < Chance;
    Publish(EPSRouteEventKind::DoubleMove, Self, Defender, bBit ? FName(TEXT("Bit")) : FName(TEXT("Stayed")), bBit ? Settings.BiteFreezeSeconds : 0.f);
}

void UPSRouteRunnerComponent::ReadOption(APSPlayerPawn* Self, APSOffenseController* Controller, const TArray<APSPlayerPawn*>& Pawns)
{
    const FRouteRunningTuningRow& Settings = GetTuning();
    const APSPlayerPawn* Defender = PSRouteRunnerPrivate::NearestOpponentWithin(Pawns, Self, Settings.ManReadRadius, false);
    CoverageRead = Defender ? EPSCoverageRead::Man : EPSCoverageRead::Zone;
    bRead = true;
    bBroken = true;

    // Man: break away from the defender's leverage (the branch is authored breaking outside).
    // Zone: settle in the hole.
    float BranchMirror = Mirror;
    if (Defender && (Defender->GetActorLocation().Y - Self->GetActorLocation().Y) * Mirror > 0.f)
    {
        BranchMirror = -Mirror;
    }
    const FName BranchId = Defender ? VsManBranch : VsZoneBranch;
    const UDataTable* RouteLibrary = Library.Get();
    const FPSRoute* Branch = RouteLibrary && !BranchId.IsNone() ? RouteLibrary->FindRow<FPSRoute>(BranchId, TEXT("UPSRouteRunnerComponent"), false) : nullptr;

    const FVector ReadPoint = Controller->GetCurrentTargetLocation();
    TArray<FVector> Rest;
    TArray<bool> RestFakes;
    if (Branch)
    {
        for (const FPSRouteWaypoint& Waypoint : Branch->Waypoints)
        {
            Rest.Add(ReadPoint + FVector(Waypoint.Offset.X, Waypoint.Offset.Y * BranchMirror, Waypoint.Offset.Z));
            RestFakes.Add(Waypoint.bFake);
        }
    }
    if (Rest.Num() == 0)
    {
        // No branch to run: he sits at the read point.
        Rest.Add(ReadPoint);
        RestFakes.Add(false);
    }
    Controller->SetAssignedRoute(Rest);
    PlannedWaypoints = Rest;
    FakeWaypoints = RestFakes;
    OptionReadIndex = INDEX_NONE;
    BreakIndex = INDEX_NONE;
    StartLocation = ReadPoint;
    Publish(EPSRouteEventKind::OptionRead, Self, Defender, Defender ? FName(TEXT("Man")) : FName(TEXT("Zone")), 0.f);
}

void UPSRouteRunnerComponent::Publish(EPSRouteEventKind Kind, const APSPlayerPawn* Self, const APSPlayerPawn* Defender, FName Outcome, float Seconds)
{
    UWorld* World = GetWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus)
    {
        return;
    }
    FPSTelemetryRouteEvent Event;
    Event.Kind = Kind;
    Event.ReceiverName = Self ? Self->GetAttributes().DisplayName : FString();
    Event.DefenderName = Defender ? Defender->GetAttributes().DisplayName : FString();
    Event.Outcome = Outcome;
    Event.Seconds = Seconds;
    Bus->PublishRouteRunning(Event);
}
