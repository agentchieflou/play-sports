// PSOwnerEconomy.h - Epic 95: owner mode and the league's economics
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSEconomyData.h"
#include "PSLeagueData.h"
#include "PSOwnerEconomy.generated.h"

class UPSContractManager;
class UPSFranchiseSaveGame;

/**
 * UPSOwnerEconomy is the one authority on the league's business (Epic 95): each team's ticket
 * price, its fans' satisfaction, its budget and its revenue. A light layer above the GM's game,
 * tuned in Data/owner_economics.json:
 *
 *  - Revenue: each home game draws a crowd from the team's record (as the caller reads it from
 *    the standings), its fans' satisfaction and its ticket price, which pays the gate and the
 *    concessions; every team takes an equal media share at the season's end.
 *  - Budget: the owner splits a share of revenue between scouting (Epic 86), training (Epic 90)
 *    and the coaching staff (Epic 89). Those systems read their funding here (GetFundingIndex: 1
 *    is the league's average) when they arrive.
 *  - Fans: wins and losses, prices above the base and winning or losing seasons move satisfaction;
 *    a team that keeps losing until its fans give up comes under relocation pressure.
 *  - The season's books: revenue less payroll (the cap the team's contracts used, Epic 87) and
 *    budget is the owner's profit.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSOwnerEconomy : public UObject
{
    GENERATED_BODY()

public:
    /** Data/owner_economics.json under the project directory. */
    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion. False, with the
     *  tuning unchanged, on a missing or malformed file. */
    UFUNCTION(BlueprintCallable, Category = "Economy")
    bool LoadTuningFromJson(const FString& JsonFilePath);

    void SetTuning(const FPSEconomyTuning& InTuning) { Tuning = InTuning; }

    UFUNCTION(BlueprintPure, Category = "Economy")
    const FPSEconomyTuning& GetTuning() const { return Tuning; }

    /** Problems with a tuning, one line each; empty when it is sound. */
    static TArray<FString> ValidateTuning(const FPSEconomyTuning& InTuning);

    /** Adds a team at the base price, the starting satisfaction and the default budget. */
    UFUNCTION(BlueprintCallable, Category = "Economy")
    void RegisterTeam(FName TeamId);

    /** TeamId's business; false for a team never registered. */
    UFUNCTION(BlueprintPure, Category = "Economy")
    bool GetTeam(FName TeamId, FPSTeamEconomy& OutTeam) const;

    UFUNCTION(BlueprintPure, Category = "Economy")
    const FPSLeagueEconomy& GetLeague() const { return League; }

    /** Sets TeamId's ticket price, held to the tuning's range; returns the price set. */
    UFUNCTION(BlueprintCallable, Category = "Economy")
    float SetTicketPrice(FName TeamId, float Price);

    /** Sets TeamId's budget. False, with nothing changed, for a negative share or a total over
     *  MaxBudgetFraction. */
    UFUNCTION(BlueprintCallable, Category = "Economy")
    bool SetBudget(FName TeamId, const FPSTeamBudget& Budget);

    /** The crowd TeamId would draw at home with this win percentage (0-1), at its price and its
     *  fans' satisfaction: what the owner's pricing screen previews. */
    UFUNCTION(BlueprintPure, Category = "Economy")
    int32 PredictAttendance(FName TeamId, float WinPercentage) const;

    /** A game's result: the home team's gate (HomeWinPercentage is its record before the game)
     *  and both fan bases' reaction. */
    UFUNCTION(BlueprintCallable, Category = "Economy")
    FPSGameGate RecordGame(FName HomeTeamId, FName AwayTeamId, int32 HomeScore, int32 AwayScore, float HomeWinPercentage);

    /** The season's end: every team's books (the media share added, payroll from Contracts when
     *  given), winning and losing seasons' effect on the fans, relocation pressure. Next season's
     *  budgets are shares of these revenues. */
    UFUNCTION(BlueprintCallable, Category = "Economy")
    TArray<FPSEconomySeasonReport> EndSeason(const TArray<FPSTeamStanding>& Standings, const UPSContractManager* Contracts);

    /** What TeamId spends on Department this season: its budget share of last season's revenue
     *  (of the media share before a first season). */
    UFUNCTION(BlueprintPure, Category = "Economy")
    int32 GetDepartmentFunding(FName TeamId, EPSBudgetDepartment Department) const;

    /** TeamId's funding of Department against the league's average: 1 is average, 0 unfunded. */
    UFUNCTION(BlueprintPure, Category = "Economy")
    float GetFundingIndex(FName TeamId, EPSBudgetDepartment Department) const;

    /** Writes every team's business into the franchise save. */
    void SaveTo(UPSFranchiseSaveGame* Save) const;

    /** Reads it back; false (keeping the current economy) when the save has none. */
    bool LoadFrom(const UPSFranchiseSaveGame* Save);

private:
    FPSTeamEconomy& FindOrAddTeam(FName TeamId);
    const FPSTeamEconomy* FindTeam(FName TeamId) const;

    UPROPERTY(Transient)
    FPSEconomyTuning Tuning;

    UPROPERTY(Transient)
    FPSLeagueEconomy League;
};
