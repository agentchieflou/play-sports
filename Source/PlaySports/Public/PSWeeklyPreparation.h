// PSWeeklyPreparation.h - Epic 90: training, gameplan and weekly preparation
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSOpponentModelTypes.h"
#include "PSPlayerAttributes.h"
#include "PSTrainingData.h"
#include "PSWeeklyPreparation.generated.h"

class UPSFranchiseSaveGame;
class UPSInjuryModel;
class UPSOwnerEconomy;
class UPSRoster;
class UPSStaffManager;

/**
 * UPSWeeklyPreparation is the one authority on the week between games (Epic 90): each player's
 * condition (his freshness and any practice injury) and each team's practice and gameplan, tuned
 * in Data/training.json. Every team runs the same system; a team that chooses nothing runs the
 * recommendation, which is all a CPU team ever runs.
 *
 *  - Allocation: a week's practice splits between developing players, gameplanning for the next
 *    opponent and rest. Development adds rating points to the roster's players (more for a player
 *    far from 100, under a coordinator who develops players, with more training funding); the
 *    gameplan lifts the focus areas' players against that opponent; rest restores freshness.
 *    Development and gameplanning are practice: the harder the week, the more it tires players and
 *    the likelier an injury.
 *  - Gameplan: a scouting report reads the opponent's play calling (Epic 78's tendency read: the
 *    calls the opponent model has seen it make, else its coordinators' schemes, Epic 89). A focus
 *    area that prepares for what the opponent calls most is worth the most.
 *  - Fatigue and injury (Core 19): freshness is the stamina ratio Core 19's injury model rolls
 *    practice injuries with; games and practice spend it (less for a player with more Stamina),
 *    each week and rest restore it. An injured player sits his games until healed; a tired one plays
 *    below his ratings.
 *  - Funding: development and the gameplan scale with the team's training funding against the
 *    league's average (UPSOwnerEconomy::GetFundingIndex, Epic 95).
 *
 * UPSFranchiseFlow runs each week's practice before its games and applies the result to the
 * simulation's copies of the players (ApplyPreparation); the condition persists in the franchise
 * save (SaveTo / LoadFrom).
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSWeeklyPreparation : public UObject
{
    GENERATED_BODY()

public:
    /** Data/training.json under the project directory. */
    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion. False, with the
     *  tuning unchanged, on a missing or malformed file. */
    UFUNCTION(BlueprintCallable, Category = "Training")
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** The training tuning and the opponent model's (Data/opponent_model.json: how many calls a
     *  read of a team's play calling needs), from their default paths. */
    UFUNCTION(BlueprintCallable, Category = "Training")
    bool LoadDefaults();

    void SetTuning(const FPSTrainingTuning& InTuning) { Tuning = InTuning; }

    UFUNCTION(BlueprintPure, Category = "Training")
    const FPSTrainingTuning& GetTuning() const { return Tuning; }

    void SetOpponentModelTuning(const FPSOpponentModelTuning& InTuning) { OpponentTuning = InTuning; }

    /** Problems with a tuning, one line each; empty when it is sound. With the opponent model's
     *  tuning, also each focus category it doesn't track on that side. */
    static TArray<FString> ValidateTuning(const FPSTrainingTuning& InTuning, const FPSOpponentModelTuning* InOpponentTuning = nullptr);

    // --- Choices ---------------------------------------------------------------------------

    /** TeamId's own allocation from now on. False, with nothing changed, for a negative share or
     *  shares summing to 0. */
    UFUNCTION(BlueprintCallable, Category = "Training")
    bool SetAllocation(FName TeamId, const FPSTrainingAllocation& Allocation);

    /** Back to the recommended allocation. */
    UFUNCTION(BlueprintCallable, Category = "Training")
    void ClearAllocation(FName TeamId);

    /** TeamId's focus areas for the next week it prepares. False, with nothing changed, for an
     *  unknown or repeated focus area or more than MaxFocusAreas; an empty list runs the
     *  recommendation. */
    UFUNCTION(BlueprintCallable, Category = "Training")
    bool SetGameplanFocus(FName TeamId, const TArray<FName>& FocusIds);

    /** The calls Epic 78's opponent model has seen TeamId make (UPSOpponentModel::GetCareerCells
     *  for the human's team): its scouting report reads these once they are enough for a read. */
    void SetObservedCalls(FName TeamId, const TArray<FPSTendencyCell>& Cells);

    // --- What a team would do ----------------------------------------------------------------

    /** The allocation a team with Roster runs without its own choice: DefaultAllocation, shifted to
     *  rest when its healthy players are tired, to the gameplan late in the season (SeasonProgress
     *  0-1). */
    UFUNCTION(BlueprintPure, Category = "Training")
    FPSTrainingAllocation RecommendAllocation(const UPSRoster* Roster, float SeasonProgress) const;

    /** What is known of OpponentId's play calling (Staffs may be null). */
    UFUNCTION(BlueprintPure, Category = "Training")
    FPSOpponentScoutingReport BuildScoutingReport(FName OpponentId, const UPSStaffManager* Staffs) const;

    /** How much FocusId is worth against Report's team: the share of the focus area's categories
     *  in its calls against an even mix, at most MaxRelevance; UnscoutedRelevance with no read on
     *  that side, 0 for an unknown focus area. */
    UFUNCTION(BlueprintPure, Category = "Training")
    float GetFocusRelevance(const FPSOpponentScoutingReport& Report, FName FocusId) const;

    /** The AIFocusAreas focus areas worth the most against Report's team, most first. */
    UFUNCTION(BlueprintPure, Category = "Training")
    TArray<FName> RecommendFocus(const FPSOpponentScoutingReport& Report) const;

    /** What a team's training funding (its index against the league's average) multiplies
     *  development and the gameplan by. */
    UFUNCTION(BlueprintPure, Category = "Training")
    float GetFundingMultiplier(float FundingIndex) const;

    // --- The week ------------------------------------------------------------------------------

    /** TeamId's practice in Week, against OpponentId (None on a bye), once a week: its allocation
     *  (its own, else the recommendation at SeasonProgress), injuries healing, each healthy
     *  player's recovery, practice injury roll, practice fatigue and development (on Roster's
     *  rows), and its gameplan. Economy and Staffs may be null (average funding, no coaching).
     *  Returns what happened; nothing when Week is already prepared. */
    UFUNCTION(BlueprintCallable, Category = "Training")
    TArray<FPSTrainingEvent> PrepareTeam(FName TeamId, UPSRoster* Roster, int32 Week, FName OpponentId, float SeasonProgress,
        const UPSOwnerEconomy* Economy, const UPSStaffManager* Staffs);

    /** The players who played a game for TeamId: each spends GameFatigue of his freshness. */
    UFUNCTION(BlueprintCallable, Category = "Training")
    void RecordGame(FName TeamId, const TArray<FPlayerAttributes>& Played);

    /** Player as he plays for TeamId against OpponentId: his Speed, Agility, Strength,
     *  Acceleration and Awareness scaled by his freshness and lifted by TeamId's gameplan when it
     *  is against OpponentId (size and stamina untouched). The roster keeps his own ratings. */
    UFUNCTION(BlueprintPure, Category = "Training")
    FPlayerAttributes ApplyPreparation(FName TeamId, FName OpponentId, const FPlayerAttributes& Player) const;

    /** The season is over: every player heals and is fresh, every team's gameplan and prepared
     *  week are cleared (its chosen allocation stays). */
    UFUNCTION(BlueprintCallable, Category = "Training")
    void EndSeason();

    // --- Reading it -----------------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Training")
    bool IsInjured(FName PlayerId) const;

    /** PlayerId's freshness; 1 for a player never prepared. */
    UFUNCTION(BlueprintPure, Category = "Training")
    float GetFreshness(FName PlayerId) const;

    UFUNCTION(BlueprintPure, Category = "Training")
    bool GetCondition(FName PlayerId, FPSPlayerCondition& OutCondition) const;

    UFUNCTION(BlueprintPure, Category = "Training")
    bool GetTeamTraining(FName TeamId, FPSTeamTraining& OutTeam) const;

    /** TeamId's last prepared week, line by line: the allocation, funding, the gameplan and its
     *  focus areas' bonuses, injuries and freshness. */
    UFUNCTION(BlueprintPure, Category = "Training")
    TArray<FString> DescribeTeamWeek(FName TeamId) const;

    UFUNCTION(BlueprintPure, Category = "Training")
    const FPSTrainingState& GetState() const { return State; }

    /** Writes every condition and team's practice into the franchise save. */
    void SaveTo(UPSFranchiseSaveGame* Save) const;

    /** Reads them back; false (keeping the current ones) when the save has none. */
    bool LoadFrom(const UPSFranchiseSaveGame* Save);

