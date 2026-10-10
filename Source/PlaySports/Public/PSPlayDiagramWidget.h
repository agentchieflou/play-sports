// PSPlayDiagramWidget.h - Epic 102.1: paints a play diagram, fitted to whatever size it is given
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSPlayDiagram.h"
#include "PSPlayDiagramWidget.generated.h"

/**
 * UPSPlayDiagramWidget paints one play diagram (PSPlayDiagram) with Slate lines: a backdrop, then
 * the strokes fitted to the widget's own size, upfield up, so it reads the same as a thumbnail
 * on a phone or a large preview on a monitor. It has no designer tree and no fixed size; the slot
 * it sits in sizes it (the play-call screen puts it in a size box of the style's PreviewWidth by
 * PreviewHeight). A Widget Blueprint can place one and call ShowPlay.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSPlayDiagramWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Shows InDiagram, drawn with InStyle. */
    void SetDiagram(const FPSPlayDiagram& InDiagram, const FPSPlayDiagramStyle& InStyle);

    /** Shows the preview of InPlayId: the play resolved for the players where they stand
     *  (UPSOverlayPlayArtSubsystem::BuildPlayDiagram). False, and nothing shown, when there is no
     *  such play or nobody on the field. */
    UFUNCTION(BlueprintCallable, Category = "PlayCall")
    bool ShowPlay(FName InPlayId);

    /** Shows nothing. */
    UFUNCTION(BlueprintCallable, Category = "PlayCall")
    void ClearDiagram();

    UFUNCTION(BlueprintPure, Category = "PlayCall")
    FPSPlayDiagram GetDiagram() const { return Diagram; }

    UFUNCTION(BlueprintPure, Category = "PlayCall")
    FPSPlayDiagramStyle GetDiagramStyle() const { return DiagramStyle; }

    /** The strokes as painted in a widget of Size (Slate units): PSPlayDiagram::ToWidget with
     *  the style's MinStrokeWidth. */
    TArray<FPSWidgetStroke> GetStrokesForSize(const FVector2D& Size) const;

protected:
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
    UPROPERTY(Transient)
    FPSPlayDiagram Diagram;

    UPROPERTY(Transient)
    FPSPlayDiagramStyle DiagramStyle;

    /** A plain brush: drawn as a solid box in the backdrop's color. */
    FSlateBrush BackdropBrush;

    /** The strokes fitted to the size last painted at, kept until the size or diagram changes. */
    mutable FVector2D PaintedSize = FVector2D::ZeroVector;
    mutable TArray<FPSWidgetStroke> PaintedStrokes;
};
