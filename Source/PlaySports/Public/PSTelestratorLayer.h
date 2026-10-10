// PSTelestratorLayer.h - Epic 44: the telestrator's marks as lines on any screen, and its cursor (pure)
#pragma once

#include "CoreMinimal.h"
#include "PSInputConfigTypes.h"
#include "PSTelestratorTypes.h"
#include "PSWidgetDrawing.h"

/**
 * The drawing layer's arithmetic (UPSTelestratorWidget), with no world or Slate access so
 * headless tests drive it with any screen size. Marks keep normalized screen points ((0,0) top
 * left, the film frame's space); here they become lines in a widget's local space, sized as
 * shares of its shorter side so a phone and a monitor show the same drawing.
 */
namespace PSTelestratorLayer
{
    /** The tool after Tool: freehand, arrow, circle, player, and round to freehand. */
    PLAYSPORTS_API EPSTelestratorTool NextTool(EPSTelestratorTool Tool);

    /** The shorter side of a widget of WidgetSize (0 for an empty one). */
    PLAYSPORTS_API float ShortSide(const FVector2D& WidgetSize);

    /** A point in a widget of WidgetSize (its local space) as a normalized frame point, kept on
     *  the screen; the centre for an empty widget. */
    PLAYSPORTS_API FVector2D ToFrame(const FVector2D& Local, const FVector2D& WidgetSize);

    /** A normalized frame point in a widget of WidgetSize. */
    PLAYSPORTS_API FVector2D ToWidget(const FVector2D& FramePoint, const FVector2D& WidgetSize);

    /**
     * One mark's lines in a widget of WidgetSize, in Color, MarkWidth wide (no thinner than
     * MinStrokeWidth): freehand, a line through its points (a dot for a tap); arrow, a line from
     * its tail to its head with an arrowhead there ("Arrowhead"); circle, a ring round its first
     * point through its second, round on the screen whatever the widget's shape; player, a ring
     * PlayerRingRadius round where he stood. Tagged by tool ("Freehand", "Arrow", "Circle",
     * "Player"). Nothing for a mark without the points its tool needs.
     */
    PLAYSPORTS_API TArray<FPSWidgetStroke> MarkStrokes(EPSTelestratorTool Tool, const TArray<FVector2D>& ScreenPoints, const FLinearColor& Color,
        const FVector2D& WidgetSize, const FPSTelestratorTuning& Tuning);

    /**
     * Everything the layer draws in a widget of WidgetSize: every mark of Telestration (in
     * MarkColor, an auto-annotation's in AutoMarkColor), then the stroke being drawn with
     * LiveTool, on the layer above ("Live"): a freehand line so far, or the arrow or circle from
     * where it started to where it is. A player tap shows nothing until it lands.
     */
    PLAYSPORTS_API TArray<FPSWidgetStroke> BuildStrokes(const FPSTelestration& Telestration, const TArray<FVector2D>& LiveStroke, EPSTelestratorTool LiveTool,
        const FVector2D& WidgetSize, const FPSTelestratorTuning& Tuning);

    /** The drawing cursor at Cursor (widget local): a ring CursorRadius round with a cross in
     *  it, in MarkColor ("Cursor"). */
    PLAYSPORTS_API TArray<FPSWidgetStroke> CursorStrokes(const FVector2D& Cursor, const FVector2D& WidgetSize, const FPSTelestratorTuning& Tuning);

    /**
     * Where the cursor action's bindings push it now, X right and Y up, no longer than 1: each
     * held key pushes +X, or +Y with bSwizzleYX, the other way with bNegate (as the catalog's
     * modifiers make it push the action); a Gamepad_Left2D binding adds the left stick and a
     * Gamepad_Right2D one the right, each only past DeadZone and rescaled from there to full
     * tilt.
     */
    PLAYSPORTS_API FVector2D CursorDirection(const TArray<FPSInputKeyBinding>& Bindings, const TSet<FName>& HeldKeys, const FVector2D& LeftStick,
        const FVector2D& RightStick, float DeadZone);

    /** Cursor (widget local) moved along Direction (Y up the screen) for DeltaSeconds at
     *  CursorSpeed shorter sides a second, kept inside the widget. */
    PLAYSPORTS_API FVector2D MoveCursor(const FVector2D& Cursor, const FVector2D& Direction, float DeltaSeconds, const FVector2D& WidgetSize,
        const FPSTelestratorTuning& Tuning);
}
