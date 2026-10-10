// PSReplayTypes.h - Epic 41: the replay system's tuning and transport state
#pragma once

#include "CoreMinimal.h"
#include "PSCameraDirectorComponent.h"
#include "PSReplayTypes.generated.h"

/** Where a replay's transport stands. */
UENUM(BlueprintType)
enum class EPSReplayState : uint8
{
    /** No replay: the live game is on. */
    Idle,
    Playing,
    /** Held on one moment: paused by the viewer, scrubbed, stepped, or at the clip's end. */
    Paused
};

/** What makes a play worth replaying by itself. */
UENUM(BlueprintType)
enum class EPSReplayTrigger : uint8
{
    /** Points went up: a touchdown, a field goal, a safety. */
    Score,
    /** The ball changed hands in the play: an interception, a lost fumble, a stop on downs. */
    Turnover
};

/** How a play that Trigger makes worth it is replayed by itself. */
USTRUCT(BlueprintType)
struct FPSAutoReplayRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    EPSReplayTrigger Trigger = EPSReplayTrigger::Score;

    /** The angle the replay opens on, in the camera director's vocabulary (Epic 38); the
     *  director covers it from there. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    EPSDirectorShot Shot = EPSDirectorShot::EndZone;

    /** The speed it plays at (1 is real time). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float PlaybackRate = 0.5f;
};

/**
 * How replays are cut, played, seen and saved (Data/replay.json; Architecture rule 4). Defaults
 * equal the file. How often a replay re-poses the field is per platform tier (ReplayPoseRateHz
 * in Data/platform_tiers.json).
 */
USTRUCT(BlueprintType)
struct FPSReplayTuning
{
    GENERATED_BODY()

    /** A clip starts this long before its first event (the snap of a play), so the formation
     *  is seen set. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float PreRollSeconds = 1.f;

    /** ...and runs this long after its last event (the whistle), for the reaction. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float PostRollSeconds = 1.5f;

    /** The speeds the slow-motion control steps through, in order; the first is the speed a
     *  replay starts at. 1 is real time. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    TArray<float> PlaybackRates;

    /** Clip seconds a held scrub button moves the playhead per real second. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float ScrubSecondsPerSecond = 1.f;

    /** A saved replay keeps a scheduled frame at most this often (keyframes, the clip's first
     *  frame and its last are always kept), so a file stays small; playback blends between
     *  them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float SaveFrameRateHz = 15.f;

    /** The cameras the camera control steps through, in order; a replay starts on the first.
     *  "Director" is the camera director (Epic 38), "Skycam" its cable rig (Epic 39), "Free"
     *  the viewer's own camera, and any other name an all-22 rig of Data/camera_all22.json
     *  (Epic 40). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    TArray<FName> Cameras;

    /** The free camera circles the ball at this distance (cm) to begin with... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float FreeCamDistanceCm = 1500.f;

    /** ...which the Move stick's forward and back bring between these (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float FreeCamMinDistanceCm = 500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float FreeCamMaxDistanceCm = 4000.f;

    /** How steeply the free camera looks down on the ball (degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float FreeCamPitchDegrees = 25.f;

    /** Degrees the free camera circles per second at full stick left or right. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float FreeCamOrbitDegreesPerSecond = 90.f;

    /** cm the free camera closes in or backs off per second at full stick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float FreeCamZoomCmPerSecond = 1500.f;

    /** Replay plays worth it by themselves after the whistle (AutoReplays). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    bool bAutoReplay = true;

    /** Seconds from the end of a play to its replay, for the live reaction. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float AutoReplayDelaySeconds = 1.5f;

    /** Seconds an automatic replay holds its last frame before the game comes back. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float AutoReplayHoldSeconds = 1.f;

    /** What makes a play worth replaying, and how it is replayed: one rule per trigger. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    TArray<FPSAutoReplayRule> AutoReplays;

    /** With Reduced motion on (Epic 103.5), an automatic replay is seen from this still
     *  all-22 rig instead of a director's angle that may fly or chase. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FName ReducedMotionCamera = FName(TEXT("Sideline"));

    FPSReplayTuning()
    {
        PlaybackRates = { 1.f, 0.5f, 0.25f };
        Cameras = { FName(TEXT("Director")), FName(TEXT("Sideline")), FName(TEXT("EndZone")), FName(TEXT("Skycam")), FName(TEXT("Free")) };

        FPSAutoReplayRule& ScoreRule = AutoReplays.AddDefaulted_GetRef();
        ScoreRule.Trigger = EPSReplayTrigger::Score;
        ScoreRule.Shot = EPSDirectorShot::EndZone;
        ScoreRule.PlaybackRate = 0.5f;
        FPSAutoReplayRule& TurnoverRule = AutoReplays.AddDefaulted_GetRef();
        TurnoverRule.Trigger = EPSReplayTrigger::Turnover;
        TurnoverRule.Shot = EPSDirectorShot::Skycam;
        TurnoverRule.PlaybackRate = 0.5f;
    }
};
