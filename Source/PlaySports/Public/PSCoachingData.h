#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSSpecialTeamsData.h"
#include "PSCoachingData.generated.h"

/** Per-opponent tendency profile (Architecture rule 4: tuning lives in DataTables,
 *  not hardcoded magic numbers). AggressionScore biases 4th-down/2-point/deep-shot
 *  decisions; CategoryWeights biases play-category selection ("Run", "ShortPass",
 *  "DeepPass", "PlayAction", "Screen" for offense, "Base"/"Blitz"/"Prevent" for
 *  defense) relative to the situational base weight. */
USTRUCT(BlueprintType)
struct FPSTendencyProfile : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName TeamId;

    /** 0 = conservative (favors the percentage play, punts/kicks by the book),
     *  1 = aggressive (goes for it, attempts 2-pointers, dials up deep shots/blitzes). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float AggressionScore = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TMap<FString, float> CategoryWeights;

    /** Where the weights come from, when a coordinator's scheme sets them (Epic 89): "West
     *  Coast". The play-call reasons then read "West Coast scheme (x1.4)". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Label;
};

/** Down/distance/clock/score snapshot the coaching AI reads to weight play
 *  categories and make situational decisions. Not a DataTable row -- built
 *  per-play from the live FPlayState (Epic 9's UPSPlaySimulation). */
USTRUCT(BlueprintType)
struct FPSSituationContext
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Down = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Distance = 10;

    /** 0 = own goal line, 100 = opponent's goal line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 YardLine = 20;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Quarter = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float GameClockSeconds = 900.f;

    /** Possessing team's score minus the opponent's score. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 ScoreDifferential = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 TimeoutsRemaining = 3;

    /** The defense's timeouts (the possessing team's opponent). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 OpponentTimeoutsRemaining = 3;

    /** True while the game clock runs before the snap (after a tackle in bounds). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bClockRunning = false;

    /** The down is a kickoff (Epic 75): the possessing team kicks, the other returns. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bKickoff = false;

    /** The kick the offense's call shows the defense (Punt or FieldGoal; a fake shows the same),
     *  once the offense has called. The defense picks its return or block against it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPSSpecialTeamsPlay OffenseKick = EPSSpecialTeamsPlay::None;

    /** The home team has the ball: which team's plan (FPSTeamPlan) each side calls with. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHomeHasPossession = true;
};

/** A team's identity for a game (Epic 89, from its coaching staff): the tendencies its
 *  coordinators call with and the plays its playbook keeps. Empty, it calls from the whole
 *  playbook with neutral tendencies. */
USTRUCT(BlueprintType)
struct FPSTeamPlan
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FPSTendencyProfile OffenseTendency;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FPSTendencyProfile DefenseTendency;

    /** The playbook plays the team runs, both sides; empty for all of them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FName> PlayIds;
};

/** One play as the coaching AI rates it for a situation (Epic 102's suggestions): its
 *  weight and the situational reasons behind it, e.g. "3rd down: the percentage play (+0.8)". */
USTRUCT(BlueprintType)
struct FPSPlaySuggestion
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FName PlayId;

    UPROPERTY(BlueprintReadOnly)
    FString DisplayName;

    UPROPERTY(BlueprintReadOnly)
    FString Category;

    UPROPERTY(BlueprintReadOnly)
    float Weight = 0.f;

    UPROPERTY(BlueprintReadOnly)
    TArray<FString> Reasons;
};

