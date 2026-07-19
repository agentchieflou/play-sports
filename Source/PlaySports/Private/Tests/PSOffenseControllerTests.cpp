#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSPlayerPawn.h"
#include "PSOffenseController.h"
#include "PSTelemetryBus.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Offense Controller Possession & Blackboard Scaffolding Setup
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOffenseControllerPossessionTest,
    "PlaySports.AI.OffenseControllerPossession",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FOffenseControllerPossessionTest::RunTest(const FString& Parameters)
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
    TestNotNull(TEXT("Pawn spawned successfully"), Pawn);

    APSOffenseController* Controller = World->SpawnActor<APSOffenseController>(
        APSOffenseController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    TestNotNull(TEXT("OffenseController spawned successfully"), Controller);

    if (Pawn && Controller)
    {
        Controller->Possess(Pawn);

        TestEqual(TEXT("Controller possesses Pawn"), Controller->GetPawn(), Pawn);
        TestEqual(TEXT("Pawn is possessed by Controller"), Pawn->GetController(), Controller);

        UBlackboardComponent* BB = Controller->GetBlackboardComponent();
        if (TestNotNull(TEXT("Blackboard component is initialized"), BB))
        {
            TestEqual(TEXT("Default PlayPhase key is PreSnap (0)"), BB->GetValueAsInt(TEXT("PlayPhase")), 0);
            TestFalse(TEXT("Default bHasPossession key is false"), BB->GetValueAsBool(TEXT("bHasPossession")));
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Offense Controller Blackboard Telemetry Sync
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOffenseControllerTelemetrySyncTest,
    "PlaySports.AI.OffenseControllerTelemetrySync",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FOffenseControllerTelemetrySyncTest::RunTest(const FString& Parameters)
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
    APSOffenseController* Controller = World->SpawnActor<APSOffenseController>(
        APSOffenseController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

    if (Pawn && Controller)
    {
        Controller->Possess(Pawn);

        UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
        if (TestNotNull(TEXT("TelemetryBus exists in World"), Bus))
        {
            // 1. Verify PhaseChange updates blackboard key
            FPSTelemetryPhaseChangeEvent PhaseEvent;
            PhaseEvent.OldPhase = TEXT("PreSnap");
            PhaseEvent.NewPhase = TEXT("PassRush");
            Bus->PublishPhaseChange(PhaseEvent);

            UBlackboardComponent* BB = Controller->GetBlackboardComponent();
            if (TestNotNull(TEXT("Blackboard is valid"), BB))
            {
                TestEqual(TEXT("Blackboard updates PlayPhase to PassRush (2)"), BB->GetValueAsInt(TEXT("PlayPhase")), 2);
            }

            // 2. Give pawn possession and verify Snap event updates bHasPossession
            Pawn->GainPossession();
            FPSTelemetrySnapEvent SnapEvent;
            Bus->PublishSnap(SnapEvent);

            if (BB)
            {
                TestTrue(TEXT("Blackboard updates bHasPossession to true on Snap"), BB->GetValueAsBool(TEXT("bHasPossession")));
            }

            // 3. Verify Tackle event clears bHasPossession
            FPSTelemetryTackleEvent TackleEvent;
            Bus->PublishTackle(TackleEvent);

            if (BB)
            {
                TestFalse(TEXT("Blackboard updates bHasPossession to false on Tackle"), BB->GetValueAsBool(TEXT("bHasPossession")));
            }
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);

    return true;
}

#endif
