// PSPlayArt.h - Epic 27: play art compiled from a resolved play, and drawn as debug shapes until the editor renderer exists
#pragma once

#include "CoreMinimal.h"
#include "PSPlayArtTypes.h"
#include "PSPlayResolution.h"

class UDataTable;
class UPSCoverageMatchupSubsystem;
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

    /** Problems with a style, one line each (empty when sound), its Diagram block's too. */
    PLAYSPORTS_API TArray<FString> ValidateStyle(const FPSPlayArtStyle& Style);

    /** Points on the turf: each at GroundZ plus the style's GroundOffset. */
    PLAYSPORTS_API TArray<FVector> OnTurf(const TArray<FVector>& Points, float GroundZ, const FPSPlayArtStyle& Style);

    /** The five-pointed star of a zone's landmark round Center, flat on its plane: ten points,
     *  the tips Radius out (the first upfield, +X) and the notches between them 0.4 of that. */
    PLAYSPORTS_API TArray<FVector> StarOutline(const FVector& Center, float Radius);

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

    /**
     * The defense's icons for a resolved defensive play (Epic 31), its man matchups resolved
     * (PSPlayResolution::ResolveManMatchups): a star at each zone defender's landmark, a
     * connector from each man defender to his receiver (Source says how he got him: "Shadow",
     * "Press" or "Man"), and an arrow from each rusher through the line, RushArrowDepth behind
     * it -- "Blitz" for the call's blitzers, "Rush" for the rest. Run fits draw nothing.
     * LineOfScrimmage is the line the art lies on.
     */
    PLAYSPORTS_API TArray<FPSPlayArtPrimitive> CompileDefenseArt(const TArray<FPSResolvedAssignment>& Resolved, const FPSPlayArtStyle& Style,
        const FVector& LineOfScrimmage, const UPSCoverageMatchupSubsystem* Matchups = nullptr);

    // --- The play-art pipeline (Epic 35): one format drives the AI and the art ---

    /** Whether a badge letter is one or two capitals or digits. */
    PLAYSPORTS_API bool IsValidBadgeLetter(const FString& Letter);

    /** Problems with an assignment's annotation (FPSPlayArtAnnotation), one line each. */
    PLAYSPORTS_API TArray<FString> ValidateAnnotation(const FPSPlayArtAnnotation& Annotation);

    /** Layers an assignment's annotation over a piece of its art: its color, and its emphasis
     *  (EmphasisScale larger, bEmphasized). */
    PLAYSPORTS_API void ApplyAnnotation(FPSPlayArtPrimitive& Primitive, const FPSPlayArtAnnotation& Annotation, const FPSPlayArtStyle& Style);

    /** True for a play whose category draws no art (NoRouteArtCategories, NoDefenseArtCategories). */
    PLAYSPORTS_API bool DrawsNoArt(const FPSPlayDefinition& Play, const FPSPlayArtStyle& Style);

    /**
     * The compiler: a play's data, resolved for the players where they stand (PSPlayResolution,
     * the AI's own resolution; a defense's man matchups resolved too), into the primitives a
     * renderer draws -- CompileRouteArt for an offense, CompileDefenseArt for a defense, the
     * play's annotations layered on, nothing for a category that draws none.
     */
    PLAYSPORTS_API TArray<FPSPlayArtPrimitive> CompilePlayArt(const FPSPlayDefinition& Play, const TArray<FPSResolvedAssignment>& Resolved, const UDataTable* RouteLibrary,
        const FPSPlayArtStyle& Style, float BreakMinAngleDegrees, const FVector& LineOfScrimmage, const UPSCoverageMatchupSubsystem* Matchups = nullptr);

    /**
     * Problems with a play's art against the jobs its players are handed, one line each (empty
     * when consistent): every annotation sound; for an offense, each player running a route from
     * the library has exactly one ribbon through the waypoints he is handed (to an option's read)
     * with an end, at his read order, and nobody else has art; a ranked or library-less route is
     * reported; for a defense, each zone (or uncovered man) defender has one star where he plays,
     * each man defender one line to his receiver, each rusher one arrow from his spot, and run
     * fits nothing; no art for anybody outside the play; none at all for a category that draws
     * none.
     */
    PLAYSPORTS_API TArray<FString> ValidatePlayArt(const FPSPlayDefinition& Play, const TArray<FPSResolvedAssignment>& Resolved, const TArray<FPSPlayArtPrimitive>& Art,
        const UDataTable* RouteLibrary, const FPSPlayArtStyle& Style);

    /** Draws Primitives as debug shapes at Opacity (0-1) for one frame. Development builds only;
     *  nothing in shipping, or without a world. */
    PLAYSPORTS_API void DrawDebug(const UWorld* World, const TArray<FPSPlayArtPrimitive>& Primitives, const FPSPlayArtStyle& Style, float Opacity);
}
