// PSPlayDiagram.h - Epic 102.1: a play's compiled art drawn flat, as the play-call screen's preview
#pragma once

#include "CoreMinimal.h"
#include "PSPlayArtTypes.h"
#include "PSPlaybookData.h"
#include "PSPlayResolution.h"
#include "PSWidgetDrawing.h"
#include "PSPlayDiagram.generated.h"

/**
 * A play drawn as a flat diagram, in the field's plane: X upfield (where the offense goes), Y
 * across (the offense's right is +Y), cm. PSPlayDiagram::ToWidget fits it to any widget.
 */
USTRUCT(BlueprintType)
struct FPSPlayDiagram
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    FName PlayId;

    /** An offensive play: the offense is the side drawn, the defense the reference. */
    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    bool bOffense = true;

    /** Every line, in the field's plane, widths in cm. */
    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    TArray<FPSWidgetStroke> Strokes;

    /** The field the diagram shows: its centre (on the ball's Y) ... */
    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    FVector2D ViewCenter = FVector2D::ZeroVector;

    /** ... and half its depth (X) and width (Y). */
    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    FVector2D ViewExtent = FVector2D::ZeroVector;

    bool IsEmpty() const { return Strokes.Num() == 0; }
};

/** Where the field's plane lands in a widget: upfield is up, the offense's right is right, one
 *  scale both ways, centred. */
USTRUCT(BlueprintType)
struct FPSPlayDiagramTransform
{
    GENERATED_BODY()

    /** The field point at the widget's centre (X upfield, Y across). */
    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    FVector2D ViewCenter = FVector2D::ZeroVector;

    /** The widget's centre, in its local space. */
    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    FVector2D WidgetCenter = FVector2D::ZeroVector;

    /** Widget units per field cm. */
    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    float Scale = 0.f;

    /** A field point (X upfield, Y across) in the widget's local space (X right, Y down). */
    FVector2D ToWidget(const FVector2D& FieldPoint) const
    {
        return FVector2D(WidgetCenter.X + (FieldPoint.Y - ViewCenter.Y) * Scale, WidgetCenter.Y - (FieldPoint.X - ViewCenter.X) * Scale);
    }
};

/**
 * The play diagram (Epic 102.1): the play art the field draws (PSPlayArt, compiled from the play
 * as PSPlayResolution resolves it for the players where they stand) laid flat, so a preview shows
 * exactly what the AI will run. Pure: no world access.
 */
namespace PSPlayDiagram
{
    /** Problems with a diagram style, one line each (empty when sound). */
    PLAYSPORTS_API TArray<FString> ValidateStyle(const FPSPlayDiagramStyle& Style);

    /** A world location on the field's plane: (X upfield, Y across). */
    inline FVector2D ToField(const FVector& Location) { return FVector2D(Location.X, Location.Y); }

    /**
     * Play's diagram from its resolution (Resolved, one entry per player on the field) and its
     * compiled art (PSPlayArt::CompilePlayArt), against the line of scrimmage. Strokes, by layer:
     *  0. The line of scrimmage across the view ("Line"); the other side's players, faint
     *     ("Opponent"; none at an OpponentOpacity of 0).
     *  1. What the field art leaves out, faint in the side's color: a zone defender's drop from
     *     his spot to his star ("Drop"); a "go to your spot" -- a quarterback's drop, a back's
     *     path to the mesh -- with its arrowhead ("Spot"); a blocker's T in BlockColor ("Block"),
     *     its stem upfield for a run block and back for a pass block.
     *  2. The art: each ribbon as a polyline ("Route", or "Branch" for an option's branch, at the
     *     ribbon's opacity) ending in an arrowhead ("Arrowhead"), except an option's stem, whose
     *     branches go on from it; each star's outline ("Zone"); each man line (tagged by its
     *     Source: "Man", "Press" or "Shadow"); each rusher's arrow with its head (its Source:
     *     "Blitz" or "Rush"). The rings at the ends of routes are left out: the arrowhead marks
     *     the end. Widths are the art's, times WidthScale.
     *  3. The side's players ("Player"): a ring for an offensive player, an X for a defender.
     * The view is centred across on the ball, takes in every stroke with FieldMargin round it, and
     * is at least MinFieldWidth across and MinFieldDepth deep.
     */
    PLAYSPORTS_API FPSPlayDiagram BuildDiagram(const FPSPlayDefinition& Play, const TArray<FPSResolvedAssignment>& Resolved, const TArray<FPSPlayArtPrimitive>& Art,
        const FVector& LineOfScrimmage, const FPSPlayArtStyle& Style);

    /** Fits Diagram's view into a widget of WidgetSize: the largest scale at which the whole view
     *  fits both ways, centred. A zero Scale for an empty view or widget. */
    PLAYSPORTS_API FPSPlayDiagramTransform MakeTransform(const FPSPlayDiagram& Diagram, const FVector2D& WidgetSize);

    /** Diagram's strokes in a widget of WidgetSize (its local space), widths scaled with the
     *  field and no thinner than MinStrokeWidth. Empty for an empty diagram or widget. */
    PLAYSPORTS_API TArray<FPSWidgetStroke> ToWidget(const FPSPlayDiagram& Diagram, const FVector2D& WidgetSize, float MinStrokeWidth);
}
