// PSPhotoModeSubsystem.h - Epic 45: photo mode, a free camera over a paused game or replay
#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "Engine/Scene.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSPhotoModeTypes.h"
#include "PSPhotoModeSubsystem.generated.h"

class AActor;
class APSBroadcastCamera;
class APSPlayerController;
class UUserWidget;
class UPSReplaySubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPSPhotoModeSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSPhotoCaptureSignature, const FPSPhotoCapture&, Photo);

/**
 * UPSPhotoModeSubsystem is photo mode (Epic 45): the game (or a replay, Epic 41) holds still and
 * the broadcast camera flies free to compose a still.
 *
 *  - Entering: the PhotoMode button on the field or in a replay (or EnterPhotoMode). The game
 *    pauses, a replay is held (UPSReplaySubsystem::SetHeld: its playhead, its free camera and
 *    its automatic replays all wait), and every player controller gets the PhotoMode input
 *    context over whatever it had, with the Replay context taken off while photo mode is on.
 *    It refuses with no broadcast camera or with a menu open.
 *  - The camera: the Move stick flies it level, the turn buttons turn it (a step at once, then
 *    steadily while held), and it rises and sinks, zooms (field of view), rolls, and focuses
 *    (depth of field: a focus distance and the apertures in the tuning, the first being off).
 *    It stays within MaxDistanceCm of where photo mode began and above MinHeightCm.
 *  - Filters: a stack of the tuning's Filters, composed into one grade (ComposeLook) and laid
 *    over the camera's own post-process settings. The filter button steps through the
 *    tuning's Presets (named stacks); PushFilter and PopFilter edit the stack for a UI.
 *  - Framing guides (thirds, centre) as lines for a widget to draw, and a UI-hide toggle that
 *    hides every viewport widget, the HUD and the on-field overlays (the control reticle,
 *    Epic 28, and the ball-flight arc, Epic 30), and puts back exactly what it hid.
 *  - Capture: a high-resolution screenshot (the viewport's size times the tuning's
 *    multiplier, capped) to the engine's screenshot folder, in Photo. A headless run has no
 *    viewport and captures nothing.
 *  - Leaving puts everything back: the camera where it was with its view and post-process
 *    settings, the UI, the input contexts, the replay as it was (playing again if it was), and
 *    the game unpaused if photo mode paused it. A pause menu opened over photo mode holds it.
 *
 * It ticks paused or not and calls AdvancePhotoMode; headless tests, whose worlds don't tick,
 * call AdvancePhotoMode themselves. Drawing the guides and the controls is a widget's, not
 * built; this keeps the state and the math.
 */
