#pragma once

#include "CoreMinimal.h"
#include "PSLeagueData.h"
#include "PSScheduleEngine.h"
#include "PSFranchiseSeason.generated.h"

/**
 * Turns the (already-working) UPSScheduleEngine week list into a playable season loop:
 * generates matchups per week, records game results into standings, and tracks the
 * current week (Epic 20). One authority for "what week is it / who plays whom / who's
 * won what" for a franchise -- the GameMode/UI reads this rather than duplicating state.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSFranchiseSeason : public UObject
{
    GENERATED_BODY()

public:
    /** Builds a circle-method round-robin schedule of matchups across NonByeWeeks,
     *  and seeds a zero standing for every team. TeamIds.Num() should be even. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    void InitializeSeason(const TArray<FName>& TeamIds, const TArray<FSeasonWeek>& Weeks);

    UFUNCTION(BlueprintCallable, Category = "Franchise")
    TArray<FPSWeekMatchup> GetMatchupsForWeek(int32 WeekNumber) const;

    /** Records a final score for one matchup, updating both teams' standings.
     *  Returns false if the matchup can't be found or was already played. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    bool RecordGameResult(int32 WeekNumber, FName HomeTeamId, FName AwayTeamId, int32 HomeScore, int32 AwayScore);

    UFUNCTION(BlueprintPure, Category = "Franchise")
    TArray<FPSTeamStanding> GetStandings() const { return Standings; }

    /** Standings sorted by win percentage descending (point differential as tiebreak). */
    UFUNCTION(BlueprintPure, Category = "Franchise")
    TArray<FPSTeamStanding> GetSortedStandings() const;

    UFUNCTION(BlueprintPure, Category = "Franchise")
    int32 GetCurrentWeek() const { return CurrentWeek; }

    UFUNCTION(BlueprintCallable, Category = "Franchise")
    void AdvanceWeek() { ++CurrentWeek; }

    UFUNCTION(BlueprintPure, Category = "Franchise")
    const TArray<FPSWeekMatchup>& GetAllMatchups() const { return Matchups; }

private:
    UPROPERTY(Transient)
    TArray<FPSTeamStanding> Standings;

    UPROPERTY(Transient)
    TArray<FPSWeekMatchup> Matchups;

    int32 CurrentWeek = 1;
};
