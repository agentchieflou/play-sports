// PSLeagueHistory.h - Epic 94: the league's history, retirements and the hall of fame
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSLeagueData.h"
#include "PSLegacyData.h"
#include "PSPlayerAttributes.h"
#include "PSLeagueHistory.generated.h"

class UPSFranchiseSaveGame;
class UPSStatsEngine;

/**
 * UPSLeagueHistory is the one authority on the league's past (Epic 94), tuned in
 * Data/legacy.json. It keeps what is over and nothing else does:
 *
 *  - The season archive: each finished season's final standings in finishing order, its
 *    champion (the team that finished first: no playoffs are played yet) and its statistical
 *    leaders, read from the statistics engine (Epic 92) as the season ends.
 *  - Retired players: whoever decides a player retires records him here (RecordRetirement) with
 *    his career totals and the team he played the most games for, read from the statistics engine
 *    then; the archive no longer needs it.
 *  - The hall of fame: after each season the retired players who have waited WaitSeasons and
 *    played MinSeasons are voted in when their hall score reaches InductionScore: their best career
 *    total against its threshold (awards join it with Epic 93).
 *  - Franchise history: a team's seasons, finishes, championships and legends (its hall of famers).
 *
 * UPSFranchiseFlow archives each season and holds the vote at the season's end; the history
 * persists in the franchise save (SaveTo / LoadFrom). Screens to browse it are Track I's.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSLeagueHistory : public UObject
{
    GENERATED_BODY()

public:
    /** Data/legacy.json under the project directory. */
    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion. False, with the
     *  tuning unchanged, on a missing or malformed file. */
    UFUNCTION(BlueprintCallable, Category = "Legacy")
    bool LoadTuningFromJson(const FString& JsonFilePath);

    void SetTuning(const FPSLegacyTuning& InTuning) { Tuning = InTuning; }

    UFUNCTION(BlueprintPure, Category = "Legacy")
    const FPSLegacyTuning& GetTuning() const { return Tuning; }

    /** Problems with a tuning, one line each; empty when it is sound. */
    static TArray<FString> ValidateTuning(const FPSLegacyTuning& InTuning);

    // --- Recording the past ------------------------------------------------------------------

    /** Archives a finished season: SortedStandings in finishing order (the first is its champion),
     *  and each LeaderCategories leader from Stats (which may be null) for that season. False, with
     *  nothing changed, for a season already archived or without standings. */
    UFUNCTION(BlueprintCallable, Category = "Legacy")
    bool ArchiveSeason(int32 Season, const TArray<FPSTeamStanding>& SortedStandings, const UPSStatsEngine* Stats);

    /** Player retires from TeamId after Season, for Reason: his career totals, seasons played and
     *  the team he played the most games for come from Stats (none without one). False for a player
     *  already retired or without an id. */
    UFUNCTION(BlueprintCallable, Category = "Legacy")
    bool RecordRetirement(const FPlayerAttributes& Player, FName TeamId, int32 Season, const FString& Reason, const UPSStatsEngine* Stats);

    /** The hall of fame vote after Season: every retired player who waited WaitSeasons since
     *  retiring, played MinSeasons and reaches InductionScore, the best scores first, at most
     *  MaxInducteesPerSeason. Returns the class (also kept in the season's archive). */
    UFUNCTION(BlueprintCallable, Category = "Legacy")
    TArray<FPSRetiredPlayer> RunHallOfFameVote(int32 Season);

    /** A career's hall score: the highest of its category totals over their thresholds' values. */
    UFUNCTION(BlueprintPure, Category = "Legacy")
    float GetHallScore(const FPSPlayerStatLine& Career) const;

    // --- Reading it ------------------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Legacy")
    const TArray<FPSSeasonArchive>& GetSeasons() const { return State.Seasons; }

    UFUNCTION(BlueprintPure, Category = "Legacy")
    bool FindSeason(int32 Season, FPSSeasonArchive& OutSeason) const;

    UFUNCTION(BlueprintPure, Category = "Legacy")
    const TArray<FPSRetiredPlayer>& GetRetiredPlayers() const { return State.Retired; }

    UFUNCTION(BlueprintPure, Category = "Legacy")
    bool FindRetiredPlayer(FName PlayerId, FPSRetiredPlayer& OutPlayer) const;

    UFUNCTION(BlueprintPure, Category = "Legacy")
    bool IsRetired(FName PlayerId) const;

    /** Every hall of famer, in the order they were voted in. */
    UFUNCTION(BlueprintPure, Category = "Legacy")
    TArray<FPSRetiredPlayer> GetHallOfFame() const;

    /** TeamId's seasons in the archive, its championships, its record and its legends. */
    UFUNCTION(BlueprintPure, Category = "Legacy")
    FPSFranchiseHistory GetFranchiseHistory(FName TeamId) const;

    /** A season, line by line: the champion, the standings, the leaders, retirements and the hall
     *  of fame class. */
    UFUNCTION(BlueprintPure, Category = "Legacy")
    TArray<FString> DescribeSeason(int32 Season) const;

    /** A franchise's history, line by line. */
    UFUNCTION(BlueprintPure, Category = "Legacy")
    TArray<FString> DescribeFranchise(FName TeamId) const;

    UFUNCTION(BlueprintPure, Category = "Legacy")
    const FPSLeagueHistoryState& GetState() const { return State; }

    /** Writes the league's history into the franchise save. */
    void SaveTo(UPSFranchiseSaveGame* Save) const;

    /** Reads it back; false (keeping the current one) when the save has none. */
    bool LoadFrom(const UPSFranchiseSaveGame* Save);

private:
    FPSSeasonArchive* FindMutableSeason(int32 Season);

    UPROPERTY(Transient)
    FPSLegacyTuning Tuning;

    UPROPERTY(Transient)
    FPSLeagueHistoryState State;
};
