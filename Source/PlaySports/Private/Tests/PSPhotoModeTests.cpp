// PSPhotoModeTests.cpp -- Epic 45 (photo mode)
//
// A broadcast camera, a player controller and a player in a headless world. Photo mode holds the
// game (or a replay) and flies the camera free, with zoom, roll, depth of field, filters,
// framing guides, a UI-hide toggle and capture.
//
// Tests covered:
//   1. Data/photo_mode.json loads and validates; bad tunings are refused; filter stacks compose
//      into one grade; the framing guides' lines; a photo's size against the viewport.
//   2. The catalog: a PhotoMode context over Replay, each button with a key, a pad button, their
//      glyphs and a touch twin, all triggering while the game is paused, and the way in from
//      the field and from a replay.
//   3. The free camera through the controller's buttons: entering from the camera's viewer,
//      flying, turning, rising, its leash and floor, zoom, roll, focus and aperture, filter
//      presets and a hand-made stack on the camera's post-process settings, guides, and
//      leaving with the camera put back as it was.
//   4. Over a replay: the replay is held (playhead, buttons) and its context taken off, and
//      both come back on leaving; a replay ending under photo mode ends it.
//   5. The UI toggle hides the HUD and the on-field overlays and shows exactly those again;
//      capture asks nothing of a headless run.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Misc/Paths.h"
#include "PSBroadcastCamera.h"
#include "PSDataIngestion.h"
#include "PSInputConfig.h"
#include "PSOverlayReticle.h"
#include "PSPhotoModeSubsystem.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSPlaySimulation.h"
#include "PSReplayFormat.h"
#include "PSReplaySubsystem.h"
#include "PSTouchControls.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPhotoModeTests
{
    UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    void DestroyTestWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    /** A camera on the sideline looking across the field, its viewer, and a player it follows. */
    struct FPhotoFixture
    {
        UWorld* World = nullptr;
        UPSPhotoModeSubsystem* Photo = nullptr;
        UPSReplaySubsystem* Replay = nullptr;
        APSBroadcastCamera* Camera = nullptr;
        APSPlayerController* Controller = nullptr;
        APSPlayerPawn* Player = nullptr;

        bool Setup()
        {
            World = CreateTestWorld();
            if (!World)
            {
                return false;
            }
            Photo = World->GetSubsystem<UPSPhotoModeSubsystem>();
            Replay = World->GetSubsystem<UPSReplaySubsystem>();
            FActorSpawnParameters SpawnParams;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Player = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector(0.0, 0.0, 90.0), FRotator::ZeroRotator, SpawnParams);
            if (Player)
            {
                FPlayerAttributes Attributes;
                Attributes.PlayerId = TEXT("WR_01");
                Attributes.DisplayName = TEXT("WR_01");
                Attributes.Role = EPlayerRole::WideReceiver;
                Player->InitializePlayer(Attributes);
            }
            Camera = World->SpawnActor<APSBroadcastCamera>(APSBroadcastCamera::StaticClass(), FVector(0.0, -2800.0, 600.0), FRotator(-10.0, 90.0, 0.0), SpawnParams);
            Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
            if (Camera)
            {
                Camera->SetTargetActor(Player);
            }
            return Photo && Replay && Camera && Controller && Player;
        }

        void Teardown()
        {
            if (Photo)
            {
                Photo->ExitPhotoMode();
            }
            if (Replay)
            {
                Replay->StopReplay();
            }
            if (World)
            {
                DestroyTestWorld(World);
            }
        }

        void Press(FName ActionId) const
        {
            Controller->OnCatalogActionStarted.Broadcast(ActionId);
        }

        void Release(FName ActionId) const
        {
            Controller->OnCatalogActionCompleted.Broadcast(ActionId);
        }

        /** Holds ActionId for Seconds of photo mode, then lets it go. */
        void Hold(FName ActionId, float Seconds) const
        {
            Press(ActionId);
            Photo->AdvancePhotoMode(Seconds);
            Release(ActionId);
        }
    };
}

