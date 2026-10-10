#pragma once

#include "CoreMinimal.h"
#include "PSPlaySimulation.h"
#include "PSReplayFormat.h"
#include "PSQuickSimRunner.generated.h"

class UPSReplayRecorder;

USTRUCT(BlueprintType)
struct FPSQuickSimResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int32 HomeScore = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 AwayScore = 0;
};

/** Resolves a non-played schedule game headlessly by driving UPSPlaySimulation's
 *  existing quick-sim statistical rolls to completion, with no rendering (Epic 20).
 *  Reuses UPSPlaySimulation as the single authority on play outcomes rather than
 *  duplicating scoring logic.
 *
 *  It also records a game and plays a recording back (Epic 115): RecordGame plays the game
 *  SimulateGame would, seeded, and records the game state the simulation publishes on the
 *  telemetry bus; ReplayGame re-simulates a recording from what it holds alone (rosters,
 *  initial state, seed and step: Mode 2 in Specs/Determinism_Audit.md) and records the rerun,
 *  for UPSDeterminism::FindFirstDivergence to compare. Both run in a world of their own (a
 *  test's or a tool's): with a world, the simulation publishes on its bus. A world with a game
 *  mode is a live game and is refused. The simulation rolls on the process-global random
 *  stream (audit A1), so both seed it with the game's seed and hand it back to chance after. */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSQuickSimRunner : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Franchise|QuickSim")
    FPSQuickSimResult SimulateGame(const TArray<FPlayerAttributes>& HomeRoster, const TArray<FPlayerAttributes>& AwayRoster);

    /** Plays HomeRoster against AwayRoster as SimulateGame does, with the random stream seeded
     *  with Seed, and records it into OutRecording: every bus event the game publishes, each
     *  stamped with the step it happened in (0 for the setup), under a header carrying Seed
     *  and SecondsPerPlayAdvance. Seed 0 records a game that can't be replayed (0 is the
     *  format's "unseeded"). Returns the final score; OutRecording has no events when
     *  WorldContextObject's world can't be used. */
    UFUNCTION(BlueprintCallable, Category = "Franchise|QuickSim|Replay", meta = (WorldContext = "WorldContextObject"))
    FPSQuickSimResult RecordGame(UObject* WorldContextObject, const TArray<FPlayerAttributes>& HomeRoster, const TArray<FPlayerAttributes>& AwayRoster,
        int32 Seed, FPSReplayRecording& OutRecording);

    /** Re-simulates Recording from its rosters, initial state, seed and step, recording the
     *  rerun into OutReplay the way RecordGame recorded the original, and gives its final
     *  score. Returns false, with OutFailure saying why, for a recording that can't be
     *  re-simulated: no seed (an event-playback-only recording), no fixed step, an initial
     *  state that isn't a quick-sim game's opening, or no usable world. */
    UFUNCTION(BlueprintCallable, Category = "Franchise|QuickSim|Replay", meta = (WorldContext = "WorldContextObject"))
    bool ReplayGame(UObject* WorldContextObject, const FPSReplayRecording& Recording, FPSReplayRecording& OutReplay,
        FPSQuickSimResult& OutResult, FString& OutFailure);

    /** Safety cap on simulation ticks (AdvancePlay calls, not football plays -- each
     *  play takes several ticks to progress through phases) so a stuck game clock
     *  can't hang the season loop. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Franchise|QuickSim")
    int32 MaxPlaysPerGame = 2000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Franchise|QuickSim")
    float SecondsPerPlayAdvance = 6.f;

    /** Every play of every game this runner simulates, as the simulation resolves it
     *  (UPSPlaySimulation::OnPlayResolved; Epic 92's statistics record from it). */
    FPSTelemetryPlayResultMC OnPlayResolved;

private:
    /** A quick-sim simulation set up for a game between the two rosters, not yet on a bus. */
    UPSPlaySimulation* MakeGameSimulation(const TArray<FPlayerAttributes>& HomeRoster, const TArray<FPlayerAttributes>& AwayRoster);

    /** Steps Sim until the fourth quarter is over (or MaxPlaysPerGame steps), StepSeconds at a
     *  time, telling Recorder (when given) which step each event happens in. */
    FPSQuickSimResult RunGame(UPSPlaySimulation& Sim, float StepSeconds, UPSReplayRecorder* Recorder) const;

    /** Records Sim's game on World's bus from its setup to its end under Start's header and
     *  initial state; the random stream must already be seeded. */
    FPSQuickSimResult RecordRun(UWorld& World, UPSPlaySimulation& Sim, const FPSReplayRecording& Start, FPSReplayRecording& OutRecording);

    /** The world a record or replay runs in, or null (with OutFailure) when there is none or
     *  it is a live game's. */
    static UWorld* GetRecordingWorld(UObject* WorldContextObject, FString& OutFailure);
};
