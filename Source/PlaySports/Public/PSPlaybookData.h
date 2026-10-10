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
};
