#include "PSPlayDiagramWidget.h"
#include "PSOverlayPlayArtSubsystem.h"
#include "PSUITeamCatalog.h"
#include "Engine/World.h"
#include "Rendering/DrawElements.h"

void UPSPlayDiagramWidget::SetDiagram(const FPSPlayDiagram& InDiagram, const FPSPlayDiagramStyle& InStyle)
{
    Diagram = InDiagram;
    DiagramStyle = InStyle;
    PaintedSize = FVector2D::ZeroVector;
    PaintedStrokes.Reset();
}

bool UPSPlayDiagramWidget::ShowPlay(FName InPlayId)
{
    UWorld* World = GetWorld();
    UPSOverlayPlayArtSubsystem* PlayArt = World ? World->GetSubsystem<UPSOverlayPlayArtSubsystem>() : nullptr;
    FPSPlayDiagram Built;
    if (!PlayArt || !PlayArt->BuildPlayDiagram(InPlayId, Built))
    {
        ClearDiagram();
        return false;
    }
    SetDiagram(Built, PlayArt->GetStyle().Diagram);
    return true;
}

void UPSPlayDiagramWidget::ClearDiagram()
{
    SetDiagram(FPSPlayDiagram(), DiagramStyle);
}

TArray<FPSWidgetStroke> UPSPlayDiagramWidget::GetStrokesForSize(const FVector2D& Size) const
{
    return PSPlayDiagram::ToWidget(Diagram, Size, DiagramStyle.MinStrokeWidth);
}

int32 UPSPlayDiagramWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    int32 TopLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
    if (Diagram.IsEmpty())
    {
        return TopLayer;
    }

    // The widget's color and opacity: a fading menu fades its previews with it.
    const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
    FLinearColor Backdrop = FLinearColor::Black;
    UPSUITeamCatalog::ParseHexColor(DiagramStyle.BackgroundColor, Backdrop);
    Backdrop.A = FMath::Clamp(DiagramStyle.BackgroundOpacity, 0.f, 1.f);
    if (Backdrop.A > 0.f)
    {
        ++TopLayer;
        FSlateDrawElement::MakeBox(OutDrawElements, TopLayer, AllottedGeometry.ToPaintGeometry(), &BackdropBrush, ESlateDrawEffect::None, Backdrop * Tint);
    }

    const FVector2D Size = AllottedGeometry.GetLocalSize();
    if (!PaintedSize.Equals(Size))
    {
        PaintedSize = Size;
        PaintedStrokes = GetStrokesForSize(Size);
    }
    return PSWidgetDrawing::Paint(PaintedStrokes, AllottedGeometry, OutDrawElements, TopLayer, Tint, DiagramStyle.MinStrokeWidth);
}
