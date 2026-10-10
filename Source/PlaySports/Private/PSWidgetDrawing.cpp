#include "PSWidgetDrawing.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"

FPSWidgetStroke PSWidgetDrawing::MakeStroke(const TArray<FVector2D>& Points, const FLinearColor& Color, float Width, FName Tag, int32 Layer, bool bClosed)
{
    FPSWidgetStroke Stroke;
    Stroke.Points = Points;
    Stroke.Color = Color;
    Stroke.Width = Width;
    Stroke.Tag = Tag;
    Stroke.Layer = Layer;
    Stroke.bClosed = bClosed;
    return Stroke;
}

TArray<FVector2D> PSWidgetDrawing::ArrowHead(const FVector2D& From, const FVector2D& Tip, float Length, float HalfAngleDegrees)
{
    const FVector2D Direction = (Tip - From).GetSafeNormal();
    if (Direction.IsNearlyZero())
    {
        return TArray<FVector2D>();
    }
    const FVector2D Back = -Direction * Length;
    return { Tip + Back.GetRotated(HalfAngleDegrees), Tip, Tip + Back.GetRotated(-HalfAngleDegrees) };
}

TArray<FVector2D> PSWidgetDrawing::Circle(const FVector2D& Center, float Radius, int32 Segments)
{
    const int32 Count = FMath::Max(3, Segments);
    TArray<FVector2D> Points;
    Points.Reserve(Count);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const float Angle = 2.f * PI * Index / Count;
        Points.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
    }
    return Points;
}

TArray<FVector2D> PSWidgetDrawing::CrossArm(const FVector2D& Center, float Radius, bool bRising)
{
    const float Reach = Radius * UE_HALF_SQRT_2;
    const FVector2D Arm = bRising ? FVector2D(Reach, Reach) : FVector2D(Reach, -Reach);
    return { Center - Arm, Center + Arm };
}

int32 PSWidgetDrawing::Paint(const TArray<FPSWidgetStroke>& Strokes, const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId,
    const FLinearColor& Tint, float MinWidth)
{
    int32 TopLayer = LayerId;
    for (const FPSWidgetStroke& Stroke : Strokes)
    {
        if (Stroke.Points.Num() < 2)
        {
            continue;
        }
        TArray<FVector2f> Points;
        Points.Reserve(Stroke.Points.Num() + 1);
        for (const FVector2D& Point : Stroke.Points)
        {
            Points.Add(FVector2f(Point));
        }
        if (Stroke.bClosed)
        {
            const FVector2f First = Points[0];
            Points.Add(First);
        }
        const FLinearColor Color = Stroke.Color * Tint;
        if (Color.A <= 0.f)
        {
            continue;
        }
        const int32 Layer = LayerId + 1 + FMath::Max(0, Stroke.Layer);
        FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, Color, true,
            FMath::Max(Stroke.Width, MinWidth));
        TopLayer = FMath::Max(TopLayer, Layer);
    }
    return TopLayer;
}
