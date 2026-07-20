#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSPlayOrchestrator.h"
#include "PSPlaybookData.h"
#include "PSPlayerPawn.h"
#include "PSOffenseController.h"
#include "PSDefenseController.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "BehaviorTree/BlackboardComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Distributing an offensive play assigns routes to matching pawns
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPlayOrchestratorOffenseDistributionTest,
    "PlaySports.AI.PlayOrchestratorOffenseDistribution",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlayOrchestratorOffenseDistributionTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("Test world created"), World);
    if (!World)
    {
        return false;
    }

    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    APSPlayerPawn* WRPawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    FPlayerAttributes WRAttrs;
    WRAttrs.Role = EPlayerRole::WideReceiver;
    if (WRPawn)
    {
        WRPawn->InitializePlayer(WRAttrs);
    }

    APSOffenseController* WRController = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (WRPawn && WRController)
    {
        WRController->Possess(WRPawn);
    }

    UDataTable* RouteTable = NewObject<UDataTable>();
    RouteTable->RowStruct = FPSRoute::StaticStruct();
    FPSRoute SlantRoute;
    SlantRoute.RouteId = FName("Slant");
    FPSRouteWaypoint Waypoint;
    Waypoint.Offset = FVector(300.f, 0.f, 0.f);
    Waypoint.TimingSeconds = 0.6f;
    SlantRoute.Waypoints.Add(Waypoint);
    RouteTable->AddRow(FName("Slant"), SlantRoute);

    FPSPlayDefinition Play;
    Play.PlayId = FName("TestSlant");
    Play.bIsOffensivePlay = true;
    FPSPlayAssignment Assignment;
    Assignment.Role = EPlayerRole::WideReceiver;
    Assignment.Kind = EPSAssignmentKind::Route;
    Assignment.RouteId = FName("Slant");
    Play.Assignments.Add(Assignment);

    UPSPlayOrchestrator* Orchestrator = NewObject<UPSPlayOrchestrator>();
    TArray<APSPlayerPawn*> Pawns;
    Pawns.Add(WRPawn);

    const FVector LineOfScrimmage(1000.f, 0.f, 0.f);
    Orchestrator->DistributePlayCall(Play, Pawns, RouteTable, LineOfScrimmage);

    if (WRController)
    {
        UBlackboardComponent* BB = WRController->GetBlackboardComponent();
        if (TestNotNull(TEXT("WR blackboard initialized"), BB))
        {
            TestEqual(TEXT("WR route waypoint assigned in world space"), BB->GetValueAsVector(TEXT("TargetLocation")), LineOfScrimmage + FVector(300.f, 0.f, 0.f));
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Distributing a defensive play assigns coverage to matching pawns
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPlayOrchestratorDefenseDistributionTest,
    "PlaySports.AI.PlayOrchestratorDefenseDistribution",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlayOrchestratorDefenseDistributionTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("Test world created"), World);
    if (!World)
    {
        return false;
    }

    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    APSPlayerPawn* DBPawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    FPlayerAttributes DBAttrs;
    DBAttrs.Role = EPlayerRole::DefensiveBack;
    if (DBPawn)
    {
        DBPawn->InitializePlayer(DBAttrs);
    }

    APSDefenseController* DBController = World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (DBPawn && DBController)
    {
        DBController->Possess(DBPawn);
    }

    FPSPlayDefinition Play;
    Play.PlayId = FName("TestCover2");
    Play.bIsOffensivePlay = false;
    FPSPlayAssignment Assignment;
    Assignment.Role = EPlayerRole::DefensiveBack;
    Assignment.Kind = EPSAssignmentKind::ZoneCoverage;
    Assignment.ZoneOffset = FVector(1200.f, -600.f, 0.f);
    Play.Assignments.Add(Assignment);

    UPSPlayOrchestrator* Orchestrator = NewObject<UPSPlayOrchestrator>();
    TArray<APSPlayerPawn*> Pawns;
    Pawns.Add(DBPawn);

    const FVector LineOfScrimmage = FVector::ZeroVector;
    Orchestrator->DistributePlayCall(Play, Pawns, nullptr, LineOfScrimmage);

    if (DBController)
    {
        TestEqual(TEXT("DB receives ZoneCoverage assignment"), DBController->GetAssignment(), EPSDefensiveAssignmentType::ZoneCoverage);
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);

    return true;
}

#endif
