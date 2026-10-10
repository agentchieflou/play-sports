#include "PSPlayResolution.h"
#include "PSDefenderPreSnapSubsystem.h"
#include "PSPlayerPawn.h"
#include "PSPreSnapSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

const FPSPlayAssignment* PSPlayResolution::FindAssignmentSlot(const FPSPlayDefinition& Play, EPlayerRole Role, int32 RoleIndex)
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

TArray<FVector> PSPlayResolution::PlaceRoute(const FPSRoute& Route, const FVector& Origin, float Mirror)
{
    TArray<FVector> Placed;
    Placed.Reserve(Route.Waypoints.Num());
    for (const FPSRouteWaypoint& Waypoint : Route.Waypoints)
    {
        Placed.Add(Origin + FVector(Waypoint.Offset.X, Waypoint.Offset.Y * Mirror, Waypoint.Offset.Z));
    }
    return Placed;
}

TArray<FVector> PSPlayResolution::ResolveRouteWaypoints(FName RouteId, const UDataTable* RouteLibrary, const FVector& Origin, float Mirror, bool bWarnIfMissing)
{
    if (!RouteLibrary || RouteId.IsNone())
    {
        return TArray<FVector>();
    }
    const FPSRoute* Route = RouteLibrary->FindRow<FPSRoute>(RouteId, TEXT("PSPlayOrchestrator"), bWarnIfMissing);
    return Route ? PlaceRoute(*Route, Origin, Mirror) : TArray<FVector>();
}

EPSDefensiveAssignmentType PSPlayResolution::ToDefensiveAssignmentType(EPSAssignmentKind Kind)
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

TArray<FPSResolvedAssignment> PSPlayResolution::ResolvePlay(const FPSPlayDefinition& Play, const TArray<APSPlayerPawn*>& Pawns, const UDataTable* RouteLibrary, const FVector& LineOfScrimmage, bool bWarnIfRouteMissing)
{
    TArray<FPSResolvedAssignment> Resolved;
    Resolved.Reserve(Pawns.Num());

    // How many players of each role already have a slot, so repeated role slots in the play
    // (several WideReceiver assignments) go to distinct players in order.
    TMap<EPlayerRole, int32> RoleCursor;

    for (APSPlayerPawn* Pawn : Pawns)
    {
        if (!Pawn)
        {
            continue;
        }
        FPSResolvedAssignment& Entry = Resolved.AddDefaulted_GetRef();
        Entry.Pawn = Pawn;
        Entry.PawnLocation = Pawn->GetActorLocation();

        int32& Cursor = RoleCursor.FindOrAdd(Pawn->GetAttributes().Role, 0);
        const FPSPlayAssignment* Slot = FindAssignmentSlot(Play, Pawn->GetAttributes().Role, Cursor);
        if (!Slot)
        {
            continue;
        }
        ++Cursor;
        Entry.bHasSlot = true;
        Entry.Assignment = *Slot;

        // Assignments are authored for a player lined up right of the ball (+Y); one lined up
        // left of it runs them mirrored.
        const float PawnY = Entry.PawnLocation.Y;
        Entry.Mirror = PawnY < 0.f ? -1.f : 1.f;

        UWorld* World = Pawn->GetWorld();
        if (Play.bIsOffensivePlay)
        {
            // The offense's pre-snap changes for this player (Epic 66): a hot route, a back kept
            // in to block, a tight end released.
            if (UPSPreSnapSubsystem* PreSnap = World ? World->GetSubsystem<UPSPreSnapSubsystem>() : nullptr)
            {
                PreSnap->ApplyAdjustment(Pawn, Entry.Assignment);
            }
            if (Entry.Assignment.Kind != EPSAssignmentKind::Route)
            {
                continue;
            }

            // A route starts from the player's own split, not the ball.
            const FVector Offset = Entry.Assignment.FormationOffset;
            Entry.RouteOrigin = FVector(LineOfScrimmage.X + Offset.X, PawnY + Offset.Y * Entry.Mirror, LineOfScrimmage.Z + Offset.Z);
            Entry.Waypoints = ResolveRouteWaypoints(Entry.Assignment.RouteId, RouteLibrary, Entry.RouteOrigin, Entry.Mirror, bWarnIfRouteMissing);
            // A route with no RouteId is "go to your spot": the QB's drop, the RB's mesh point on
            // a run (Epic 14).
            if (Entry.Waypoints.Num() == 0 && Entry.Assignment.RouteId.IsNone())
            {
                Entry.Waypoints.Add(Entry.RouteOrigin);
            }
        }
        else
        {
            // A zone is played on the defender's own side of the field.
            FVector Zone = Entry.Assignment.ZoneOffset;
            if (PawnY * Zone.Y < 0.f)
            {
                Zone.Y = -Zone.Y;
            }
            Entry.ZoneSpot = LineOfScrimmage + Zone;
            Entry.DefensiveType = ToDefensiveAssignmentType(Entry.Assignment.Kind);

            // A shadow matchup set before the snap overrides the call for its defender (Epic 67).
            AActor* CoverageTarget = nullptr;
            if (const UPSDefenderPreSnapSubsystem* DefensePreSnap = World ? World->GetSubsystem<UPSDefenderPreSnapSubsystem>() : nullptr)
            {
                DefensePreSnap->ApplyMatchup(Pawn, Entry.DefensiveType, CoverageTarget);
            }
            Entry.CoverageTarget = CoverageTarget;
        }
    }
    return Resolved;
}
