// PSRouteRunning.h - Epic 68: routes as contested skills -- releases, breaks, double moves, option reads
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.h"
#include "PSPlaybookData.h"
#include "PSRouteRunning.generated.h"

/** How a receiver got off the line. */
UENUM(BlueprintType)
enum class EPSReleaseOutcome : uint8
{
    /** Nobody pressed him. */
    Unpressed,
    /** He beat the press cleanly. */
    Win,
    /** The jam held him up. */
    Delay,
    /** The jam pushed him off his stem, toward the sideline. */
    Reroute
};

/** What an option route's receiver reads at his read point. */
UENUM(BlueprintType)
enum class EPSCoverageRead : uint8
{
    Zone,
    Man
};

/** Route-running tuning (Data/route_running.json; Architecture rule 4). Distances in cm,
 *  chances 0-1. */
USTRUCT(BlueprintType)
struct FRouteRunningTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    /** A defender this close in front of a receiver at the snap presses him. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float PressRadius = 250.f;

    /** The receiver's chance to win the release when his release rating equals the presser's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float ReleaseBaseWinChance = 0.55f;

    /** Added per point the receiver's release rating ((Agility + Strength) / 2) beats the
     *  presser's ((Strength + Agility) / 2) by. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float ReleaseRatingWeight = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float ReleaseMinWinChance = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float ReleaseMaxWinChance = 0.95f;

    /** Of the releases a receiver loses, the share that are a delay; the rest are reroutes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float DelayShare = 0.6f;

    /** A jammed receiver is held this long. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float DelaySeconds = 0.5f;

    /** A rerouted receiver's whole route moves this far toward his sideline ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float RerouteOffset = 150.f;

    /** ... and he is held this long. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float RerouteDelaySeconds = 0.2f;

    /** A waypoint where the route turns at least this much is a break. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float BreakMinAngleDegrees = 30.f;

    /** A receiver with 0 Agility rounds his break: he turns for the next leg this far before the
     *  corner. At 100 he cuts on the spot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float MaxBreakRounding = 150.f;

    /** The separation a break makes when receiver and defender are equally agile ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float BreakSeparationBase = 50.f;

    /** ... plus this per point of Agility the receiver has on him (none below zero). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float BreakSeparationPerAgility = 2.f;

    /** A double move's receiver sells the fake this long. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float FakeSellSeconds = 0.2f;

    /** The defender nearest the receiver, within this distance, is the one the fake works on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float BiteRadius = 600.f;

    /** The bite chance at Agility 0 against Awareness 0 ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float BiteBaseChance = 0.5f;

    /** ... plus this times the receiver's Agility / 100 ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float BiteAgilityWeight = 0.4f;

    /** ... minus this times the defender's Awareness / 100. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float BiteAwarenessWeight = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float BiteMinChance = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float BiteMaxChance = 0.9f;

    /** A defender who bites freezes this long. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float BiteFreezeSeconds = 0.7f;

    /** An option route's receiver reads man when a defender is this close at the read point. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routes")
    float ManReadRadius = 300.f;
};

/**
 * The route-running model (Epic 68) as pure functions over ratings and route geometry, shared
 * by UPSRouteRunnerComponent (which runs it) and the quarterback's timing reads
 * (UPSSkillPlayerAIComponent). Rolls are passed in, so callers own their seeded randomness.
 */
namespace PSRouteRunning
{
    /** A player's release rating: (Agility + Strength) / 2, for receiver and presser alike. */
    PLAYSPORTS_API float ReleaseRating(const FPlayerAttributes& Player);

    /** The receiver's chance to win his release against Presser. */
    PLAYSPORTS_API float ReleaseWinChance(const FPlayerAttributes& Receiver, const FPlayerAttributes& Presser, const FRouteRunningTuningRow& Tuning);

    /** The release for a Roll in [0, 1): below the win chance he wins; of the rest, the first
     *  DelayShare is a delay and the remainder a reroute. */
    PLAYSPORTS_API EPSReleaseOutcome ResolveRelease(const FPlayerAttributes& Receiver, const FPlayerAttributes& Presser, float Roll, const FRouteRunningTuningRow& Tuning);

    /** How far before a break's corner a receiver with this Agility turns for the next leg. */
    PLAYSPORTS_API float BreakRounding(float Agility, const FRouteRunningTuningRow& Tuning);

    /** How much the path From -> Corner -> To turns at Corner, in degrees (0 straight on, 180
     *  straight back), on the ground. */
    PLAYSPORTS_API float TurnAngleDegrees(const FVector& From, const FVector& Corner, const FVector& To);

    /** The separation (cm) a receiver gains on the defender with him at a break. */
    PLAYSPORTS_API float BreakSeparationGain(float ReceiverAgility, float DefenderAgility, const FRouteRunningTuningRow& Tuning);

    /** The chance a defender bites on a double move's fake. */
    PLAYSPORTS_API float BiteChance(float ReceiverAgility, float DefenderAwareness, const FRouteRunningTuningRow& Tuning);

    /** The index of Route's break: its first waypoint (after any fake, not the last) where it
     *  turns by BreakMinAngleDegrees or more, starting from the player's spot. INDEX_NONE for a
     *  route that runs straight on (a go, a double move's go after the fake). */
    PLAYSPORTS_API int32 FindBreakWaypoint(const FPSRoute& Route, const FRouteRunningTuningRow& Tuning);

    /** When the quarterback's read of Route comes up, in seconds after the snap: its break; an
     *  option route's read plus its quicker branch's first leg; a straight route's end. */
    PLAYSPORTS_API float ReadTime(const FPSRoute& Route, const UDataTable* RouteLibrary, const FRouteRunningTuningRow& Tuning);
}
