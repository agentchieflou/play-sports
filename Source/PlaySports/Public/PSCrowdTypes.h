// PSCrowdTypes.h - Epic 23.2: what moves the crowd, and how much
#pragma once

#include "CoreMinimal.h"
#include "PSTelemetryBus.h"
#include "PSCrowdTypes.generated.h"

/** A moment the crowd reacts to (Epic 23.2). UPSCrowdExcitementSubsystem reads them from the bus;
 *  each one benefits one team, whose fans react one way and the other team's another. */
UENUM(BlueprintType)
enum class EPSCrowdStimulus : uint8
{
    /** A deep ball in the air: the whole stadium gasps. Benefits the offense. */
    DeepPass,
    /** A play that gained BigGainYards or more. */
    BigGain,
    FirstDown,
    /** A pass fell incomplete. Benefits the defense. */
    Incompletion,
    Touchdown,
    FieldGoalGood,
    /** Benefits the defense. */
    FieldGoalMissed,
    /** Benefits the defense. */
    Safety,
    Sack,
    Interception,
    /** A fumble the defense recovered. */
    FumbleLost,
    /** A hit of BigHitDamage or more on the ball carrier. Benefits the defense. */
    BigHit,
    TurnoverOnDowns,
    /** A flag: benefits the team that didn't foul. */
    Flag
};

/** How a stimulus moves the crowd (Data/crowd.json). The fans of the team it benefits change the
 *  crowd's excitement by FansDelta, the other team's fans by RivalsDelta, each weighted by their
 *  share of the stadium; whichever fans are the majority give the crowd's reaction. */
USTRUCT(BlueprintType)
struct FPSCrowdReactionDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    EPSCrowdStimulus Stimulus = EPSCrowdStimulus::Touchdown;

    /** -1..1: the excitement the benefiting team's fans add. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float FansDelta = 0.f;

    /** -1..1: the excitement the other team's fans add (a stunned crowd goes quiet: negative). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float RivalsDelta = 0.f;

    /** The reaction when the benefiting team's fans are the majority. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    EPSCrowdReaction FansReaction = EPSCrowdReaction::Cheer;

    /** The reaction when the other team's fans are. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    EPSCrowdReaction RivalsReaction = EPSCrowdReaction::Groan;
};

/** The least excitement a level takes (Data/crowd.json). */
USTRUCT(BlueprintType)
struct FPSCrowdLevelThreshold
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    EPSCrowdLevel Level = EPSCrowdLevel::Murmur;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float MinExcitement = 0.f;
};

/**
 * The crowd's excitement model (Data/crowd.json, Epic 23.2; Architecture rule 4). Defaults equal
 * the JSON's numbers; the arrays come from the file.
 */
USTRUCT(BlueprintType)
struct FPSCrowdTuning
{
    GENERATED_BODY()

    /** 0-1: where the excitement settles between moments. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float RestingExcitement = 0.25f;

    /** From LateGameQuarter on, while the margin is CloseGameMargin points or fewer, the crowd
     *  rests LateCloseBonus higher. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    int32 LateGameQuarter = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    int32 CloseGameMargin = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float LateCloseBonus = 0.15f;

    /** Seconds for the excitement to settle half way back to rest. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float HalfLifeSeconds = 6.f;

    /** 0-1: the home team's fans' share of the stadium, unless the match says otherwise. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float DefaultHomeShare = 0.85f;

    /** The excitement must fall this far under a level's threshold to drop out of it, so the
     *  crowd doesn't flicker between two. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float LevelHysteresis = 0.03f;

    /** A play gaining this many yards or more is a BigGain. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    int32 BigGainYards = 20;

    /** A hit of this much damage (Epic 139) or more is a BigHit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float BigHitDamage = 30.f;

    /** A pass thrown this far (cm, start to target) or farther is a DeepPass. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float DeepPassCm = 2500.f;

    /** Every EPSCrowdLevel once, thresholds rising from Hush's 0. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    TArray<FPSCrowdLevelThreshold> Levels;

    /** Every EPSCrowdStimulus once. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    TArray<FPSCrowdReactionDef> CrowdReactions;
};