// ---------------------------------------------------------------------------
// 1. Tuning, filters, guides and the photo's size
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSPhotoModeTuningTest,
    "PlaySports.PhotoMode.TuningFiltersAndFraming",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPhotoModeTuningTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSPhotoModeTuning Loaded;
    if (!TestTrue(TEXT("Data/photo_mode.json loads"), Ingestion->LoadPhotoModeTuningFromJson(UPSPhotoModeSubsystem::GetDefaultTuningPath(), Loaded)))
    {
        return false;
    }
    for (const FString& Problem : UPSPhotoModeSubsystem::ValidateTuning(Loaded))
    {
        AddError(FString::Printf(TEXT("photo_mode.json: %s"), *Problem));
    }
    const FPSPhotoModeTuning Defaults;
    TestEqual(TEXT("Defaults equal the file: the flying speed"), Loaded.MoveCmPerSecond, Defaults.MoveCmPerSecond);
    TestEqual(TEXT("... the leash"), Loaded.MaxDistanceCm, Defaults.MaxDistanceCm);
    TestEqual(TEXT("... the largest photo"), Loaded.MaxCaptureDimension, Defaults.MaxCaptureDimension);
    TestTrue(TEXT("The first aperture is depth of field off"), Loaded.Apertures.Num() > 1 && Loaded.Apertures[0] == 0.f);

    FPSPhotoModeTuning Bad = Loaded;
    Bad.MoveCmPerSecond = 0.f;
    Bad.MaxPitchDegrees = 95.f;
    Bad.MinFieldOfView = 50.f;
    Bad.MaxFieldOfView = 40.f;
    Bad.Apertures = { 2.8f, 1.4f };
    FPSPhotoFilter& Red = Bad.Filters.AddDefaulted_GetRef();
    Red.FilterId = TEXT("Red");
    Red.Tint = TEXT("red");
    Bad.Presets[0].Filters.Add(TEXT("Sepia"));
    Bad.CaptureResolutionMultiplier = 0.5f;
    TestEqual(TEXT("Each problem is named"), UPSPhotoModeSubsystem::ValidateTuning(Bad).Num(), 7);

    // Filter stacks.
    const FPSPhotoLook Plain = UPSPhotoModeSubsystem::ComposeLook(Loaded, TArray<FName>(), 1.f);
    TestTrue(TEXT("No filters change nothing"), !Plain.bColorGrade && !Plain.bWhiteTemp && !Plain.bVignette);
    const FPSPhotoLook Mono = UPSPhotoModeSubsystem::ComposeLook(Loaded, { FName(TEXT("Mono")) }, 1.f);
    TestTrue(TEXT("Mono takes the colour out"), Mono.bColorGrade && FMath::IsNearlyZero(Mono.Saturation) && !Mono.bWhiteTemp && !Mono.bVignette);
    const FPSPhotoLook HalfMono = UPSPhotoModeSubsystem::ComposeLook(Loaded, { FName(TEXT("Mono")) }, 0.5f);
    TestTrue(TEXT("... half of it at half strength"), FMath::IsNearlyEqual(HalfMono.Saturation, 0.5f));
    const FPSPhotoLook Noir = UPSPhotoModeSubsystem::ComposeLook(Loaded, { FName(TEXT("Mono")), FName(TEXT("Punch")), FName(TEXT("Vignette")) }, 1.f);
    TestTrue(TEXT("A stack multiplies its grades and keeps the vignette"), FMath::IsNearlyZero(Noir.Saturation) && FMath::IsNearlyEqual(Noir.Contrast, 1.2f)
        && Noir.bVignette && FMath::IsNearlyEqual(Noir.Vignette, 0.6f));
    const FPSPhotoLook Warm = UPSPhotoModeSubsystem::ComposeLook(Loaded, { FName(TEXT("Warm")) }, 1.f);
    TestTrue(TEXT("Warm shifts the white balance and tints"), Warm.bWhiteTemp && FMath::IsNearlyEqual(Warm.WhiteTemp, 5200.f)
        && Warm.bColorGrade && FMath::IsNearlyEqual(Warm.Tint.R, 1.f) && Warm.Tint.B < Warm.Tint.G && Warm.Tint.G < 1.f);
    const FPSPhotoLook Both = UPSPhotoModeSubsystem::ComposeLook(Loaded, { FName(TEXT("Warm")), FName(TEXT("Cool")) }, 1.f);
    TestTrue(*FString::Printf(TEXT("White balance shifts add up (%.0f K)"), Both.WhiteTemp), FMath::IsNearlyEqual(Both.WhiteTemp, 6500.f - 1300.f + 1500.f));
    const FPSPhotoLook Unknown = UPSPhotoModeSubsystem::ComposeLook(Loaded, { FName(TEXT("Sepia")) }, 1.f);
    TestFalse(TEXT("An unknown filter is skipped"), Unknown.bColorGrade);

    // Framing guides.
    TestEqual(TEXT("No guide, no lines"), UPSPhotoModeSubsystem::GetGuideLines(EPSPhotoGuide::None).Num(), 0);
    const TArray<FPSPhotoGuideLine> Thirds = UPSPhotoModeSubsystem::GetGuideLines(EPSPhotoGuide::Thirds);
    TestEqual(TEXT("Thirds: two lines each way"), Thirds.Num(), 4);
    TestTrue(TEXT("... a third of the way in"), Thirds.Num() == 4 && FMath::IsNearlyEqual(Thirds[0].Start.X, 1.0 / 3.0) && FMath::IsNearlyEqual(Thirds[0].End.Y, 1.0)
        && FMath::IsNearlyEqual(Thirds[3].Start.Y, 2.0 / 3.0));
    const TArray<FPSPhotoGuideLine> Center = UPSPhotoModeSubsystem::GetGuideLines(EPSPhotoGuide::Center);
    TestTrue(TEXT("Centre: a cross through the middle"), Center.Num() == 2 && FMath::IsNearlyEqual(Center[0].Start.X, 0.5) && FMath::IsNearlyEqual(Center[1].Start.Y, 0.5));

    // A photo's size.
    TestEqual(TEXT("A 1080p view makes a 4K photo"), UPSPhotoModeSubsystem::GetCaptureSize(FIntPoint(1920, 1080), Loaded), FIntPoint(3840, 2160));
    TestEqual(TEXT("A 4K view stops at the maximum"), UPSPhotoModeSubsystem::GetCaptureSize(FIntPoint(3840, 2160), Loaded), FIntPoint(7680, 4320));
    TestEqual(TEXT("A view past the maximum keeps its own size"), UPSPhotoModeSubsystem::GetCaptureSize(FIntPoint(8000, 4000), Loaded), FIntPoint(8000, 4000));
    TestEqual(TEXT("No view, no photo"), UPSPhotoModeSubsystem::GetCaptureSize(FIntPoint(0, 0), Loaded), FIntPoint(0, 0));
    return true;
}

