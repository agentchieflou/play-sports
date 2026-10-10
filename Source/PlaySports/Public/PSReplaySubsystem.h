// PSReplaySubsystem.h - Epic 41: replays cut from the bus and the snapshots, played back on the field
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSPlatformTiers.h"
#include "PSReplayFormat.h"
#include "PSReplayTypes.h"
#include "PSTelemetryBus.h"
#include "PSReplaySubsystem.generated.h"

class AActor;
class APSBall;
class APSPlayerPawn;
class UPSTelemetrySamplingSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPSReplaySignature);

/**
 * UPSReplaySubsystem is the replay system (Epic 41): any recent play, shown again on the field.
 *
 *  - Recording is what already exists, joined. A clip is cut, when it is wanted, from the
 *    telemetry bus's event history (C1) and the telemetry sampler's snapshots (Epic 26): the
 *    sampler's frames over the span and the bus's events in it, as an FPSReplayRecording
 *    (Epic 115's format, with its Frames). There is no third buffer; a clip is a copy taken
 *    once, so the history can roll on while it plays.
 *  - Playback is state playback, not re-simulation (the physical game isn't deterministic:
 *    Specs/Determinism_Audit.md). At the playhead, the frame blended from the clip's frames
 *    either side poses every pawn (by PlayerId) and the ball, with their collision off so a
 *    posed pawn never tackles or catches anything. The sampler shows the same frame as the
 *    present, so whatever reads its latest frame (the camera director, the skycam, overlays)
 *    follows the replay, and it records none of it. The game is paused while a replay plays
 *    (re-paused if something else unpauses it) and the broadcast camera keeps ticking.
 *    StopReplay puts every pawn and the ball back as they were, collision and all.
 *  - Transport: pause, slow motion through the tuning's PlaybackRates, frame steps between the
 *    clip's captured frames, scrubbing, and setting the playhead.
 *  - Per-frame cost: the platform tier's ReplayPoseRateHz limits how often the field is
 *    re-posed (0 is every frame); a scrub or a frame step shows at once.
 *  - Persistence: SaveClip writes a clip as JSON (Saved/Replays by default), its scheduled
 *    frames thinned to SaveFrameRateHz; LoadClip and ListSavedClips read them back.
 *
 * It ticks with its world, paused or not, and calls AdvanceReplay; headless tests, whose worlds
 * don't tick, call AdvanceReplay themselves.
 */
