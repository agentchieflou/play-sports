// PSForceFeedbackTypes.h - Epic 128: controller rumble patterns as authored in Data/force_feedback.json
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSForceFeedbackTypes.generated.h"

/** A gameplay moment the controller rumbles for. Each maps from a UPSTelemetryBus event. */
UENUM(BlueprintType)
enum class EPSForceFeedbackCue : uint8
{
    /** A tackle that landed on the ball carrier (the bus Damage event). */
    Hit,
    /** The carrier is down (Tackle event). */
    Tackle,
    /** The quarterback is down behind the line before throwing (Tackle event, bIsSack). */
    Sack,
    /** A completed pass (Catch event). */
    Catch,
    /** A pass caught by the defense (Catch event, bIsInterception). */
    Interception,
    /** The ball came loose or was recovered (Fumble event). */
    Fumble,
    /** Points on the board (Score event). */
    Score
};

/** One cue's rumble: how hard, how long, which motors, and who feels it. */
USTRUCT(BlueprintType)
struct FForceFeedbackTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ForceFeedback")
    EPSForceFeedbackCue Cue = EPSForceFeedbackCue::Hit;

    /** 0-1 before MasterIntensity; 0 turns the cue off. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ForceFeedback")
    float Intensity = 0.5f;

    /** Seconds. Always positive: a negative duration would rumble until stopped. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ForceFeedback")
    float Duration = 0.2f;

    /** The large motors carry heavy, low thuds; the small ones a light buzz. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ForceFeedback")
    bool bLeftLarge = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ForceFeedback")
    bool bLeftSmall = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ForceFeedback")
    bool bRightLarge = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ForceFeedback")
    bool bRightSmall = false;

    /** True: only when the event names the pawn this player controls (the tackler or
     *  carrier, the receiver, ...). False: every player feels it (scores, interceptions). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ForceFeedback")
    bool bOnlyWhenInvolved = true;
};

/** Top-level shape of Data/force_feedback.json: one row per cue. */
USTRUCT(BlueprintType)
struct FPSForceFeedbackTuning
{
    GENERATED_BODY()

    /** Scales every cue (0-1). The player's own vibration setting is Epic 103's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ForceFeedback")
    float MasterIntensity = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ForceFeedback")
    TArray<FForceFeedbackTuningRow> Cues;

    const FForceFeedbackTuningRow* FindCue(EPSForceFeedbackCue Cue) const
    {
        return Cues.FindByPredicate([Cue](const FForceFeedbackTuningRow& Row) { return Row.Cue == Cue; });
    }
};

/** One rumble the component decided on: what the pattern resolved to and whether it reached
 *  the gamepad's motors (only while a gamepad is the active device). */
USTRUCT(BlueprintType)
struct FPSForceFeedbackDispatch
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ForceFeedback")
    EPSForceFeedbackCue Cue = EPSForceFeedbackCue::Hit;

    /** The row's Intensity times MasterIntensity. */
    UPROPERTY(BlueprintReadOnly, Category = "ForceFeedback")
    float Intensity = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "ForceFeedback")
    float Duration = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "ForceFeedback")
    bool bLeftLarge = false;

    UPROPERTY(BlueprintReadOnly, Category = "ForceFeedback")
    bool bLeftSmall = false;

    UPROPERTY(BlueprintReadOnly, Category = "ForceFeedback")
    bool bRightLarge = false;

    UPROPERTY(BlueprintReadOnly, Category = "ForceFeedback")
    bool bRightSmall = false;

    /** True when it went to APlayerController::PlayDynamicForceFeedback; false while the
     *  player is on keyboard/mouse (nothing to shake). */
    UPROPERTY(BlueprintReadOnly, Category = "ForceFeedback")
    bool bPlayedOnGamepad = false;
};
