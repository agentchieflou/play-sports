// PSTouchHudWidget.h - Epic 146.4: draws the on-screen touch controls, built in code
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSTouchHudTypes.h"
#include "PSTouchHudWidget.generated.h"

class UCanvasPanel;
class UTextBlock;
class UPSTouchInputComponent;

/**
 * UPSTouchHudWidget draws the touch controls (Specs/Touch_Controls_Spec.md section 6) while its
 * player's active input device is Touch: the stick, every button the active contexts bind with
 * its action's touch glyph, a held button darkened, the held stick's knob at its deflection, and
 * a recognised swipe's arrow, briefly. It reads the touch layer (UPSTouchInputComponent's
 * GetControlViews and GetLastSwipe) each frame, so it redraws as contexts change, and
 * PSTouchHud::BuildDrawing decides what to draw; the look is Data/touch_hud.json.
 *
 * It never takes a tap: every part is hit-test invisible, so fingers reach the touch layer,
 * which does the hit-testing. With no designer tree it builds its own (a canvas for the labels)
 * in code, so it needs no Widget Blueprint (Epic 146). APSHUD shows it.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSTouchHudWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeOnInitialized() override;
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
    void BuildDefaultLayout();
    void ShowLabels(const TArray<FPSTouchHudLabel>& Labels);
    UPSTouchInputComponent* GetTouchLayer() const;
    bool IsTouchActive() const;
    bool IsReducedMotion() const;

    UPROPERTY(Transient)
    UCanvasPanel* Canvas = nullptr;

    UPROPERTY(Transient)
    TArray<UTextBlock*> LabelBlocks;

    /** The type size each label was last given, so an unchanged one isn't re-laid out. */
    TArray<int32> LabelFontSizes;

    FPSTouchHudStyle Style;

    /** This frame's drawing, in the widget's local space. */
    FPSTouchHudDrawing Drawing;
};
