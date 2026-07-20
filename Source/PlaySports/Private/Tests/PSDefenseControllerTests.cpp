#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSPlayerPawn.h"
#include "PSDefenseController.h"
#include "PSTelemetryBus.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Defense Controller default assignment & blackboard scaffolding
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDefenseControllerAssignmentTest,
    "PlaySports.AI.DefenseControllerAssignment",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDefenseControllerAssignmentTest::RunTest(const FString& Parameters)
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

    APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(
        APSPlayerPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

    FPlayerAttributes DBAttrs;
    DBAttrs.Role = EPlayerRole::DefensiveBack;
    DBAttrs.Awareness = 80.f;
    if (Pawn)
    {
        Pawn->InitializePlayer(DBAttrs);
    }

    APSDefenseController* Controller = World->SpawnActor<APSDefenseController>(
        APSDefenseController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    TestNotNull(TEXT("DefenseController spawned successfully"), Controller);

    if (Pawn && Controller)
    {
        Controller->Possess(Pawn);

        TestEqual(TEXT("DB role defaults to ManCoverage assignment"), Controller->GetAssignment(), EPSDefensiveAssignmentType::ManCoverage);

        Controller->SetAssignment(EPSDefensiveAssignmentType::ZoneCoverage, nullptr, FVector(100.f, 0.f, 0.f));
        TestEqual(TEXT("SetAssignment updates current assignment"), Controller->GetAssignment(), EPSDefensiveAssignmentType::ZoneCoverage);

        UBlackboardComponent* BB = Controller->GetBlackboardComponent();
        if (TestNotNull(TEXT("Blackboard component is initialized"), BB))
        {
            TestEqual(TEXT("AssignmentType key reflects ZoneCoverage"), BB->GetValueAsInt(TEXT("AssignmentType")), static_cast<int32>(EPSDefensiveAssignmentType::ZoneCoverage));
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Pursuit intercept point scales with Awareness
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDefenseControllerPursuitTest,
    "PlaySports.AI.DefenseControllerPursuit",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDefenseControllerPursuitTest::RunTest(const FString& Parameters)
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

    APSPlayerPawn* HighAwarenessPawn = World->SpawnActor<APSPlayerPawn>(
        APSPlayerPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    FPlayerAttributes HighAttrs;
    HighAttrs.Role = EPlayerRole::Linebacker;
    HighAttrs.Awareness = 95.f;
    if (HighAwarenessPawn)
    {
        HighAwarenessPawn->InitializePlayer(HighAttrs);
    }

    APSDefenseController* HighController = World->SpawnActor<APSDefenseController>(
        APSDefenseController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

    APSPlayerPawn* LowAwarenessPawn = World->SpawnActor<APSPlayerPawn>(
        APSPlayerPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    FPlayerAttributes LowAttrs;
    LowAttrs.Role = EPlayerRole::Linebacker;
    LowAttrs.Awareness = 10.f;
    if (LowAwarenessPawn)
    {
        LowAwarenessPawn->InitializePlayer(LowAttrs);
    }

    APSDefenseController* LowController = World->SpawnActor<APSDefenseController>(
        APSDefenseController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

    if (HighAwarenessPawn && HighController && LowAwarenessPawn && LowController)
    {
        HighController->Possess(HighAwarenessPawn);
        LowController->Possess(LowAwarenessPawn);

        const FVector CarrierLocation(1000.f, 0.f, 0.f);
        const FVector CarrierVelocity(0.f, 500.f, 0.f);
        const FVector SelfLocation(0.f, 0.f, 0.f);

        const FVector HighIntercept = HighController->ComputePursuitInterceptPoint(CarrierLocation, CarrierVelocity, SelfLocation);
        const FVector LowIntercept = LowController->ComputePursuitInterceptPoint(CarrierLocation, CarrierVelocity, SelfLocation);

        const float HighLeadDistance = FVector::Dist(HighIntercept, CarrierLocation);
        const float LowLeadDistance = FVector::Dist(LowIntercept, CarrierLocation);

        TestTrue(TEXT("Higher awareness leads the intercept point further than lower awareness"), HighLeadDistance > LowLeadDistance);
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);

    return true;
}

#endif
