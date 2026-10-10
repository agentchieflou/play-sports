#include "PSPlayResolution.h"
#include "PSCoverageMatchupSubsystem.h"
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
            // A quarterback lined up deeper than his drop (the shotgun, Data/formations.json)
            // holds his depth instead of stepping up to it.
            if (Pawn->GetAttributes().Role == EPlayerRole::Quarterback && Entry.Assignment.RouteId.IsNone())
            {
                Entry.RouteOrigin.X = FMath::Min(Entry.RouteOrigin.X, Entry.PawnLocation.X);
            }
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

bool PSPlayResolution::IsCoverable(const APSPlayerPawn* Pawn, EPlayerRole Role)
{
    if (!Pawn || Pawn->TeamSide != EPSTeamSide::Offense)
    {
        return false;
    }
    return Role == EPlayerRole::WideReceiver || Role == EPlayerRole::TightEnd || Role == EPlayerRole::RunningBack;
}

APSPlayerPawn* PSPlayResolution::NearestOpenReceiver(const APSPlayerPawn* Defender, const TArray<APSPlayerPawn*>& Pawns, const TArray<EPlayerRole>& Roles, const TSet<const APSPlayerPawn*>& Taken)
{
    if (!Defender)
    {
        return nullptr;
    }
    APSPlayerPawn* Nearest = nullptr;
    float NearestDistance = TNumericLimits<float>::Max();
    for (int32 Index = 0; Index < Pawns.Num() && Index < Roles.Num(); ++Index)
    {
        APSPlayerPawn* Candidate = Pawns[Index];
        if (!IsCoverable(Candidate, Roles[Index]) || Taken.Contains(Candidate))
        {
            continue;
        }
        const float Distance = FVector::Dist2D(Candidate->GetActorLocation(), Defender->GetActorLocation());
        if (Distance < NearestDistance)
        {
            NearestDistance = Distance;
            Nearest = Candidate;
        }
    }
    return Nearest;
}

void PSPlayResolution::ResolveManMatchups(TArray<FPSResolvedAssignment>& Resolved, const TArray<APSPlayerPawn*>& Pawns, const TArray<EPlayerRole>& Roles, const UPSCoverageMatchupSubsystem* Matchups)
{
    // The receivers the defenders before him have taken, as each defender's AI holds his man
    // once it has taken up its assignment.
    TSet<const APSPlayerPawn*> Chosen;
    for (FPSResolvedAssignment& Entry : Resolved)
    {
        const APSPlayerPawn* Defender = Entry.Pawn.Get();
        if (!Defender || !Entry.bHasSlot || Entry.DefensiveType != EPSDefensiveAssignmentType::ManCoverage)
        {
            continue;
        }
        APSPlayerPawn* Named = Cast<APSPlayerPawn>(Entry.CoverageTarget.Get());
        APSPlayerPawn* Pressed = Matchups ? Matchups->GetPlannedReceiver(Defender) : nullptr;
        APSPlayerPawn* Receiver = Named ? Named : Pressed;
        if (!Receiver)
        {
            // Open: nobody before him has the receiver, and no other defender lines up to press him.
            TSet<const APSPlayerPawn*> Taken = Chosen;
            for (const APSPlayerPawn* Other : Pawns)
            {
                const APSPlayerPawn* OtherPressed = Matchups && Other && Other != Defender ? Matchups->GetPlannedReceiver(Other) : nullptr;
                if (OtherPressed)
                {
                    Taken.Add(OtherPressed);
                }
            }
            Receiver = NearestOpenReceiver(Defender, Pawns, Roles, Taken);
        }
        Entry.ManReceiver = Receiver;
        if (Receiver)
        {
            Chosen.Add(Receiver);
        }
    }
}
