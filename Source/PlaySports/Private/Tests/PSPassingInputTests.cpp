// PSPassingInputTests.cpp -- Epic 104 (controller feel and input depth): contexts and passing
//
// Tests covered:
//   1. The gameplay-depth context follows the play: PreSnap before the snap, Passing while the
//      human's QB holds the ball behind the line, BallCarrier past it, none for an offensive
//      player without the ball, Defense on defense, PreSnap again after the whistle.
//   2. Passing: receiver slots run left to right across the field; a tap throws touch, a hold
//      throws a bullet, the Move stick places the ball; nobody without the ball can throw;
//      the slot buttons reach the throw through the controller's catalog-action events.
//   3. The pump fake: the ball stays put, coverage with low Awareness freezes, sharp coverage
//      doesn't, and the freeze wears off.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSPassingComponent.h"
#include "PSPlayContextComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPassingInputTests
{
    static UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    static void DestroyTestWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    /** A pawn at Location under its side's AI controller; defense AIs are bound to the bus. */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location, float Awareness = 0.f, float Strength = 50.f)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.PlayerId = FName(PlayerId);
        Attributes.DisplayName = PlayerId;
        Attributes.Role = Role;
        Attributes.Awareness = Awareness;
        Attributes.Strength = Strength;
        Pawn->InitializePlayer(Attributes);

        if (Pawn->TeamSide == EPSTeamSide::Defense)
        {
            if (APSDefenseController* AI = World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
            {
                AI->Possess(Pawn);
                AI->GetDefenderAI()->BindToBus();
            }
        }
        else if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
        }
        return Pawn;
    }

    static APSPlayerController* SpawnPlayerController(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    }

    static APSBall* GiveBall(UWorld* World, APSPlayerPawn* Carrier)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), Carrier->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);
        if (Ball)
        {
            Ball->AttachToCarrier(Carrier, TEXT("HandSocket"));
            Carrier->GainPossession();
        }
        return Ball;
    }

    static void Snap(UPSTelemetryBus* Bus)
    {
        FPSTelemetrySnapEvent Event;
        Event.LineOfScrimmage = FVector::ZeroVector;
        Bus->PublishSnap(Event);
    }

    /** The passer's full arm (APSPlayerPawn::ThrowPass: 1500 cm/s plus 15 per Strength point). */
    static float FullArm(float Strength)
    {
        return 1500.f + Strength * 15.f;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The depth context follows the play
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayContextTest,
    "PlaySports.Input.ContextFollowsThePlay",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayContextTest::RunTest(const FString& Parameters)
{
    using namespace PSPassingInputTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    APSPlayerController* Controller = World ? SpawnPlayerController(World) : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-200.f, 0.f, 100.f));
    APSPlayerPawn* DB = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB"), FVector(900.f, 0.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("DB"), DB) || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }

    UPSPlayContextComponent* Context = Controller->GetPlayContextComponent();
    Context->BindToBus();
    Context->Refresh();
    TestEqual(TEXT("Controlling nobody, no depth context"), Controller->GetDepthContext(), FName(NAME_None));

    TestTrue(TEXT("The human takes the QB"), Controller->TakeControlOf(QB));
    Context->Refresh();
    TestTrue(TEXT("Before the snap: PreSnap"), Controller->IsInputContextActive(TEXT("PreSnap")));
    TestTrue(TEXT("...over the gameplay context"), Controller->IsInputContextActive(TEXT("OnField")));

    Snap(Bus);
    Context->Refresh();
    TestTrue(TEXT("The QB holds the ball behind the line: Passing"), Controller->IsInputContextActive(TEXT("Passing")));
    TestFalse(TEXT("...and only Passing"), Controller->IsInputContextActive(TEXT("PreSnap")));

    QB->SetActorLocation(FVector(150.f, 0.f, 100.f));
    Context->Refresh();
    TestEqual(TEXT("Past the line he is a ball carrier"), Controller->GetDepthContext(), FName(TEXT("BallCarrier")));
    TestFalse(TEXT("...and can't throw from the Passing context"), Controller->IsInputContextActive(TEXT("Passing")));

    QB->LosePossession();
    Context->Refresh();
    TestEqual(TEXT("An offensive player without the ball has no depth context"), Controller->GetDepthContext(), FName(NAME_None));
    TestTrue(TEXT("...but still the gameplay context"), Controller->IsInputContextActive(TEXT("OnField")));

    TestTrue(TEXT("The human switches to the DB"), Controller->TakeControlOf(DB));
    Context->Refresh();
    TestEqual(TEXT("On defense: Defense"), Controller->GetDepthContext(), FName(TEXT("Defense")));

    FPSTelemetryPhaseChangeEvent Whistle;
    Whistle.NewPhase = TEXT("Scoring");
    Bus->PublishPhaseChange(Whistle);
    Context->Refresh();
    TestEqual(TEXT("After the whistle, on defense: DefensePreSnap"), Controller->GetDepthContext(), FName(TEXT("DefensePreSnap")));

    Controller->ReleaseControl();
    TestEqual(TEXT("Releasing the pawn clears the depth context"), Controller->GetDepthContext(), FName(NAME_None));
    TestFalse(TEXT("...off the stack"), Controller->IsInputContextActive(TEXT("DefensePreSnap")));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Receiver slots, touch and bullet, placement
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPassingSlotsTest,
    "PlaySports.Input.PassingSlotsAndThrows",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPassingSlotsTest::RunTest(const FString& Parameters)
{
    using namespace PSPassingInputTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    APSPlayerController* Controller = World ? SpawnPlayerController(World) : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    // An accurate passer (Awareness 100: no scatter) with an average arm.
    const float Strength = 50.f;
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f), 100.f, Strength);
    APSPlayerPawn* LeftWR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_L"), FVector(1000.f, -900.f, 100.f));
    APSPlayerPawn* Back = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-600.f, -150.f, 100.f));
    APSPlayerPawn* TightEnd = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE"), FVector(1000.f, 450.f, 100.f));
    APSPlayerPawn* RightWR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_R"), FVector(1000.f, 900.f, 100.f));
    APSPlayerPawn* Lineman = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL"), FVector(-50.f, 0.f, 100.f));
    APSPlayerPawn* Corner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB"), FVector(1500.f, -300.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("WR_L"), LeftWR) || !TestNotNull(TEXT("RB"), Back) || !TestNotNull(TEXT("TE"), TightEnd)
        || !TestNotNull(TEXT("WR_R"), RightWR) || !TestNotNull(TEXT("OL"), Lineman) || !TestNotNull(TEXT("CB"), Corner))
    {
        DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryThrowEvent> Throws;
    const FDelegateHandle Handle = Bus->OnThrowMC.AddLambda([&Throws](const FPSTelemetryThrowEvent& Event) { Throws.Add(Event); });

    UPSPassingComponent* Passing = Controller->GetPassingComponent();
    const FPassingInputTuningRow& Tuning = Passing->GetTuning();
    TestTrue(TEXT("The human takes the QB"), Controller->TakeControlOf(QB));
    TestFalse(TEXT("No ball, no pass"), Passing->CanPass());
    TestFalse(TEXT("...a slot button does nothing"), Passing->ThrowToSlot(0, 0.f, FVector2D::ZeroVector));

    GiveBall(World, QB);
    TestTrue(TEXT("With the ball the QB can pass"), Passing->CanPass());
    const TArray<APSPlayerPawn*> Slots = Passing->GetReceiverSlots();
    if (TestEqual(TEXT("Four eligible receivers (not the lineman, not the corner)"), Slots.Num(), 4))
    {
        TestTrue(TEXT("Slots run left to right across the field"),
            Slots[0] == LeftWR && Slots[1] == Back && Slots[2] == TightEnd && Slots[3] == RightWR);
    }
    TestFalse(TEXT("An empty slot throws nothing"), Passing->ThrowToSlot(Slots.Num(), 0.f, FVector2D::ZeroVector));

    // A tap: a touch pass to slot 1.
    TestTrue(TEXT("A tap throws"), Passing->ThrowToSlot(0, Tuning.BulletHoldSeconds * 0.5f, FVector2D::ZeroVector));
    if (TestEqual(TEXT("One throw"), Throws.Num(), 1))
    {
        TestEqual(TEXT("...to the left receiver"), Throws[0].TargetReceiverName, FString(TEXT("WR_L")));
        TestTrue(TEXT("...a touch pass, softer than his full arm"), FMath::IsNearlyEqual(Throws[0].LaunchSpeed, FullArm(Strength) * Tuning.TouchSpeedScale, 1.f));
        TestTrue(TEXT("...right on the (standing) receiver"), Throws[0].TargetLocation.Equals(LeftWR->GetActorLocation(), 1.f));
    }
    TestFalse(TEXT("The ball is gone"), Passing->CanPass());

    // A hold with the stick forward: a bullet placed deeper.
    GiveBall(World, QB);
    TestTrue(TEXT("A hold throws"), Passing->ThrowToSlot(2, Tuning.BulletHoldSeconds + 0.1f, FVector2D(0.f, 1.f)));
    if (TestEqual(TEXT("Two throws"), Throws.Num(), 2))
    {
        TestEqual(TEXT("...to the tight end"), Throws[1].TargetReceiverName, FString(TEXT("TE")));
        TestTrue(TEXT("...a bullet at his full arm"), FMath::IsNearlyEqual(Throws[1].LaunchSpeed, FullArm(Strength), 1.f));
        TestTrue(TEXT("...placed deeper by the stick"),
            Throws[1].TargetLocation.Equals(TightEnd->GetActorLocation() + FVector(Tuning.PlacementDepth, 0.f, 0.f), 1.f));
    }

    // The stick to the left places it to the receiver's left.
    GiveBall(World, QB);
    TestTrue(TEXT("Throw to the right receiver"), Passing->ThrowToSlot(3, Tuning.BulletHoldSeconds + 0.1f, FVector2D(-1.f, 0.f)));
    if (TestEqual(TEXT("Three throws"), Throws.Num(), 3))
    {
        TestTrue(TEXT("...placed to his left by the stick"),
            Throws[2].TargetLocation.Equals(RightWR->GetActorLocation() - FVector(0.f, Tuning.PlacementWidth, 0.f), 1.f));
    }

    // The buttons: press and release the second slot through the controller's events. Time
    // stands still in a test world, so this is a tap.
    GiveBall(World, QB);
    Passing->BindToController();
    if (TestTrue(TEXT("The second slot is a catalog action"), Tuning.SlotActions.IsValidIndex(1)))
    {
        Controller->OnCatalogActionStarted.Broadcast(Tuning.SlotActions[1]);
        TestEqual(TEXT("Pressing doesn't throw yet"), Throws.Num(), 3);
        Controller->OnCatalogActionCompleted.Broadcast(Tuning.SlotActions[1]);
        if (TestEqual(TEXT("Releasing throws"), Throws.Num(), 4))
        {
            TestEqual(TEXT("...to the back, slot 2"), Throws[3].TargetReceiverName, FString(TEXT("RB")));
            TestTrue(TEXT("...a touch pass after a tap"), Throws[3].LaunchSpeed < FullArm(Strength) - 1.f);
        }
    }

    // Only a QB passes.
    TestTrue(TEXT("The human takes the tight end"), Controller->TakeControlOf(TightEnd));
    GiveBall(World, TightEnd);
    TestFalse(TEXT("A tight end with the ball can't throw from the slots"), Passing->ThrowToSlot(0, 0.f, FVector2D::ZeroVector));

    Bus->OnThrowMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The pump fake
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPumpFakeTest,
    "PlaySports.Input.PumpFake",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPumpFakeTest::RunTest(const FString& Parameters)
{
    using namespace PSPassingInputTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    APSPlayerController* Controller = World ? SpawnPlayerController(World) : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f));
    APSPlayerPawn* Raw = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_RAW"), FVector(900.f, 500.f, 100.f), 0.f);
    APSPlayerPawn* Sharp = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_SHARP"), FVector(900.f, -500.f, 100.f), 100.f);
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Raw DB"), Raw) || !TestNotNull(TEXT("Sharp DB"), Sharp)
        || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }
    TestTrue(TEXT("The human takes the QB"), Controller->TakeControlOf(QB));
    UPSPassingComponent* Passing = Controller->GetPassingComponent();

    TArray<FPSTelemetryPumpFakeEvent> Fakes;
    const FDelegateHandle FakeHandle = Bus->OnPumpFakeMC.AddLambda([&Fakes](const FPSTelemetryPumpFakeEvent& Event) { Fakes.Add(Event); });
    int32 ThrowCount = 0;
    const FDelegateHandle ThrowHandle = Bus->OnThrowMC.AddLambda([&ThrowCount](const FPSTelemetryThrowEvent&) { ++ThrowCount; });

    // Both safeties drop to a deep zone 600 cm upfield of where they line up.
    Snap(Bus);
    APSDefenseController* RawAI = Cast<APSDefenseController>(Raw->GetController());
    APSDefenseController* SharpAI = Cast<APSDefenseController>(Sharp->GetController());
    if (!TestTrue(TEXT("Both have defense AIs"), RawAI && SharpAI))
    {
        DestroyTestWorld(World);
        return false;
    }
    RawAI->SetAssignment(EPSDefensiveAssignmentType::ZoneCoverage, nullptr, Raw->GetActorLocation() + FVector(600.f, 0.f, 0.f));
    SharpAI->SetAssignment(EPSDefensiveAssignmentType::ZoneCoverage, nullptr, Sharp->GetActorLocation() + FVector(600.f, 0.f, 0.f));
    UPSDefenderAIComponent* RawDefender = RawAI->GetDefenderAI();
    UPSDefenderAIComponent* SharpDefender = SharpAI->GetDefenderAI();
    RawDefender->TickAI(0.1f);
    SharpDefender->TickAI(0.1f);
    TestTrue(TEXT("Both drop into their zones"), RawDefender->GetDesiredDirection().X > 0.f && SharpDefender->GetDesiredDirection().X > 0.f);

    TestTrue(TEXT("The QB pump-fakes"), Passing->PumpFake());
    if (TestEqual(TEXT("The fake is announced"), Fakes.Num(), 1))
    {
        TestEqual(TEXT("...by the passer"), Fakes[0].PasserName, FString(TEXT("QB")));
    }
    TestEqual(TEXT("Nothing is thrown"), ThrowCount, 0);
    TestTrue(TEXT("The ball stays in his hands"), QB->HasPossession());

    RawDefender->TickAI(0.1f);
    SharpDefender->TickAI(0.1f);
    TestTrue(TEXT("The raw safety bites and freezes"), RawDefender->IsFrozen() && RawDefender->GetDesiredDirection().IsNearlyZero());
    TestTrue(TEXT("The sharp one keeps dropping"), !SharpDefender->IsFrozen() && SharpDefender->GetDesiredDirection().X > 0.f);

    RawDefender->TickAI(RawDefender->GetTuning().PumpFakeFreezeSeconds);
    TestTrue(TEXT("The freeze wears off"), !RawDefender->IsFrozen() && RawDefender->GetDesiredDirection().X > 0.f);

    QB->LosePossession();
    TestFalse(TEXT("No ball, no pump fake"), Passing->PumpFake());

    Bus->OnPumpFakeMC.Remove(FakeHandle);
    Bus->OnThrowMC.Remove(ThrowHandle);
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