// ---------------------------------------------------------------------------
// 2. The controls' catalog and touch twins
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSPhotoModeCatalogTest,
    "PlaySports.PhotoMode.CatalogAndTouch",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPhotoModeCatalogTest::RunTest(const FString& Parameters)
{
    UPSInputConfig* Input = NewObject<UPSInputConfig>();
    if (!TestTrue(TEXT("The input catalog loads"), Input->LoadDefaults()))
    {
        return false;
    }
    const UPSPhotoModeSubsystem* Probe = GetDefault<UPSPhotoModeSubsystem>();
    const FName Context = Probe->PhotoContextId;
    const FName ReplayContext = GetDefault<UPSReplaySubsystem>()->ReplayContextId;
    TestNotNull(TEXT("The PhotoMode context is in the catalog"), Input->FindContext(Context));
    TestTrue(TEXT("It outranks the replay's"), Input->GetContextPriority(Context) > Input->GetContextPriority(ReplayContext));

    FPSTouchLayout Layout;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    TestTrue(TEXT("The touch layout loads"), Ingestion->LoadTouchLayoutFromJson(PSTouchControls::GetDefaultLayoutPath(), Layout));
    for (const FString& Problem : PSTouchControls::ValidateLayout(Layout, &Input->Catalog, nullptr))
    {
        AddError(FString::Printf(TEXT("touch_controls.json: %s"), *Problem));
    }

    // Each touch control and the pad button it stands for.
    TMap<FName, FKey> PadTwins;
    PadTwins.Add(TEXT("ButtonBottom"), EKeys::Gamepad_FaceButton_Bottom);
    PadTwins.Add(TEXT("ButtonRight"), EKeys::Gamepad_FaceButton_Right);
    PadTwins.Add(TEXT("ButtonLeft"), EKeys::Gamepad_FaceButton_Left);
    PadTwins.Add(TEXT("ButtonTop"), EKeys::Gamepad_FaceButton_Top);
    PadTwins.Add(TEXT("ButtonUpperLeft"), EKeys::Gamepad_LeftShoulder);
    PadTwins.Add(TEXT("ButtonUpperRight"), EKeys::Gamepad_RightShoulder);
    PadTwins.Add(TEXT("TriggerLeft"), EKeys::Gamepad_LeftTrigger);
    PadTwins.Add(TEXT("Sprint"), EKeys::Gamepad_RightTrigger);
    PadTwins.Add(TEXT("DPadLeft"), EKeys::Gamepad_DPad_Left);
    PadTwins.Add(TEXT("DPadRight"), EKeys::Gamepad_DPad_Right);
    PadTwins.Add(TEXT("DPadUp"), EKeys::Gamepad_DPad_Up);
    PadTwins.Add(TEXT("DPadDown"), EKeys::Gamepad_DPad_Down);
    PadTwins.Add(TEXT("ButtonView"), EKeys::Gamepad_Special_Left);
    PadTwins.Add(TEXT("ButtonRightStick"), EKeys::Gamepad_RightThumbstick);
    PadTwins.Add(TEXT("SwipeLeft"), EKeys::Gamepad_RightStick_Left);
    PadTwins.Add(TEXT("SwipeRight"), EKeys::Gamepad_RightStick_Right);
    PadTwins.Add(TEXT("SwipeUp"), EKeys::Gamepad_RightStick_Up);
    PadTwins.Add(TEXT("SwipeDown"), EKeys::Gamepad_RightStick_Down);

    const TArray<FName> Stack = { FName(TEXT("OnField")), ReplayContext, Context };
    const TArray<FName> Actions = { Probe->TurnLeftActionId, Probe->TurnRightActionId, Probe->TurnUpActionId, Probe->TurnDownActionId,
        Probe->RiseActionId, Probe->LowerActionId, Probe->ZoomInActionId, Probe->ZoomOutActionId, Probe->RollLeftActionId,
        Probe->RollRightActionId, Probe->FocusNearActionId, Probe->FocusFarActionId, Probe->ApertureActionId, Probe->FilterActionId,
        Probe->GuidesActionId, Probe->HideUIActionId, Probe->CaptureActionId, Probe->ExitActionId };
    for (const FName& ActionId : Actions)
    {
        const FString Name = ActionId.ToString();
        const FPSInputActionDef* Def = Input->Catalog.Actions.FindByPredicate([ActionId](const FPSInputActionDef& Candidate) { return Candidate.ActionId == ActionId; });
        TestTrue(*FString::Printf(TEXT("%s is a Boolean action of the PhotoMode context"), *Name),
            Def && Def->ValueType == EInputActionValueType::Boolean && Def->Contexts.Contains(Context));
        const UInputAction* Action = Input->FindAction(ActionId);
        TestTrue(*FString::Printf(TEXT("%s triggers while the game is paused"), *Name), Action && Action->bTriggerWhenPaused);
        FPSInputGlyph Glyph;
        TestTrue(*FString::Printf(TEXT("%s: the pad glyph"), *Name), Input->GetGlyphForAction(ActionId, Context, EPSInputDevice::Gamepad, Glyph));
        TestTrue(*FString::Printf(TEXT("%s: the key glyph"), *Name), Input->GetGlyphForAction(ActionId, Context, EPSInputDevice::KeyboardMouse, Glyph));
        TestTrue(*FString::Printf(TEXT("%s: the touch glyph"), *Name), Input->GetGlyphForAction(ActionId, Context, EPSInputDevice::Touch, Glyph));

        // Its touch twin does what that control's pad button does in photo mode.
        bool bHasTwin = false;
        for (const TPair<FName, FKey>& Twin : PadTwins)
        {
            FName TouchAction;
            FName TouchContext;
            if (PSTouchControls::ResolveControl(Layout, Input->Catalog, Twin.Key, Stack, TouchAction, TouchContext) && TouchAction == ActionId)
            {
                bHasTwin = true;
                TestEqual(*FString::Printf(TEXT("%s: the %s control is its pad button's twin"), *Name, *Twin.Key.ToString()),
                    Input->FindActionForKey(Twin.Value, Context), ActionId);
            }
        }
        TestTrue(*FString::Printf(TEXT("%s has a touch control"), *Name), bHasTwin);
    }

    // The way in, from the field and from a replay.
    const FName Enter = Probe->EnterActionId;
    const FPSInputActionDef* EnterDef = Input->Catalog.Actions.FindByPredicate([Enter](const FPSInputActionDef& Candidate) { return Candidate.ActionId == Enter; });
    TestTrue(TEXT("PhotoMode lives on the field and in a replay"), EnterDef && EnterDef->Contexts.Contains(FName(TEXT("OnField"))) && EnterDef->Contexts.Contains(ReplayContext));
    const UInputAction* EnterAction = Input->FindAction(Enter);
    TestTrue(TEXT("... and works over a paused replay"), EnterAction && EnterAction->bTriggerWhenPaused);
    TestEqual(TEXT("View enters it on the field"), Input->FindActionForKey(EKeys::Gamepad_Special_Left, FName(TEXT("OnField"))), Enter);
    TestEqual(TEXT("... and in a replay"), Input->FindActionForKey(EKeys::Gamepad_Special_Left, ReplayContext), Enter);
    TestEqual(TEXT("A right-stick flick down enters it too"), Input->FindActionForKey(EKeys::Gamepad_RightStick_Down, FName(TEXT("OnField"))), Enter);
    TestEqual(TEXT("Before the snap View is the timeout"), Input->FindActionForKey(EKeys::Gamepad_Special_Left, FName(TEXT("PreSnap"))), FName(TEXT("Timeout")));
    TestTrue(TEXT("... but the flick down reaches the field's way in"), Input->FindActionForKey(EKeys::Gamepad_RightStick_Down, FName(TEXT("PreSnap"))).IsNone()
        && Input->FindActionForKey(EKeys::Gamepad_RightStick_Down, FName(TEXT("DefensePreSnap"))).IsNone());
    FName TouchAction;
    FName TouchContext;
    TestTrue(TEXT("... and so does a swipe down on touch"), PSTouchControls::ResolveControl(Layout, Input->Catalog, TEXT("SwipeDown"),
        { FName(TEXT("OnField")), FName(TEXT("PreSnap")) }, TouchAction, TouchContext) && TouchAction == Enter);
    TestTrue(TEXT("The touch View button enters it in a replay"), PSTouchControls::ResolveControl(Layout, Input->Catalog, TEXT("ButtonView"),
        { FName(TEXT("OnField")), ReplayContext }, TouchAction, TouchContext) && TouchAction == Enter);

    // The stick flies the camera; Start still pauses.
    TestEqual(TEXT("The left stick is Move in photo mode"), Input->FindActionForKey(EKeys::Gamepad_Left2D, Context), FName(TEXT("Move")));
    TestTrue(TEXT("Start isn't a photo button"), Input->FindActionForKey(EKeys::Gamepad_Special_Right, Context).IsNone());
    return true;
}

