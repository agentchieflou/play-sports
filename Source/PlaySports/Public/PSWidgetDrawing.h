// PSWidgetDrawing.h - 2D strokes that code-built widgets paint: play diagrams (Epic 102.1), the telestrator (Epic 44)
#pragma once

#include "CoreMinimal.h"
#include "PSWidgetDrawing.generated.h"

struct FGeometry;
class FSlateWindowElementList;

/** One line a widget paints: a polyline through Points (closed back to the first when bClosed),
 *  in whatever space its maker says (a play diagram's field plane, a widget's local space). */
USTRUCT(BlueprintType)
struct FPSWidgetStroke
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Widget")
    TArray<FVector2D> Points;

    /** Its color; the alpha is how opaque it is drawn. */
    UPROPERTY(BlueprintReadOnly, Category = "Widget")
    FLinearColor Color = FLinearColor::White;

    /** Its thickness, in the same space as Points. */
    UPROPERTY(BlueprintReadOnly, Category = "Widget")
    float Width = 1.f;

    UPROPERTY(BlueprintReadOnly, Category = "Widget")
    bool bClosed = false;

    /** Painted above every stroke of a lower layer (0 is the lowest). */
    UPROPERTY(BlueprintReadOnly, Category = "Widget")
    int32 Layer = 0;

    /** What it draws, as its maker names it ("Route", "Arrowhead", "Zone", ...). */
    UPROPERTY(BlueprintReadOnly, Category = "Widget")
    FName Tag;
};

/**
 * The geometry and painting shared by widgets that draw with lines (Slate's
 * FSlateDrawElement::MakeLines) instead of designer-made images, so they scale to any screen.
 * Everything but Paint is pure.
 */
namespace PSWidgetDrawing
{
    /** A stroke through Points. */
    PLAYSPORTS_API FPSWidgetStroke MakeStroke(const TArray<FVector2D>& Points, const FLinearColor& Color, float Width, FName Tag, int32 Layer = 0, bool bClosed = false);

    /** An arrowhead at Tip for a line coming from From: the left barb, the tip and the right barb,
     *  each barb Length back from the tip at HalfAngleDegrees off the line. Empty when From and
     *  Tip coincide. */
    PLAYSPORTS_API TArray<FVector2D> ArrowHead(const FVector2D& From, const FVector2D& Tip, float Length, float HalfAngleDegrees);

    /** Segments points evenly round a circle (at least 3), for a closed stroke. */
    PLAYSPORTS_API TArray<FVector2D> Circle(const FVector2D& Center, float Radius, int32 Segments);

    /** One arm of an X centred on Center, its ends Radius from it: the arm through (+,+) and
     *  (-,-) when bRising, else the one through (+,-) and (-,+). An X is both arms. */
    PLAYSPORTS_API TArray<FVector2D> CrossArm(const FVector2D& Center, float Radius, bool bRising);

    /**
     * Paints Strokes, already in Geometry's local space, as anti-aliased lines: each stroke's color
     * times Tint (the widget's own color and opacity), no thinner than MinWidth, each layer above
     * LayerId. Returns the highest layer painted on (LayerId when nothing was).
     */
    PLAYSPORTS_API int32 Paint(const TArray<FPSWidgetStroke>& Strokes, const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId,
        const FLinearColor& Tint, float MinWidth);
}
