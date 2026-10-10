// PSPerfTypes.h - Epic 114: per-system frame-time budgets, the profiling harness's settings and report
#pragma once

#include "CoreMinimal.h"
#include "PSPerfTypes.generated.h"

/** The game's own systems, as frame-time budgets name them (Specs/Platform_Audit.md section 7). */
UENUM(BlueprintType)
enum class EPSPerfSystem : uint8
{
    /** The play simulation and the game mode around it. */
    Simulation,
    /** Every AI decision, the field scan, pass-rush moves and gap fits. */
    AI,
    /** The telemetry bus recording events, and the snapshot sampler. */
    Telemetry,
    /** Broadcast overlays, ball flight, the reticle and the position badges. */
    Overlays,
    /** The HUD and menus. */
    UI,
    /** Skeletal animation (Track A); nothing to measure until it is built. */
    Animation,
    /** The stadium crowd (Track R); nothing to measure until it is built. */
    Crowd,
    /** Audio (Track E); nothing to measure until it is built. */
    Audio
};

/** Work the profiler counts exactly, so CI can check it whatever the machine's speed. */
UENUM(BlueprintType)
enum class EPSPerfCounter : uint8
{
    /** Events the telemetry bus recorded. */
    BusEvents,
    /** AI decisions (what a behaviour tree evaluation would be): skill players and defenders. */
    AIDecisions,
    /** Field scans by UPSAIFieldSnapshot. */
    FieldScans
};

/** One system's frame-time budget on a tier, in ms of game-thread time per frame. */
USTRUCT(BlueprintType)
struct FPSSystemBudget
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    EPSPerfSystem System = EPSPerfSystem::Simulation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float BudgetMs = 0.f;
};

/** The profiling harness and the CI check (Data/perf_harness.json; Architecture rule 4). */
USTRUCT(BlueprintType)
struct FPSPerfHarnessTuning
{
    GENERATED_BODY()

    /** Seconds per simulated frame (1/60 is a 60 fps frame). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float FrameSeconds = 1.f / 60.f;

    /** Frames run before the capture starts, so first-use loading isn't measured. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    int32 WarmupFrames = 30;

    /** Frames from the snap to the catch: the rush, the routes and the coverage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    int32 PassFrames = 150;

    /** Frames from the catch to the tackle: pursuit of the ball carrier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    int32 PursuitFrames = 120;

    /** Frames after the whistle: the next down's pre-snap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    int32 PreSnapFrames = 60;

    /** Width of a per-system time histogram bucket, ms: the report's resolution. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float HistogramBucketMs = 0.005f;

    /** Buckets per system; slower frames land in one overflow bucket that keeps the slowest. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    int32 HistogramBucketCount = 4000;

    /** The most telemetry-bus events one standard play may publish. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    int32 MaxBusEventsPerPlay = 60;

    /** CI fails when a system's 95th-percentile frame time is over its budget times this;
     *  between the budget and this, it warns. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float HardFailMultiplier = 3.f;

    /** CI warns when a system's 95th percentile is this fraction over the median of its recent
     *  runs on main ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float RegressionTolerance = 0.25f;

    /** ... and at least this many ms over it, so microsecond noise is not a regression. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float MinRegressionMs = 0.05f;

    /** How many recent main runs the trend is the median of. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    int32 TrendWindow = 10;
};

/** One system's time over a capture. */
USTRUCT(BlueprintType)
struct FPSPerfSystemResult
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    EPSPerfSystem System = EPSPerfSystem::Simulation;

    /** False when nothing of the system ran during the capture (or it isn't built yet). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    bool bMeasured = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float BudgetMs = 0.f;

    /** Per-frame time, ms: mean, median, 95th percentile and the slowest frame. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float MeanMs = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float P50Ms = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float P95Ms = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float MaxMs = 0.f;

    /** The 95th percentile is over the budget. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    bool bOverBudget = false;
};

/** What a capture measured (Saved/Profiling/*.json; tools/perf_budget.py reads it). */
USTRUCT(BlueprintType)
struct FPSPerfReport
{
    GENERATED_BODY()

    /** "StandardPlay" for the harness, "Live" for a capture of real play. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    FString Scenario;

    /** The platform tier whose budgets it is held to. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    FName TierId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    FString Platform;

    /** ISO 8601, UTC. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    FString Date;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float TargetFrameRate = 60.f;

    /** 1000 / TargetFrameRate. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float FrameBudgetMs = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    int32 Frames = 0;

    /** One per EPSPerfSystem, in its order. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    TArray<FPSPerfSystemResult> Systems;

    /** The sum of the measured systems' 95th percentiles: a pessimistic game-thread total. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float SystemsP95Ms = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    int32 SystemsOverBudget = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    int32 BusEvents = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    int32 AIDecisions = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    int32 FieldScans = 0;

    /** Recorded bus events per ms of telemetry time: the bus's throughput. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    float BusEventsPerMs = 0.f;

    const FPSPerfSystemResult* FindSystem(EPSPerfSystem InSystem) const
    {
        return Systems.FindByPredicate([InSystem](const FPSPerfSystemResult& Result) { return Result.System == InSystem; });
    }
};
