// PSLeagueNarrative.h - Epic 93: the league's storylines, weekly news, awards and talking points
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSGameIntelligenceTypes.h"
#include "PSNarrativeTypes.h"
#include "PSStatsData.h"
#include "PSTelemetryBus.h"
#include "PSTradeData.h"
#include "PSLeagueNarrative.generated.h"

class UPSFranchiseSaveGame;
class UPSFranchiseSeason;
class UPSGameIntelligenceSubsystem;
class UPSOverlayBroadcastSubsystem;
class UPSStatsEngine;
class UPSTradeMarket;

DECLARE_MULTICAST_DELEGATE_OneParam(FPSAwardGivenMC, const FPSAwardRecord& /* Award */);

/**
 * UPSLeagueNarrative makes a franchise's seasons tell stories (Epic 93). It owns no league facts:
 * the season (UPSFranchiseSeason) is the authority on results, the statistics engine
 * (UPSStatsEngine, Epic 92) on numbers and records. UPSFranchiseFlow drives it (SetNarrative):
 * CloseWeek after each week's games, AwardSeason at the season's end.
 *
 *  - Storylines (93.1), detected after each week: a team's win or losing streak of StreakMin or
 *    more; a rookie (a player in his first season of a league with history) among a category's
 *    RookieSurgeTopN; next week's revenge game against a team that beat it this season; a record
 *    the stats engine announced broken (OnRecordBroken); a close MVP race from
 *    AwardRaceFromWeek; each trade the trade market made since (Epic 88, OnTradeCompleted).
 *    Each carries its kind's weight.
 *  - The weekly news digest (93.2): the week's storylines and honors, heaviest first, written
 *    from templates in the string tables (Data/ui_text.csv's Narrative.* rows, UPSLocalization).
 *    With Epic 82's bridge online (SetIntelligence) it asks a model to write the week up (a
 *    NewsDigest request, DigestTask's routing); the answer is kept as the digest's ModelText.
 *  - Awards (93.3): each week the offensive and defensive player of the week, the best game
 *    scores of the week (OffenseScoring, DefenseScoring); at the season's end MVP, offensive and
 *    defensive player of the year and rookie of the year by a vote: VoterCount voters, each
 *    seeing every score off by up to VoterNoise, rank BallotPoints.Num() players for those
 *    points; most points wins. The vote is seeded by VotingSeed and the season, so it is the same
 *    every time. Epic 94's hall of fame reads the record: GetAwards, GetAwardsForPlayer,
 *    CountAwards, and OnAwardGiven as each is given.
 *  - The broadcast (93.4): FeedBroadcast queues the heaviest storylines about a game's two teams
 *    as chyrons through Track A's UPSOverlayBroadcastSubsystem::PushChyron; GetTalkingPoints is
 *    the same list for a commentary engine (Track H) to read.
 *  - Persistence: SaveTo/LoadFrom the franchise save.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSLeagueNarrative : public UObject
{
    GENERATED_BODY()

public:
    // --- Tuning ----------------------------------------------------------------------------

    static FString GetDefaultTuningPath();

    /** The tuning, loaded from the default path on first use. */
    const FPSNarrativeTuning& GetTuning();

    /** Replaces the tuning with JsonFilePath's (through UPSDataIngestion); one that can't be read
     *  or fails ValidateTuning is refused and the current tuning kept. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    bool SetTuning(const FPSNarrativeTuning& InTuning);

    /** Problems with InTuning, one line each (empty when sound). */
    static TArray<FString> ValidateTuning(const FPSNarrativeTuning& InTuning);

    // --- Sources ---------------------------------------------------------------------------

    /** The league's statistics: box scores, season lines, leaders and broken records. */
    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void SetStats(UPSStatsEngine* InStats);

    /** Epic 82's hooks; with the bridge online each digest is offered to a model. */
    void SetIntelligence(UPSGameIntelligenceSubsystem* InIntelligence);

    /** The league's trades (Epic 88): each one made becomes a storyline at the week's close. */
    void SetTradeMarket(UPSTradeMarket* InTradeMarket);

    // --- Storylines and news (93.1, 93.2) ----------------------------------------------------

    /** After Week's games: the week's storylines replace the last week's, its honors are given and
     *  its digest written. Returns the digest. */
    UFUNCTION(BlueprintCallable, Category = "Narrative")
    FPSNewsDigest CloseWeek(UPSFranchiseSeason* Season, int32 Week);

    /** The storylines Week's results and the numbers so far tell (CloseWeek keeps them). */
    TArray<FPSStoryline> DetectStorylines(const UPSFranchiseSeason* Season, int32 Week);

    /** The storylines being told now, the heaviest first. */
    UFUNCTION(BlueprintPure, Category = "Narrative")
    const TArray<FPSStoryline>& GetActiveStorylines() const { return State.ActiveStorylines; }

    /** The weekly digests kept, oldest first. */
    UFUNCTION(BlueprintPure, Category = "Narrative")
    const TArray<FPSNewsDigest>& GetDigests() const { return State.Digests; }

    /** A storyline as the news tells it. */
    FPSNewsItem DescribeStoryline(const FPSStoryline& Storyline) const;

    /** An award as the news tells it. */
    FPSNewsItem DescribeAward(const FPSAwardRecord& Award) const;

    // --- Awards (93.3) -----------------------------------------------------------------------

    /** Week's offensive and defensive players of the week, from its box scores. */
    TArray<FPSAwardRecord> AwardWeeklyHonors(int32 Week);

    /** The season's awards by vote, from its box scores and (for the MVP) the standings; also
     *  writes the season-end digest. Once per season: empty when it has been given. */
    UFUNCTION(BlueprintCallable, Category = "Narrative")
    TArray<FPSAwardRecord> AwardSeason(const UPSFranchiseSeason* Season);

    /** Every award given, in order. */
    UFUNCTION(BlueprintPure, Category = "Narrative")
    const TArray<FPSAwardRecord>& GetAwards() const { return State.Awards; }

    /** PlayerId's awards, in order. */
    UFUNCTION(BlueprintPure, Category = "Narrative")
    TArray<FPSAwardRecord> GetAwardsForPlayer(FName PlayerId) const;

    /** How many times PlayerId has won Award. */
    UFUNCTION(BlueprintPure, Category = "Narrative")
    int32 CountAwards(FName PlayerId, EPSAwardKind Award) const;

    /** An award was given. */
    FPSAwardGivenMC OnAwardGiven;

    /** Line's score by Weights: each statistic times its weight. */
    static float ScoreLine(const FPSPlayerStatLine& Line, const TArray<FPSAwardStatWeight>& Weights);

    /** True for a player in his first season of a league that has finished one before. */
    UFUNCTION(BlueprintPure, Category = "Narrative")
    bool IsRookie(FName PlayerId) const;

    /**
     * The vote: Voters voters, each seeing every candidate's score off by up to Noise (from
     * Stream), rank the best BallotPoints.Num() and give them those points. Returns the
     * candidates' points and first-place votes, most points first (ties: the higher score, then
     * the id).
     */
    static TArray<FPSAwardRecord> RunVote(const TArray<FPSAwardRecord>& Candidates, int32 Voters, const TArray<int32>& BallotPoints, float Noise, FRandomStream& Stream);

    // --- The broadcast (93.4) ----------------------------------------------------------------

    /** The heaviest storylines about either team, as news items, best first (at most Max). */
    UFUNCTION(BlueprintCallable, Category = "Narrative")
    TArray<FPSNewsItem> GetTalkingPoints(FName HomeTeamId, FName AwayTeamId, int32 Max) const;

    /** Queues the game's MaxBroadcastStorylines talking points as chyrons on Broadcast. Returns how
     *  many it took. */
    UFUNCTION(BlueprintCallable, Category = "Narrative")
    int32 FeedBroadcast(UPSOverlayBroadcastSubsystem* Broadcast, FName HomeTeamId, FName AwayTeamId);

    // --- Persistence -------------------------------------------------------------------------

    /** Writes the storylines, digests and awards into the franchise save. */
    void SaveTo(UPSFranchiseSaveGame* Save) const;

    /** Reads them back; false (keeping the current state) when the save has none. */
    bool LoadFrom(const UPSFranchiseSaveGame* Save);

    const FPSNarrativeState& GetState() const { return State; }

