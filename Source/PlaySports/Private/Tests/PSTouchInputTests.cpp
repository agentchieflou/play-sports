// PSTouchInputTests.cpp -- Epic 130 (touch input abstraction)
//
// Tests covered:
//   1. Data/touch_controls.json loads through UPSDataIngestion and validates against the input
//      catalog and the glyph table; each validation rule fires on a broken layout. The Touch
//      glyph set answers per action, touch keys belong to the Touch device, and the active
//      device follows a finger (with the glyph a prompt would show following the bus event).
//   2. Injected touch gestures resolve to the same catalog actions, with the same values, as
//      their gamepad equivalents: the virtual stick against the left stick (dead zone, curve,
//      clamping), every on-screen button against its pad twin in every gameplay context, and
//      swipes against the pad button of the action they fire. The layer stands down while a
//      menu is open.
//
// Headless worlds have no local player, so the Enhanced Input subsystem is not here: the test
// reads the frame's samples (what the controller injects) and compares them with what the
// catalog's own mapping contexts give the gamepad key, read independently of the touch code.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSInputConfig.h"
#include "PSInputDeviceComponent.h"
#include "PSInputGlyphs.h"
#include "PSMenuComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "PSTouchControls.h"
#include "PSTouchInputComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSTouchInputTests
{
    static bool HasProblem(const TArray<FString>& Problems, const TCHAR* Fragment)
    {
        return Problems.ContainsByPredicate([Fragment](const FString& Problem) { return Problem.Contains(Fragment); });
    }

    static FPSTouchContextDef* FindTouchContext(FPSTouchLayout& Layout, const TCHAR* ContextId)
    {
        const FName Id(ContextId);
        return Layout.TouchContexts.FindByPredicate([Id](const FPSTouchContextDef& Def) { return Def.ContextId == Id; });
    }

    static FPSTouchControlDef* FindTouchControl(FPSTouchLayout& Layout, const TCHAR* ControlId)
    {
        const FName Id(ControlId);
        return Layout.TouchControls.FindByPredicate([Id](const FPSTouchControlDef& Def) { return Def.ControlId == Id; });
    }

    /** What a gamepad gives for Key pressed with RawValue while Contexts are active, read
     *  straight from the catalog's mapping contexts: the action the highest-priority active
     *  context maps Key to (the later one on a tie), and the value after that mapping's
     *  modifiers. False when no active context maps Key. */
    static bool GamepadEquivalent(const UPSInputConfig* Config, const TArray<FName>& Contexts, const FKey& Key, const FInputActionValue& RawValue,
        FName& OutActionId, FInputActionValue& OutValue)
    {
        bool bFound = false;
        int32 BestPriority = 0;
        for (const FName& ContextId : Contexts)
        {
            const UInputMappingContext* Context = Config->FindContext(ContextId);
            if (!Context)
            {
                continue;
            }
            const int32 Priority = Config->GetContextPriority(ContextId);
            for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
            {
                if (Mapping.Key != Key || (bFound && Priority < BestPriority))
                {
                    continue;
                }
                bFound = true;
                BestPriority = Priority;
                OutActionId = Config->FindActionId(Mapping.Action.Get());
                FInputActionValue Value = RawValue;
                for (UInputModifier* Modifier : Mapping.Modifiers)
                {
                    if (Modifier)
                    {
                        Value = Modifier->ModifyRaw(nullptr, Value, 0.f);
                    }
                }
                OutValue = Value;
            }
        }
        return bFound;
    }

    static bool SameValue(const FInputActionValue& A, const FInputActionValue& B)
    {
        return A.GetValueType() == B.GetValueType() && A.Get<FVector>().Equals(B.Get<FVector>(), 1.e-4);
    }

    static const FPSTouchActionSample* FindSample(const TArray<FPSTouchActionSample>& Samples, FName ActionId)
    {
        return Samples.FindByPredicate([ActionId](const FPSTouchActionSample& Sample) { return Sample.ActionId == ActionId; });
    }

    /** A point given in the layout's safe-area coordinates, in viewport pixels. */
    static FVector2D SafePoint(UPSTouchInputComponent* Touch, double X, double Y)
    {
        const FPSTouchSafeZone& Safe = Touch->GetLayout().SafeZone;
        const FVector2D Viewport = Touch->GetViewportSize();
        const FVector2D Origin(Safe.Left * Viewport.X, Safe.Top * Viewport.Y);
        const FVector2D Size(Viewport.X * (1.0 - Safe.Left - Safe.Right), Viewport.Y * (1.0 - Safe.Top - Safe.Bottom));
        return Origin + FVector2D(X, Y) * Size;
    }

    static double SafeHeight(UPSTouchInputComponent* Touch)
    {
        const FPSTouchSafeZone& Safe = Touch->GetLayout().SafeZone;
        return Touch->GetViewportSize().Y * (1.0 - Safe.Top - Safe.Bottom);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The layout and the touch glyphs validate; the device follows a finger
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTouchLayoutTest,
    "PlaySports.Input.TouchLayoutValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTouchLayoutTest::RunTest(const FString& Parameters)
{
    using namespace PSTouchInputTests;

    UPSInputConfig* Config = NewObject<UPSInputConfig>();
    TestTrue(TEXT("Catalog, tuning and glyphs load from Data/"), Config->LoadDefaults());
    UPSInputGlyphs* Glyphs = Config->GetGlyphs();
    if (!TestNotNull(TEXT("The input config owns a glyph table"), Glyphs))
    {
        return false;
    }

    FPSTouchLayout Layout;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    TestTrue(TEXT("touch_controls.json loads through UPSDataIngestion"), Ingestion->LoadTouchLayoutFromJson(PSTouchControls::GetDefaultLayoutPath(), Layout));
    TestTrue(TEXT("The layout has controls"), Layout.TouchControls.Num() > 0);

    const TArray<FString> Problems = PSTouchControls::ValidateLayout(Layout, &Config->Catalog, Glyphs);
    for (const FString& Problem : Problems)
    {
        AddError(FString::Printf(TEXT("touch_controls.json: %s"), *Problem));
    }
    TestEqual(TEXT("The layout validates with no problems"), Problems.Num(), 0);

    // Every gameplay context the controller pushes has a touch button set.
    const TArray<FName> GameplayContexts = { TEXT("OnField"), TEXT("PreSnap"), TEXT("Passing"), TEXT("BallCarrier"), TEXT("Defense") };
    for (const FName& ContextId : GameplayContexts)
    {
        TestTrue(*FString::Printf(TEXT("Touch covers %s"), *ContextId.ToString()),
            Layout.TouchContexts.ContainsByPredicate([ContextId](const FPSTouchContextDef& Def) { return Def.ContextId == ContextId; }));
    }

    // Broken layouts report each mistake.
    {
        FPSTouchLayout Broken = Layout;
        FPSTouchBindingDef Unknown;
        Unknown.ControlId = TEXT("ButtonTop");
        Unknown.ActionId = TEXT("NotARealAction");
        FindTouchContext(Broken, TEXT("OnField"))->Bindings.Add(Unknown);
        TestTrue(TEXT("An action the catalog lacks is reported"),
            HasProblem(PSTouchControls::ValidateLayout(Broken, &Config->Catalog, nullptr), TEXT("'NotARealAction' is not an action in the input catalog")));
    }
    {
        FPSTouchLayout Broken = Layout;
        FPSTouchBindingDef Elsewhere;
        Elsewhere.ControlId = TEXT("ButtonTop");
        Elsewhere.ActionId = TEXT("Juke");
        FindTouchContext(Broken, TEXT("OnField"))->Bindings.Add(Elsewhere);
        TestTrue(TEXT("An action outside its context is reported"),
            HasProblem(PSTouchControls::ValidateLayout(Broken, &Config->Catalog, nullptr), TEXT("'Juke' does not live in context 'OnField'")));
    }
    {
        FPSTouchLayout Broken = Layout;
        FPSTouchBindingDef StickOnButton;
        StickOnButton.ControlId = TEXT("Stick");
        StickOnButton.ActionId = TEXT("Spin");
        FindTouchContext(Broken, TEXT("BallCarrier"))->Bindings.Add(StickOnButton);
        TestTrue(TEXT("A stick bound to a Boolean action is reported"),
            HasProblem(PSTouchControls::ValidateLayout(Broken, &Config->Catalog, nullptr), TEXT("needs a 2D axis action")));
    }
    {
        FPSTouchLayout Broken = Layout;
        FindTouchContext(Broken, TEXT("Passing"))->Bindings.RemoveAll([](const FPSTouchBindingDef& Def) { return Def.ActionId == FName(TEXT("PumpFake")); });
        TestTrue(TEXT("An action of a covered context without a touch control is reported"),
            HasProblem(PSTouchControls::ValidateLayout(Broken, &Config->Catalog, nullptr), TEXT("action 'PumpFake' has no touch control")));
    }
    {
        FPSTouchLayout Broken = Layout;
        const FPSTouchControlDef* Bottom = FindTouchControl(Broken, TEXT("ButtonBottom"));
        FPSTouchControlDef* Right = FindTouchControl(Broken, TEXT("ButtonRight"));
        if (TestNotNull(TEXT("The layout has ButtonBottom"), Bottom) && TestNotNull(TEXT("The layout has ButtonRight"), Right))
        {
            Right->Position = Bottom->Position + FVector2D(0.01, 0.0);
            TestTrue(TEXT("Overlapping buttons are reported"), HasProblem(PSTouchControls::ValidateLayout(Broken, nullptr, nullptr), TEXT("overlap")));
        }
    }
    {
        UPSInputGlyphs* NoSlide = NewObject<UPSInputGlyphs>();
        NoSlide->Table = Glyphs->Table;
        for (FPSInputGlyphSetDef& Set : NoSlide->Table.GlyphSets)
        {
            if (Set.Device == EPSInputDevice::Touch)
            {
                Set.Actions.RemoveAll([](const FPSActionGlyphDef& Def) { return Def.ActionId == FName(TEXT("Slide")); });
            }
        }
        TestTrue(TEXT("A touch-bound action without a touch glyph is reported"),
            HasProblem(PSTouchControls::ValidateLayout(Layout, &Config->Catalog, NoSlide), TEXT("no glyph for 'Slide'")));
    }

    // Touch glyphs: the action's own picture, in any context the action lives in.
    TestNotNull(TEXT("A default Touch glyph set"), Glyphs->GetDefaultSet(EPSInputDevice::Touch));
    FPSInputGlyph Glyph;
    TestTrue(TEXT("Move on touch has a glyph"), Config->GetGlyphForAction(TEXT("Move"), TEXT("OnField"), EPSInputDevice::Touch, Glyph));
    TestEqual(TEXT("Move on touch is the stick"), Glyph.GlyphId, FName(TEXT("Touch_Stick")));
    TestTrue(TEXT("Juke on touch has a glyph"), Config->GetGlyphForAction(TEXT("Juke"), TEXT("BallCarrier"), EPSInputDevice::Touch, Glyph));
    TestEqual(TEXT("Juke on touch is its button"), Glyph.GlyphId, FName(TEXT("Touch_Juke")));
    TestFalse(TEXT("Juke has no glyph on the field outside the carrier context"), Config->GetGlyphForAction(TEXT("Juke"), TEXT("OnField"), EPSInputDevice::Touch, Glyph));
    TestTrue(TEXT("Touch keys belong to the Touch device"), UPSInputGlyphs::GetDeviceForKey(EKeys::TouchKeys[ETouchIndex::Touch1]) == EPSInputDevice::Touch);
    TestTrue(TEXT("Gamepad keys still belong to the gamepad"), UPSInputGlyphs::GetDeviceForKey(EKeys::Gamepad_FaceButton_Bottom) == EPSInputDevice::Gamepad);

    // The active device follows a finger, and the glyph a prompt shows follows the bus event.
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Test world created"), World))
    {
        return false;
    }
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    UPSInputDeviceComponent* Devices = Controller ? Controller->GetInputDeviceComponent() : nullptr;
    if (TestNotNull(TEXT("Telemetry bus exists"), Bus) && TestNotNull(TEXT("Controller has a device component"), Devices))
    {
        UPSInputConfig* ControllerConfig = Controller->GetInputConfig();
        TArray<FName> PromptGlyphs;
        const FDelegateHandle Handle = Bus->OnInputDeviceChangeMC.AddLambda([&PromptGlyphs, ControllerConfig](const FPSTelemetryInputDeviceEvent& Event)
        {
            FPSInputGlyph Prompt;
            ControllerConfig->GetGlyphForAction(TEXT("Confirm"), TEXT("OnField"), Event.ActiveDevice, Prompt);
            PromptGlyphs.Add(Prompt.GlyphId);
        });

        Devices->NotifyTouch();
        TestTrue(TEXT("A finger switches to Touch"), Devices->GetActiveDevice() == EPSInputDevice::Touch);
        Devices->NotifyInput(EKeys::Gamepad_FaceButton_Bottom, 1.f);
        TestTrue(TEXT("A pad button switches to the gamepad"), Devices->GetActiveDevice() == EPSInputDevice::Gamepad);
        Devices->NotifyInput(EKeys::TouchKeys[ETouchIndex::Touch1], 1.f);
        TestTrue(TEXT("A touch key switches back to Touch"), Devices->GetActiveDevice() == EPSInputDevice::Touch);

        TestEqual(TEXT("Three device changes reached the bus"), PromptGlyphs.Num(), 3);
        if (PromptGlyphs.Num() == 3)
        {
            TestEqual(TEXT("On touch the Confirm prompt is the touch button"), PromptGlyphs[0], FName(TEXT("Touch_Confirm")));
            TestEqual(TEXT("On the pad it is Xbox A"), PromptGlyphs[1], FName(TEXT("Xbox_A")));
            TestEqual(TEXT("Back on touch it is the touch button again"), PromptGlyphs[2], FName(TEXT("Touch_Confirm")));
        }

        // On a touch-screen device a lost gamepad falls back to touch, not keyboard/mouse.
        const bool bHadTouchScreen = Devices->bHasTouchScreen;
        Devices->bHasTouchScreen = true;
        Devices->NotifyInput(EKeys::Gamepad_FaceButton_Bottom, 1.f);
        Devices->NotifyConnectionChange(false, true);
        TestTrue(TEXT("A phone falls back to Touch when its pad disconnects"), Devices->GetActiveDevice() == EPSInputDevice::Touch);
        Devices->bHasTouchScreen = bHadTouchScreen;

        Bus->OnInputDeviceChangeMC.Remove(Handle);
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Touch gestures give the gamepad's actions and values
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTouchMatchesGamepadTest,
    "PlaySports.Input.TouchGesturesMatchGamepad",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTouchMatchesGamepadTest::RunTest(const FString& Parameters)
{
    using namespace PSTouchInputTests;

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Test world created"), World))
    {
        return false;
    }
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    UPSTouchInputComponent* Touch = Controller ? Controller->GetTouchInputComponent() : nullptr;
    if (!TestNotNull(TEXT("Pawn spawned"), Pawn) || !TestNotNull(TEXT("Controller has a touch component"), Touch))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }

    const UPSInputConfig* Config = Controller->GetInputConfig();
    Controller->Possess(Pawn);
    TestTrue(TEXT("Possession puts OnField on the stack"), Controller->IsInputContextActive(TEXT("OnField")));
    Touch->SetViewportSize(FVector2D(2400.0, 1100.0));
    TestTrue(TEXT("The touch layout loads"), Touch->GetLayout().TouchControls.Num() > 0);

    double Clock = 10.0;
    int32 Finger = 0;

    // Every sample must be exactly what its gamepad key gives in the same contexts.
    auto CheckAgainstPad = [this, Config, Controller](const FPSTouchActionSample& Sample, const TCHAR* What)
    {
        FName PadAction;
        FInputActionValue PadValue;
        const bool bPad = GamepadEquivalent(Config, Controller->GetActiveInputContexts(), Sample.GamepadKey, Sample.RawValue, PadAction, PadValue);
        TestTrue(*FString::Printf(TEXT("%s: its gamepad key does something here"), What), bPad);
        TestEqual(*FString::Printf(TEXT("%s: same action as the pad"), What), Sample.ActionId, PadAction);
        TestTrue(*FString::Printf(TEXT("%s: same value as the pad"), What), SameValue(Sample.Value, PadValue));
    };

    // The virtual stick against the left stick: dead zone, curve and clamping all match.
    {
        FVector2D StickCenter;
        float StickRadius = 0.f;
        TestTrue(TEXT("The stick has a placement"), Touch->GetControlPlacement(TEXT("Stick"), StickCenter, StickRadius));
        struct FStickCase
        {
            FVector2D FingerOffset;
            FVector2D PadStick;
            const TCHAR* Name;
        };
        const TArray<FStickCase> Cases = {
            { FVector2D(0.1, 0.0), FVector2D(0.1, 0.0), TEXT("A small push, inside the dead zone") },
            { FVector2D(0.6, 0.0), FVector2D(0.6, 0.0), TEXT("A partial push right") },
            { FVector2D(1.0, 0.0), FVector2D(1.0, 0.0), TEXT("A full push right") },
            { FVector2D(1.5, 0.0), FVector2D(1.0, 0.0), TEXT("A push past the rim, clamped") },
            { FVector2D(-0.4, -0.5), FVector2D(-0.4, 0.5), TEXT("Up and left on screen is forward and left") }
        };
        for (const FStickCase& Case : Cases)
        {
            const int32 StickFinger = ++Finger;
            Touch->TouchStarted(StickFinger, StickCenter, Clock);
            Touch->TouchMoved(StickFinger, StickCenter + Case.FingerOffset * StickRadius, Clock + 0.05);
            const TArray<FPSTouchActionSample> Samples = Touch->GatherActionSamples();
            const FPSTouchActionSample* MoveSample = FindSample(Samples, TEXT("Move"));
            if (TestNotNull(*FString::Printf(TEXT("%s: the stick drives Move"), Case.Name), MoveSample))
            {
                TestTrue(*FString::Printf(TEXT("%s: through the left stick's mapping"), Case.Name), MoveSample->GamepadKey == EKeys::Gamepad_Left2D);
                TestTrue(*FString::Printf(TEXT("%s: raw value is the pad's stick position"), Case.Name),
                    MoveSample->RawValue.Get<FVector2D>().Equals(Case.PadStick, 1.e-4));
                CheckAgainstPad(*MoveSample, Case.Name);

                FName PadAction;
                FInputActionValue PadValue;
                GamepadEquivalent(Config, Controller->GetActiveInputContexts(), EKeys::Gamepad_Left2D, FInputActionValue(Case.PadStick), PadAction, PadValue);
                TestTrue(*FString::Printf(TEXT("%s: equals the left stick pushed the same way"), Case.Name), SameValue(MoveSample->Value, PadValue));

                // The pawn moves the same way under either value.
                Pawn->ConsumeMovementInputVector();
                Controller->HandleMove(MoveSample->Value);
                const FVector FromTouch = Pawn->ConsumeMovementInputVector();
                Controller->HandleMove(PadValue);
                const FVector FromPad = Pawn->ConsumeMovementInputVector();
                TestTrue(*FString::Printf(TEXT("%s: the pawn gets the same movement"), Case.Name), FromTouch.Equals(FromPad, 1.e-4));
            }
            Touch->TouchEnded(StickFinger, StickCenter + Case.FingerOffset * StickRadius, Clock + 0.1);
            TestNull(*FString::Printf(TEXT("%s: releasing the stick stops Move"), Case.Name), FindSample(Touch->GatherActionSamples(), TEXT("Move")));
            Clock += 1.0;
        }
        Controller->HandleMove(FInputActionValue(FVector2D::ZeroVector));
    }

    // Every on-screen button against its pad twin, in every gameplay context.
    struct FButtonTwin
    {
        const TCHAR* ControlId;
        FKey PadKey;
    };
    const TArray<FButtonTwin> Twins = {
        { TEXT("ButtonBottom"), EKeys::Gamepad_FaceButton_Bottom },
        { TEXT("ButtonRight"), EKeys::Gamepad_FaceButton_Right },
        { TEXT("ButtonLeft"), EKeys::Gamepad_FaceButton_Left },
        { TEXT("ButtonTop"), EKeys::Gamepad_FaceButton_Top },
        { TEXT("ButtonUpperLeft"), EKeys::Gamepad_LeftShoulder },
        { TEXT("ButtonUpperRight"), EKeys::Gamepad_RightShoulder },
        { TEXT("Sprint"), EKeys::Gamepad_RightTrigger },
        { TEXT("TriggerLeft"), EKeys::Gamepad_LeftTrigger },
        { TEXT("Pause"), EKeys::Gamepad_Special_Right },
        { TEXT("ButtonView"), EKeys::Gamepad_Special_Left },
        { TEXT("DPadUp"), EKeys::Gamepad_DPad_Up },
        { TEXT("DPadDown"), EKeys::Gamepad_DPad_Down },
        { TEXT("DPadLeft"), EKeys::Gamepad_DPad_Left },
        { TEXT("DPadRight"), EKeys::Gamepad_DPad_Right }
    };
    const TArray<FName> DepthContexts = { NAME_None, TEXT("PreSnap"), TEXT("Passing"), TEXT("BallCarrier"), TEXT("Defense") };
    for (const FName& Depth : DepthContexts)
    {
        Controller->SetDepthContext(Depth);
        const FString StackName = Depth.IsNone() ? FString(TEXT("OnField")) : FString::Printf(TEXT("OnField+%s"), *Depth.ToString());
        for (const FButtonTwin& Twin : Twins)
        {
            const FString What = FString::Printf(TEXT("%s, %s"), *StackName, Twin.ControlId);
            FVector2D Center;
            float Radius = 0.f;
            if (!TestTrue(*FString::Printf(TEXT("%s: the button has a placement"), *What), Touch->GetControlPlacement(Twin.ControlId, Center, Radius)))
            {
                continue;
            }

            FName PadAction;
            FInputActionValue PadValue;
            const bool bPadActs = GamepadEquivalent(Config, Controller->GetActiveInputContexts(), Twin.PadKey, FInputActionValue(true), PadAction, PadValue);

            // A press near the button's rim still lands on it.
            const int32 ButtonFinger = ++Finger;
            Touch->TouchStarted(ButtonFinger, Center + FVector2D(Radius * 0.7, 0.0), Clock);
            const TArray<FPSTouchActionSample> Held = Touch->GatherActionSamples();
            if (bPadActs)
            {
                const FPSTouchActionSample* Press = FindSample(Held, PadAction);
                if (TestNotNull(*FString::Printf(TEXT("%s: does what the pad button does (%s)"), *What, *PadAction.ToString()), Press))
                {
                    TestEqual(*FString::Printf(TEXT("%s: from this button"), *What), Press->ControlId, FName(Twin.ControlId));
                    TestTrue(*FString::Printf(TEXT("%s: same value as the pad"), *What), SameValue(Press->Value, PadValue));
                    CheckAgainstPad(*Press, *What);
                }
                TestEqual(*FString::Printf(TEXT("%s: one action per press"), *What), Held.Num(), 1);
                TestEqual(*FString::Printf(TEXT("%s: still held next frame"), *What), Touch->GatherActionSamples().Num(), 1);
            }
            else
            {
                TestEqual(*FString::Printf(TEXT("%s: does nothing, like the pad button"), *What), Held.Num(), 0);
            }
            Touch->TouchEnded(ButtonFinger, Center, Clock + 0.05);
            TestEqual(*FString::Printf(TEXT("%s: released"), *What), Touch->GatherActionSamples().Num(), 0);
            Clock += 1.0;
        }
    }

    // A held button never turns into a different press when its context changes, as a held key
    // doesn't on the pad: the A-twin held from the hike must not throw to slot 5 when Passing
    // comes on (the rule Epic 104.4's input buffer relies on).
    Controller->SetDepthContext(NAME_None);
    {
        FVector2D Center;
        float Radius = 0.f;
        Touch->GetControlPlacement(TEXT("ButtonBottom"), Center, Radius);
        Touch->TouchStarted(200, Center, Clock);
        TestNotNull(TEXT("Held on the field, the A-twin confirms"), FindSample(Touch->GatherActionSamples(), TEXT("Confirm")));
        Controller->SetDepthContext(TEXT("Passing"));
        TestEqual(TEXT("When Passing comes on, the held button neither confirms nor throws"), Touch->GatherActionSamples().Num(), 0);
        TestEqual(TEXT("...and stays silent while it is held"), Touch->GatherActionSamples().Num(), 0);
        Touch->TouchEnded(200, Center, Clock + 0.3);
        Touch->TouchStarted(201, Center, Clock + 0.5);
        TestNotNull(TEXT("Pressed again, it throws to receiver 5"), FindSample(Touch->GatherActionSamples(), TEXT("PassTarget5")));
        Touch->TouchEnded(201, Center, Clock + 0.6);
        TestEqual(TEXT("Released"), Touch->GatherActionSamples().Num(), 0);
        Clock += 1.0;
    }

    // Swipes: the carrier's moves, each equal to the pad button of the same move.
    struct FSwipeCase
    {
        FVector2D Drag;
        double Seconds;
        FName ExpectedAction;
        const TCHAR* Name;
    };
    const double Height = SafeHeight(Touch);
    const FVector2D SwipeStart = SafePoint(Touch, 0.6, 0.5);
    Controller->SetDepthContext(TEXT("BallCarrier"));
    const TArray<FSwipeCase> Swipes = {
        { FVector2D(0.0, -0.3), 0.15, TEXT("Hurdle"), TEXT("Swipe up") },
        { FVector2D(0.0, 0.3), 0.15, TEXT("Slide"), TEXT("Swipe down") },
        { FVector2D(-0.3, 0.05), 0.15, TEXT("Juke"), TEXT("Swipe left") },
        { FVector2D(0.3, -0.05), 0.15, TEXT("Juke"), TEXT("Swipe right") },
        { FVector2D(0.0, -0.3), 0.8, NAME_None, TEXT("A slow drag") },
        { FVector2D(0.0, -0.03), 0.1, NAME_None, TEXT("A short flick") }
    };
    for (const FSwipeCase& Swipe : Swipes)
    {
        const int32 SwipeFinger = ++Finger;
        const FVector2D End = SwipeStart + Swipe.Drag * Height;
        Touch->TouchStarted(SwipeFinger, SwipeStart, Clock);
        TestEqual(*FString::Printf(TEXT("%s: nothing while the finger is down"), Swipe.Name), Touch->GatherActionSamples().Num(), 0);
        Touch->TouchMoved(SwipeFinger, End, Clock + Swipe.Seconds * 0.5);
        Touch->TouchEnded(SwipeFinger, End, Clock + Swipe.Seconds);
        const TArray<FPSTouchActionSample> Samples = Touch->GatherActionSamples();
        if (Swipe.ExpectedAction.IsNone())
        {
            TestEqual(*FString::Printf(TEXT("%s: is not a swipe"), Swipe.Name), Samples.Num(), 0);
        }
        else
        {
            TestEqual(*FString::Printf(TEXT("%s: one action"), Swipe.Name), Samples.Num(), 1);
            const FPSTouchActionSample* SwipeSample = FindSample(Samples, Swipe.ExpectedAction);
            if (TestNotNull(*FString::Printf(TEXT("%s: fires %s"), Swipe.Name, *Swipe.ExpectedAction.ToString()), SwipeSample))
            {
                CheckAgainstPad(*SwipeSample, Swipe.Name);
            }
            TestEqual(*FString::Printf(TEXT("%s: fires once"), Swipe.Name), Touch->GatherActionSamples().Num(), 0);
        }
        Clock += 1.0;
    }

    // No swipes on the field without the ball: the pad has no such button either.
    Controller->SetDepthContext(NAME_None);
    {
        const int32 SwipeFinger = ++Finger;
        Touch->TouchStarted(SwipeFinger, SwipeStart, Clock);
        Touch->TouchEnded(SwipeFinger, SwipeStart + FVector2D(0.0, -0.3) * Height, Clock + 0.15);
        TestEqual(TEXT("A swipe without the ball does nothing"), Touch->GatherActionSamples().Num(), 0);
        Clock += 1.0;
    }

    // Two fingers: the stick and a button at once, each its own action.
    {
        FVector2D StickCenter;
        FVector2D ButtonCenter;
        float Unused = 0.f;
        Touch->GetControlPlacement(TEXT("Stick"), StickCenter, Unused);
        Touch->GetControlPlacement(TEXT("Sprint"), ButtonCenter, Unused);
        Touch->TouchStarted(100, StickCenter, Clock);
        Touch->TouchMoved(100, StickCenter + FVector2D(0.0, -1000.0), Clock + 0.05);
        Touch->TouchStarted(101, ButtonCenter, Clock + 0.05);
        const TArray<FPSTouchActionSample> Samples = Touch->GatherActionSamples();
        TestNotNull(TEXT("Two fingers: Move"), FindSample(Samples, TEXT("Move")));
        TestNotNull(TEXT("Two fingers: Sprint"), FindSample(Samples, TEXT("Sprint")));

        // While a menu is open the touch layer stands down; menus take taps themselves.
        UPSMenuComponent* Menus = Controller->GetMenuComponent();
        if (TestNotNull(TEXT("Controller has a menu component"), Menus))
        {
            Menus->OpenRootScreen();
            TestEqual(TEXT("Nothing reaches the actions while a menu is open"), Touch->GatherActionSamples().Num(), 0);
            TestEqual(TEXT("No controls are shown while a menu is open"), Touch->GetActiveControls().Num(), 0);
            Menus->Resume();
            TestTrue(TEXT("Controls come back when the menu closes"), Touch->GetActiveControls().Num() > 0);
        }
        Touch->TouchEnded(100, StickCenter, Clock + 0.2);
        Touch->TouchEnded(101, ButtonCenter, Clock + 0.2);
        TestEqual(TEXT("All fingers up: no actions"), Touch->GatherActionSamples().Num(), 0);
    }

    // Headless there is no local player: injection reports that rather than pretending.
    TestFalse(TEXT("No Enhanced Input subsystem to inject into headless"),
        Controller->InjectCatalogInput(TEXT("Confirm"), FInputActionValue(true), TArray<UInputModifier*>(), TArray<UInputTrigger*>()));

    Controller->UnPossess();
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
