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

    /** Seconds after the snap this waypoint should be reached. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float TimingSeconds = 0.f;
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

    /** Defensive front, e.g. "4-3", "3-4", "Nickel"; empty for offensive plays. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Front;

    /** Defensive coverage shell, e.g. "Cover2", "Cover3", "ManFree"; empty for offensive plays. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString CoverageShell;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FPSPlayAssignment> Assignments;
};
