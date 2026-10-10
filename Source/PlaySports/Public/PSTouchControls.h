// PSTouchControls.h - Epic 130: the touch layout (Data/touch_controls.json) and how a touch control resolves to a catalog action
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "InputActionValue.h"
#include "PSInputConfigTypes.h"
#include "PSTouchControls.generated.h"

class UPSInputGlyphs;

/** What a touch control is: a virtual stick, an on-screen button, or a swipe gesture. */
UENUM(BlueprintType)
enum class EPSTouchControlKind : uint8
{
    Stick,
    Button,
    Swipe
};

/** A swipe's direction on screen (Up is toward the top of the screen). */
UENUM(BlueprintType)
enum class EPSSwipeDirection : uint8
{
    None,
    Left,
    Right,
    Up,
    Down
};

/** One place on the screen a finger can act: where it is and how big, not what it does.
 *  Positions are in the HUD-safe area: 0..1 across its width and height, from the top left.
 *  Sizes are fractions of the safe area's height, so a control stays round on any screen. */
USTRUCT(BlueprintType)
struct FPSTouchControlDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    FName ControlId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    EPSTouchControlKind Kind = EPSTouchControlKind::Button;

    /** Button: its centre. Stick: where it is drawn at rest (and its centre when the stick is
     *  not floating). Unused by swipes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    FVector2D Position = FVector2D::ZeroVector;

    /** Button: the hit radius. Stick: how far the finger travels for a full push. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    float Radius = 0.f;

    /** Swipes only. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    EPSSwipeDirection Direction = EPSSwipeDirection::None;
};

/** In one input context, the catalog action a touch control drives. */
USTRUCT(BlueprintType)
struct FPSTouchBindingDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    FName ControlId;

    /** A catalog action (Data/input_actions.json) that lives in this context. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    FName ActionId;
};

/** The touch button set of one catalog context. Contexts stack exactly as their mapping
 *  contexts do: a control bound in a higher-priority active context takes over the control. */
USTRUCT(BlueprintType)
struct FPSTouchContextDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    FName ContextId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    TArray<FPSTouchBindingDef> Bindings;
};

/** A rectangle in the HUD-safe area (0..1 coordinates, from the top left). */
USTRUCT(BlueprintType)
struct FPSTouchZone
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    FVector2D Min = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    FVector2D Max = FVector2D(1.f, 1.f);
};

/** Margins kept clear of controls, as fractions of the viewport: the Dynamic Island, the
 *  rounded corners and the home indicator on a phone held in landscape. */
USTRUCT(BlueprintType)
struct FPSTouchSafeZone
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    float Left = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    float Top = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    float Right = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    float Bottom = 0.f;
};

/** Top-level shape of Data/touch_controls.json (Epic 130). Loaded by
 *  UPSDataIngestion::LoadTouchLayoutFromJson; Specs/Touch_Controls_Spec.md explains it. */
USTRUCT(BlueprintType)
struct FPSTouchLayout
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    FPSTouchSafeZone SafeZone;

    /** Width over height of the safe area the layout is designed for. Used only to check that
     *  buttons fit and don't overlap; at runtime the real screen decides. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    float LayoutAspect = 2.f;

    /** True: the stick centres where the finger lands in StickZone. False: it stays put. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    bool bFloatingStick = true;

    /** Where a touch that lands on no button takes the stick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    FPSTouchZone StickZone;

    /** Where a touch that lands on no button can swipe. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    FPSTouchZone GestureZone;

    /** The shortest drag that counts as a swipe, in safe-area heights. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    float SwipeMinDistance = 0.12f;

    /** The longest a swipe may take; a slower drag is not a swipe. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    float SwipeMaxSeconds = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    TArray<FPSTouchControlDef> TouchControls;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    TArray<FPSTouchContextDef> TouchContexts;

    /** Catalog contexts deliberately left without touch controls (menus take taps through
     *  their own widgets). Every other catalog context needs a TouchContexts entry, so a new
     *  context can't slip past touch unnoticed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch")
    TArray<FName> ContextsWithoutTouch;
};

/** One action value the touch layer hands Enhanced Input this frame. */
USTRUCT(BlueprintType)
struct FPSTouchActionSample
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Touch")
    FName ControlId;

    UPROPERTY(BlueprintReadOnly, Category = "Touch")
    FName ActionId;

    /** The active context whose touch binding won the control. */
    UPROPERTY(BlueprintReadOnly, Category = "Touch")
    FName ContextId;

    /** The gamepad key the catalog binds the action to there. The value goes through that
     *  key's mapping (its modifiers and triggers), so it equals the gamepad's value. */
    UPROPERTY(BlueprintReadOnly, Category = "Touch")
    FKey GamepadKey;

    /** The gesture's value before the mapping: the stick's deflection, or true for a press. */
    UPROPERTY(BlueprintReadOnly, Category = "Touch")
    FInputActionValue RawValue;

    /** The value after the mapping's modifiers: what the action receives. */
    UPROPERTY(BlueprintReadOnly, Category = "Touch")
    FInputActionValue Value;
};

/**
 * The touch layout's rules, shared by the component and its validation. The touch layer has
 * no actions of its own: every control names a catalog action, and its value goes through
 * that action's gamepad mapping (Specs/Touch_Controls_Spec.md).
 */
namespace PSTouchControls
{
    /** Absolute path of the authored layout: <ProjectDir>/Data/touch_controls.json. */
    PLAYSPORTS_API FString GetDefaultLayoutPath();

    PLAYSPORTS_API const FPSTouchControlDef* FindControl(const FPSTouchLayout& Layout, FName ControlId);

    /** The swipe control for Direction, or null when the layout has none. */
    PLAYSPORTS_API const FPSTouchControlDef* FindSwipe(const FPSTouchLayout& Layout, EPSSwipeDirection Direction);

    /** The action ControlId drives while ActiveContexts are on the stack: the binding from the
     *  highest-priority active context that binds the control, the later one on a tie, the way
     *  Enhanced Input resolves a key two active contexts bind. False when none binds it. */
    PLAYSPORTS_API bool ResolveControl(const FPSTouchLayout& Layout, const FPSInputCatalog& Catalog, FName ControlId,
        const TArray<FName>& ActiveContexts, FName& OutActionId, FName& OutContextId);

    /** A finger drag of Delta (in safe-area heights, Y down) lasting Seconds: the swipe's
     *  direction, or None when it is too short or too slow. */
    PLAYSPORTS_API EPSSwipeDirection ClassifySwipe(const FVector2D& Delta, double Seconds, float MinDistance, float MaxSeconds);

    /** The stick's value for a finger Delta from its centre (safe-area heights, Y down):
     *  X right, Y forward (up the screen), full at Radius and clamped to the unit circle. */
    PLAYSPORTS_API FVector2D StickValue(const FVector2D& Delta, float Radius);

    /** Problems with Layout, one line each (empty when sound). With Catalog: every catalog
     *  context has a touch button set or is in ContextsWithoutTouch, every bound action exists
     *  and lives in its context with the right value type, and every action of a covered
     *  context has a touch control there. With Glyphs: the default touch glyph set draws every
     *  bound action. */
    PLAYSPORTS_API TArray<FString> ValidateLayout(const FPSTouchLayout& Layout, const FPSInputCatalog* Catalog, const UPSInputGlyphs* Glyphs);
}
