// PSPerfHarness.h - Epic 114: the standard play under full load, profiled per system
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSPerfTypes.h"
#include "PSPlatformTiers.h"
#include "PSPerfHarness.generated.h"

class APSPlayerPawn;
class UPSPlaySimulation;
class UWorld;

/**
 * UPSPerfHarness runs the profiling scenario (Epic 114): a standard play under full load,
 * timed per system by PSPerf against a platform tier's budgets.
 *
 * The play: two full elevens lined up as the game lines them up, each under its side's AI, and
 * the ball. After WarmupFrames of pre-snap, the capture covers the snap (the CPU calls both
 * sides through UPSPlayCallSubsystem), the rush, routes and coverage, a throw and a catch,
 * pursuit of the receiver, the tackle and the whistle, and the next down's pre-snap. Every frame
 * steps what the game ticks, in the game's order: the field snapshot's new frame, the play
 * simulation, every AI player at the tier's decision interval with its pass rush and the gap
 * fits, the telemetry sampler, and the ball-flight and broadcast overlays. Bus events are
 * published as the game publishes them.
 *
 * Nothing here needs a renderer, a game mode or ticking, so the same play runs headless in CI
 * (PlaySports.Perf.StandardPlayProfile writes Saved/Profiling/StandardPlay_<tier>.json, which
 * tools/perf_budget.py checks) and on a device (`PS.Perf.RunHarness`). `PS.Perf.Capture
 * <seconds>` profiles real play instead.
 */
UCLASS()
class PLAYSPORTS_API UPSPerfHarness : public UObject
{
    GENERATED_BODY()

public:
    /** The report's scenario name: "StandardPlay". */
    static const TCHAR* StandardPlayScenario;

    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion; tuning with
     *  problems is refused. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** The tuning in use, loaded from the default path on first use. */
    const FPSPerfHarnessTuning& GetTuning();

    /** Problems with Candidate, one line each (empty when sound). */
    static TArray<FString> ValidateTuning(const FPSPerfHarnessTuning& Candidate);

    /** Frames the capture covers: pass, pursuit and pre-snap. */
    static int32 GetCapturedFrameCount(const FPSPerfHarnessTuning& InTuning);

    /** Runs the standard play in World, a game world with no players in it yet, and returns
     *  the capture held to Tier's budgets. The players stay in World. */
    FPSPerfReport RunStandardPlay(UWorld* World, const FPSPlatformTier& Tier);

    /** The same in a game world of its own, created and destroyed here (`PS.Perf.RunHarness`). */
    FPSPerfReport RunStandardPlayInScratchWorld(const FPSPlatformTier& Tier);

    /** One log line per system: time against budget. */
    static void LogReport(const FPSPerfReport& Report);

private:
    /** One frame of everything the game ticks, then the profiler's frame end. */
    void StepFrame(UWorld* World, const TArray<APSPlayerPawn*>& Players, UPSPlaySimulation* Simulation, float DeltaSeconds, float DecisionInterval);

    FPSPerfHarnessTuning Tuning;
    bool bTuningLoaded = false;
};
