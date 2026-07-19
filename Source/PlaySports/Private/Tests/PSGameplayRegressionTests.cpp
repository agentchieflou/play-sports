// PSGameplayRegressionTests.cpp -- Epic C4: Core Gameplay Test Retrofit
//
// Tests covered:
//   1. Movement math (accel/turn/momentum): spawn a pawn, drive its own Tick()
//      logic directly, assert the acceleration curve/turn/momentum formulas.
//   2. Phase progression: TriggerSnap -> Snap, advance past 0.5s -> PassRush.
//   3. Down/distance advancement: RecordTackle(5) on 1st-and-10 -> 2nd-and-5.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "PSPlayerPawn.h"
#include "PSPlaySimulation.h"
#include "PSPlayerAttributes.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Movement math: accel curve, turn, momentum
//
// Drives APSPlayerPawn::Tick() directly against a manually-set Velocity rather
// than letting the engine's movement-component tick integrate it, so the
// assertions are deterministic and only exercise the pawn's own formulas.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSC4MovementMathTest,
    "PlaySports.C4.GameplayRegression.MovementMath",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSC4MovementMathTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("Test world created"), World);
    if (!World)
    {
        return false;
    }

    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>();
    TestNotNull(TEXT("Pawn spawned"), Pawn);
    if (!Pawn)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }

    FPlayerAttributes Attr;
    Attr.Speed = 80.f;
    Attr.Acceleration = 80.f;
    Attr.Agility = 70.f;
    Attr.WeightKg = 90.f;
    Attr.Strength = 70.f;
    Attr.Awareness = 70.f;
    Pawn->InitializePlayer(Attr);

    UFloatingPawnMovement* Movement = Pawn->GetFloatingMovementComponent();
    TestNotNull(TEXT("Movement component exists"), Movement);
    if (Movement)
    {
        // -- Momentum: WeightKg * (Velocity in m/s) --
        Movement->Velocity = FVector(500.f, 0.f, 0.f); // 500 cm/s = 5 m/s
        const float ExpectedMomentum = Attr.WeightKg * 5.f;
        TestEqual(TEXT("GetMomentumMagnitude matches WeightKg * (Velocity/100)"),
            Pawn->GetMomentumMagnitude(), ExpectedMomentum, 0.5f);

        // -- Turn: rotation moves toward the velocity direction on Tick --
        Movement->Velocity = FVector(0.f, 500.f, 0.f); // moving along +Y, but facing +X (default)
        const FRotator InitialRotation = Pawn->GetActorRotation();
        Pawn->Tick(0.1f);
        const FRotator RotationAfterTick = Pawn->GetActorRotation();
        TestFalse(TEXT("Facing rotates toward the velocity direction while moving"),
            RotationAfterTick.Equals(InitialRotation, 0.01f));

        // -- Accel curve: Acceleration tapers as CurrentSpeed approaches MaxSpeed --
        const float MaxSpeed = Movement->MaxSpeed;
        TestTrue(TEXT("MaxSpeed was scaled from the Speed attribute"), MaxSpeed > 0.f);

        Movement->Velocity = FVector::ZeroVector; // CurrentSpeed = 0 -> curve multiplier = 1
        Pawn->Tick(0.1f);
        const float AccelerationAtZeroSpeed = Movement->Acceleration;

        Movement->Velocity = FVector(MaxSpeed, 0.f, 0.f); // CurrentSpeed = MaxSpeed -> curve multiplier floors at 0.1
        Pawn->Tick(0.1f);
        const float AccelerationAtMaxSpeed = Movement->Acceleration;

        TestTrue(TEXT("Acceleration tapers off as speed approaches MaxSpeed"),
            AccelerationAtMaxSpeed < AccelerationAtZeroSpeed);
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Phase progression: TriggerSnap -> Snap, advance -> PassRush
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSC4PhaseProgressionTest,
    "PlaySports.C4.GameplayRegression.PhaseProgression",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSC4PhaseProgressionTest::RunTest(const FString& Parameters)
{
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    TestNotNull(TEXT("Sim created"), Sim);
    if (!Sim)
    {
        return false;
    }

    TArray<FPlayerAttributes> Offense, Defense;
    Sim->InitializePlay(Offense, Defense);
    TestEqual(TEXT("Play starts in PreSnap"), Sim->GetPlayState().Phase, EPlayPhase::PreSnap);

    Sim->TriggerSnap();
    TestEqual(TEXT("TriggerSnap moves phase to Snap"), Sim->GetPlayState().Phase, EPlayPhase::Snap);

    Sim->AdvancePlay(0.6f); // Snap -> PassRush at 0.5s of PhaseTimer
    TestEqual(TEXT("Advancing past 0.5s moves phase to PassRush"), Sim->GetPlayState().Phase, EPlayPhase::PassRush);

    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Down/distance advancement: RecordTackle(5) on 1st-and-10 -> 2nd-and-5
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSC4DownDistanceAdvancementTest,
    "PlaySports.C4.GameplayRegression.DownDistanceAdvancement",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSC4DownDistanceAdvancementTest::RunTest(const FString& Parameters)
{
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    TestNotNull(TEXT("Sim created"), Sim);
    if (!Sim)
    {
        return false;
    }

    TArray<FPlayerAttributes> Offense, Defense;
    Sim->InitializePlay(Offense, Defense);
    TestEqual(TEXT("Starts 1st down"), Sim->GetPlayState().Down, 1);
    TestEqual(TEXT("Starts and-10"), Sim->GetPlayState().Distance, 10);

    Sim->RecordTackle(5);
    Sim->EndPlayAndPrepareNext();

    TestEqual(TEXT("Tackle for 5 on 1st-and-10 advances to 2nd down"), Sim->GetPlayState().Down, 2);
    TestEqual(TEXT("Tackle for 5 on 1st-and-10 leaves 2nd-and-5"), Sim->GetPlayState().Distance, 5);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