UCLASS()
class PLAYSPORTS_API UPSPhotoModeSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    UPSPhotoModeSubsystem();

    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickableWhenPaused() const override { return true; }

    // --- Tuning --------------------------------------------------------------------------

    static FString GetDefaultTuningPath();

    /** The tuning, loaded from the default path on first use. */
    const FPSPhotoModeTuning& GetTuning();

    /** Replaces the tuning with JsonFilePath's (through UPSDataIngestion); a file that can't be
     *  read or fails ValidateTuning is refused and the current tuning kept. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    bool SetTuning(const FPSPhotoModeTuning& NewTuning);

    /** Problems with InTuning, one line each (empty when sound): speeds or limits not above 0,
     *  a field of view, roll or focus range out of order, apertures not ascending or negative,
     *  a filter without an id or listed twice, a bad tint, a saturation or contrast below 0, a
     *  vignette outside 0 to 1, no presets, a preset without an id, listed twice or naming a
     *  filter that isn't there, a capture multiplier below 1 or a maximum below 1. */
    static TArray<FString> ValidateTuning(const FPSPhotoModeTuning& InTuning);

    // --- Entering and leaving ------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    bool EnterPhotoMode();

    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    void ExitPhotoMode();

    UFUNCTION(BlueprintPure, Category = "PhotoMode")
    bool IsPhotoModeActive() const { return bActive; }

    /** Listens to Controller's buttons: the PhotoMode button at any time, the rest in photo
     *  mode. APSBroadcastCamera calls it for each controller that looks through it. Idempotent. */
    void BindController(APSPlayerController* Controller);

    void UnbindController(APSPlayerController* Controller);

    /** One step: flies the camera by the held buttons and the Move stick. The tick calls it;
     *  headless tests call it directly. */
    void AdvancePhotoMode(float DeltaSeconds);

    // --- The camera ----------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "PhotoMode")
    FVector GetCameraLocation() const { return CameraLocation; }

    /** Pitch, yaw and roll. */
    UFUNCTION(BlueprintPure, Category = "PhotoMode")
    FRotator GetCameraRotation() const { return FRotator(CameraPitch, CameraYaw, CameraRoll); }

    UFUNCTION(BlueprintPure, Category = "PhotoMode")
    float GetFieldOfView() const { return FieldOfView; }

    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    void SetFieldOfView(float Degrees);

    UFUNCTION(BlueprintPure, Category = "PhotoMode")
    float GetRoll() const { return CameraRoll; }

    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    void SetRoll(float Degrees);

    UFUNCTION(BlueprintPure, Category = "PhotoMode")
    float GetFocusDistance() const { return FocusDistance; }

    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    void SetFocusDistance(float Centimetres);

    /** The f-stop in use; 0 is depth of field off. */
    UFUNCTION(BlueprintPure, Category = "PhotoMode")
    float GetAperture() const;

    /** Steps to the next of the tuning's Apertures (after the last, the first) and returns it. */
    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    float CycleAperture();

    // --- Filters -------------------------------------------------------------------------

    /** The preset last chosen; None once the stack has been edited by hand. */
    UFUNCTION(BlueprintPure, Category = "PhotoMode")
    FName GetPresetId() const { return PresetId; }

    /** Makes PresetName's filters the stack. False for a name not in the tuning's Presets. */
    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    bool SetPreset(FName PresetName);

    /** Steps to the next of the tuning's Presets (after the last, the first) and returns it. */
    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    FName CyclePreset();

    /** Lays FilterId on top of the stack. False for a filter not in the tuning. */
    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    bool PushFilter(FName FilterId);

    /** Takes the top filter off the stack. False when it is empty. */
    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    bool PopFilter();

    UFUNCTION(BlueprintPure, Category = "PhotoMode")
    TArray<FName> GetFilterStack() const { return FilterStack; }

    /** How strongly the stack is applied, 0 (not at all) to 1. */
    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    void SetFilterStrength(float Strength);

    UFUNCTION(BlueprintPure, Category = "PhotoMode")
    float GetFilterStrength() const { return FilterStrength; }

    /** The grade the stack makes now. */
    UFUNCTION(BlueprintPure, Category = "PhotoMode")
    FPSPhotoLook GetLook() const;

    /**
     * Stack's filters (by id, in InTuning's Filters; unknown ids skipped) composed into one
     * grade, each blended toward no change by 1 - Strength. Saturation, contrast and tint
     * multiply; white balance shifts add up from 6500 K; the strongest vignette wins. A part
     * no filter changes stays the camera's own.
     */
    static FPSPhotoLook ComposeLook(const FPSPhotoModeTuning& InTuning, const TArray<FName>& Stack, float Strength);

    // --- Framing and the UI --------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    void SetGuide(EPSPhotoGuide InGuide) { Guide = InGuide; }

    UFUNCTION(BlueprintPure, Category = "PhotoMode")
    EPSPhotoGuide GetGuide() const { return Guide; }

    /** None, thirds, centre, none again. Returns the guide now shown. */
    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    EPSPhotoGuide CycleGuide();

    /** The lines InGuide draws. */
    UFUNCTION(BlueprintPure, Category = "PhotoMode")
    static TArray<FPSPhotoGuideLine> GetGuideLines(EPSPhotoGuide InGuide);

    /** Hides (or shows again) the viewport widgets, the HUD and the on-field overlays. Only in
     *  photo mode; leaving shows them. */
    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    void SetUIHidden(bool bHidden);

    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    void ToggleUIHidden() { SetUIHidden(!bUIHidden); }

    UFUNCTION(BlueprintPure, Category = "PhotoMode")
    bool IsUIHidden() const { return bUIHidden; }

    // --- Capture -------------------------------------------------------------------------

    /** Photo, in the engine's screenshot folder (FPaths::ScreenShotDir). */
    static FString GetDefaultCaptureDirectory();

    /** A photo's size for a viewport of ViewportSize under InTuning: the viewport times the
     *  multiplier, scaled down so its longer side is at most the maximum, never below the
     *  viewport itself. */
    static FIntPoint GetCaptureSize(const FIntPoint& ViewportSize, const FPSPhotoModeTuning& InTuning);

    /** Asks for a high-resolution screenshot of the camera's view to Directory (the default when
     *  empty). True when the engine took the request; false outside photo mode or with no game
     *  viewport (OutCapture still names the file it would have been). */
    UFUNCTION(BlueprintCallable, Category = "PhotoMode")
    bool Capture(const FString& Directory, FPSPhotoCapture& OutCapture);

    // --- Events --------------------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "PhotoMode")
    FPSPhotoModeSignature OnPhotoModeEntered;

    UPROPERTY(BlueprintAssignable, Category = "PhotoMode")
    FPSPhotoModeSignature OnPhotoModeExited;

    UPROPERTY(BlueprintAssignable, Category = "PhotoMode")
    FPSPhotoCaptureSignature OnPhotoCaptured;

    // --- Controls (Data/input_actions.json) ----------------------------------------------

    /** The context pushed on every player controller in photo mode. */
    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName PhotoContextId;

    /** Enters photo mode from the field or a replay. */
    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName EnterActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName TurnLeftActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName TurnRightActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName TurnUpActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName TurnDownActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName RiseActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName LowerActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName ZoomInActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName ZoomOutActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName RollLeftActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName RollRightActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName FocusNearActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName FocusFarActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName ApertureActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName FilterActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName GuidesActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName HideUIActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName CaptureActionId;

    UPROPERTY(EditDefaultsOnly, Category = "PhotoMode|Input")
    FName ExitActionId;

