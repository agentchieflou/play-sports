// PSStatsEngine.h - Epic 92: the statistics engine and the record book
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSStatsData.h"
#include "PSTelemetryBus.h"
#include "PSStatsEngine.generated.h"

class UPSFranchiseSaveGame;

/**
 * UPSStatsEngine keeps every statistic the league produces (Epic 92). It records nothing on its
 * own: each play arrives as the play simulation resolved it (FPSTelemetryPlayResultEvent: from the
 * bus in a played game, from UPSQuickSimRunner::OnPlayResolved in a simulated one) and is
 * attributed once, to the passer, receiver, rusher, tackler and interceptor and to both teams.
 * Each flag's ruling arrives the same way (#176's FPSTelemetryPenaltyEvent: the bus's Penalty
 * event, UPSQuickSimRunner::OnPenaltyRuled), just before its play: an accepted penalty is a team
 * stat (Penalties, PenaltyYards, the fouling team's), and the play it wiped out credits nobody.
 * Everything else is derived from those box scores; the stat book (FPSStatBook) persists in the
 * franchise save.
 *
 *  - Layers: the game box score; season totals (summed from this season's box scores, kept as
 *    totals once the season ends); careers (a player's seasons); franchise (a team's seasons);
 *    league (every team in a season).
 *  - Leaderboards for any category over a game, a season or a career, and a record book of the
 *    best of each. A record that falls is announced (OnRecordBroken, and RecordBroken on the bus
 *    when bound) as the game that broke it is finished: a single-game record whenever it is
 *    beaten, a season or career record when someone new passes the holder (or the season's
 *    holder sets it again in a later season).
 *  - Derived metrics: completion percentage, yards per attempt, passer rating, yards per carry
 *    and catch, catch rate; a team's yards per play, third-down and red-zone rates, field-goal
 *    percentage and turnover margin, from its down and red-zone splits.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSStatsEngine : public UObject
{
    GENERATED_BODY()

public:
    // --- Seasons and games -----------------------------------------------------------------

    /** Numbers the season being played (a franchise's first league year). False, with nothing
     *  changed, once this season has games. */
    UFUNCTION(BlueprintCallable, Category = "Stats")
    bool StartSeason(int32 Season);

    UFUNCTION(BlueprintPure, Category = "Stats")
    int32 GetSeason() const { return Book.Season; }

    /** Archives the season: its totals are kept, its box scores let go; the next season starts. */
    UFUNCTION(BlueprintCallable, Category = "Stats")
    void EndSeason();

    /** Opens a box score for a game in Week of this season (a season of 1 if none started). An
     *  unfinished game before it is dropped. */
    UFUNCTION(BlueprintCallable, Category = "Stats")
    void BeginGame(int32 Week, FName HomeTeamId, FName AwayTeamId);

    /** Attributes one play to the open game's players and teams. Ignored with no game open. A
     *  play an accepted penalty wiped out (RecordPenalty, just before it) credits nobody. */
    void RecordPlay(const FPSTelemetryPlayResultEvent& Event);

    /** A flag's ruling (Epic 23's Penalty event): accepted, it is a penalty against the fouling
     *  team for its yards, and the play it rules on credits nobody. A flag or a declined one
     *  changes nothing. Ignored with no game open. */
    void RecordPenalty(const FPSTelemetryPenaltyEvent& Event);

    /** Closes the open game: its box score joins the season and the record book is checked. */
    UFUNCTION(BlueprintCallable, Category = "Stats")
    void FinishGame();

    UFUNCTION(BlueprintPure, Category = "Stats")
    bool IsGameInProgress() const { return bGameInProgress; }

    /** The open game's box score as it stands (the last finished one's after FinishGame). */
    UFUNCTION(BlueprintPure, Category = "Stats")
    const FPSBoxScore& GetCurrentGame() const { return Current; }

    /** Records each PlayResult and each penalty ruling the bus announces (a played game). */
    void BindToBus(UPSTelemetryBus* Bus);

    void UnbindFromBus();

    // --- Queries ---------------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Stats")
    const TArray<FPSBoxScore>& GetSeasonGames() const { return Book.Games; }

    /** TeamId's box score in Week of this season. */
    UFUNCTION(BlueprintPure, Category = "Stats")
    bool FindGame(int32 Week, FName TeamId, FPSBoxScore& OutGame) const;

    /** PlayerId's totals in Season (this one or a finished one); an empty line for none. */
    UFUNCTION(BlueprintPure, Category = "Stats")
    FPSPlayerStatLine GetPlayerSeason(FName PlayerId, int32 Season) const;

    /** Every season of PlayerId's, this one included. */
    UFUNCTION(BlueprintPure, Category = "Stats")
    FPSPlayerStatLine GetPlayerCareer(FName PlayerId) const;

    UFUNCTION(BlueprintPure, Category = "Stats")
    FPSTeamStatLine GetTeamSeason(FName TeamId, int32 Season) const;

    /** Every season of TeamId's, this one included: the franchise's history. */
    UFUNCTION(BlueprintPure, Category = "Stats")
    FPSTeamStatLine GetFranchiseTotals(FName TeamId) const;

    /** Every team's Season summed (TeamId None). */
    UFUNCTION(BlueprintPure, Category = "Stats")
    FPSTeamStatLine GetLeagueTotals(int32 Season) const;

    /** The best Count in Category (all with Count 0 or less), most first, ties by id: single
     *  games this season, Season's totals, or careers (a team's history for a team category). */
    UFUNCTION(BlueprintPure, Category = "Stats")
    TArray<FPSLeaderEntry> GetLeaders(EPSStatCategory Category, EPSStatScope Scope, int32 Season, int32 Count) const;

    UFUNCTION(BlueprintPure, Category = "Stats")
    const TArray<FPSRecordEntry>& GetRecords() const { return Book.Records; }

    UFUNCTION(BlueprintPure, Category = "Stats")
    bool FindRecord(EPSStatCategory Category, EPSStatScope Scope, FPSRecordEntry& OutRecord) const;

    UFUNCTION(BlueprintPure, Category = "Stats")
    const FPSStatBook& GetStatBook() const { return Book; }

    UFUNCTION(BlueprintPure, Category = "Stats")
    static FPSPlayerMetrics ComputePlayerMetrics(const FPSPlayerStatLine& Line);

    UFUNCTION(BlueprintPure, Category = "Stats")
    static FPSTeamMetrics ComputeTeamMetrics(const FPSTeamStatLine& Line);

    /** The NFL passer rating of a line: 0 to 158.3; 0 without attempts. */
    UFUNCTION(BlueprintPure, Category = "Stats")
    static float ComputePasserRating(const FPSPlayerStatLine& Line);

    /** TeamPoints and TeamTotalYards. */
    static bool IsTeamCategory(EPSStatCategory Category);

    /** A record that fell (also published on the bound bus). */
    FPSTelemetryRecordBrokenMC OnRecordBroken;

    // --- Persistence -----------------------------------------------------------------------

    /** Writes the stat book into the franchise save (a game still being played is not saved). */
    void SaveTo(UPSFranchiseSaveGame* Save) const;

    /** Reads it back; false (keeping the current book) when the save has none. */
    bool LoadFrom(const UPSFranchiseSaveGame* Save);

    /** Situation names of the splits. */
    static FName DownSplit(int32 Down);
    static const FName RedZoneSplit;

private:
    FPSPlayerStatLine& FindOrAddPlayer(FName PlayerId, FName TeamId);

    /** Every player's (team's) totals in Season, keyed by id. */
    TMap<FName, FPSPlayerStatLine> SeasonPlayerTotals(int32 Season) const;
    TMap<FName, FPSTeamStatLine> SeasonTeamTotals(int32 Season) const;

    /** Every player's (team's) totals over all seasons. */
    TMap<FName, FPSPlayerStatLine> CareerPlayerTotals() const;
    TMap<FName, FPSTeamStatLine> FranchiseTeamTotals() const;

    /** Checks Game against the record book, announcing what falls. */
    void UpdateRecords(const FPSBoxScore& Game);

    void CheckRecord(EPSStatCategory Category, EPSStatScope Scope, FName HolderId, FName TeamId, int32 Value, int32 Week);

    UPROPERTY(Transient)
    FPSStatBook Book;

    UPROPERTY(Transient)
    FPSBoxScore Current;

    bool bGameInProgress = false;

    /** An accepted penalty was ruled on the play about to be announced. */
    bool bPenaltyOnPlay = false;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    FDelegateHandle PlayResultHandle;
    FDelegateHandle PenaltyHandle;
};
