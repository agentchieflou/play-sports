#include "PSPlayOrchestrator.h"
#include "PSAIFieldSnapshot.h"
#include "PSPlayerPawn.h"
#include "PSOffenseController.h"
#include "PSDefenseController.h"
#include "PSPreSnapSubsystem.h"
#include "PSRouteRunnerComponent.h"
#include "PSDataIngestion.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"

EPSDefensiveAssignmentType UPSPlayOrchestrator::ToDefensiveAssignmentType(EPSAssignmentKind Kind)
{
    switch (Kind)
    {
    case EPSAssignmentKind::PassRush:
        return EPSDefensiveAssignmentType::PassRush;
    case EPSAssignmentKind::RunFit:
        return EPSDefensiveAssignmentType::RunFit;
    case EPSAssignmentKind::ManCoverage:
        return EPSDefensiveAssignmentType::ManCoverage;
    case EPSAssignmentKind::ZoneCoverage:
        return EPSDefensiveAssignmentType::ZoneCoverage;
    case EPSAssignmentKind::Blitz:
        return EPSDefensiveAssignmentType::PassRush;
    default:
        return EPSDefensiveAssignmentType::RunFit;
    }
}

TArray<FVector> UPSPlayOrchestrator::ResolveRouteWaypoints(const FName& RouteId, const UDataTable* RouteLibrary, const FVector& Origin, float MirrorY) const
{
    TArray<FVector> WorldWaypoints;
    if (!RouteLibrary || RouteId.IsNone())
    {
        return WorldWaypoints;
    }

    const FPSRoute* Route = RouteLibrary->FindRow<FPSRoute>(RouteId, TEXT("PSPlayOrchestrator"));
    if (!Route)
    {
        return WorldWaypoints;
    }

    for (const FPSRouteWaypoint& Waypoint : Route->Waypoints)
    {
        WorldWaypoints.Add(Origin + FVector(Waypoint.Offset.X, Waypoint.Offset.Y * MirrorY, Waypoint.Offset.Z));
    }

    return WorldWaypoints;
}

const FPSPlayAssignment* UPSPlayOrchestrator::FindAssignmentSlot(const FPSPlayDefinition& Play, EPlayerRole Role, int32 RoleIndex)
{
    // The sample plays list one slot per role; a role with more players than slots repeats
    // its last slot, so every receiver runs a route and every defender has a job (Epic 14).
    const FPSPlayAssignment* Matched = nullptr;
    int32 SeenForRole = 0;
    for (const FPSPlayAssignment& Assignment : Play.Assignments)
    {
        if (Assignment.Role != Role)
        {
            continue;
        }
        Matched = &Assignment;
        if (SeenForRole == RoleIndex)
        {
            break;
        }
        ++SeenForRole;
    }
    return Matched;
}

void UPSPlayOrchestrator::DistributePlayCall(const FPSPlayDefinition& Play, const TArray<APSPlayerPawn*>& OnFieldPawns, const UDataTable* RouteLibrary, const FVector& LineOfScrimmage)
{
    // Track how many pawns of each role have already been assigned so repeated
    // role slots in the play (e.g. multiple WideReceiver assignments) map to
    // distinct pawns rather than all receiving the first assignment.
    TMap<EPlayerRole, int32> RoleAssignmentCursor;

    for (APSPlayerPawn* Pawn : OnFieldPawns)
    {
        if (!Pawn)
        {
            continue;
        }
        // The play it hands out is the one it re-coordinates if it breaks down.
        BindToBus(Pawn->GetWorld());

        const EPlayerRole PawnRole = Pawn->GetAttributes().Role;
        int32& Cursor = RoleAssignmentCursor.FindOrAdd(PawnRole, 0);
        const FPSPlayAssignment* MatchedAssignment = FindAssignmentSlot(Play, PawnRole, Cursor);
        if (!MatchedAssignment)
        {
            // An offensive player the play gives no job doesn't run last play's route.
            APSOffenseController* Unassigned = (Play.bIsOffensivePlay && Pawn->TeamSide == EPSTeamSide::Offense) ? Cast<APSOffenseController>(Pawn->GetController()) : nullptr;
            if (Unassigned)
            {
                Unassigned->SetAssignedRoute(TArray<FVector>());
                if (UPSRouteRunnerComponent* Runner = Unassigned->GetRouteRunner())
                {
                    Runner->ClearRoutePlan();
                }
            }
            continue;
        }
        ++Cursor;

        // Assignments are authored for a player lined up right of the ball (+Y); one lined
        // up left of it runs them mirrored.
        const float PawnY = Pawn->GetActorLocation().Y;
        const float Mirror = PawnY < 0.f ? -1.f : 1.f;

        if (Play.bIsOffensivePlay)
        {
            APSOffenseController* OffenseController = Cast<APSOffenseController>(Pawn->GetController());
            if (!OffenseController)
            {
                continue;
            }

            // The offense's pre-snap changes for this player (Epic 66): a hot route, a back
            // kept in to block, a tight end released.
            FPSPlayAssignment Assignment = *MatchedAssignment;
            UWorld* World = Pawn->GetWorld();
            if (UPSPreSnapSubsystem* PreSnap = World ? World->GetSubsystem<UPSPreSnapSubsystem>() : nullptr)
            {
                PreSnap->ApplyAdjustment(Pawn, Assignment);
            }

            if (Assignment.Kind == EPSAssignmentKind::Route)
            {
                // A route starts from the player's own split, not the ball.
                const FVector Offset = Assignment.FormationOffset;
                const FVector Origin(LineOfScrimmage.X + Offset.X, PawnY + Offset.Y * Mirror, LineOfScrimmage.Z + Offset.Z);
                TArray<FVector> Waypoints = ResolveRouteWaypoints(Assignment.RouteId, RouteLibrary, Origin, Mirror);
                // A route with no RouteId is "go to your spot": the QB's drop, the RB's mesh
                // point on a run (Epic 14).
                if (Waypoints.Num() == 0 && Assignment.RouteId.IsNone())
                {
                    Waypoints.Add(Origin);
                }
                OffenseController->SetAssignedRoute(Waypoints);

                // The pattern itself -- its break, fakes, option read -- goes to his route
                // runner, with this play's seed for his contests (Epic 68).
                const FPSRoute* Route = (RouteLibrary && !Assignment.RouteId.IsNone()) ? RouteLibrary->FindRow<FPSRoute>(Assignment.RouteId, TEXT("PSPlayOrchestrator"), false) : nullptr;
                if (UPSRouteRunnerComponent* Runner = OffenseController->GetRouteRunner())
                {
                    if (Route)
                    {
                        Runner->SetRoutePlan(*Route, Waypoints, Mirror, RouteLibrary, DeterminismStream.RandHelper(MAX_int32));
                    }
                    else
                    {
                        Runner->ClearRoutePlan();
                    }
                }
            }
            else
            {
                // A blocker has no route: his AI blocks (Epic 14), whatever he ran last play.
                OffenseController->SetAssignedRoute(TArray<FVector>());
                if (UPSRouteRunnerComponent* Runner = OffenseController->GetRouteRunner())
                {
                    Runner->ClearRoutePlan();
                }
            }
        }
        else
        {
            APSDefenseController* DefenseController = Cast<APSDefenseController>(Pawn->GetController());
            if (!DefenseController)
            {
                continue;
            }

            // A zone is played on the defender's own side of the field.
            FVector Zone = MatchedAssignment->ZoneOffset;
            if (PawnY * Zone.Y < 0.f)
            {
                Zone.Y = -Zone.Y;
            }
            const EPSDefensiveAssignmentType AssignmentType = ToDefensiveAssignmentType(MatchedAssignment->Kind);
            DefenseController->SetAssignment(AssignmentType, nullptr, LineOfScrimmage + Zone);
        }
    }
}

