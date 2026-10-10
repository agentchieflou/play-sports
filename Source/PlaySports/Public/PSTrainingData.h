// PSTrainingData.h - Epic 90: training, gameplan and weekly preparation
#pragma once

#include "CoreMinimal.h"
#include "PSInjuryModel.h"
#include "PSOpponentModelTypes.h"
#include "PSPlayerAttributes.h"
#include "PSTrainingData.generated.h"

/** How a team spends its practice week: shares of developing its players, gameplanning for its
 *  next opponent and resting. Only the proportions matter; Normalized scales them to sum to 1. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTrainingAllocation
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float Develop = 0.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float Gameplan = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float Rest = 0.25f;

    float GetTotal() const { return Develop + Gameplan + Rest; }

    /** The shares scaled to sum to 1; an even split when they sum to 0 or less. */
    FPSTrainingAllocation Normalized() const
    {
        FPSTrainingAllocation Result;
        const float Total = GetTotal();
        if (Total <= 0.f)
        {
            Result.Develop = Result.Gameplan = Result.Rest = 1.f / 3.f;
            return Result;
        }
        Result.Develop = Develop / Total;
        Result.Gameplan = Gameplan / Total;
        Result.Rest = Rest / Total;
        return Result;
    }
};

/** How much each of a player's skill ratings counts (0 or more each). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSRatingWeights
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float Speed = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float Agility = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float Strength = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float Acceleration = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float Awareness = 0.f;
};

/** A gameplan focus area (Data/training.json): what a week's gameplanning prepares for, and
 *  which players it sharpens. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSGameplanFocusDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    FName FocusId;

    /** "Run defense" -- shown on the week's report. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    FString Label;

    /** True when it prepares for the opponent's offense (Categories are its offensive calls),
     *  false for its defense. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    bool bVersusOffense = true;

    /** The opponent's play categories it prepares for, as Epic 78's opponent model names them:
     *  Run, ShortPass, DeepPass, PlayAction, Screen; Base, Blitz, Prevent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    TArray<FString> Categories;

    /** The roles whose ratings the gameplan lifts. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    TArray<EPlayerRole> Roles;

    /** Which of their ratings, and how much of the gameplan's bonus each takes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    FPSRatingWeights Ratings;
};

/** The practice week (Data/training.json, Epic 90). The defaults equal the file's. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTrainingTuning
{
    GENERATED_BODY()

    /** The allocation a team runs when it hasn't chosen one, before the recommendation's shifts. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Allocation")
    FPSTrainingAllocation DefaultAllocation;

    /** How hard a full week of each is: the week's intensity is Develop x DevelopIntensity plus
     *  Gameplan x GameplanIntensity (rest is 0). It drives practice fatigue and injury risk. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Allocation")
    float DevelopIntensity = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Allocation")
    float GameplanIntensity = 0.6f;

    /** Rating points a full week of development adds to a rating of weight 1, at the league's
     *  average training funding and coaching, for a player with full headroom. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Development")
    float DevelopPointsPerWeek = 0.6f;

    /** Which ratings practice develops, and how much of the week's points each takes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Development")
    FPSRatingWeights DevelopRatings;

    /** A rating within this many points of 100 develops in proportion to its distance from 100;
     *  one this far below or more develops fully. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Development")
    float DevelopHeadroom = 40.f;

    /** Development's multiplier from the player's coordinator's Development (Epic 89): this at 0
     *  to MaxCoachDevelopment at 100; 1 without a coordinator. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Development")
    float MinCoachDevelopment = 0.75f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Development")
    float MaxCoachDevelopment = 1.25f;

    /** Training funding (the owner economy's training budget, Epic 95): development and the
     *  gameplan are multiplied by FundingFloor + (1 - FundingFloor) x the team's funding index
     *  (1 the league's average, 0 unfunded), at most MaxFundingMultiplier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Funding")
    float FundingFloor = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Funding")
    float MaxFundingMultiplier = 1.5f;

    /** The gameplan's bonus: GameplanBonusPerShare x the gameplan share x the funding multiplier,
     *  split between the focus areas, each part times its relevance against the opponent, at most
     *  MaxGameplanBonus. A focus area's players play that much above their ratings (times each
     *  rating's weight) in the game against that opponent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Gameplan")
    float GameplanBonusPerShare = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Gameplan")
    float MaxGameplanBonus = 0.12f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Gameplan")
    int32 MaxFocusAreas = 2;

    /** Relevance: the opponent's share of a focus area's categories against an even mix of its
     *  side's categories, at most MaxRelevance; UnscoutedRelevance without a scouting read. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Gameplan")
    float MaxRelevance = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Gameplan")
    float UnscoutedRelevance = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Gameplan")
    TArray<FPSGameplanFocusDef> FocusAreas;

    /** Freshness (0-1, the stamina ratio Core 19's injury model reads): a game costs GameFatigue,
     *  a full-intensity practice week PracticeFatigue (each less StaminaFatigueRelief x his
     *  Stamina / 100 of it); every week gives back WeeklyRecovery, plus RestRecovery x the rest
     *  share. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Fatigue")
    float GameFatigue = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Fatigue")
    float PracticeFatigue = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Fatigue")
    float WeeklyRecovery = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Fatigue")
    float RestRecovery = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Fatigue")
    float StaminaFatigueRelief = 0.5f;

    /** A player plays this much below his ratings at freshness 0 (in proportion above it). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Fatigue")
    float FatiguePerformanceSwing = 0.08f;

    /** Practice injuries, through Core 19's injury model: BaseInjuryChance is a player's chance in
     *  a full-intensity week at full freshness (times the week's intensity), MaxFatigueMultiplier
     *  that at freshness 0; an injury keeps him out Min..MaxRecoveryWeeks weeks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Injury")
    FPSInjuryTuning PracticeInjury;

    /** Seeds each team's practice rolls with its id and its weeks of practice, so a week rolls the
     *  same however the franchise got there (a loaded save included). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Injury")
    int32 RandomSeed = 90;

    /** The recommendation (what every team without its own allocation runs, CPU or not): from
     *  DefaultAllocation, AIRestShift moves from development to rest when the healthy players'
     *  freshness averages under AIRestFreshness, and AILateGameplanShift from development to the
     *  gameplan once AILateSeasonProgress of the season has gone; the AIFocusAreas most relevant
     *  focus areas are its gameplan. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Recommendation")
    float AIRestFreshness = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Recommendation")
    float AIRestShift = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Recommendation")
    float AILateSeasonProgress = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Recommendation")
    float AILateGameplanShift = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Recommendation")
    int32 AIFocusAreas = 1;

    FPSTrainingTuning()
    {
        DevelopRatings.Speed = 0.2f;
        DevelopRatings.Agility = 0.4f;
        DevelopRatings.Strength = 0.6f;
        DevelopRatings.Acceleration = 0.3f;
        DevelopRatings.Awareness = 1.f;
        PracticeInjury.BaseInjuryChance = 0.004f;
        PracticeInjury.MaxFatigueMultiplier = 4.f;
        PracticeInjury.MinRecoveryWeeks = 1;
        PracticeInjury.MaxRecoveryWeeks = 4;
    }

    const FPSGameplanFocusDef* FindFocus(FName FocusId) const
    {
        return FocusAreas.FindByPredicate([FocusId](const FPSGameplanFocusDef& Focus) { return Focus.FocusId == FocusId; });
    }
};

/** A player's condition between games. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSPlayerCondition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    FName PlayerId;

    /** The team he last practised with. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    FName TeamId;

    /** 0 (spent) to 1 (fresh). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float Freshness = 1.f;

    /** Weeks of practice before he can play again; 0 when healthy. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    int32 InjuryWeeks = 0;

    /** Rating points practice has added this season. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float DevelopmentGained = 0.f;
};

/** A focus area in a week's gameplan, and what it is worth against the opponent. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSGameplanFocus
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    FName FocusId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float Relevance = 0.f;

    /** What its players' ratings are lifted by (times each rating's weight). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float Bonus = 0.f;
};

/** A team's gameplan for one week's opponent. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTeamGameplan
{
    GENERATED_BODY()

    /** None on a bye week, or before any week was prepared. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    FName OpponentId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    int32 Week = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    TArray<FPSGameplanFocus> Focus;
};

/** A team's practice: its own choices, and the last week it prepared. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTeamTraining
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    FName TeamId;

    /** Its own allocation, kept week to week once chosen; without one it runs the
     *  recommendation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    FPSTrainingAllocation ChosenAllocation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    bool bAllocationChosen = false;

    /** Focus areas it chose for the next week it prepares (then cleared: the next opponent is
     *  another); empty runs the recommended focus. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    TArray<FName> ChosenFocus;

    /** The last week it prepared (0 before its first this season). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    int32 PreparedWeek = 0;

    /** Practice weeks it has run, ever: seeds its injury rolls. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    int32 PracticeWeeks = 0;

    /** What the last prepared week ran. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    FPSTrainingAllocation Allocation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    bool bRecommendedAllocation = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float FundingIndex = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float Intensity = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    float DevelopmentPoints = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    int32 Injuries = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    FPSTeamGameplan Gameplan;
};

/** Every player's condition and every team's practice, as the franchise save keeps them
 *  (UPSFranchiseSaveGame). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTrainingState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    TArray<FPSPlayerCondition> Players;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    TArray<FPSTeamTraining> Teams;
};

/** What a scouting report knows of a team's play calling on each side: Epic 78's tendency read,
 *  from the calls the opponent model has seen it make when it has seen enough, else from its
 *  coordinator's scheme (Epic 89). Basis None on a side nothing is known of. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSOpponentScoutingReport
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Training")
    FName TeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Training")
    FPSTendencyRead Offense;

    UPROPERTY(BlueprintReadOnly, Category = "Training")
    FPSTendencyRead Defense;

    /** Whether each side's read is the calls it was seen to make (else its scheme's). */
    UPROPERTY(BlueprintReadOnly, Category = "Training")
    bool bOffenseObserved = false;

    UPROPERTY(BlueprintReadOnly, Category = "Training")
    bool bDefenseObserved = false;
};

UENUM(BlueprintType)
enum class EPSTrainingEventKind : uint8
{
    /** Hurt in practice. */
    Injury,
    /** Back from an injury. */
    Recovered
};

/** Something that happened in a practice week. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTrainingEvent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Training")
    EPSTrainingEventKind Kind = EPSTrainingEventKind::Injury;

    UPROPERTY(BlueprintReadOnly, Category = "Training")
    FName PlayerId;

    UPROPERTY(BlueprintReadOnly, Category = "Training")
    FName TeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Training")
    int32 Week = 0;

    /** For an injury, the weeks he is out. */
    UPROPERTY(BlueprintReadOnly, Category = "Training")
    int32 Weeks = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Training")
    FString Description;
};
