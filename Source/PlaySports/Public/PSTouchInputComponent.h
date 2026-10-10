// PSTouchInputComponent.h - Epic 130: touch drives the same catalog actions as the gamepad
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSTouchControls.h"
#include "PSTouchInputComponent.generated.h"

class APSPlayerController;
class UPSInputConfig;
class FPSTouchPreProcessor;

/** A finger the touch layer is tracking, and the control it landed on. */
struct FPSTouchPointer
{
    EPSTouchControlKind Kind = EPSTouchControlKind::Button;

    /** The stick or button it holds; none for a swipe in progress. */
    FName ControlId;

    /** The action the control drove when the finger landed. If a context change gives the
     *  control another action, the finger goes silent until it lifts, as Enhanced Input ignores
     *  a key held across a mapping change until it is released. */
    FName LatchedAction;
    bool bSilenced = false;

    /** Viewport pixels: where the finger landed (the stick's centre) and where it is now. */
    FVector2D Origin = FVector2D::ZeroVector;
    FVector2D Current = FVector2D::ZeroVector;

    double StartSeconds = 0.0;
};

/**
 * UPSTouchInputComponent is the touch layer (Epic 130). It turns a virtual stick, on-screen
 * buttons and swipes into the same catalog actions, with the same values, as the gamepad; it
 * has no gameplay of its own (Specs/Touch_Controls_Spec.md).
 *
 *  - The layout (Data/touch_controls.json, read through UPSDataIngestion) places the controls
 *    and gives each input context its button set; every control names a catalog action.
 *  - A control's action is the one the highest-priority active context binds it to, the same
 *    rule Enhanced Input applies to a key two contexts bind.
 *  - Its value goes through the gamepad mapping the catalog gives that action in that
 *    context: the stick through the left stick's dead zone and response curve, a press as a
 *    pressed button. APSPlayerController injects it into Enhanced Input, so Move, Sprint and
 *    every OnCatalogActionStarted consumer receive it exactly as they receive a gamepad.
 *
 * Fingers arrive from a Slate input pre-processor that only observes (never consumes) touch
 * events, in viewport pixels. While a menu is open, or the telestrator's analysis mode is on
 * (Epic 44), the layer stands down: menus take taps through their own widgets, and the
 * telestrator's drawing layer takes every finger. APSPlayerController owns one.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSTouchInputComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSTouchInputComponent();

    /** Loads the layout through UPSDataIngestion. False when the file is missing or
     *  malformed, which leaves no touch controls. */
    UFUNCTION(BlueprintCallable, Category = "Touch")
    bool LoadLayoutFromJson(const FString& JsonFilePath);

    /** The layout, loaded from Data/touch_controls.json on first use. */
    const FPSTouchLayout& GetLayout();

    /** The game viewport's size in pixels. The pre-processor keeps it current on a device;
     *  headless tests set it. */
    UFUNCTION(BlueprintCallable, Category = "Touch")
    void SetViewportSize(const FVector2D& InViewportSize);

    UFUNCTION(BlueprintPure, Category = "Touch")
    FVector2D GetViewportSize() const { return ViewportSize; }

    /** Where ControlId is drawn, in viewport pixels: its centre and its radius. False for a
     *  swipe, an unknown control, or before the viewport size is known. */
    UFUNCTION(BlueprintCallable, Category = "Touch")
    bool GetControlPlacement(FName ControlId, FVector2D& OutCenter, float& OutRadius);

    /** The controls to draw now, with the action each drives in the active contexts. */
    UFUNCTION(BlueprintCallable, Category = "Touch")
    TArray<FPSTouchBindingDef> GetActiveControls();

    /** What the touch HUD draws now (Epic 146.4): the stick and every button an active context
     *  binds, each placed (GetControlPlacement), with the action and context it drives and
     *  whether a finger holds it; the held stick also gives its finger's touch-down point and
     *  current position. Empty while the layer stands down or before the viewport size is
     *  known. */
    UFUNCTION(BlueprintCallable, Category = "Touch")
    TArray<FPSTouchControlView> GetControlViews();

    /** The last swipe recognised and delivered (Direction None before the first). */
    const FPSTouchSwipeView& GetLastSwipe() const { return LastSwipe; }

    /** Finger events in viewport pixels from the top left, with a clock in seconds. Public so
     *  headless tests can inject gestures. */
    void TouchStarted(int32 FingerId, const FVector2D& Position, double TimeSeconds);
    void TouchMoved(int32 FingerId, const FVector2D& Position, double TimeSeconds);
    void TouchEnded(int32 FingerId, const FVector2D& Position, double TimeSeconds);

    /** The action values touch drives this frame: the held stick and buttons, plus each swipe
     *  once. At most one sample per action. Empty while the layer stands down. Public so tests can
     *  read a frame without a tick. */
    TArray<FPSTouchActionSample> GatherActionSamples();

    /** Converts a Slate screen-space position to game-viewport pixels and refreshes the
     *  viewport size. False when there is no game viewport (headless). */
    bool ScreenToViewport(const FVector2D& ScreenPosition, FVector2D& OutPosition);

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    APSPlayerController* GetPlayerController() const;
    TArray<FName> GetActiveContexts() const;
    bool IsStoodDown() const;

    /** The safe area in viewport pixels: its top-left corner and its size. */
    void GetSafeArea(FVector2D& OutOrigin, FVector2D& OutSize);

    /** The sample ControlId gives with RawValue in Contexts. False when no active context binds
     *  the control or its action has no gamepad mapping there. */
    bool MakeSample(FName ControlId, const FInputActionValue& RawValue, const TArray<FName>& Contexts, FPSTouchActionSample& OutSample);

    /** Hands a sample to the controller to inject, through its gamepad mapping. */
    void DeliverSample(const FPSTouchActionSample& Sample);

    UPROPERTY(Transient)
    FPSTouchLayout Layout;

    bool bLayoutLoaded = false;

    FVector2D ViewportSize = FVector2D::ZeroVector;

    /** Fingers on a control or mid-swipe, by finger index. */
    TMap<int32, FPSTouchPointer> Pointers;

    /** Swipes finished since the last gather, delivered once. */
    TArray<FPSTouchActionSample> PendingSwipes;

    FPSTouchSwipeView LastSwipe;

    TSharedPtr<FPSTouchPreProcessor> PreProcessor;
};
