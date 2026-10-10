// PSSessionTelemetryTypes.h - Epic 117: what a session reports (tuning, summary, report)
#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "PSSessionTelemetryTypes.generated.h"

/**
 * Tuning for session telemetry and crash breadcrumbs (Data/session_telemetry.json, Epic 117;
 * Architecture rule 4). The defaults equal the JSON. Specs/Privacy_Telemetry.md says what is
 * kept and why.
 */
USTRUCT(BlueprintType)
struct FPSSessionTelemetryTuning
{
    GENERATED_BODY()

    FPSSessionTelemetryTuning()
    {
        Percentiles = { 50.f, 95.f, 99.f };
    }

    /** Width of one frame-time histogram bucket, in milliseconds. Percentiles are reported to
     *  this resolution, rounded up. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float FrameTimeBucketMs = 0.5f;

    /** Number of histogram buckets. A frame slower than FrameTimeBucketMs * FrameTimeBucketCount
     *  lands in one overflow bucket, which reports the slowest frame seen. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 FrameTimeBucketCount = 200;

    /** The frame-time percentiles (0-100) every session records. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    TArray<float> Percentiles;

    /** A session that ends cleanly with less play than this (seconds) is not kept. A session
     *  that never ended cleanly is always kept: that is a crash. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float MinSessionSeconds = 5.f;

    /** How many sessions the local store keeps; the oldest goes first. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 MaxStoredSessions = 20;

    /** The open session is saved again every this many plays, so a crash still leaves its
     *  counts behind. 0 saves only at the start and the end. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 CheckpointEveryPlays = 5;

    /** How many of the telemetry bus's most recent events a crash report carries. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 CrashBreadcrumbCount = 12;
};

/** One frame-time percentile of a session. */
USTRUCT(BlueprintType)
struct FPSFrameTimePercentile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float Percentile = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float Milliseconds = 0.f;
};

/**
 * One session: a game world from its BeginPlay to its teardown (the front end, a Play Now game,
 * a practice). Anonymous by construction: no names, no account, no machine; SessionId is random
 * per session and links nothing across sessions.
 */
USTRUCT(BlueprintType)
struct FPSSessionSummary
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FGuid SessionId;

    /** The travel URL's mode option (PlayNow, Franchise, Practice), else its game option (Menu
     *  for the front end), else Unspecified. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Mode;

    /** The project version (Project Settings) the session ran. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString BuildVersion;

    /** Windows, Mac, IOS, Android. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Platform;

    /** The platform tier the run used (Data/platform_tiers.json); frame times only compare
     *  within a tier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString PlatformTier;

    /** UTC day the session was last saved, YYYY-MM-DD. No time of day. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Date;

    /** Seconds of unpaused play (the sum of recorded frame times). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float DurationSeconds = 0.f;

    /** Snaps seen on the telemetry bus. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 PlayCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 FrameCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float MaxFrameMs = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    TArray<FPSFrameTimePercentile> FrameTimePercentiles;

    /** False while the session runs. A stored session still false after it is over never
     *  reached its teardown: the game crashed or was killed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bEndedCleanly = false;
};

/** Sessions, plays and play time in one mode, over the stored sessions. */
USTRUCT(BlueprintType)
struct FPSModeUsage
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Mode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Sessions = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Plays = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float Seconds = 0.f;
};

/** Session health over the stored sessions: the anonymous report a future uploader would send
 *  (Specs/Privacy_Telemetry.md). */
USTRUCT(BlueprintType)
struct FPSSessionTelemetryReport
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Sessions = 0;

    /** Sessions that never ended cleanly, including one still running. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 UncleanSessions = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    TArray<FPSModeUsage> Modes;
};

/** Whether the player has answered the telemetry question (Specs/Privacy_Telemetry.md). */
UENUM(BlueprintType)
enum class EPSTelemetryConsent : uint8
{
    NotAsked,
    OptedIn,
    OptedOut
};

/**
 * Frame-time histogram with fixed memory: percentiles over a whole session without keeping
 * every frame. Buckets are FrameTimeBucketMs wide plus one overflow bucket.
 */
struct PLAYSPORTS_API FPSFrameTimeHistogram
{
    /** Empties the histogram and sets its shape (BucketCount buckets of BucketMs each). */
    void Reset(float InBucketMs, int32 InBucketCount);

    void AddFrame(float DeltaSeconds);

    /** The time (ms) the Percentile-th fastest frame took, to bucket resolution rounded up and
     *  never above the slowest frame; 0 with no frames. */
    float GetPercentileMs(float Percentile) const;

    int32 Num() const { return FrameCount; }

    float GetMaxMs() const { return MaxMs; }

private:
    float BucketMs = 0.5f;
    TArray<int32> Buckets;
    int32 FrameCount = 0;
    float MaxMs = 0.f;
};
