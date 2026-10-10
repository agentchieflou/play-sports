// PSReplayTypes.h - Epic 41: the replay system's tuning and transport state
#pragma once

#include "CoreMinimal.h"
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

/**
 * How replays are cut, played and saved (Data/replay.json; Architecture rule 4). Defaults equal
 * the file. How often a replay re-poses the field is per platform tier (ReplayPoseRateHz in
 * Data/platform_tiers.json).
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

    /** A saved replay keeps a scheduled frame at most this often (keyframes, the clip's first
     *  frame and its last are always kept), so a file stays small; playback blends between
     *  them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float SaveFrameRateHz = 15.f;

    FPSReplayTuning()
    {
        PlaybackRates = { 1.f, 0.5f, 0.25f };
    }
};
