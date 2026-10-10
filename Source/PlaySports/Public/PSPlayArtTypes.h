// PSPlayArtTypes.h - Epic 27: play art as primitives (ribbons, rings, stars, connectors, arrows) and its style
#pragma once

#include "CoreMinimal.h"
#include "PSPlayArtTypes.generated.h"

class APSPlayerPawn;

/** The shapes all play art is made of: the offense's routes (Epic 27), the defense's assignments
 *  (Epic 31), and whatever the play-art pipeline compiles from play data (Epic 35). */
UENUM(BlueprintType)
enum class EPSPlayArtShape : uint8
{
    /** A path along the turf: Points is the polyline, from the player's feet. */
    Ribbon,
    /** A flat ring at Points[0]: where a route ends. */
    Ring,
    /** A five-pointed star at Points[0]: a zone's landmark. */
    Star,
    /** A straight line from Points[0] to Points[1]: a defender to his man. */
    Connector,
    /** A line from Points[0] with a head at Points[1]: a rusher's path. */
    Arrow
};

/** One piece of play art. Everything a renderer needs is here; it decides nothing. */
USTRUCT(BlueprintType)
struct FPSPlayArtPrimitive
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    EPSPlayArtShape Shape = EPSPlayArtShape::Ribbon;

    /** In world space, on the turf (raised the style's GroundOffset). A ribbon's polyline; a
     *  ring's or star's centre; a connector's or arrow's two ends. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    TArray<FVector> Points;

    /** A ribbon's cuts: indices into Points where it turns by the route-running model's break
     *  angle or more (FRouteRunningTuningRow::BreakMinAngleDegrees), or splits at an option
     *  route's read. The ribbon keeps a sharp corner there. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    TArray<int32> BreakIndices;

    /** A ribbon's fake breaks, a double move's (Epic 68): indices into Points. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    TArray<int32> FakeIndices;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FLinearColor Color = FLinearColor::White;

    /** A ribbon's or line's width; a ring's or star's radius (cm). */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float Size = 0.f;

    /** How opaque it is before any fade (0-1): an option route's branches are lighter. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float Opacity = 1.f;

    /** The route's place in the quarterback's progression (FPSPlayAssignment::ReadOrder): 1 for
     *  the primary read; 0 unranked. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    int32 ReadOrder = 0;

    /** Part of an option route that is run on one read only (Epic 68): a branch, or its ring. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bBranch = false;

    /** What it draws: a route's RouteId; on defense "Zone", "Man", "Shadow", "Press", "Blitz"
     *  or "Rush". */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FName Source;

    /** The player it belongs to. */
    UPROPERTY()
    TWeakObjectPtr<APSPlayerPawn> Pawn;

    /** A connector's other player: the receiver a man defender covers. */
    UPROPERTY()
    TWeakObjectPtr<APSPlayerPawn> Target;
};

/**
 * How the play art looks (Data/play_art.json; Architecture rule 4). Sizes are cm, colors
 * "#RRGGBB". These are what the editor-made renderer reads (Specs/Route_Ribbons_Spec.md); until
 * it exists, development builds draw debug lines.
 */
USTRUCT(BlueprintType)
struct FPSPlayArtStyle
{
    GENERATED_BODY()

    /** A route ribbon's width. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float RibbonWidth = 30.f;

    /** The primary read's ribbon is this many times as wide. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float PrimaryWidthScale = 1.5f;

    /** Art lies this far above the turf, clear of it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float GroundOffset = 2.f;

    /** The ring at a route's end. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float RingRadius = 45.f;

    /** The debug draw's mark at a cut (the editor ribbon articulates the corner itself). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float BreakMarkerRadius = 12.f;

    /** Route colors by read: the first for the primary read (ReadOrder 1), the next for the
     *  second, and so on; a read past the list takes its last color. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    TArray<FString> ReadColors;

    /** A route the play doesn't rank. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString UnrankedColor = TEXT("#FFFFFF");

    /** An option route's branches, each run on one read only, are drawn this opaque (0-1). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float BranchOpacity = 0.5f;

    /** At the snap the art fades out over this long on a Full tier; on others it goes at once,
     *  as it does everywhere at 0. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float SnapFadeSeconds = 0.4f;

    /** Plays of these PlayCategory values draw no route art: kicks and clock plays, whose
     *  "routes" are coverage lanes or none. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    TArray<FString> NoRouteArtCategories;

    /** The defense's icons (Epic 31). A zone's landmark is a star this size (its points'
     *  radius) ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float ZoneStarRadius = 60.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString ZoneStarColor = TEXT("#FFFFFF");

    /** ... a man defender is joined to his receiver by a line this wide ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float ManLineWidth = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString ManLineColor = TEXT("#FF8A3D");

    /** ... and a rusher has an arrow this wide, from his spot through the line to this far
     *  behind it: a blitzer's (the call's Blitz) in BlitzArrowColor, a lineman's rush in
     *  RushArrowColor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float RushArrowWidth = 12.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float RushArrowDepth = 200.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString BlitzArrowColor = TEXT("#F85149");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString RushArrowColor = TEXT("#C9D1D9");

    /** Defensive plays of these PlayCategory values draw no icons: the kicking game's returns,
     *  blocks and hands teams. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    TArray<FString> NoDefenseArtCategories;

    /** Development builds draw the art as debug lines until the editor-made renderer exists. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    bool bDrawDebug = true;
};
