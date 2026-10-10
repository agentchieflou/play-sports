// PSTelemetrySamplingSubsystem.h - Epic 26: per-tick spatial snapshots on top of the event bus
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/Function.h"
#include "PSPlatformTiers.h"
#include "PSTelemetryBus.h"
#include "PSTelemetrySamplingTypes.h"
#include "PSTelemetrySamplingSubsystem.generated.h"

class AActor;
class APSBall;
class APSPlayerPawn;

/**
 * UPSTelemetrySamplingSubsystem records where everyone is, continuously: the overlay-grade
 * stream that UPSTelemetryBus's discrete events don't carry (Epic 26). Overlays (badges,
 * trails, ball arcs), replay and commentary read it; none of them keep their own copy.
 *
 *  - Scheduled frames: every pawn's location, velocity, acceleration and facing, plus the
 *    ball, at FPSTelemetrySamplingTuning::SampleRateHz, kept in a ring covering
 *    HistorySeconds.
 *  - Keyframes: the same capture, taken the instant a KeyframeEvents event is published on
 *    the bus, before any subscriber reacts. A keyframe is kept exactly as long as its event
 *    stays in the bus's history, so the two buffers always answer the same window.
 *  - Correlation: every frame carries the bus Sequence it follows, and the subsystem notes
 *    when (on its clock) each event still in the bus's history happened. Queries join the
 *    two: the frame at an event (exact keyframe, or blended from the frames either side),
 *    the frames between two events, the events inside a time window. The events themselves
 *    are only ever read from the bus (rule 6).
 *  - Budget: each scheduled frame's cost is measured (and shown by `stat
 *    PSTelemetrySampling`). A run of frames over SampleBudgetMs halves the rate, down to
 *    MaxDegradeLevel halvings; a run well under it doubles the rate back. The rate and the
 *    budget are the platform tier's (PSPlatformTiers::GetActiveTier), so a phone samples less
 *    often from the start.
 *
 * The subsystem ticks with its world and calls AdvanceTime; headless tests, whose worlds
 * don't tick, call AdvanceTime themselves. Its clock is game time: the sum of the ticks'
 * DeltaSeconds, plus the world time elapsed since the last tick for an event published
 * mid-tick.
 */
UCLASS()
class PLAYSPORTS_API UPSTelemetrySamplingSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion; fields the file
     *  doesn't set (the tier's rate and budget) keep their current values. A result that fails
     *  ValidateTuning is refused and the current tuning kept. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Takes the rate and the budget from Tier (Data/platform_tiers.json, Epic 129). The
     *  subsystem applies the run's active tier when it starts; a test can apply any. */
    void ApplyPlatformTier(const FPSPlatformTier& Tier);

    /** Applies NewTuning. History is kept: the ring is resized to the new rate, dropping the
     *  oldest frames if it shrinks. */
    void SetTuning(const FPSTelemetrySamplingTuning& NewTuning);

    const FPSTelemetrySamplingTuning& GetTuning() const { return Tuning; }

    /** Problems with a tuning, one line each (empty when sound). */
    static TArray<FString> ValidateTuning(const FPSTelemetrySamplingTuning& InTuning);

    /** Sets the full sampling rate, keeping the rest of the tuning. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void SetSampleRateHz(float RateHz);

    /** Off stops scheduled frames and keyframes (the clock still runs). History is kept, so
     *  a query across the pause blends the frames either side of it. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void SetSamplingEnabled(bool bEnabled);

    UFUNCTION(BlueprintPure, Category = "Telemetry")
    bool IsSamplingEnabled() const { return bSamplingEnabled; }

    /** SampleRateHz after the budget's halvings. */
    UFUNCTION(BlueprintPure, Category = "Telemetry")
    float GetEffectiveSampleRateHz() const;

    UFUNCTION(BlueprintPure, Category = "Telemetry")
    int32 GetDegradeLevel() const { return DegradeLevel; }

    UFUNCTION(BlueprintPure, Category = "Telemetry")
    FPSTelemetrySamplingStats GetStats() const;

    /** The sampler's clock now, seconds. */
    UFUNCTION(BlueprintPure, Category = "Telemetry")
    float GetClock() const { return static_cast<float>(GetClockSeconds()); }

    /** One step: advances the clock by DeltaSeconds and takes a scheduled frame when one is
     *  due (at most one per call). The tick calls this; headless tests call it directly. */
    void AdvanceTime(float DeltaSeconds);

    /** Feeds one scheduled frame's measured cost to the budget: updates the cost stats and
     *  may halve or double the rate. Called after every scheduled frame; public so a test
     *  can drive the budget with chosen costs. */
    void RecordSampleCost(float CostMs);

    /** Re-reads which pawns and ball are in the world, and who each pawn is. Happens by itself
     *  before the next frame after one is spawned or destroyed, or after a substitution (a
     *  Personnel event) points a pawn at another player. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RefreshRoster();

    // --- History -------------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Telemetry")
    int32 GetScheduledFrameCount() const { return Ring.Num(); }

    UFUNCTION(BlueprintPure, Category = "Telemetry")
    int32 GetKeyframeCount() const { return Keyframes.Num(); }

    /** The most recently captured frame, scheduled or keyframe; while a replay plays, the
     *  replay's frame (SetReplayFrame). */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    bool GetLatestFrame(FPSSnapshotFrame& OutFrame) const;

    /** Every captured frame (scheduled and keyframes) with FromTime <= Time <= ToTime, in
     *  capture order. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    TArray<FPSSnapshotFrame> GetFramesBetween(float FromTime, float ToTime) const;

    /** Everyone at Time: a captured frame at that time, or one blended from the frames
     *  either side (bInterpolated). False when Time is before the history or more than one
     *  sampling interval after its newest frame. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    bool SampleAt(float Time, FPSSnapshotFrame& OutFrame) const;

    /** One pawn's snapshots with FromTime <= Time <= ToTime, oldest first: a trail. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    TArray<FPSPawnSnapshot> GetPawnTrail(FName PlayerId, float FromTime, float ToTime) const;

    /** PlayerId's snapshot in Frame. */
    UFUNCTION(BlueprintPure, Category = "Telemetry")
    static bool FindPawnSnapshot(const FPSSnapshotFrame& Frame, FName PlayerId, FPSPawnSnapshot& OutSnapshot);

    // --- Correlation with the bus --------------------------------------------------------

    /** When, on this clock, the event with EventSequence happened. False when it was
     *  published while sampling was off, or has left the bus's history. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    bool GetEventTime(int32 EventSequence, float& OutTime) const;

    /** Everyone at the moment of an event: its keyframe when it took one, otherwise a frame
     *  blended at its time. OutFrame.EventSequence is the event's. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    bool GetFrameAtEvent(int32 EventSequence, FPSSnapshotFrame& OutFrame) const;

    /** Everyone at the most recent EventType event still in the bus's history, e.g. the
     *  positions of all 22 at the catch. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    bool GetFrameAtLatestEvent(EPSTelemetryEventType EventType, FPSSnapshotFrame& OutFrame, FPSTelemetryEvent& OutEvent) const;

    /** The frames from one event to another, in capture order: the frame at FromSequence,
     *  every frame captured after it and before ToSequence, and the frame at ToSequence
     *  (the replay of "snap to tackle"). */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    TArray<FPSSnapshotFrame> GetFramesBetweenEvents(int32 FromSequence, int32 ToSequence) const;

    /** The bus's events that happened with FromTime <= time <= ToTime, oldest first. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    TArray<FPSTelemetryEvent> GetEventsBetween(float FromTime, float ToTime) const;

    /** Everyone Alpha of the way from From to To: locations, velocities and facing blended,
     *  each pawn matched by pawn or PlayerId; who has the ball switches at the halfway mark.
     *  The frame SampleAt gives between two captured frames, and the one a replay (Epic 41)
     *  shows between its frames. */
    static void BlendFrames(const FPSSnapshotFrame& From, const FPSSnapshotFrame& To, float Alpha, FPSSnapshotFrame& OutFrame);

    // --- Replay (Epic 41) ----------------------------------------------------------------

    /** Shows Frame as the present while a replay plays: GetLatestFrame returns it, so cameras
     *  and overlays that read the latest frame follow the replay, and nothing is captured
     *  (no scheduled frames, keyframes or event times; the clock still runs), so the history
     *  stays the live game's. Each call replaces the frame shown. */
    void SetReplayFrame(const FPSSnapshotFrame& Frame);

    /** Back to the live game: capture resumes and GetLatestFrame is the newest captured frame
     *  again. */
    void ClearReplayFrame();

    UFUNCTION(BlueprintPure, Category = "Telemetry")
    bool IsShowingReplay() const { return bShowingReplay; }

