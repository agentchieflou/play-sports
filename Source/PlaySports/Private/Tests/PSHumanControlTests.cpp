// PSHumanControlTests.cpp -- Epic 127 (Xbox gamepad bring-up and human possession)
//
// Tests covered:
//   1. FInputTuningRow from Data/input_tuning.json becomes a radial dead zone and response
//      curve on the gamepad stick binding, and the dead zone actually zeroes small input.
//   2. Active-device changes (last-input heuristic and connect/disconnect) round-trip on
//      UPSTelemetryBus, and stick drift below the tuned threshold does not flip the device.
//   3. Human <-> AI possession handoff in both directions with no orphaned controllers.
//   4. SwitchPlayer goes to the ball carrier when the human's side has the ball, otherwise
//      to the nearest teammate that is not downed, and hands the old pawn back to its AI.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSInputConfig.h"
#include "PSInputDeviceComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSOffenseController.h"
#include "PSHealthComponent.h"
#include "PSTelemetryBus.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSHumanControlTests
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

    /** A pawn with the given role at Location, possessed by its own AI controller (test
     *  worlds don't auto-possess). */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const FName PlayerId, const FVector& Location, bool bWithAI = true)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }

        FPlayerAttributes Attributes;
        Attributes.PlayerId = PlayerId;
        Attributes.DisplayName = PlayerId.ToString();
        Attributes.Role = Role;
        Pawn->InitializePlayer(Attributes);

        if (bWithAI)
        {
            if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
            {
                AI->Possess(Pawn);
            }
        }
        return Pawn;
    }

    static APSPlayerController* SpawnPlayerController(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    }

    static int32 CountOrphanedAIControllers(UWorld* World)
    {
        int32 Orphans = 0;
        for (TActorIterator<AAIController> It(World); It; ++It)
        {
            if (!It->GetPawn())
            {
                ++Orphans;
            }
        }
        return Orphans;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Stick tuning becomes dead zone + response curve modifiers
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSGamepadStickTuningTest,
    "PlaySports.Input.GamepadStickTuningApplied",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSGamepadStickTuningTest::RunTest(const FString& Parameters)
{
    UPSInputConfig* Config = NewObject<UPSInputConfig>();
    TestTrue(TEXT("Catalog and tuning load from Data/"), Config->LoadDefaults());
    for (const FString& Error : Config->Validate())
    {
        AddError(FString::Printf(TEXT("input config: %s"), *Error));
    }

    // The tuning in use is the file's, read through UPSDataIngestion.
    FInputTuningRow FromFile;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    TestTrue(TEXT("input_tuning.json loads"), Ingestion->LoadInputTuningFromJson(UPSInputConfig::GetDefaultTuningPath(), FromFile));
    TestEqual(TEXT("Dead zone lower comes from the file"), Config->Tuning.StickDeadZoneLower, FromFile.StickDeadZoneLower);
    TestEqual(TEXT("Response exponent comes from the file"), Config->Tuning.StickResponseExponent, FromFile.StickResponseExponent);

    const UInputMappingContext* OnField = Config->FindContext(TEXT("OnField"));
    const UInputAction* Move = Config->FindAction(TEXT("Move"));
    if (!TestNotNull(TEXT("OnField context exists"), OnField) || !TestNotNull(TEXT("Move action exists"), Move))
    {
        return false;
    }

    const FEnhancedActionKeyMapping* StickMapping = nullptr;
    const FEnhancedActionKeyMapping* KeyboardMapping = nullptr;
    for (const FEnhancedActionKeyMapping& Mapping : OnField->GetMappings())
    {
        if (Mapping.Action == Move && Mapping.Key == EKeys::Gamepad_Left2D)
        {
            StickMapping = &Mapping;
        }
        if (Mapping.Action == Move && Mapping.Key == EKeys::W)
        {
            KeyboardMapping = &Mapping;
        }
    }
    if (!TestNotNull(TEXT("Left stick is mapped to Move on the field"), StickMapping) || !TestNotNull(TEXT("W is mapped to Move"), KeyboardMapping))
    {
        return false;
    }

    UInputModifierDeadZone* DeadZone = nullptr;
    UInputModifierResponseCurveExponential* Curve = nullptr;
    for (UInputModifier* Modifier : StickMapping->Modifiers)
    {
        if (UInputModifierDeadZone* AsDeadZone = Cast<UInputModifierDeadZone>(Modifier))
        {
            DeadZone = AsDeadZone;
        }
        if (UInputModifierResponseCurveExponential* AsCurve = Cast<UInputModifierResponseCurveExponential>(Modifier))
        {
            Curve = AsCurve;
        }
    }
    if (!TestNotNull(TEXT("Stick binding has a dead zone"), DeadZone) || !TestNotNull(TEXT("Stick binding has a response curve"), Curve))
    {
        return false;
    }

    TestTrue(TEXT("Dead zone is radial"), DeadZone->Type == EDeadZoneType::Radial);
    TestEqual(TEXT("Dead zone lower threshold is the tuned value"), DeadZone->LowerThreshold, Config->Tuning.StickDeadZoneLower);
    TestEqual(TEXT("Dead zone upper threshold is the tuned value"), DeadZone->UpperThreshold, Config->Tuning.StickDeadZoneUpper);
    TestEqual(TEXT("Curve exponent is the tuned value"), static_cast<float>(Curve->CurveExponent.X), Config->Tuning.StickResponseExponent);

    // The dead zone does its job: drift inside it reads as zero, a full push reads as full.
    const FVector2D Drift(Config->Tuning.StickDeadZoneLower * 0.5f, 0.f);
    const FVector2D Drifted = DeadZone->ModifyRaw(nullptr, FInputActionValue(Drift), 0.f).Get<FVector2D>();
    TestTrue(TEXT("Stick drift inside the dead zone reads as zero"), Drifted.IsNearlyZero());
    const FVector2D Full = DeadZone->ModifyRaw(nullptr, FInputActionValue(FVector2D(1.f, 0.f)), 0.f).Get<FVector2D>();
    TestTrue(TEXT("A full push still reads as full"), FMath::IsNearlyEqual(static_cast<float>(Full.X), 1.f, 0.01f));

    TestEqual(TEXT("Keyboard bindings get no stick modifiers"),
        KeyboardMapping->Modifiers.FilterByPredicate([](const UInputModifier* Modifier) { return Modifier && Modifier->IsA<UInputModifierDeadZone>(); }).Num(), 0);

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Device changes round-trip on the bus
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSInputDeviceBusTest,
    "PlaySports.Input.DeviceChangeRoundTripsOnBus",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSInputDeviceBusTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSHumanControlTests::CreateTestWorld();
    if (!TestNotNull(TEXT("Test world created"), World))
    {
        return false;
    }

    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    APSPlayerController* Controller = PSHumanControlTests::SpawnPlayerController(World);
    UPSInputDeviceComponent* Devices = Controller ? Controller->GetInputDeviceComponent() : nullptr;
    if (!TestNotNull(TEXT("Telemetry bus exists"), Bus) || !TestNotNull(TEXT("Controller has a device component"), Devices))
    {
        PSHumanControlTests::DestroyTestWorld(World);
        return false;
    }

    const float Threshold = Controller->GetInputConfig()->Tuning.DeviceSwitchAnalogThreshold;
    TestEqual(TEXT("Device component uses the tuned analog threshold"), Devices->AnalogThreshold, Threshold);

    TArray<FPSTelemetryInputDeviceEvent> Events;
    const FDelegateHandle Handle = Bus->OnInputDeviceChangeMC.AddLambda([&Events](const FPSTelemetryInputDeviceEvent& Event)
    {
        Events.Add(Event);
    });

    TestTrue(TEXT("Starts on keyboard/mouse"), Devices->GetActiveDevice() == EPSInputDevice::KeyboardMouse);

    Devices->NotifyInput(EKeys::Gamepad_FaceButton_Bottom, 1.f);
    TestEqual(TEXT("A gamepad button publishes one change"), Events.Num(), 1);
    if (Events.Num() == 1)
    {
        TestTrue(TEXT("Event: now gamepad"), Events[0].ActiveDevice == EPSInputDevice::Gamepad);
        TestTrue(TEXT("Event: was keyboard/mouse"), Events[0].PreviousDevice == EPSInputDevice::KeyboardMouse);
        TestFalse(TEXT("Event: from the last-input heuristic"), Events[0].bFromConnectionChange);
    }

    Devices->NotifyInput(EKeys::Gamepad_FaceButton_Right, 1.f);
    TestEqual(TEXT("More gamepad input while on gamepad publishes nothing"), Events.Num(), 1);

    Devices->NotifyInput(EKeys::W, 1.f);
    TestEqual(TEXT("A key press switches back to keyboard/mouse"), Events.Num(), 2);
    TestTrue(TEXT("Active device is keyboard/mouse"), Devices->GetActiveDevice() == EPSInputDevice::KeyboardMouse);

    Devices->NotifyInput(EKeys::Gamepad_LeftX, Threshold * 0.5f);
    TestEqual(TEXT("Stick drift below the threshold does not switch device"), Events.Num(), 2);

    Devices->NotifyInput(EKeys::Gamepad_LeftX, FMath::Min(1.f, Threshold * 1.5f));
    TestEqual(TEXT("A real stick push switches to gamepad"), Events.Num(), 3);

    Devices->NotifyConnectionChange(true, true);
    TestEqual(TEXT("A gamepad connect is published"), Events.Num(), 4);
    if (Events.Num() == 4)
    {
        TestTrue(TEXT("Connect event is a connection change"), Events[3].bFromConnectionChange && Events[3].bConnected);
    }

    Devices->NotifyConnectionChange(false, true);
    TestEqual(TEXT("A gamepad disconnect is published"), Events.Num(), 5);
    TestTrue(TEXT("Disconnecting the active gamepad falls back to keyboard/mouse"), Devices->GetActiveDevice() == EPSInputDevice::KeyboardMouse);
    if (Events.Num() == 5)
    {
        TestTrue(TEXT("Disconnect event: was gamepad"), Events[4].PreviousDevice == EPSInputDevice::Gamepad);
        TestTrue(TEXT("Disconnect event is a disconnection"), Events[4].bFromConnectionChange && !Events[4].bConnected);
    }

    Devices->NotifyConnectionChange(false, false);
    TestEqual(TEXT("Non-gamepad connection changes are ignored"), Events.Num(), 5);

    const int32 HistoryCount = Bus->GetEventHistory().FilterByPredicate([](const FPSTelemetryEvent& Entry)
    {
        return Entry.EventType == EPSTelemetryEventType::InputDeviceChange;
    }).Num();
    TestEqual(TEXT("Every device event is in the bus history"), HistoryCount, 5);

    Bus->OnInputDeviceChangeMC.Remove(Handle);
    PSHumanControlTests::DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Human <-> AI possession handoff, both directions, no orphans
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSHumanAIHandoffTest,
    "PlaySports.Input.HumanAIPossessionHandoff",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSHumanAIHandoffTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSHumanControlTests::CreateTestWorld();
    if (!TestNotNull(TEXT("Test world created"), World))
    {
        return false;
    }

    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    APSPlayerPawn* WR = PSHumanControlTests::SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_TEST"), FVector(500.f, 0.f, 0.f));
    APSPlayerPawn* QB = PSHumanControlTests::SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB_TEST"), FVector::ZeroVector);
    APSPlayerPawn* TE = PSHumanControlTests::SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE_TEST"), FVector(0.f, 500.f, 0.f), false);
    APSPlayerController* Controller = PSHumanControlTests::SpawnPlayerController(World);
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("WR"), WR) || !TestNotNull(TEXT("TE"), TE) || !TestNotNull(TEXT("Controller"), Controller))
    {
        PSHumanControlTests::DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryControlChangeEvent> Events;
    const FDelegateHandle Handle = Bus->OnControlChangeMC.AddLambda([&Events](const FPSTelemetryControlChangeEvent& Event)
    {
        Events.Add(Event);
    });

    AController* QBAI = QB->GetController();
    TestTrue(TEXT("QB starts under AI"), QBAI && !QBAI->IsPlayerController());

    // AI -> human: the default pawn is the QB on offense, even though the WR spawned first.
    TestTrue(TEXT("TakeDefaultControl succeeds"), Controller->TakeDefaultControl());
    TestTrue(TEXT("Human controls the QB"), QB->GetController() == Controller);
    TestTrue(TEXT("QB reports user control"), QB->IsUserControlled());
    TestFalse(TEXT("WR is untouched"), WR->IsUserControlled());
    TestNull(TEXT("The QB's AI is parked, not destroyed"), QBAI ? QBAI->GetPawn() : nullptr);
    TestTrue(TEXT("Gameplay context is active"), Controller->IsInputContextActive(Controller->GameplayContextId));
    TestEqual(TEXT("One control event so far"), Events.Num(), 1);
    if (Events.Num() == 1)
    {
        TestTrue(TEXT("Event: QB is now human-controlled"), Events[0].PlayerId == FName(TEXT("QB_TEST")) && Events[0].bHumanControlled);
    }

    // Human -> AI: the same AI controller takes the QB back.
    Controller->ReleaseControl();
    TestTrue(TEXT("QB's own AI resumes"), QB->GetController() == QBAI && QBAI->GetPawn() == QB);
    TestFalse(TEXT("QB no longer reports user control"), QB->IsUserControlled());
    TestNull(TEXT("Controller holds no pawn"), Controller->GetPawn());
    TestFalse(TEXT("Gameplay context is removed"), Controller->IsInputContextActive(Controller->GameplayContextId));
    TestEqual(TEXT("Release is published"), Events.Num(), 2);
    if (Events.Num() == 2)
    {
        TestFalse(TEXT("Event: QB back to AI"), Events[1].bHumanControlled);
    }

    // A pawn that had no AI gets a fresh default AI controller on release.
    TestTrue(TEXT("Take a pawn with no AI"), Controller->TakeControlOf(TE));
    Controller->ReleaseControl();
    AController* TEController = TE->GetController();
    TestTrue(TEXT("Released pawn without an AI gets its default AI controller"), TEController && !TEController->IsPlayerController() && TEController->IsA<APSOffenseController>());

    TestEqual(TEXT("No AI controller is left without a pawn"), PSHumanControlTests::CountOrphanedAIControllers(World), 0);

    Bus->OnControlChangeMC.Remove(Handle);
    PSHumanControlTests::DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- SwitchPlayer: carrier first, else nearest teammate that isn't downed
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSwitchPlayerTest,
    "PlaySports.Input.SwitchPlayerFollowsBallAndCarrier",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSwitchPlayerTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSHumanControlTests::CreateTestWorld();
    if (!TestNotNull(TEXT("Test world created"), World))
    {
        return false;
    }

    // Defense: DB at 0, LB at 1000, a downed DL at 2000.
    APSPlayerPawn* DB = PSHumanControlTests::SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_TEST"), FVector(0.f, 0.f, 0.f));
    APSPlayerPawn* LB = PSHumanControlTests::SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB_TEST"), FVector(1000.f, 0.f, 0.f));
    APSPlayerPawn* DL = PSHumanControlTests::SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL_TEST"), FVector(2000.f, 0.f, 0.f));
    // Offense: QB at 0, WR at 3000.
    APSPlayerPawn* QB = PSHumanControlTests::SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB_TEST"), FVector(0.f, 1000.f, 0.f));
    APSPlayerPawn* WR = PSHumanControlTests::SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_TEST"), FVector(3000.f, 1000.f, 0.f));
    APSPlayerController* Controller = PSHumanControlTests::SpawnPlayerController(World);
    if (!TestNotNull(TEXT("DB"), DB) || !TestNotNull(TEXT("LB"), LB) || !TestNotNull(TEXT("DL"), DL) || !TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("WR"), WR) || !TestNotNull(TEXT("Controller"), Controller))
    {
        PSHumanControlTests::DestroyTestWorld(World);
        return false;
    }
    DL->GetHealthComponent()->Kill();

    // On defense with the ball loose near the downed DL: the LB is the nearest eligible.
    AController* DBAI = DB->GetController();
    TestTrue(TEXT("Human takes the DB"), Controller->TakeControlOf(DB));
    TestTrue(TEXT("Switch succeeds"), Controller->SwitchToBestPawn(FVector(1900.f, 0.f, 0.f)));
    TestTrue(TEXT("Control moved to the nearest teammate that is not downed (LB)"), LB->GetController() == Controller);
    TestFalse(TEXT("The downed DL was skipped"), DL->IsUserControlled());
    TestTrue(TEXT("The DB went back to its own AI"), DB->GetController() == DBAI && DBAI && !DBAI->IsPlayerController());
    TestFalse(TEXT("Switching never crosses to the other side"), QB->IsUserControlled() || WR->IsUserControlled());

    // On offense with the WR carrying: the carrier gets control even though the QB is
    // nearer the ball location given.
    WR->GainPossession();
    TestTrue(TEXT("Human takes the QB"), Controller->TakeControlOf(QB));
    TestTrue(TEXT("LB returned to AI when control moved"), LB->GetController() && !LB->GetController()->IsPlayerController());
    TestTrue(TEXT("Switch succeeds"), Controller->SwitchToBestPawn(QB->GetActorLocation()));
    TestTrue(TEXT("Control goes to the ball carrier"), WR->GetController() == Controller);
    TestFalse(TEXT("Switching while controlling the carrier does nothing"), Controller->SwitchToBestPawn(FVector::ZeroVector));
    TestTrue(TEXT("Still on the carrier"), WR->GetController() == Controller);

    Controller->ReleaseControl();
    TestEqual(TEXT("No AI controller is left without a pawn"), PSHumanControlTests::CountOrphanedAIControllers(World), 0);

    PSHumanControlTests::DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
