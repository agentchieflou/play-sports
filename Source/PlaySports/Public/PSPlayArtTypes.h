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

    /** The play emphasizes this assignment (its Art.bEmphasis, Epic 35): drawn EmphasisScale
     *  larger, for a renderer to make it stand out further. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bEmphasized = false;

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
 * How a play is drawn as a flat diagram (Epic 102.1): the play-call screen's previews, made from
 * the same compiled art as the field's (PSPlayDiagram). The play art's own sizes and colors carry
 * over; these add the diagram's players, the marks the field art leaves out (blocks, a "go to your
 * spot", a zone defender's drop) and its framing. Sizes are field cm unless they say otherwise,
 * colors "#RRGGBB". Data/play_art.json's Diagram block.
 */
USTRUCT(BlueprintType)
struct FPSPlayDiagramStyle
{
    GENERATED_BODY()

    /** The diagram shows at least this much field across, centred on the ball ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float MinFieldWidth = 3600.f;

    /** ... and at least this much of it deep. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float MinFieldDepth = 2400.f;

    /** Field kept clear round the drawing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float FieldMargin = 250.f;

    /** The field art's line widths (a ribbon's, a man line's) are drawn this many times as wide. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float WidthScale = 1.f;

    /** No line is drawn thinner than this on screen (Slate units, which scale with the display). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float MinStrokeWidth = 1.5f;

    /** A player: an offensive player's ring, a defender's X, this far out. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float PlayerRadius = 75.f;

    /** The width of the diagram's own marks: players, blocks, guides and the line of scrimmage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float MarkWidth = 14.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString OffenseColor = TEXT("#FFFFFF");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString DefenseColor = TEXT("#FF8A3D");

    /** The other side's players are drawn this opaque (0-1) for reference; 0 leaves them out. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float OpponentOpacity = 0.35f;

    /** The marks of where a player goes that the field art leaves out -- a zone defender's drop to
     *  his landmark, a quarterback's drop or a back's path to his spot -- are drawn this opaque
     *  (0-1) in his side's color. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float GuideOpacity = 0.5f;

    /** Every route, guide and rush ends in an arrowhead this long ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float ArrowheadLength = 110.f;

    /** ... its barbs this many degrees off the line (above 0, below 90). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float ArrowheadAngleDegrees = 28.f;

    /** A blocker's mark is a T: a run blocker's stem this far upfield, a pass blocker's this far
     *  back toward his quarterback ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float RunBlockStemLength = 90.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float PassBlockStemLength = 45.f;

    /** ... with a bar this wide across its end. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float BlockBarWidth = 130.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString BlockColor = TEXT("#C9D1D9");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString LineOfScrimmageColor = TEXT("#56CCF2");

    /** A ring is drawn with this many segments (6 or more). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    int32 CircleSegments = 16;

    /** The diagram's backdrop, at BackgroundOpacity (0-1; 0 draws none). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString BackgroundColor = TEXT("#0E2A18");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float BackgroundOpacity = 0.85f;

    /** The play-call screen's preview beside each play, in Slate units (they scale with the
     *  display, so the preview is the same share of a phone's screen as a monitor's). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float PreviewWidth = 150.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float PreviewHeight = 100.f;
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

    /** An assignment the play emphasizes (its Art.bEmphasis, Epic 35) is drawn this many times as
     *  large: a ribbon's width, a ring's, star's or line's size. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float EmphasisScale = 1.5f;

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

    /** The same art drawn flat, as the play-call screen's previews (Epic 102.1). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FPSPlayDiagramStyle Diagram;
};