UCLASS()
class PLAYSPORTS_API UPSReplaySubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickableWhenPaused() const override { return true; }

    // --- Tuning --------------------------------------------------------------------------

    static FString GetDefaultTuningPath();

    /** The tuning, loaded from the default path on first use. */
    const FPSReplayTuning& GetTuning();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion. A file that
     *  can't be read, or fails ValidateTuning, is refused and the current tuning kept. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Applies NewTuning when it passes ValidateTuning. */
    bool SetTuning(const FPSReplayTuning& NewTuning);

    /** Problems with InTuning, one line each (empty when sound): negative rolls, no playback
     *  rates, a rate not in (0, 1] or listed twice, a first rate other than 1, a save rate not
     *  above 0. */
    static TArray<FString> ValidateTuning(const FPSReplayTuning& InTuning);

    /** Takes the pose rate from Tier (Data/platform_tiers.json). The run's active tier is
     *  applied at startup; a test can apply any. */
    void ApplyPlatformTier(const FPSPlatformTier& Tier);

    /** Times per second the field is re-posed during playback; 0 is every frame. */
    UFUNCTION(BlueprintPure, Category = "Replay")
    float GetPoseRateHz() const { return PoseRateHz; }

    // --- Recording: clips cut from the bus and the snapshots ------------------------------

    /**
     * The clip from the event FromSequence to the event ToSequence, PreRollSeconds before the
     * first and PostRollSeconds after the second (as far as frames have been captured): the
     * sampler's frames over that span and the bus's events in it, in order. Each event is
     * named by type, timed on the frames' clock, and its TickIndex is the index of the last
     * frame at or before it. The clip opens on the last game state announced at or before the
     * first event, and carries no seed: it plays back, it doesn't re-simulate. False when
     * either event has left the bus's history or wasn't timed by the sampler (sampling off),
     * or no frame covers the span.
     */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    bool CaptureClip(int32 FromSequence, int32 ToSequence, FPSReplayRecording& OutClip);

    /** The latest play, snap to whistle (FindLastPlay), as CaptureClip cuts it. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    bool CaptureLastPlay(FPSReplayRecording& OutClip);

    /**
     * In History (oldest first), the latest snap and the event that ended its play: the first
     * after it to announce the whistle (a PhaseChange to Scoring, or a GameState in the Scoring
     * phase), else the newest event while the play is still live. False without a snap.
     */
    static bool FindLastPlay(const TArray<FPSTelemetryEvent>& History, int32& OutFromSequence, int32& OutToSequence);

    // --- Playback ------------------------------------------------------------------------

    /** Starts playing InClip from its first frame at the first playback rate, ending any replay
     *  already playing. False when the clip has no frames. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    bool StartReplay(const FPSReplayRecording& InClip);

    /** Ends the replay and gives the field back: every posed pawn and the ball where they
     *  were, with their velocity and collision, the sampler live again, the game unpaused if
     *  the replay paused it. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    void StopReplay();

    UFUNCTION(BlueprintPure, Category = "Replay")
    bool IsReplaying() const { return ReplayState != EPSReplayState::Idle; }

    UFUNCTION(BlueprintPure, Category = "Replay")
    EPSReplayState GetState() const { return ReplayState; }

    /** One step: moves the playhead DeltaSeconds times the playback rate while playing, stops
     *  at the clip's end, and re-poses the field when the tier's pose rate allows. The tick
     *  calls it; headless tests call it directly. */
    void AdvanceReplay(float DeltaSeconds);

    /** The clip playing (or last played). */
    const FPSReplayRecording& GetClip() const { return Clip; }

    /** Seconds from the clip's first frame to the playhead. */
    UFUNCTION(BlueprintPure, Category = "Replay")
    float GetPlayhead() const { return Playhead; }

    /** Seconds from the clip's first frame to its last. */
    UFUNCTION(BlueprintPure, Category = "Replay")
    float GetDuration() const;

    UFUNCTION(BlueprintPure, Category = "Replay")
    bool IsAtEnd() const;

    UFUNCTION(BlueprintPure, Category = "Replay")
    float GetPlaybackRate() const { return PlaybackRate; }

    /** The frame last shown on the field. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    bool GetShownFrame(FPSSnapshotFrame& OutFrame) const;

    /** How many times the field has been posed since the replay started. */
    UFUNCTION(BlueprintPure, Category = "Replay")
    int32 GetPoseCount() const { return PoseCount; }

    /** Everyone at Time on InClip's clock: the frame there, blended from the frames either side,
     *  or the first or last frame outside the clip. False when the clip has no frames. */
    static bool SampleClip(const FPSReplayRecording& InClip, float Time, FPSSnapshotFrame& OutFrame);

    // --- Transport -----------------------------------------------------------------------

    /** Holds (true) or plays (false). Playing from the clip's end starts it over. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    void SetPaused(bool bPaused);

    UFUNCTION(BlueprintCallable, Category = "Replay")
    void TogglePause();

    /** Plays at Rate times real time (above 0). */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    void SetPlaybackRate(float Rate);

    /** Steps to the next of the tuning's PlaybackRates (after the last, the first) and returns
     *  it. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    float CycleSlowMotion();

    /** Holds the replay and moves the playhead Frames captured frames on (back when negative),
     *  onto that frame's time. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    void StepFrames(int32 Frames);

    /** Holds the replay and moves the playhead by Seconds (back when negative). */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    void Scrub(float Seconds);

    /** Puts the playhead at Seconds from the clip's start, within the clip, and shows it. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    void SetPlayhead(float Seconds);

    /** Plays the clip again from its start. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    void Restart();

    // --- Persistence ---------------------------------------------------------------------

    /** Saved/Replays. */
    static FString GetDefaultSaveDirectory();

    /** Writes InClip as JSON to Directory (GetDefaultSaveDirectory when empty) as Name.json (a
     *  name made safe for a file; a timestamped one when empty), its scheduled frames thinned
     *  to the tuning's SaveFrameRateHz. False when it has no frames or the file can't be
     *  written. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    bool SaveClip(const FPSReplayRecording& InClip, const FString& Name, const FString& Directory, FString& OutFilePath);

    /** Reads a saved clip (through UPSReplayFormat's version gate). */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    static bool LoadClip(const FString& FilePath, FPSReplayRecording& OutClip);

    /** The saved clips in Directory (GetDefaultSaveDirectory when empty), by file name. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    static TArray<FString> ListSavedClips(const FString& Directory);

    /** InClip with its scheduled frames at most MaxRateHz apart; keyframes and the first and
     *  last frames stay, and the events' TickIndex follows the frames that remain. */
    static FPSReplayRecording ThinFrames(const FPSReplayRecording& InClip, float MaxRateHz);

    /** Sets each event's TickIndex to the index of the last of InOutClip's frames at or before
     *  its TimestampSeconds (0 before the first). */
    static void AssignEventTicks(FPSReplayRecording& InOutClip);

    /** Fires when a replay starts, after the field is posed at its first frame. */
    UPROPERTY(BlueprintAssignable, Category = "Replay")
    FPSReplaySignature OnReplayStarted;

    /** Fires when the playhead reaches the clip's end. */
    UPROPERTY(BlueprintAssignable, Category = "Replay")
    FPSReplaySignature OnReplayReachedEnd;

    /** Fires when a replay ends and the field is the live game's again. */
    UPROPERTY(BlueprintAssignable, Category = "Replay")
    FPSReplaySignature OnReplayEnded;