private:
    /** A pawn being sampled, with what its next acceleration is measured against. */
    struct FSampledPawn
    {
        TWeakObjectPtr<APSPlayerPawn> Pawn;
        FName PlayerId;
        EPlayerRole Role = EPlayerRole::Quarterback;
        FVector LastVelocity = FVector::ZeroVector;
        FVector LastAcceleration = FVector::ZeroVector;
        double LastTime = 0.0;
        bool bHasLast = false;
    };

    /** When, on this clock, a bus event happened. */
    struct FEventMark
    {
        int32 Sequence = 0;
        double Time = 0.0;
    };

    void HandleEventRecorded(const FPSTelemetryEvent& Event);
    void HandleActorSpawned(AActor* Actor);

    double GetClockSeconds() const;
    double GetEffectiveSampleInterval() const;
    int32 GetRingCapacity() const;

    void TakeScheduledFrame();
    void CaptureFrame(FPSSnapshotFrame& OutFrame, double Now, int32 EventSequence);
    FPSSnapshotFrame& PushScheduledSlot();
    const FPSSnapshotFrame& GetScheduledFrame(int32 Index) const;
    void ResizeRing(int32 NewCapacity);

    /** Drops keyframes and event marks whose events have left the bus's history. */
    void PruneToBus(const UPSTelemetryBus& Bus);

    /** Every captured frame passing Filter, in capture order. */
    void GatherFrames(TArray<const FPSSnapshotFrame*>& OutFrames, TFunctionRef<bool(const FPSSnapshotFrame&)> Filter) const;
    const FPSSnapshotFrame* FindKeyframe(int32 EventSequence) const;

    FPSTelemetrySamplingTuning Tuning;

    TArray<FSampledPawn> Roster;
    TWeakObjectPtr<APSBall> Ball;
    bool bRosterDirty = true;

    /** Scheduled frames; once full, RingStart is the oldest and each new frame overwrites it. */
    TArray<FPSSnapshotFrame> Ring;
    int32 RingStart = 0;

    TArray<FPSSnapshotFrame> Keyframes;
    TArray<FEventMark> EventMarks;

    double Clock = 0.0;
    double WorldTimeAtLastAdvance = 0.0;
    double SinceLastSample = 0.0;
    int32 NextFrameIndex = 0;
    bool bSamplingEnabled = true;

    /** A replay's frame, shown as the present while bShowingReplay. */
    FPSSnapshotFrame ReplayFrame;
    bool bShowingReplay = false;

    int32 DegradeLevel = 0;
    int32 OverBudgetStreak = 0;
    int32 UnderBudgetStreak = 0;
    bool bHasCostSample = false;
    FPSTelemetrySamplingStats Stats;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    FDelegateHandle ActorSpawnedHandle;
};