void UPSPlayOrchestrator::TriggerScrambleDrill(const TArray<APSPlayerPawn*>& OnFieldPawns, const FVector& QBLocation, float ScrambleSide)
{
    const FPocketTuningRow& Tuning = GetScrambleTuning();
    const float Side = ScrambleSide != 0.f ? ScrambleSide : (QBLocation.Y < 0.f ? -1.f : 1.f);
    for (APSPlayerPawn* Pawn : OnFieldPawns)
    {
        if (!Pawn || Pawn->TeamSide != EPSTeamSide::Offense)
        {
            continue;
        }
        const EPlayerRole Role = Pawn->GetAttributes().Role;
        if (Role != EPlayerRole::WideReceiver && Role != EPlayerRole::TightEnd && Role != EPlayerRole::RunningBack)
        {
            continue;
        }

        // Only players out on a route: a blocker keeps blocking.
        APSOffenseController* OffenseController = Cast<APSOffenseController>(Pawn->GetController());
        if (!OffenseController || OffenseController->GetRouteWaypointCount() == 0)
        {
            continue;
        }

        // Scramble drill: abandon the route for a spot the QB can throw to, jittered
        // deterministically per receiver.
        const FVector2D Jitter(DeterminismStream.FRandRange(-1.f, 1.f), DeterminismStream.FRandRange(-1.f, 1.f));
        TArray<FVector> ScrambleTarget;
        ScrambleTarget.Add(PSPocket::ScrambleDrillSpot(Pawn->GetActorLocation(), QBLocation, Side, Jitter, Tuning));
        OffenseController->SetAssignedRoute(ScrambleTarget);
        if (UPSRouteRunnerComponent* Runner = OffenseController->GetRouteRunner())
        {
            Runner->ClearRoutePlan();
        }
    }
}

void UPSPlayOrchestrator::BindToBus(UWorld* World)
{
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }
    if (UPSTelemetryBus* Previous = BoundBus.Get())
    {
        Previous->OnPocketMC.RemoveAll(this);
    }
    Bus->OnPocketMC.AddUObject(this, &UPSPlayOrchestrator::HandlePocket);
    BoundBus = Bus;
}

void UPSPlayOrchestrator::HandlePocket(const FPSTelemetryPocketEvent& Event)
{
    UPSTelemetryBus* Bus = BoundBus.Get();
    if (Event.Kind != EPSPocketEventKind::Escape || !Bus)
    {
        return;
    }
    // The field as the AI reads it this frame (Epic 17.5).
    TriggerScrambleDrill(UPSAIFieldSnapshot::GetFieldPawns(Bus->GetWorld()), Event.Location, Event.ScrambleSide);
}

const FPocketTuningRow& UPSPlayOrchestrator::GetScrambleTuning()
{
    if (!bScrambleTuningLoaded)
    {
        bScrambleTuningLoaded = true;
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
        FPocketTuningRow Loaded;
        if (Ingestion->LoadPocketTuningFromJson(UPSPocketComponent::GetDefaultTuningPath(), Loaded))
        {
            ScrambleTuning = Loaded;
        }
    }
    return ScrambleTuning;
}

void UPSPlayOrchestrator::SeedDeterminism(int32 Seed)
{
    DeterminismStream.Initialize(Seed);
}