private:
    /** An actor the replay moves, and how to put it back. */
    struct FPosedActor
    {
        TWeakObjectPtr<AActor> Actor;
        FName PlayerId;
        FTransform SavedTransform;
        FVector SavedVelocity = FVector::ZeroVector;
        bool bSavedCollision = true;
    };

    UPSTelemetryBus* GetBus() const;
    UPSTelemetrySamplingSubsystem* GetSampler() const;

    /** Remembers every pawn and the ball as they are, and turns their collision off. */
    void TakeField();

    /** Puts every remembered actor back. */
    void ReleaseField();

    /** Pauses the game for the replay, if it isn't already, and keeps the broadcast cameras
     *  ticking through the pause. */
    void HoldGame();

    /** Unpauses the game if the replay paused it, and lets the cameras rest with it again. */
    void ReleaseGame();

    /** Poses the field at the playhead and shows the frame on the sampler. */
    void ShowPlayhead();

    float GetClipStartTime() const;
    void ClampPlayhead();

    UPROPERTY(Transient)
    FPSReplayTuning Tuning;

    bool bTuningLoaded = false;
    float PoseRateHz = 0.f;

    UPROPERTY(Transient)
    FPSReplayRecording Clip;

    EPSReplayState ReplayState = EPSReplayState::Idle;
    float Playhead = 0.f;
    float PlaybackRate = 1.f;
    int32 RateIndex = 0;

    /** Real seconds since the field was last posed, against the tier's pose rate. */
    float SincePose = 0.f;
    int32 PoseCount = 0;

    UPROPERTY(Transient)
    FPSSnapshotFrame ShownFrame;
    bool bHasShownFrame = false;

    TArray<FPosedActor> PosedPawns;
    TMap<FName, int32> PosedPawnIndexById;
    FPosedActor PosedBall;
    bool bHasPosedBall = false;

    /** Cameras made to tick through the pause, and whether each did before. */
    TArray<TPair<TWeakObjectPtr<AActor>, bool>> HeldCameras;

    /** The replay paused the game, and unpauses it at the end. */
    bool bPausedGame = false;
};
