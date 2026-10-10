// PSTelemetrySamplingTypes.h - Epic 26: what the telemetry sampler records, and its tuning
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "PSTelemetrySamplingTypes.generated.h"

/**
 * How the telemetry sampler runs (Architecture rule 4). Sampling is the one per-frame cost of
 * the overlay layer, so its rate and its budget are data, and the rate degrades by itself when
 * the budget is exceeded. The rate and the budget are per platform tier (TelemetrySampleRateHz
 * and TelemetrySampleBudgetMs in Data/platform_tiers.json); the rest is
 * Data/telemetry_sampling.json.
 */
USTRUCT(BlueprintType)
struct FPSTelemetrySamplingTuning
{
    GENERATED_BODY()

    /** Scheduled frames per second at full rate, from the platform tier. At most one frame is
     *  taken per game tick, so a rate above the frame rate samples every tick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float SampleRateHz = 30.f;

    /** How many seconds of scheduled frames the history keeps at full rate. The ring holds
     *  SampleRateHz * HistorySeconds frames, so a degraded rate covers a longer span. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float HistorySeconds = 20.f;

    /** Bus events that take a keyframe: every pawn captured at the instant the event is
     *  published, before anything reacts to it. A keyframe lives exactly as long as its
     *  event stays in the bus's history. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    TArray<EPSTelemetryEventType> KeyframeEvents;

    /** What one scheduled frame may cost, in milliseconds of game-thread time, from the
     *  platform tier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float SampleBudgetMs = 0.25f;

    /** This many frames in a row over budget halve the rate (one degrade level). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 DegradeAfterSamples = 5;

    /** This many frames in a row under RecoverBelowFraction of the budget double it back. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 RecoverAfterSamples = 90;

    /** A frame counts toward recovery when it costs less than this fraction of the budget. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float RecoverBelowFraction = 0.5f;

    /** How many times the rate may halve: 3 is one eighth of SampleRateHz at worst. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 MaxDegradeLevel = 3;

    FPSTelemetrySamplingTuning()
    {
        KeyframeEvents.Add(EPSTelemetryEventType::Snap);
        KeyframeEvents.Add(EPSTelemetryEventType::Throw);
        KeyframeEvents.Add(EPSTelemetryEventType::Catch);
        KeyframeEvents.Add(EPSTelemetryEventType::Tackle);
        KeyframeEvents.Add(EPSTelemetryEventType::Fumble);
        KeyframeEvents.Add(EPSTelemetryEventType::Score);
        KeyframeEvents.Add(EPSTelemetryEventType::PumpFake);
    }
};

/** One pawn at one instant. A record of the past, never the authority on the present: live
 *  state stays on the pawn and its components (rule 6). */
USTRUCT(BlueprintType)
struct FPSPawnSnapshot
{
    GENERATED_BODY()

    UPROPERTY()
    TWeakObjectPtr<APSPlayerPawn> Pawn;

    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    FName PlayerId;

    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    EPSTeamSide TeamSide = EPSTeamSide::Offense;

    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    EPlayerRole Role = EPlayerRole::Quarterback;

    /** World space, cm. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    FVector Location = FVector::ZeroVector;

    /** cm/s, from the pawn's movement component. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    FVector Velocity = FVector::ZeroVector;

    /** cm/s^2: the change in velocity since the pawn's previous frame, over the time between
     *  them. Zero on a pawn's first frame. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    FVector Acceleration = FVector::ZeroVector;

    /** The way the pawn faces: its yaw in degrees (0 is +X, 90 is +Y). */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    float FacingYaw = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    bool bHasBall = false;

    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    bool bUserControlled = false;
};

/** Every pawn (and the ball) at one instant. */
USTRUCT(BlueprintType)
struct FPSSnapshotFrame
{
    GENERATED_BODY()

    /** Capture order across scheduled frames and keyframes, from 0; -1 for a frame
     *  interpolated by a query. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    int32 FrameIndex = -1;

    /** The sampler's clock, in seconds of game time. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    float Time = 0.f;

    /** The bus's last event Sequence when the frame was taken: every event up to and
     *  including it had been published. For a keyframe, the event that took it. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    int32 EventSequence = 0;

    /** Taken at the instant of a bus event rather than on the schedule. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    bool bKeyframe = false;

    /** For a keyframe, the type of the event that took it. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    EPSTelemetryEventType KeyframeEventType = EPSTelemetryEventType::Snap;

    /** Blended from the frames either side of Time by a query, not captured. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    bool bInterpolated = false;

    /** False when the world had no ball. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    bool bBallSampled = false;

    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    FVector BallLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    FVector BallVelocity = FVector::ZeroVector;

    /** Offense first, then defense, each by PlayerId. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    TArray<FPSPawnSnapshot> Pawns;

    /** The snapshot of PlayerId in this frame, or null. */
    const FPSPawnSnapshot* FindPawn(FName PlayerId) const
    {
        return Pawns.FindByPredicate([PlayerId](const FPSPawnSnapshot& Snapshot) { return Snapshot.PlayerId == PlayerId; });
    }
};

/** What sampling costs, and the rate it runs at because of it (Epic 26's budget). */
USTRUCT(BlueprintType)
struct FPSTelemetrySamplingStats
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    int32 ScheduledFramesTaken = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    int32 KeyframesTaken = 0;

    /** Cost of the latest scheduled frame, ms. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    float LastSampleCostMs = 0.f;

    /** Moving average of the scheduled frames' cost, ms. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    float AverageSampleCostMs = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    float SampleBudgetMs = 0.f;

    /** 0 at full rate; each level halves it. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    int32 DegradeLevel = 0;

    /** The rate sampling runs at now. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    float EffectiveSampleRateHz = 0.f;
};
