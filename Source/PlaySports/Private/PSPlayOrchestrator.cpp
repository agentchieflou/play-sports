#include "PSPlayOrchestrator.h"
#include "PSAIFieldSnapshot.h"
#include "PSPlayResolution.h"
#include "PSPlayerPawn.h"
#include "PSOffenseController.h"
#include "PSDefenseController.h"
#include "PSRouteRunnerComponent.h"
#include "PSDataIngestion.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"

const FPSPlayAssignment* UPSPlayOrchestrator::FindAssignmentSlot(const FPSPlayDefinition& Play, EPlayerRole Role, int32 RoleIndex)
{
    return PSPlayResolution::FindAssignmentSlot(Play, Role, RoleIndex);
}

void UPSPlayOrchestrator::DistributePlayCall(const FPSPlayDefinition& Play, const TArray<APSPlayerPawn*>& OnFieldPawns, const UDataTable* RouteLibrary, const FVector& LineOfScrimmage)
{
    // The call resolves once, in PSPlayResolution: the same jobs the play art draws before the
    // snap (Epic 27) are the ones handed out here.
    const TArray<FPSResolvedAssignment> Resolved = PSPlayResolution::ResolvePlay(Play, OnFieldPawns, RouteLibrary, LineOfScrimmage);
    for (const FPSResolvedAssignment& Entry : Resolved)
    {
        APSPlayerPawn* Pawn = Entry.Pawn.Get();
        if (!Pawn)
        {
            continue;
        }
        // The play it hands out is the one it re-coordinates if it breaks down.
        BindToBus(Pawn->GetWorld());

        if (!Entry.bHasSlot)
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

        if (Play.bIsOffensivePlay)
        {
            APSOffenseController* OffenseController = Cast<APSOffenseController>(Pawn->GetController());
            if (!OffenseController)
            {
                continue;
            }

            if (Entry.Assignment.Kind == EPSAssignmentKind::Route)
            {
                OffenseController->SetAssignedRoute(Entry.Waypoints);

                // The pattern itself -- its break, fakes, option read -- goes to his route
                // runner, with this play's seed for his contests (Epic 68).
                const FName RouteId = Entry.Assignment.RouteId;
                const FPSRoute* Route = (RouteLibrary && !RouteId.IsNone()) ? RouteLibrary->FindRow<FPSRoute>(RouteId, TEXT("PSPlayOrchestrator"), false) : nullptr;
                if (UPSRouteRunnerComponent* Runner = OffenseController->GetRouteRunner())
                {
                    if (Route)
                    {
                        Runner->SetRoutePlan(*Route, Entry.Waypoints, Entry.Mirror, RouteLibrary, DeterminismStream.RandHelper(MAX_int32));
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
        else if (APSDefenseController* DefenseController = Cast<APSDefenseController>(Pawn->GetController()))
        {
            DefenseController->SetAssignment(Entry.DefensiveType, Entry.CoverageTarget.Get(), Entry.ZoneSpot);
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
        // deterministically per receiver. X, then Y, as two statements: two draws as one call's
        // arguments are drawn in whichever order the compiler picks (Epic 108's audit), so one
        // compiler's build could swap them against another's.
        const float JitterX = DeterminismStream.FRandRange(-1.f, 1.f);
        const float JitterY = DeterminismStream.FRandRange(-1.f, 1.f);
        const FVector2D Jitter(JitterX, JitterY);
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
