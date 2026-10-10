// PSPlayArt.h - Epic 27: play art compiled from a resolved play, and drawn as debug shapes until the editor renderer exists
#pragma once

#include "CoreMinimal.h"
#include "PSPlayArtTypes.h"
#include "PSPlayResolution.h"

class UDataTable;
class UWorld;

/**
 * The play-art layer every overlay that draws a play shares (Track A): what to draw comes from a
 * play resolved by PSPlayResolution -- the jobs the AI is handed at the snap -- so the art is
 * never authored twice. Pure functions; the debug drawer is the one place art is drawn in code.
 */
namespace PSPlayArt
{
    /** The color for a route of this read: ReadColors[ReadOrder - 1] (past the list, its last),
     *  or UnrankedColor for 0. A color that doesn't parse is white. */
    PLAYSPORTS_API FLinearColor ColorForRead(const FPSPlayArtStyle& Style, int32 ReadOrder);

    /** Problems with a style, one line each (empty when sound). */
    PLAYSPORTS_API TArray<FString> ValidateStyle(const FPSPlayArtStyle& Style);

    /** Points on the turf: each at GroundZ plus the style's GroundOffset. */
    PLAYSPORTS_API TArray<FVector> OnTurf(const TArray<FVector>& Points, float GroundZ, const FPSPlayArtStyle& Style);

    /** Indices of Points (not its ends) where the path turns by BreakMinAngleDegrees or more. */
    PLAYSPORTS_API TArray<int32> FindCuts(const TArray<FVector>& Points, float BreakMinAngleDegrees);

    /**
     * The offense's route art for a resolved play: for every player who runs a route from the
     * library, a ribbon from his feet through his waypoints, its cuts and fakes marked, and a
     * ring where it ends, colored and sized by his read. An option route's ribbon stops at its
     * read; each branch (placed there as the route runner places it, authored outside) is a
     * lighter ribbon with its own ring. Blockers and "go to your spot" jobs draw nothing. The
     * art lies on the turf at GroundZ (the line's height).
     */
    PLAYSPORTS_API TArray<FPSPlayArtPrimitive> CompileRouteArt(const TArray<FPSResolvedAssignment>& Resolved, const UDataTable* RouteLibrary,
        const FPSPlayArtStyle& Style, float BreakMinAngleDegrees, float GroundZ);

    /** Draws Primitives as debug shapes at Opacity (0-1) for one frame. Development builds only;
     *  nothing in shipping, or without a world. */
    PLAYSPORTS_API void DrawDebug(const UWorld* World, const TArray<FPSPlayArtPrimitive>& Primitives, const FPSPlayArtStyle& Style, float Opacity);
}