private:
    FPSPlayerCondition& FindOrAddPlayer(FName PlayerId, FName TeamId);
    const FPSPlayerCondition* FindPlayer(FName PlayerId) const;
    FPSTeamTraining& FindOrAddTeam(FName TeamId);
    const FPSTeamTraining* FindTeam(FName TeamId) const;

    /** The categories the focus areas name on a side: a read's shares are taken over these. */
    TArray<FString> GetSideCategories(bool bVersusOffense) const;

    /** A side's read from its coordinator's scheme, over that side's categories. */
    FPSTendencyRead ReadScheme(FName TeamId, bool bOffense, const UPSStaffManager* Staffs) const;

    /** How much less fatigue Player takes for his Stamina (1 = all of it). */
    float GetFatigueFactor(const FPlayerAttributes& Player) const;

    /** Development's multiplier from Player's coordinator on TeamId. */
    float GetCoachMultiplier(FName TeamId, const FPlayerAttributes& Player, const UPSStaffManager* Staffs) const;

    UPROPERTY(Transient)
    FPSTrainingTuning Tuning;

    UPROPERTY(Transient)
    FPSOpponentModelTuning OpponentTuning;

    UPROPERTY(Transient)
    FPSTrainingState State;

    UPROPERTY(Transient)
    UPSInjuryModel* InjuryModel = nullptr;

    /** The calls the opponent model saw, by team (not saved: the profile is their authority). */
    TMap<FName, TArray<FPSTendencyCell>> ObservedCalls;
};