// ---------------------------------------------------------------------------
// 3. The free camera
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSPhotoModeCameraTest,
    "PlaySports.PhotoMode.FreeCamera",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPhotoModeCameraTest::RunTest(const FString& Parameters)
{
    using namespace PSPhotoModeTests;

    FPhotoFixture Fixture;
    if (!TestTrue(TEXT("The world is set up"), Fixture.Setup()))
    {
        Fixture.Teardown();
        return false;
    }
    UPSPhotoModeSubsystem* Photo = Fixture.Photo;
    APSBroadcastCamera* Camera = Fixture.Camera;
    APSPlayerController* Controller = Fixture.Controller;
    UCameraComponent* View = Camera->GetCameraComponent();
    const FPSPhotoModeTuning& Tuning = Photo->GetTuning();
    const FTransform Before = Camera->GetActorTransform();
    const float FieldOfViewBefore = View->FieldOfView;

    // The way in: the camera's viewer's PhotoMode button.
    Fixture.Press(Photo->EnterActionId);
    TestFalse(TEXT("A controller not looking through the camera can't enter"), Photo->IsPhotoModeActive());
    Camera->BecomeViewTarget(Controller);
    Fixture.Press(Photo->EnterActionId);
    if (!TestTrue(TEXT("The camera's viewer enters photo mode"), Photo->IsPhotoModeActive()))
    {
        Fixture.Teardown();
        return false;
    }
    TestFalse(TEXT("... once"), Photo->EnterPhotoMode());
    TestTrue(TEXT("It flies the camera itself"), Camera->bIsFreeCam);
    TestTrue(TEXT("The photo buttons are on"), Controller->IsInputContextActive(Photo->PhotoContextId));
    const float ToPlayer = static_cast<float>(FVector::Dist(Before.GetLocation(), Fixture.Player->GetActorLocation()));
    TestTrue(*FString::Printf(TEXT("It focuses on the player followed (%.0f cm)"), Photo->GetFocusDistance()), FMath::IsNearlyEqual(Photo->GetFocusDistance(), ToPlayer, 1.f));
    TestEqual(TEXT("It opens on the first preset"), Photo->GetPresetId(), Tuning.Presets[0].PresetId);

    // Flying on the stick, along its heading.
    Controller->HandleMove(FInputActionValue(FVector2D(0.0, 1.0)));
    Photo->AdvancePhotoMode(1.f);
    Controller->HandleMove(FInputActionValue(FVector2D::ZeroVector));
    const FVector Flown = Before.GetLocation() + FVector(0.0, Tuning.MoveCmPerSecond, 0.0);
    TestTrue(*FString::Printf(TEXT("Stick forward flies it forward (%s)"), *Camera->GetActorLocation().ToString()), Camera->GetActorLocation().Equals(Flown, 1.0));

    // Turning: a step at once, then steadily while held.
    const float Yaw = static_cast<float>(Camera->GetActorRotation().Yaw);
    Fixture.Press(Photo->TurnRightActionId);
    TestTrue(TEXT("A turn press steps at once"), FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(Yaw, static_cast<float>(Camera->GetActorRotation().Yaw)), Tuning.TurnStepDegrees, 0.1f));
    Photo->AdvancePhotoMode(0.5f);
    Fixture.Release(Photo->TurnRightActionId);
    Photo->AdvancePhotoMode(0.5f);
    TestTrue(TEXT("... and turns on while held"), FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(Yaw, static_cast<float>(Camera->GetActorRotation().Yaw)),
        Tuning.TurnStepDegrees + 0.5f * Tuning.TurnDegreesPerSecond, 0.1f));
    Fixture.Hold(Photo->TurnUpActionId, 10.f);
    TestTrue(TEXT("Tilting stops at the pitch limit"), FMath::IsNearlyEqual(static_cast<float>(Camera->GetActorRotation().Pitch), Tuning.MaxPitchDegrees, 0.1f));
    Fixture.Hold(Photo->TurnDownActionId, 10.f);

    // Up and down, the floor and the leash.
    const double Height = Camera->GetActorLocation().Z;
    Fixture.Hold(Photo->RiseActionId, 1.f);
    TestTrue(TEXT("It rises while held"), FMath::IsNearlyEqual(Camera->GetActorLocation().Z, Height + Tuning.RiseCmPerSecond, 1.0));
    Fixture.Hold(Photo->LowerActionId, 20.f);
    TestTrue(TEXT("It sinks no lower than its floor"), FMath::IsNearlyEqual(Camera->GetActorLocation().Z, static_cast<double>(Tuning.MinHeightCm), 1.0));
    Controller->HandleMove(FInputActionValue(FVector2D(1.0, 0.0)));
    Photo->AdvancePhotoMode(100.f);
    Controller->HandleMove(FInputActionValue(FVector2D::ZeroVector));
    const double Strayed = FVector::Dist(Camera->GetActorLocation(), Before.GetLocation());
    TestTrue(*FString::Printf(TEXT("It flies no farther than its leash (%.0f cm)"), Strayed), Strayed <= Tuning.MaxDistanceCm + 1.0);

    // Zoom, roll and focus.
    Fixture.Hold(Photo->ZoomInActionId, 1.f);
    TestTrue(TEXT("Zooming in narrows the view"), FMath::IsNearlyEqual(View->FieldOfView, FieldOfViewBefore - Tuning.ZoomDegreesPerSecond, 0.1f));
    Fixture.Hold(Photo->ZoomInActionId, 20.f);
    TestTrue(TEXT("... to its limit"), FMath::IsNearlyEqual(View->FieldOfView, Tuning.MinFieldOfView, 0.1f));
    Fixture.Hold(Photo->RollRightActionId, 1.f);
    TestTrue(TEXT("Rolling tilts the horizon"), FMath::IsNearlyEqual(static_cast<float>(Camera->GetActorRotation().Roll), Tuning.RollDegreesPerSecond, 0.1f));
    Fixture.Hold(Photo->RollRightActionId, 20.f);
    TestTrue(TEXT("... to its limit"), FMath::IsNearlyEqual(Photo->GetRoll(), Tuning.MaxRollDegrees, 0.1f));
    Photo->SetFocusDistance(1000.f);
    Fixture.Hold(Photo->FocusFarActionId, 1.f);
    TestTrue(*FString::Printf(TEXT("Focusing farther doubles it each FocusDoublingsPerSecond (%.0f)"), Photo->GetFocusDistance()),
        FMath::IsNearlyEqual(Photo->GetFocusDistance(), 1000.f * FMath::Pow(2.f, Tuning.FocusDoublingsPerSecond), 1.f));

    // Depth of field on the camera.
    TestFalse(TEXT("Depth of field starts off"), View->PostProcessSettings.bOverride_DepthOfFieldFstop != 0);
    Fixture.Press(Photo->ApertureActionId);
    TestEqual(TEXT("The aperture button opens the first f-stop"), Photo->GetAperture(), Tuning.Apertures[1]);
    TestTrue(TEXT("... on the camera, at the focus"), View->PostProcessSettings.bOverride_DepthOfFieldFstop != 0
        && FMath::IsNearlyEqual(View->PostProcessSettings.DepthOfFieldFstop, Tuning.Apertures[1])
        && FMath::IsNearlyEqual(View->PostProcessSettings.DepthOfFieldFocalDistance, Photo->GetFocusDistance()));
    for (int32 Step = 1; Step < Tuning.Apertures.Num(); ++Step)
    {
        Fixture.Press(Photo->ApertureActionId);
    }
    TestTrue(TEXT("Round the apertures, it is off again"), Photo->GetAperture() == 0.f && View->PostProcessSettings.bOverride_DepthOfFieldFstop == 0);

    // Filter presets, and a stack by hand.
    Fixture.Press(Photo->FilterActionId);
    TestEqual(TEXT("The filter button steps to the next preset"), Photo->GetPresetId(), Tuning.Presets[1].PresetId);
    const FPSPhotoLook Graded = Photo->GetLook();
    TestTrue(TEXT("... graded on the camera"), View->PostProcessSettings.bOverride_ColorSaturation != 0
        && FMath::IsNearlyEqual(View->PostProcessSettings.ColorSaturation.X, static_cast<double>(Graded.Saturation), 1.0e-4)
        && FMath::IsNearlyEqual(View->PostProcessSettings.ColorContrast.X, static_cast<double>(Graded.Contrast), 1.0e-4));
    TestTrue(TEXT("Noir"), Photo->SetPreset(TEXT("Noir")));
    TestTrue(TEXT("... vignetted"), View->PostProcessSettings.bOverride_VignetteIntensity != 0 && FMath::IsNearlyEqual(View->PostProcessSettings.VignetteIntensity, 0.6f));
    TestFalse(TEXT("A preset that isn't there is refused"), Photo->SetPreset(TEXT("Sepia")));
    TestTrue(TEXT("A filter laid on top"), Photo->PushFilter(TEXT("Warm")));
    TestTrue(TEXT("... makes the stack the viewer's own"), Photo->GetPresetId().IsNone() && Photo->GetFilterStack().Num() == 4);
    TestTrue(TEXT("... and warms the camera"), View->PostProcessSettings.bOverride_WhiteTemp != 0 && FMath::IsNearlyEqual(View->PostProcessSettings.WhiteTemp, 5200.f));
    TestFalse(TEXT("A filter that isn't there is refused"), Photo->PushFilter(TEXT("Sepia")));
    TestTrue(TEXT("Taking it off"), Photo->PopFilter());
    TestTrue(TEXT("... gives the camera its own white balance back"), View->PostProcessSettings.bOverride_WhiteTemp == 0);
    Photo->SetFilterStrength(0.5f);
    TestTrue(TEXT("At half strength, half the grade"), FMath::IsNearlyEqual(Photo->GetLook().Saturation, 0.5f) && FMath::IsNearlyEqual(Photo->GetLook().Contrast, 1.1f));

    // Guides.
    Fixture.Press(Photo->GuidesActionId);
    TestEqual(TEXT("The guide button shows thirds"), Photo->GetGuide(), EPSPhotoGuide::Thirds);
    Fixture.Press(Photo->GuidesActionId);
    Fixture.Press(Photo->GuidesActionId);
    TestEqual(TEXT("... then the centre, then none"), Photo->GetGuide(), EPSPhotoGuide::None);

    // Leaving puts the camera back.
    Fixture.Press(Photo->ExitActionId);
    TestFalse(TEXT("B leaves photo mode"), Photo->IsPhotoModeActive());
    TestTrue(TEXT("The camera is back where it was"), Camera->GetActorLocation().Equals(Before.GetLocation(), 0.1)
        && Camera->GetActorRotation().Equals(Before.Rotator(), 0.1));
    TestTrue(TEXT("... with its view"), FMath::IsNearlyEqual(View->FieldOfView, FieldOfViewBefore));
    TestTrue(TEXT("... and its own post-process settings"), View->PostProcessSettings.bOverride_ColorSaturation == 0
        && View->PostProcessSettings.bOverride_VignetteIntensity == 0 && View->PostProcessSettings.bOverride_DepthOfFieldFstop == 0);
    TestTrue(TEXT("The broadcast drives it again"), !Camera->bIsFreeCam && Camera->bIsFollowing);
    TestFalse(TEXT("The photo buttons are off"), Controller->IsInputContextActive(Photo->PhotoContextId));
    const FVector Resting = Camera->GetActorLocation();
    Fixture.Hold(Photo->RiseActionId, 1.f);
    TestTrue(TEXT("A photo button after it does nothing"), Camera->GetActorLocation().Equals(Resting, 0.1));

    Fixture.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// 4. Over a replay
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSPhotoModeReplayTest,
    "PlaySports.PhotoMode.OverAReplay",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPhotoModeReplayTest::RunTest(const FString& Parameters)
{
    using namespace PSPhotoModeTests;

    FPhotoFixture Fixture;
    if (!TestTrue(TEXT("The world is set up"), Fixture.Setup()))
    {
        Fixture.Teardown();
        return false;
    }
    UPSPhotoModeSubsystem* Photo = Fixture.Photo;
    UPSReplaySubsystem* Replay = Fixture.Replay;
    APSPlayerController* Controller = Fixture.Controller;

    // A two-second clip of the receiver running.
    FPSReplayRecording Clip = UPSReplayFormat::MakeRecording(FPlayState(), TArray<FPlayerAttributes>(), TArray<FPlayerAttributes>());
    for (int32 Index = 0; Index < 3; ++Index)
    {
        FPSSnapshotFrame& Frame = Clip.Frames.AddDefaulted_GetRef();
        Frame.FrameIndex = Index;
        Frame.Time = static_cast<float>(Index);
        FPSPawnSnapshot& Snapshot = Frame.Pawns.AddDefaulted_GetRef();
        Snapshot.PlayerId = TEXT("WR_01");
        Snapshot.Location = FVector(500.0 * Index, 0.0, 90.0);
    }
    if (!TestTrue(TEXT("The clip plays"), Replay->StartReplay(Clip)))
    {
        Fixture.Teardown();
        return false;
    }
    Replay->AdvanceReplay(0.5f);
    TestTrue(TEXT("The replay's buttons are on"), Controller->IsInputContextActive(Replay->ReplayContextId));
    Fixture.Camera->BecomeViewTarget(Controller);

    // Photo mode holds the replay.
    Fixture.Press(Photo->EnterActionId);
    TestTrue(TEXT("The PhotoMode button in a replay enters photo mode"), Photo->IsPhotoModeActive());
    TestTrue(TEXT("The replay is held"), Replay->IsHeld() && Replay->GetState() == EPSReplayState::Paused);
    TestTrue(TEXT("... its buttons taken off, photo mode's on"), !Controller->IsInputContextActive(Replay->ReplayContextId)
        && Controller->IsInputContextActive(Photo->PhotoContextId));
    const float Playhead = Replay->GetPlayhead();
    Replay->AdvanceReplay(1.f);
    Fixture.Press(Replay->PlayPauseActionId);
    Replay->AdvanceReplay(1.f);
    TestTrue(TEXT("Nothing moves the held replay"), FMath::IsNearlyEqual(Replay->GetPlayhead(), Playhead) && Replay->GetState() == EPSReplayState::Paused);

    Photo->ExitPhotoMode();
    TestTrue(TEXT("Leaving lets the replay play on"), !Replay->IsHeld() && Replay->GetState() == EPSReplayState::Playing);
    TestTrue(TEXT("... with its buttons back"), Controller->IsInputContextActive(Replay->ReplayContextId) && !Controller->IsInputContextActive(Photo->PhotoContextId));

    // A replay the viewer held stays held.
    Replay->SetPaused(true);
    Photo->EnterPhotoMode();
    Photo->ExitPhotoMode();
    TestEqual(TEXT("A replay the viewer paused stays paused"), Replay->GetState(), EPSReplayState::Paused);

    // The replay ending under photo mode ends it.
    TestTrue(TEXT("In again"), Photo->EnterPhotoMode());
    Replay->StopReplay();
    TestFalse(TEXT("The replay ending ends photo mode"), Photo->IsPhotoModeActive());
    TestTrue(TEXT("... its buttons off and the replay's not back"), !Controller->IsInputContextActive(Photo->PhotoContextId)
        && !Controller->IsInputContextActive(Replay->ReplayContextId));
    TestTrue(TEXT("... the replay let go"), !Replay->IsHeld());
    TestFalse(TEXT("... and the camera the broadcast's again"), Fixture.Camera->bIsFreeCam);

    Fixture.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// 5. The UI toggle and capture
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSPhotoModeUITest,
    "PlaySports.PhotoMode.HideUIAndCapture",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPhotoModeUITest::RunTest(const FString& Parameters)
{
    using namespace PSPhotoModeTests;

    FPhotoFixture Fixture;
    if (!TestTrue(TEXT("The world is set up"), Fixture.Setup()))
    {
        Fixture.Teardown();
        return false;
    }
    UPSPhotoModeSubsystem* Photo = Fixture.Photo;
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AHUD* HUD = Fixture.World->SpawnActor<AHUD>(AHUD::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    APSOverlayReticle* Reticle = Fixture.World->SpawnActor<APSOverlayReticle>(APSOverlayReticle::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    APSOverlayReticle* Unused = Fixture.World->SpawnActor<APSOverlayReticle>(APSOverlayReticle::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (!TestNotNull(TEXT("A HUD"), HUD) || !TestNotNull(TEXT("A reticle"), Reticle) || !TestNotNull(TEXT("A second reticle"), Unused))
    {
        Fixture.Teardown();
        return false;
    }
    Reticle->SetActorHiddenInGame(false);

    Photo->SetUIHidden(true);
    TestFalse(TEXT("Outside photo mode the UI stays"), Photo->IsUIHidden() || !HUD->bShowHUD);

    Photo->EnterPhotoMode();
    Fixture.Press(Photo->HideUIActionId);
    TestTrue(TEXT("The hide button hides the UI"), Photo->IsUIHidden());
    TestTrue(TEXT("... the HUD"), !HUD->bShowHUD);
    TestTrue(TEXT("... and the reticle"), Reticle->IsHidden());
    Fixture.Press(Photo->HideUIActionId);
    TestTrue(TEXT("Again shows it"), !Photo->IsUIHidden() && HUD->bShowHUD && !Reticle->IsHidden());
    TestTrue(TEXT("... but not a reticle that was hidden already"), Unused->IsHidden());

    Fixture.Press(Photo->HideUIActionId);
    Photo->ExitPhotoMode();
    TestTrue(TEXT("Leaving photo mode shows the UI"), !Photo->IsUIHidden() && HUD->bShowHUD && !Reticle->IsHidden() && Unused->IsHidden());

    // Capture.
    const FString Directory = FPaths::AutomationTransientDir() / TEXT("PhotoModeTests");
    FPSPhotoCapture Photo1;
    TestFalse(TEXT("No photo outside photo mode"), Photo->Capture(Directory, Photo1));
    TestTrue(TEXT("... nor a name for one"), Photo1.FilePath.IsEmpty());
    Photo->EnterPhotoMode();
    FPSPhotoCapture Photo2;
    TestFalse(TEXT("A headless run has no viewport to photograph"), Photo->Capture(Directory, Photo2));
    TestTrue(TEXT("... though the photo is named, as a PNG in the folder asked for"), Photo2.FilePath.StartsWith(Directory) && Photo2.FilePath.EndsWith(TEXT(".png")));
    TestTrue(TEXT("... and nothing was asked of the engine"), !Photo2.bRequested && Photo2.Width == 0);
    TestTrue(TEXT("The default folder is the screenshots'"), UPSPhotoModeSubsystem::GetDefaultCaptureDirectory().StartsWith(FPaths::ScreenShotDir()));

    Fixture.Teardown();
    return true;
}

#endif
