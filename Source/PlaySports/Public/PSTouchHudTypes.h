// PSTouchHudTypes.h - Epic 146.4: what the touch HUD draws, from Data/touch_hud.json
#pragma once

#include "CoreMinimal.h"
#include "PSTouchControls.h"
#include "PSWidgetDrawing.h"
#include "PSTouchHudTypes.generated.h"

/**
 * How the on-screen touch controls look (Data/touch_hud.json; Specs/Touch_Controls_Spec.md
 * section 6). Where they are and what they drive is the touch layout's
 * (Data/touch_controls.json). Sizes are fractions of each control's own radius, so the look
 * scales with the layout on any screen. Defaults equal the file.
 */
USTRUCT(BlueprintType)
struct FPSTouchHudStyle
{
    GENERATED_BODY()

    /** How opaque a control is at rest, and while a finger holds it. Semi-transparent, so the
     *  field stays readable. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch|HUD")
    float RestOpacity = 0.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch|HUD")
    float PressedOpacity = 0.7f;

    /** A control's ring, and the darker colour it takes while held. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch|HUD")
    FString ControlColor = TEXT("#FFFFFF");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch|HUD")
    FString PressedColor = TEXT("#7A8590");

    /** The glyph label inside each button. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch|HUD")
    FString LabelColor = TEXT("#FFFFFF");

    /** The ring's thickness, as a fraction of the control's radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch|HUD")
    float RingWidth = 0.08f;

    /** The label's type size, as a fraction of the button's radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch|HUD")
    float LabelSize = 0.42f;

    /** The stick at rest is fainter still: RestOpacity times this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch|HUD")
    float StickIdleFade = 0.6f;

    /** The held stick's knob, as a fraction of the stick's throw. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch|HUD")
    float KnobRadius = 0.45f;

    /** How long a recognised swipe's arrow shows. With reduced motion on it shows for as long,
     *  without fading. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch|HUD")
    float SwipeFlashSeconds = 0.25f;

    /** The swipe arrow's length, as a fraction of the screen's height; its line's thickness and
     *  its head's length, as fractions of that length. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch|HUD")
    float SwipeArrowLength = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch|HUD")
    float SwipeArrowWidth = 0.06f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch|HUD")
    float SwipeArrowHead = 0.3f;

    /** Points round each ring. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch|HUD")
    int32 CircleSegments = 40;
};

/** A button's label: which control and action it names, where it goes and how big. */
USTRUCT(BlueprintType)
struct FPSTouchHudLabel
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Touch|HUD")
    FName ControlId;

    /** The action and context to look its touch glyph up with. */
    UPROPERTY(BlueprintReadOnly, Category = "Touch|HUD")
    FName ActionId;

    UPROPERTY(BlueprintReadOnly, Category = "Touch|HUD")
    FName ContextId;

    UPROPERTY(BlueprintReadOnly, Category = "Touch|HUD")
    FVector2D Center = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Touch|HUD")
    float FontSize = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Touch|HUD")
    FLinearColor Color = FLinearColor::White;
};

/** Everything the touch HUD paints in a frame, in the widget's local space. */
USTRUCT(BlueprintType)
struct FPSTouchHudDrawing
{
    GENERATED_BODY()

    /** Rings ("Button", "Stick"), the knob ("Knob") and the swipe arrow ("Swipe"). */
    UPROPERTY(BlueprintReadOnly, Category = "Touch|HUD")
    TArray<FPSWidgetStroke> Strokes;

    UPROPERTY(BlueprintReadOnly, Category = "Touch|HUD")
    TArray<FPSTouchHudLabel> Labels;
};

/**
 * The touch HUD's drawing, pure, so headless tests check it (UPSTouchHudWidget only paints it).
 */
namespace PSTouchHud
{
    PLAYSPORTS_API FString GetDefaultStylePath();

    /** Data/touch_hud.json through UPSDataIngestion; the defaults when it is missing or unsound. */
    PLAYSPORTS_API FPSTouchHudStyle LoadStyle(const FString& Path);

    /** Problems with Style, one line each (empty when sound); mirrors tools/validate_data.py. */
    PLAYSPORTS_API TArray<FString> ValidateStyle(const FPSTouchHudStyle& Style);

    /**
     * The drawing for Controls (viewport pixels) scaled into local space by PixelsToLocal, on a
     * screen ScreenHeight high (local units):
     *  - each button a ring at its centre and radius, with its label inside; while held, in
     *    PressedColor at PressedOpacity, else ControlColor at RestOpacity;
     *  - the stick at rest a fainter ring at its rest position; while held, a ring where the
     *    finger landed and a knob at the finger's deflection, clamped to the rim;
     *  - Swipe's arrow, centred where it happened and pointing its way, for SwipeFlashSeconds
     *    after it (NowSeconds on the same clock), fading out unless bReducedMotion.
     */
    PLAYSPORTS_API FPSTouchHudDrawing BuildDrawing(const TArray<FPSTouchControlView>& Controls, const FPSTouchSwipeView& Swipe,
        double NowSeconds, float PixelsToLocal, float ScreenHeight, bool bReducedMotion, const FPSTouchHudStyle& Style);
}
