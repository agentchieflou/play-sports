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
        TestEqual(TEXT("WR route waypoint assigned in world space"), WRController->GetCurrentTargetLocation(), LineOfScrimmage + FVector(300.f, 0.f, 0.f));
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

// ---------------------------------------------------------------------------
// Test 3 -- Every player of a role gets a job, run from his own side of the ball
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPlayOrchestratorFillsEveryRoleTest,
    "PlaySports.AI.PlayOrchestratorFillsEveryRole",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlayOrchestratorFillsEveryRoleTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Test world created"), World))
    {
        return false;
    }
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto Spawn = [World, &SpawnParams](EPlayerRole Role, const FVector& Location, bool bDefense) -> APSPlayerPawn*
    {
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.Role = Role;
        Pawn->InitializePlayer(Attributes);
        AController* Controller = bDefense
            ? static_cast<AController*>(World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
            : static_cast<AController*>(World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams));
        if (Controller)
        {
            Controller->Possess(Pawn);
        }
        return Pawn;
    };

    // Three receivers split right, left and wide right; one Slant slot for all of them.
    const FVector LineOfScrimmage(1000.f, 0.f, 0.f);
    APSPlayerPawn* Right = Spawn(EPlayerRole::WideReceiver, FVector(950.f, 900.f, 0.f), false);
    APSPlayerPawn* Left = Spawn(EPlayerRole::WideReceiver, FVector(950.f, -900.f, 0.f), false);
    APSPlayerPawn* Wide = Spawn(EPlayerRole::WideReceiver, FVector(950.f, 1200.f, 0.f), false);
    APSPlayerPawn* CornerRight = Spawn(EPlayerRole::DefensiveBack, FVector(1900.f, 900.f, 0.f), true);
    APSPlayerPawn* CornerLeft = Spawn(EPlayerRole::DefensiveBack, FVector(1900.f, -900.f, 0.f), true);
    if (!TestNotNull(TEXT("Right WR"), Right) || !TestNotNull(TEXT("Left WR"), Left) || !TestNotNull(TEXT("Wide WR"), Wide)
        || !TestNotNull(TEXT("Right DB"), CornerRight) || !TestNotNull(TEXT("Left DB"), CornerLeft))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }

    UDataTable* RouteTable = NewObject<UDataTable>();
    RouteTable->RowStruct = FPSRoute::StaticStruct();
    FPSRoute Slant;
    Slant.RouteId = FName("Slant");
    FPSRouteWaypoint Stem;
    Stem.Offset = FVector(300.f, 0.f, 0.f);
    FPSRouteWaypoint Break;
    Break.Offset = FVector(500.f, -400.f, 0.f);
    Slant.Waypoints = { Stem, Break };
    RouteTable->AddRow(FName("Slant"), Slant);

    FPSPlayDefinition Offense;
    Offense.bIsOffensivePlay = true;
    FPSPlayAssignment SlantSlot;
    SlantSlot.Role = EPlayerRole::WideReceiver;
    SlantSlot.Kind = EPSAssignmentKind::Route;
    SlantSlot.RouteId = FName("Slant");
    Offense.Assignments.Add(SlantSlot);

    UPSPlayOrchestrator* Orchestrator = NewObject<UPSPlayOrchestrator>();
    Orchestrator->DistributePlayCall(Offense, { Right, Left, Wide }, RouteTable, LineOfScrimmage);

    APSOffenseController* RightAI = Cast<APSOffenseController>(Right->GetController());
    APSOffenseController* LeftAI = Cast<APSOffenseController>(Left->GetController());
    const APSOffenseController* WideAI = Cast<APSOffenseController>(Wide->GetController());
    if (TestTrue(TEXT("All three receivers have an offense AI"), RightAI && LeftAI && WideAI))
    {
        TestEqual(TEXT("Every receiver runs the route, not just the first"), WideAI->GetRouteWaypointCount(), 2);
        TestTrue(TEXT("The route starts from the receiver's own split"), RightAI->GetCurrentTargetLocation().Equals(FVector(1300.f, 900.f, 0.f)));
        TestTrue(TEXT("The left receiver's stem starts from his split"), LeftAI->GetCurrentTargetLocation().Equals(FVector(1300.f, -900.f, 0.f)));
        RightAI->AdvanceToNextWaypoint();
        LeftAI->AdvanceToNextWaypoint();
        TestTrue(TEXT("A slant from the right breaks inside, to the left"), RightAI->GetCurrentTargetLocation().Equals(FVector(1500.f, 500.f, 0.f)));
        TestTrue(TEXT("...and from the left, mirrored, inside to the right"), LeftAI->GetCurrentTargetLocation().Equals(FVector(1500.f, -500.f, 0.f)));
    }

    FPSPlayDefinition Defense;
    Defense.bIsOffensivePlay = false;
    FPSPlayAssignment DeepHalf;
    DeepHalf.Role = EPlayerRole::DefensiveBack;
    DeepHalf.Kind = EPSAssignmentKind::ZoneCoverage;
    DeepHalf.ZoneOffset = FVector(1200.f, -600.f, 0.f);
    Defense.Assignments.Add(DeepHalf);
    Orchestrator->DistributePlayCall(Defense, { CornerRight, CornerLeft }, nullptr, LineOfScrimmage);

    const APSDefenseController* RightDB = Cast<APSDefenseController>(CornerRight->GetController());
    const APSDefenseController* LeftDB = Cast<APSDefenseController>(CornerLeft->GetController());
    if (TestTrue(TEXT("Both corners have a defense AI"), RightDB && LeftDB))
    {
        TestEqual(TEXT("The second corner gets the role's zone too"), LeftDB->GetAssignment(), EPSDefensiveAssignmentType::ZoneCoverage);
        TestTrue(TEXT("The right corner plays the deep half on his side"), RightDB->GetZoneLocation().Equals(FVector(2200.f, 600.f, 0.f)));
        TestTrue(TEXT("The left corner plays his"), LeftDB->GetZoneLocation().Equals(FVector(2200.f, -600.f, 0.f)));
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
