// PSForceFeedbackTests.cpp -- Epic 128 (rumble)
//
// Tests covered:
//   1. Data/force_feedback.json loads through UPSDataIngestion and validates; every
//      validation rule fires on a broken pattern table.
//   2. Telemetry events become force-feedback dispatches: each bus event maps to its cue
//      with the authored pattern, involvement-only cues follow the controlled pawn (from
//      ControlChange), the motors are driven only on a gamepad (from InputDeviceChange),
//      and disabling or unbinding stops everything.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSForceFeedbackComponent.h"
#include "PSPlayerController.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSForceFeedbackTests
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

    static bool HasProblem(const TArray<FString>& Problems, const TCHAR* Fragment)
    {
        return Problems.ContainsByPredicate([Fragment](const FString& Problem) { return Problem.Contains(Fragment); });
    }

    static FForceFeedbackTuningRow* FindRow(FPSForceFeedbackTuning& Tuning, EPSForceFeedbackCue Cue)
    {
        return Tuning.Cues.FindByPredicate([Cue](const FForceFeedbackTuningRow& Row) { return Row.Cue == Cue; });
    }

    static void PublishControl(UPSTelemetryBus* Bus, const TCHAR* PlayerName, bool bHumanControlled)
    {
        FPSTelemetryControlChangeEvent Event;
        Event.PlayerName = PlayerName;
        Event.PlayerId = FName(PlayerName);
        Event.bHumanControlled = bHumanControlled;
        Bus->PublishControlChange(Event);
    }

    static void PublishDevice(UPSTelemetryBus* Bus, EPSInputDevice Device, EPSInputDevice Previous)
    {
        FPSTelemetryInputDeviceEvent Event;
        Event.ActiveDevice = Device;
        Event.PreviousDevice = Previous;
        Bus->PublishInputDeviceChange(Event);
    }

    static void PublishTackle(UPSTelemetryBus* Bus, const TCHAR* Tackler, const TCHAR* Carrier, bool bIsSack)
    {
        FPSTelemetryTackleEvent Event;
        Event.TacklerName = Tackler;
        Event.BallCarrierName = Carrier;
        Event.YardsGained = bIsSack ? -7 : 3;
        Event.bIsSack = bIsSack;
        Bus->PublishTackle(Event);
    }

    static void PublishCatch(UPSTelemetryBus* Bus, const TCHAR* Receiver, bool bIsInterception)
    {
        FPSTelemetryCatchEvent Event;
        Event.ReceiverName = Receiver;
        Event.bIsInterception = bIsInterception;
        Bus->PublishCatch(Event);
    }

    static void PublishScore(UPSTelemetryBus* Bus)
    {
        FPSTelemetryScoreEvent Event;
        Event.ScoreType = TEXT("Touchdown");
        Event.Points = 6;
        Bus->PublishScore(Event);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The authored patterns load and validate; broken tables are reported
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSForceFeedbackTuningTest,
    "PlaySports.Input.ForceFeedbackTuningValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSForceFeedbackTuningTest::RunTest(const FString& Parameters)
{
    using namespace PSForceFeedbackTests;

    FPSForceFeedbackTuning FromFile;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    if (!TestTrue(TEXT("force_feedback.json loads through UPSDataIngestion"),
        Ingestion->LoadForceFeedbackTuningFromJson(UPSForceFeedbackComponent::GetDefaultTuningPath(), FromFile)))
    {
        return false;
    }
    for (const FString& Problem : UPSForceFeedbackComponent::ValidateTuning(FromFile))
    {
        AddError(FString::Printf(TEXT("force_feedback.json: %s"), *Problem));
    }
    TestNotNull(TEXT("Sack has a pattern"), FromFile.FindCue(EPSForceFeedbackCue::Sack));
    TestNotNull(TEXT("Score has a pattern"), FromFile.FindCue(EPSForceFeedbackCue::Score));

    {
        FPSForceFeedbackTuning Broken = FromFile;
        Broken.Cues.RemoveAll([](const FForceFeedbackTuningRow& Row) { return Row.Cue == EPSForceFeedbackCue::Score; });
        TestTrue(TEXT("A cue without a pattern is reported"), HasProblem(UPSForceFeedbackComponent::ValidateTuning(Broken), TEXT("'Score' has no pattern")));
    }
    {
        FPSForceFeedbackTuning Broken = FromFile;
        const FForceFeedbackTuningRow Duplicate = Broken.Cues[0];
        Broken.Cues.Add(Duplicate);
        TestTrue(TEXT("A cue with two patterns is reported"), HasProblem(UPSForceFeedbackComponent::ValidateTuning(Broken), TEXT("more than one pattern")));
    }
    {
        FPSForceFeedbackTuning Broken = FromFile;
        FindRow(Broken, EPSForceFeedbackCue::Tackle)->Intensity = 1.5f;
        TestTrue(TEXT("Intensity above 1 is reported"), HasProblem(UPSForceFeedbackComponent::ValidateTuning(Broken), TEXT("Intensity 1.50 is outside 0-1")));
    }
    {
        FPSForceFeedbackTuning Broken = FromFile;
        FindRow(Broken, EPSForceFeedbackCue::Hit)->Duration = -1.f;
        TestTrue(TEXT("A negative duration (rumble until stopped) is reported"), HasProblem(UPSForceFeedbackComponent::ValidateTuning(Broken), TEXT("Duration -1.00")));
    }
    {
        FPSForceFeedbackTuning Broken = FromFile;
        FForceFeedbackTuningRow* Catch = FindRow(Broken, EPSForceFeedbackCue::Catch);
        Catch->bLeftLarge = Catch->bLeftSmall = Catch->bRightLarge = Catch->bRightSmall = false;
        TestTrue(TEXT("A cue that drives no motor is reported"), HasProblem(UPSForceFeedbackComponent::ValidateTuning(Broken), TEXT("rumbles no motor")));
    }
    {
        FPSForceFeedbackTuning Broken = FromFile;
        Broken.MasterIntensity = 2.f;
        TestTrue(TEXT("MasterIntensity above 1 is reported"), HasProblem(UPSForceFeedbackComponent::ValidateTuning(Broken), TEXT("MasterIntensity")));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Telemetry event -> force-feedback dispatch mapping
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSForceFeedbackDispatchTest,
    "PlaySports.Input.TelemetryEventsDriveForceFeedback",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSForceFeedbackDispatchTest::RunTest(const FString& Parameters)
{
    using namespace PSForceFeedbackTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world created"), World))
    {
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    UPSForceFeedbackComponent* Rumble = Controller ? Controller->GetForceFeedbackComponent() : nullptr;
    if (!TestNotNull(TEXT("Telemetry bus exists"), Bus) || !TestNotNull(TEXT("Controller has a force feedback component"), Rumble))
    {
        DestroyTestWorld(World);
        return false;
    }

    // Headless worlds don't run BeginPlay, which is where the game subscribes.
    Rumble->BindToBus();
    TestTrue(TEXT("Subscribed to the bus"), Rumble->IsBoundToBus());
    const FPSForceFeedbackTuning& Tuning = Rumble->GetTuning();
    TestEqual(TEXT("The patterns in use are the authored, valid ones"), UPSForceFeedbackComponent::ValidateTuning(Tuning).Num(), 0);

    TArray<FPSForceFeedbackDispatch> Dispatches;
    const FDelegateHandle Handle = Rumble->OnDispatched.AddLambda([&Dispatches](const FPSForceFeedbackDispatch& Dispatch)
    {
        Dispatches.Add(Dispatch);
    });

    // The last dispatch is Cue, with exactly the authored pattern, on the gamepad or not.
    auto ExpectLast = [this, &Dispatches, &Tuning](const TCHAR* What, EPSForceFeedbackCue Cue, bool bOnGamepad)
    {
        if (!TestTrue(*FString::Printf(TEXT("%s: a rumble was dispatched"), What), Dispatches.Num() > 0))
        {
            return;
        }
        const FPSForceFeedbackDispatch& Last = Dispatches.Last();
        const FForceFeedbackTuningRow* Row = Tuning.FindCue(Cue);
        TestTrue(*FString::Printf(TEXT("%s: cue is %s"), What, *UEnum::GetValueAsString(Cue)), Last.Cue == Cue);
        if (Row)
        {
            TestEqual(*FString::Printf(TEXT("%s: intensity is the pattern's times the master"), What), Last.Intensity, Row->Intensity * Tuning.MasterIntensity);
            TestEqual(*FString::Printf(TEXT("%s: duration is the pattern's"), What), Last.Duration, Row->Duration);
            TestTrue(*FString::Printf(TEXT("%s: motors are the pattern's"), What),
                Last.bLeftLarge == Row->bLeftLarge && Last.bLeftSmall == Row->bLeftSmall && Last.bRightLarge == Row->bRightLarge && Last.bRightSmall == Row->bRightSmall);
        }
        TestTrue(*FString::Printf(TEXT("%s: %s the gamepad"), What, bOnGamepad ? TEXT("reached") : TEXT("did not reach")), Last.bPlayedOnGamepad == bOnGamepad);
    };

    // Nobody controlled yet: involvement-only cues stay quiet, cues for everyone play.
    PublishTackle(Bus, TEXT("DL_TEST"), TEXT("QB_TEST"), true);
    TestEqual(TEXT("A sack rumbles nobody who controls no pawn"), Dispatches.Num(), 0);
    PublishScore(Bus);
    ExpectLast(TEXT("Score before taking control"), EPSForceFeedbackCue::Score, false);

    // The human takes the QB and picks up the gamepad -- both learned from the bus.
    PublishControl(Bus, TEXT("QB_TEST"), true);
    PublishDevice(Bus, EPSInputDevice::Gamepad, EPSInputDevice::KeyboardMouse);
    TestEqual(TEXT("Controlled pawn comes from ControlChange"), Rumble->GetControlledPlayerName(), FString(TEXT("QB_TEST")));
    TestTrue(TEXT("Active device comes from InputDeviceChange"), Rumble->GetActiveDevice() == EPSInputDevice::Gamepad);

    PublishTackle(Bus, TEXT("DL_TEST"), TEXT("QB_TEST"), true);
    ExpectLast(TEXT("Human QB sacked"), EPSForceFeedbackCue::Sack, true);

    PublishTackle(Bus, TEXT("QB_TEST"), TEXT("DB_TEST"), false);
    ExpectLast(TEXT("Human makes the tackle"), EPSForceFeedbackCue::Tackle, true);

    int32 Count = Dispatches.Num();
    PublishTackle(Bus, TEXT("LB_TEST"), TEXT("RB_TEST"), false);
    TestEqual(TEXT("A tackle the human had no part in is not felt"), Dispatches.Num(), Count);

    FPSTelemetryDamageEvent Damage;
    Damage.TargetName = TEXT("QB_TEST");
    Damage.Amount = 10.f;
    Bus->PublishDamage(Damage);
    ExpectLast(TEXT("Human pawn takes a hit"), EPSForceFeedbackCue::Hit, true);

    Count = Dispatches.Num();
    Damage.TargetName = TEXT("WR_TEST");
    Bus->PublishDamage(Damage);
    PublishCatch(Bus, TEXT("WR_TEST"), false);
    TestEqual(TEXT("A teammate's hit and catch are not felt"), Dispatches.Num(), Count);

    PublishCatch(Bus, TEXT("DB_TEST"), true);
    ExpectLast(TEXT("Any interception is felt"), EPSForceFeedbackCue::Interception, true);

    // Control moves to the receiver: release then take, as APSPlayerController publishes it.
    PublishControl(Bus, TEXT("QB_TEST"), false);
    PublishControl(Bus, TEXT("WR_TEST"), true);
    PublishCatch(Bus, TEXT("WR_TEST"), false);
    ExpectLast(TEXT("Human receiver catches"), EPSForceFeedbackCue::Catch, true);

    FPSTelemetryFumbleEvent Fumble;
    Fumble.FumblerName = TEXT("Unknown");
    Fumble.RecoveryName = TEXT("WR_TEST");
    Bus->PublishFumble(Fumble);
    ExpectLast(TEXT("Human recovers a fumble"), EPSForceFeedbackCue::Fumble, true);

    // Back on keyboard: the cue is still decided but nothing is sent to motors.
    PublishDevice(Bus, EPSInputDevice::KeyboardMouse, EPSInputDevice::Gamepad);
    PublishScore(Bus);
    ExpectLast(TEXT("Score on keyboard"), EPSForceFeedbackCue::Score, false);

    // The player's vibration setting off: nothing at all.
    Count = Dispatches.Num();
    Rumble->bEnabled = false;
    PublishScore(Bus);
    TestEqual(TEXT("Disabled rumble dispatches nothing"), Dispatches.Num(), Count);
    Rumble->bEnabled = true;

    // Released back to the AI: the old pawn's moments are no longer this player's.
    PublishControl(Bus, TEXT("WR_TEST"), false);
    TestTrue(TEXT("No controlled pawn after release"), Rumble->GetControlledPlayerName().IsEmpty());
    PublishCatch(Bus, TEXT("WR_TEST"), false);
    TestEqual(TEXT("A released pawn's catch is not felt"), Dispatches.Num(), Count);

    TestEqual(TEXT("Recent dispatches match what was broadcast"), Rumble->GetRecentDispatches().Num(), FMath::Min(Dispatches.Num(), 16));

    Rumble->UnbindFromBus();
    PublishScore(Bus);
    TestEqual(TEXT("Unbound, the bus no longer reaches the component"), Dispatches.Num(), Count);

    Rumble->OnDispatched.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
