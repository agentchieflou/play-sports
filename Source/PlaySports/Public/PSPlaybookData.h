#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.h"
#include "PSDefenseController.h"
#include "PSPlaybookData.generated.h"

UENUM(BlueprintType)
enum class EPSAssignmentKind : uint8
{
    Route,
    PassBlock,
    RunBlock,
    ManCoverage,
    ZoneCoverage,
    PassRush,
    RunFit,
    Blitz
};

/** A single point along a route, relative to the player's pre-snap starting spot. */
USTRUCT(BlueprintType)
struct FPSRouteWaypoint
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector Offset = FVector::ZeroVector;

    /** Seconds after the snap this waypoint should be reached (on an option branch: after the
     *  read). The quarterback's read of the route comes up at its break (Epic 68). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float TimingSeconds = 0.f;

    /** A double move's fake break (Epic 68): the receiver sells it here, the defender on him
     *  may bite, and the route goes on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bFake = false;
};

/** Reusable route shape, referenced by name from play assignments (Route Library). */
USTRUCT(BlueprintType)
struct FPSRoute : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName RouteId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FPSRouteWaypoint> Waypoints;

    /** An option (sight-adjust) route (Epic 68): at this waypoint the receiver reads the
     *  coverage and runs the rest from VsManBranch or VsZoneBranch instead. -1: no read. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 OptionReadWaypoint = -1;

    /** The branch run against man coverage: a route from the library whose offsets start at the
     *  read point, authored breaking outside; a defender with outside leverage turns it inside. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName VsManBranch;

    /** The branch run against zone: settle in the hole. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName VsZoneBranch;
};

/** How a play draws one of its assignments (Epic 35): an annotation layered on the play data.
 *  The play art (PSPlayArt) and the position badges read it; the AI doesn't. */
USTRUCT(BlueprintType)
struct FPSPlayArtAnnotation
{
    GENERATED_BODY()

    /** The assignment's art in this color ("#RRGGBB") instead of its read's or its icon's;
     *  empty keeps the style's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Color;

    /** The assignment's art drawn larger (the style's EmphasisScale): the play's key route, its
     *  blitzer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bEmphasis = false;

    /** The letter the player wears on his position badge this play (Epic 28) -- "Y", "F", "M" --
     *  one or two capitals or digits, where he wears no pass button; empty keeps his role's
     *  label. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString BadgeLetter;

    bool IsEmpty() const { return Color.IsEmpty() && !bEmphasis && BadgeLetter.IsEmpty(); }
};

/** One position slot's assignment within a play. */
USTRUCT(BlueprintType)
struct FPSPlayAssignment
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPlayerRole Role = EPlayerRole::Quarterback;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPSAssignmentKind Kind = EPSAssignmentKind::Route;

    /** RouteId into the route library; only meaningful when Kind == Route. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName RouteId;

    /** Zone landmark offset from the formation's center; meaningful for ZoneCoverage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector ZoneOffset = FVector::ZeroVector;

    /** Pre-snap formation offset from the ball's spot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector FormationOffset = FVector::ZeroVector;

    /** Where the route sits in the quarterback's progression, as the play art colors it
     *  (Epic 27): 1 for the primary read, 2, 3, ... for the reads after it, down to the
     *  check-down; 0 for a route the play doesn't rank. Only a Route with a RouteId has one.
     *  The AI doesn't read it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 ReadOrder = 0;

    /** How the play art draws this assignment (Epic 35): its color, emphasis and badge letter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FPSPlayArtAnnotation Art;
};

/** What a play's quarterback fakes or reads after the snap (Epic 72). */
UENUM(BlueprintType)
enum class EPSDeception : uint8
{
    None,
    /** A fake hand-off, then the drop and the pass. */
    PlayAction,
    /** Run-pass option: hand it off, or throw to the pass option, on the conflict defender. */
    RPO,
    /** Hand it off, or keep it, on the end man on the line. */
    ZoneRead,
    /** Dive, keep or pitch: the dive key at the mesh, then the pitch key. */
    TripleOption
};

/** A deception play's mechanics (Epic 72). The run options (RPO, ZoneRead, TripleOption) are
 *  Run plays: the quarterback meets the back at the mesh and reads there. */
USTRUCT(BlueprintType)
struct FPSDeceptionDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPSDeception Type = EPSDeception::None;

    /** The side the option goes to: 1 right of the ball, -1 left. Its keys are the defenders on
     *  that side. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 PlaySide = 1;

    /** RPO: the pass option is the first player of this role on a route. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPlayerRole PassRole = EPlayerRole::WideReceiver;

    /** Triple option: the pitch man is the first player of this role. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPlayerRole PitchRole = EPlayerRole::TightEnd;
};

/** A full play call: formation + one assignment per position, offense or defense. */
USTRUCT(BlueprintType)
struct FPSPlayDefinition : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName PlayId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Formation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsOffensivePlay = true;

    /** Situational category used by the coaching AI's play-selection weighting
     *  (Epic 18): "Run", "ShortPass", "DeepPass", "PlayAction", "Screen" for
     *  offense; "Base", "Blitz", "Prevent" for defense. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString PlayCategory;

    /** Defensive front, e.g. "4-3", "3-4", "Nickel"; empty for offensive plays. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Front;

    /** Defensive coverage shell, e.g. "Cover2", "Cover3", "ManFree"; empty for offensive plays. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString CoverageShell;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FPSPlayAssignment> Assignments;

    /** An offensive play's fake or read (Epic 72); Type None for an ordinary play. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FPSDeceptionDef Deception;
};
