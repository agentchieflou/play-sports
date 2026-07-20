#include "PSPlayOrchestrator.h"
#include "PSPlayerPawn.h"
#include "PSOffenseController.h"
#include "PSDefenseController.h"
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

TArray<FVector> UPSPlayOrchestrator::ResolveRouteWaypoints(const FName& RouteId, const UDataTable* RouteLibrary, const FVector& Origin) const
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
        WorldWaypoints.Add(Origin + Waypoint.Offset);
    }

    return WorldWaypoints;
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

        const EPlayerRole PawnRole = Pawn->GetAttributes().Role;
        int32& Cursor = RoleAssignmentCursor.FindOrAdd(PawnRole, 0);

        const FPSPlayAssignment* MatchedAssignment = nullptr;
        int32 SeenForRole = 0;
        for (const FPSPlayAssignment& Assignment : Play.Assignments)
        {
            if (Assignment.Role != PawnRole)
            {
                continue;
            }
            if (SeenForRole == Cursor)
            {
                MatchedAssignment = &Assignment;
                break;
            }
            ++SeenForRole;
        }

        if (!MatchedAssignment)
        {
            continue;
        }
        ++Cursor;

        if (Play.bIsOffensivePlay)
        {
            APSOffenseController* OffenseController = Cast<APSOffenseController>(Pawn->GetController());
            if (!OffenseController)
            {
                continue;
            }

            if (MatchedAssignment->Kind == EPSAssignmentKind::Route)
            {
                const TArray<FVector> Waypoints = ResolveRouteWaypoints(MatchedAssignment->RouteId, RouteLibrary, LineOfScrimmage + MatchedAssignment->FormationOffset);
                OffenseController->SetAssignedRoute(Waypoints);
            }
        }
        else
        {
            APSDefenseController* DefenseController = Cast<APSDefenseController>(Pawn->GetController());
            if (!DefenseController)
            {
                continue;
            }

            const EPSDefensiveAssignmentType AssignmentType = ToDefensiveAssignmentType(MatchedAssignment->Kind);
            DefenseController->SetAssignment(AssignmentType, nullptr, LineOfScrimmage + MatchedAssignment->ZoneOffset);
        }
    }
}

void UPSPlayOrchestrator::TriggerScrambleDrill(const TArray<APSPlayerPawn*>& OnFieldPawns, const FVector& QBLocation)
{
    for (APSPlayerPawn* Pawn : OnFieldPawns)
    {
        if (!Pawn)
        {
            continue;
        }

        const EPlayerRole Role = Pawn->GetAttributes().Role;
        if (Role != EPlayerRole::WideReceiver && Role != EPlayerRole::TightEnd)
        {
            continue;
        }

        APSOffenseController* OffenseController = Cast<APSOffenseController>(Pawn->GetController());
        if (!OffenseController)
        {
            continue;
        }

        // Scramble drill: abandon the current route, break toward open space
        // near the scrambling QB, jittered deterministically per receiver.
        const float JitterX = DeterminismStream.FRandRange(-300.f, 300.f);
        const float JitterY = DeterminismStream.FRandRange(-300.f, 300.f);
        TArray<FVector> ScrambleTarget;
        ScrambleTarget.Add(QBLocation + FVector(400.f + JitterX, JitterY, 0.f));
        OffenseController->SetAssignedRoute(ScrambleTarget);
    }
}

void UPSPlayOrchestrator::SeedDeterminism(int32 Seed)
{
    DeterminismStream.Initialize(Seed);
}
