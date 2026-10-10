// PSPlayDemoRunner.h - live-play demos: real plays run end to end in a ticking world, recorded
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSPlayDemoTypes.h"
#include "PSReplayFormat.h"
#include "PSPlayDemoRunner.generated.h"

/** One demo play as it ran: its recording and its summary. */
USTRUCT(BlueprintType)
struct FPSPlayDemoRun
{
    GENERATED_BODY()

    /** The play reached its whistle and its result was recorded. */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    bool bRan = false;

    /** UPSReplayFormat's recording: header, initial state, every bus event from kickoff to the
     *  play's result, the frames from kickoff to PostWhistleSeconds after the whistle, and the
     *  teams and players a viewer labels. */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FPSReplayRecording Recording;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FPSPlayDemoSummary Summary;
};

/**
 * UPSPlayDemoRunner plays real plays end to end and records them (the Track S visual checkpoint).
 *
 * Each play runs in a game world of its own, made the way a map load makes one: APSGameMode is
 * the world's game mode (set through UWorld::SetGameMode with the demo's teams as the travel
 * options), the world's actors are initialized for play and BeginPlay is dispatched, so the
 * match starts exactly as it does in the game: the rosters, the staffs, the personnel packages,
 * the field and its surface, 22 players under their side's AI and the ball in the center's
 * hands. The world is then ticked at a fixed step (FrameRateHz) with UWorld::Tick, one engine
 * frame per step: every actor, component and tickable subsystem ticks as in a running game, the
 * players move with their movement components, the ball flies with its projectile movement, and
 * contact, catches and tackles come from the physics scene's sweeps and overlaps.
 *
 * The only thing the demo does by hand is name both calls, through UPSPlayCallSubsystem::CallPlay
 * (the call screens' path) while the kickoff call window is open. From there the game mode snaps
 * when the play-call authority says so, the orchestrator hands out the assignments, the AI plays
 * them, and the play simulation, the outcome authority, blows the whistle and announces the
 * result. Nothing is scripted, teleported or interpolated.
 *
 * The recording is the telemetry sampler's frames (Epic 26), one per step, and every event on
 * the bus (UPSReplayRecorder, Epic 115), in UPSReplayFormat, with the teams and every player the
 * frames name. Each play is held to sanity checks (CheckRun). WriteDemos writes one JSON per play
 * and index.json. PlaySports.Demo.LivePlays runs the demo set in CI.
 */
UCLASS()
class PLAYSPORTS_API UPSPlayDemoRunner : public UObject
{
    GENERATED_BODY()

public:
    /** Data/play_demos.json. */
    static FString GetDefaultCatalogPath();

    /** Saved/PlayDemos. */
    static FString GetDefaultOutputDirectory();

    /** Replaces the catalog with JsonFilePath's, read through UPSDataIngestion. A file that can't
     *  be read, or fails ValidateCatalog, is refused and the current catalog kept. */
    bool LoadCatalogFromJson(const FString& JsonFilePath);

    const FPSPlayDemoCatalog& GetCatalog() const { return Catalog; }

    void SetCatalog(const FPSPlayDemoCatalog& InCatalog) { Catalog = InCatalog; }

    /** Problems with InCatalog, one line each (empty when sound): a rate or limit not above 0, a
     *  post-whistle span not shorter than the wait for the result, no demos, a demo ID that is
     *  empty or used twice, a demo without two different teams or without both calls, fewer than
     *  one seed to try, an unknown wanted outcome. Whether a call is in the playbook and its
     *  team's scheme is checked by tools/validate_data.py and, when the play runs, by the
     *  play-call subsystem. */
    static TArray<FString> ValidateCatalog(const FPSPlayDemoCatalog& InCatalog);

    /** The outcomes a demo can look for. */
    static const TArray<FString>& GetOutcomes();

    /** Demo on its seeds in turn until one ends in its WantedOutcome (or the first, without
     *  one): the first that does, else the last. */
    FPSPlayDemoRun RunDemo(const FPSPlayDemoDef& Demo);

    /** Demo once, on Seed, in a world of its own made and destroyed here. Summary.Problems
     *  says what went wrong when it didn't run (no snap, no whistle, no result, a call refused),
     *  followed by what CheckRun found. */
    FPSPlayDemoRun RunPlay(const FPSPlayDemoDef& Demo, int32 Seed);

    /** Every demo in the catalog, in order. */
    TArray<FPSPlayDemoRun> RunAll();

    /**
     * The play's sanity checks, one line per failure: frames at the catalog's rate with no gaps,
     * all 22 players in every frame from the snap and each of them moving at least
     * MinPlayerMoveCm from his spot at the snap, the ball moving, the play's result on the bus
     * after the snap, no player faster than the summary's player limit and no ball faster than
     * its own, no non-finite number anywhere, and no capsule or ball below the ground by more
     * than GroundToleranceCm.
     */
    static TArray<FString> CheckRun(const FPSPlayDemoRun& Run, const FPSPlayDemoCatalog& InCatalog);

    /** Writes each run to Directory (GetDefaultOutputDirectory when empty) as
     *  NN_<DemoId>.json through UPSReplayFormat::SerializeToJson, and index.json listing them,
     *  after clearing the directory of earlier JSON files. Sets each summary's File. False when
     *  a file can't be written. */
    bool WriteDemos(TArray<FPSPlayDemoRun>& Runs, const FString& Directory, FString& OutIndexPath);

private:
    UPROPERTY(Transient)
    FPSPlayDemoCatalog Catalog;
};