private:
    UFUNCTION()
    void HandleActionStarted(FName ActionId);

    UFUNCTION()
    void HandleActionCompleted(FName ActionId);

    UFUNCTION()
    void HandleReplayEnded();

    APSBroadcastCamera* FindBroadcastCamera() const;
    UPSReplaySubsystem* GetReplay() const;

    /** True while any player controller has a menu open. */
    bool IsMenuOpen() const;

    /** -1, 0 or +1 from a pair of held buttons. */
    float HeldAxis(FName Negative, FName Positive) const;

    /** Turns the camera by Yaw and Pitch degrees, pitch within its limit. */
    void Turn(float YawDegrees, float PitchDegrees);

    /** Puts the camera where photo mode has it: place, view, post-process. */
    void ApplyCamera();

    /** Leaves photo mode; bRestoreCamera false when a replay ending has already given the
     *  camera back. */
    void Leave(bool bRestoreCamera);

    UPROPERTY(Transient)
    FPSPhotoModeTuning Tuning;

    bool bTuningLoaded = false;
    bool bActive = false;

    // The camera photo mode flies, and how to give it back.
    TWeakObjectPtr<APSBroadcastCamera> HeldCamera;
    FTransform SavedTransform = FTransform::Identity;
    float SavedFieldOfView = 90.f;
    bool bSavedFreeCam = false;
    bool bSavedFollowing = true;

    /** The camera's post-process settings as they were; only photo mode's own fields are
     *  read from it and put back. */
    UPROPERTY(Transient)
    FPostProcessSettings SavedPostProcess;

    FVector Anchor = FVector::ZeroVector;
    FVector CameraLocation = FVector::ZeroVector;
    float CameraPitch = 0.f;
    float CameraYaw = 0.f;
    float CameraRoll = 0.f;
    float FieldOfView = 90.f;
    float FocusDistance = 2000.f;
    int32 ApertureIndex = 0;

    TArray<FName> FilterStack;
    FName PresetId;
    int32 PresetIndex = 0;
    float FilterStrength = 1.f;

    EPSPhotoGuide Guide = EPSPhotoGuide::None;

    // The UI hidden, to show again exactly as it was.
    bool bUIHidden = false;
    TArray<TPair<TWeakObjectPtr<UUserWidget>, ESlateVisibility>> HiddenWidgets;
    TArray<TWeakObjectPtr<AActor>> HiddenActors;
    TArray<TWeakObjectPtr<AActor>> HiddenHUDs;

    // What photo mode held, to let go of.
    bool bPausedGame = false;
    bool bHeldReplay = false;
    bool bResumeReplay = false;

    TArray<TWeakObjectPtr<APSPlayerController>> BoundControllers;

    /** The controllers photo mode took the Replay context from. */
    TArray<TWeakObjectPtr<APSPlayerController>> ReplayContextControllers;

    TSet<FName> HeldActions;
    int32 CaptureCount = 0;
};
