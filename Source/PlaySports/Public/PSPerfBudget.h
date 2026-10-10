// PSPerfBudget.h - Epic 114: the profiler that holds each system to its frame-time budget
#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "PSPerfTypes.h"
#include "PSPlatformTiers.h"

/**
 * `stat PlaySports`: the game's own systems on the game thread, one cycle counter per budgeted
 * system, plus the work counted per frame (bus events, AI decisions, field scans). The AI's
 * finer counters stay in `stat PSAI`, the sampler's in `stat PSTelemetrySampling`.
 */
DECLARE_STATS_GROUP(TEXT("PlaySports"), STATGROUP_PlaySports, STATCAT_Advanced);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Simulation"), STAT_PSPerfSimulation, STATGROUP_PlaySports, PLAYSPORTS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("AI"), STAT_PSPerfAI, STATGROUP_PlaySports, PLAYSPORTS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Telemetry"), STAT_PSPerfTelemetry, STATGROUP_PlaySports, PLAYSPORTS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Overlays"), STAT_PSPerfOverlays, STATGROUP_PlaySports, PLAYSPORTS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("UI"), STAT_PSPerfUI, STATGROUP_PlaySports, PLAYSPORTS_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Bus events"), STAT_PSPerfBusEvents, STATGROUP_PlaySports, PLAYSPORTS_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("AI decisions"), STAT_PSPerfAIDecisions, STATGROUP_PlaySports, PLAYSPORTS_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Field scans"), STAT_PSPerfFieldScans, STATGROUP_PlaySports, PLAYSPORTS_API);

/**
 * The profiler (Epic 114). Each budgeted system's code opens a PS_PERF_SCOPE; while a capture
 * runs, the time inside it is added to that system's time for the frame. Time is exclusive: a
 * scope opened inside another system's (a bus event published from an AI decision) pauses the
 * outer one, so no millisecond is counted twice. EndFrame closes a frame into each system's
 * histogram (FPSFrameTimeHistogram, Epic 117's); BuildReport turns the capture into a report
 * held to a platform tier's budgets.
 *
 * Two ways to capture:
 *  - UPSPerfHarness runs the standard play headless (CI, `PS.Perf.RunHarness` on a device);
 *  - `PS.Perf.Capture <seconds>` records real play on whatever is running, a phone included.
 * Both write Saved/Profiling/<scenario>_<tier>.json, which tools/perf_budget.py checks.
 *
 * Game thread only. Outside a capture a scope costs one bool test.
 */
namespace PSPerf
{
    /** Starts a capture: empties every system's histogram (InBucketMs wide, InBucketCount
     *  buckets) and every counter. */
    PLAYSPORTS_API void BeginCapture(float InBucketMs, int32 InBucketCount);

    /** Stops adding to the capture; what it holds stays until the next BeginCapture. */
    PLAYSPORTS_API void EndCapture();

    PLAYSPORTS_API bool IsCapturing();

    /** Closes the current frame: every system's time this frame (0 if it didn't run) goes into
     *  its histogram. */
    PLAYSPORTS_API void EndFrame();

    /** Frames closed since BeginCapture. */
    PLAYSPORTS_API int32 GetCapturedFrames();

    /** Counts Amount of Counter's work: always under `stat PlaySports`, and in the capture. */
    PLAYSPORTS_API void AddCount(EPSPerfCounter Counter, int32 Amount = 1);

    /** Counter's total since BeginCapture. */
    PLAYSPORTS_API int64 GetCount(EPSPerfCounter Counter);

    /** System's time so far in the open frame, ms (tests). */
    PLAYSPORTS_API double GetOpenFrameMs(EPSPerfSystem System);

    /** The capture as a report held to Tier's budgets. */
    PLAYSPORTS_API FPSPerfReport BuildReport(const FPSPlatformTier& Tier, const FString& Scenario);

    /** Writes Report as JSON to Path (directories made as needed). */
    PLAYSPORTS_API bool WriteReport(const FPSPerfReport& Report, const FString& Path);

    PLAYSPORTS_API bool ReadReport(const FString& Path, FPSPerfReport& OutReport);

    /** Saved/Profiling. */
    PLAYSPORTS_API FString GetReportDir();

    /** Saved/Profiling/<Scenario>_<Tier>.json. */
    PLAYSPORTS_API FString GetReportPath(const FString& Scenario, FName TierId);

    /** Opens and closes a timed scope (FPSPerfScope does this). */
    PLAYSPORTS_API void EnterScope(EPSPerfSystem System);
    PLAYSPORTS_API void ExitScope();
}

/** Times its lifetime as System's while a capture runs (see PSPerf). */
struct FPSPerfScope
{
    explicit FPSPerfScope(EPSPerfSystem System)
        : bTiming(PSPerf::IsCapturing())
    {
        if (bTiming)
        {
            PSPerf::EnterScope(System);
        }
    }

    ~FPSPerfScope()
    {
        if (bTiming)
        {
            PSPerf::ExitScope();
        }
    }

    FPSPerfScope(const FPSPerfScope&) = delete;
    FPSPerfScope& operator=(const FPSPerfScope&) = delete;

private:
    bool bTiming;
};

/** Times the rest of the enclosing scope as System's: `stat PlaySports` and the profiler.
 *  System is one of Simulation, AI, Telemetry, Overlays, UI. */
#define PS_PERF_SCOPE(System) \
    SCOPE_CYCLE_COUNTER(STAT_PSPerf##System); \
    FPSPerfScope PSPerfScope_##System(EPSPerfSystem::System)

/** PS_PERF_SCOPE without the stat counter, for code that can run inside its own system's
 *  PS_PERF_SCOPE (the field scan inside a decision): a cycle counter must not nest in itself. */
#define PS_PERF_SCOPE_NESTED(System) \
    FPSPerfScope PSPerfScope_##System(EPSPerfSystem::System)