private:
    void HandleRecordBroken(const FPSTelemetryRecordBrokenEvent& Event);
    void HandleRequestAnswered(const FPSIntelRequest& Request);
    void HandleTrade(const FPSTradeRecord& Trade);

    float KindWeight(EPSStorylineKind Kind);

    /** Adds Award to the record and tells the listeners. */
    void GiveAward(const FPSAwardRecord& Award);

    /** Keeps Digest (the latest DigestsKept) and, with the bridge online, offers it to a model. */
    void PublishDigest(FPSNewsDigest Digest);

    UPROPERTY(Transient)
    FPSNarrativeTuning Tuning;

    bool bTuningLoaded = false;

    UPROPERTY(Transient)
    FPSNarrativeState State;

    /** Records broken since the last CloseWeek. */
    TArray<FPSTelemetryRecordBrokenEvent> RecordsThisWeek;

    /** The seasons whose awards have been given. */
    TSet<int32> SeasonsAwarded;

    TWeakObjectPtr<UPSStatsEngine> Stats;
    FDelegateHandle RecordHandle;

    TWeakObjectPtr<UPSGameIntelligenceSubsystem> Intelligence;
    FDelegateHandle AnsweredHandle;

    /** Trades made since the last CloseWeek. */
    TArray<FPSTradeRecord> TradesThisWeek;

    TWeakObjectPtr<UPSTradeMarket> TradeMarket;
    FDelegateHandle TradeHandle;
};
