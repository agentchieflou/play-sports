// PSTradeMarket.h - Epic 88: trade logic and the league market
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSPlayerAttributes.h"
#include "PSTradeData.h"
#include "PSTradeMarket.generated.h"

class UPSContractManager;
class UPSDraft;
class UPSFranchiseSaveGame;
class UPSFranchiseSeason;
class UPSLockerRoom;
class UPSPlayerAging;
class UPSRoster;
class UPSTelemetryBus;

/**
 * UPSTradeMarket is where the league's teams trade players and draft picks (Epic 88), tuned in
 * Data/trades.json. It owns no player, contract or pick: a trade moves each player between the
 * teams' rosters (UPSRoster), his contract with him (UPSContractManager::TradeContracts) and each
 * pick to its new holder (UPSDraft::TransferPick). It keeps only its own facts: the trades made,
 * the CPU's offers to the player's team and the league's trade telemetry, saved in the franchise
 * save. UPSFranchiseFlow connects it (SetTradeMarket) and runs its week (RunWeek).
 *
 *  - Value (88.1): a player's value is the seasons ahead of him (ValueHorizonYears), each his
 *    rating's worth at his position (the contract market's rating anchors and top-of-market cap
 *    shares) as he will be then (Epic 94's aging curve for his role), a season past his contract
 *    counting UncontrolledYearWeight; plus his contract's surplus, what the market would pay him
 *    over his base salary each season he is signed. Each team weighs the seasons ahead by its
 *    stance (a contender lives for now, a rebuilder builds), values a player at a position it is
 *    short at more (NeedValueWeight), and one who asked out (Epic 91) less. A pick's value is the
 *    chart at its projected place (its original team's standing, surer as the season goes;
 *    exact once the draft's order is set), less for a later draft, times the team's stance.
 *  - Proposals (88.2): a team answers a trade by what it gets against what it gives: it accepts
 *    at AcceptRatio, counters from CounterRatio (asking one more of the proposer's assets, or to
 *    keep one of its own), and rejects below, each with its reasons. CPU teams make proposals
 *    (GenerateProposals): another team's player worth more to them than to his team, paid for
 *    with what that team values most and they value least.
 *  - The deadline (88.3): trades close after the deadline week and open again once the season is
 *    over. As the deadline nears, contenders and rebuilders look for trades more often, contenders
 *    pay a premium and rebuilders weigh this season less: contenders buy, rebuilders sell.
 *  - Guardrails and telemetry (88.4): no trade whose neutral values (no stance, need or grudge)
 *    differ by more than MaxValueImbalance; at most MaxAssetsPerSide a side and
 *    MaxTradesPerTeamPerSeason a team; no player traded again within RetradeCooldownWeeks; no team
 *    left under MinPlayersAtRole at a position or over the cap. Every trade is published on the bus
 *    (Trade) and through OnTradeCompleted, and counted in the telemetry (GetTelemetry).
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSTradeMarket : public UObject
{
    GENERATED_BODY()

public:
    /** Data/trades.json under the project directory. */
    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion. False, with the
     *  tuning unchanged, on a missing or malformed file. */
    UFUNCTION(BlueprintCallable, Category = "Trades")
    bool LoadTuningFromJson(const FString& JsonFilePath);

    void SetTuning(const FPSTradeTuning& InTuning) { Tuning = InTuning; }

    UFUNCTION(BlueprintPure, Category = "Trades")
    const FPSTradeTuning& GetTuning() const { return Tuning; }

    /** Problems with a tuning, one line each; empty when it is sound. */
    static TArray<FString> ValidateTuning(const FPSTradeTuning& InTuning);

    // --- The league ----------------------------------------------------------------------------

    /** The season (its standings and weeks), the contracts and the draft the market trades in,
     *  and the player's team. A new season ends the off-season. Contracts and the draft may be
     *  null (no cap, no picks). Keeps the market's own state. */
    UFUNCTION(BlueprintCallable, Category = "Trades")
    void Connect(UPSFranchiseSeason* InSeason, UPSContractManager* InContracts, UPSDraft* InDraft, FName InUserTeamId);

    /** TeamId trades with Roster (null removes it). */
    UFUNCTION(BlueprintCallable, Category = "Trades")
    void RegisterTeam(FName TeamId, UPSRoster* Roster);

    /** The locker room (Epic 91): a player who asked to be traded is worth less to his team. */
    void SetLockerRoom(const UPSLockerRoom* InLockerRoom);

    /** The aging curves (Epic 94) a player's seasons ahead follow; without, Core 19's defaults. */
    void SetPlayerAging(const UPSPlayerAging* InAging);

    /** Publishes every trade on Bus (Trade). */
    UFUNCTION(BlueprintCallable, Category = "Trades")
    void BindToBus(UPSTelemetryBus* Bus);

    UFUNCTION(BlueprintCallable, Category = "Trades")
    void UnbindFromBus();

    /** The regular season is over: trades open until the next season begins. */
    UFUNCTION(BlueprintCallable, Category = "Trades")
    void BeginOffseason();

    UFUNCTION(BlueprintPure, Category = "Trades")
    FName GetUserTeamId() const { return UserTeamId; }

    // --- The calendar (88.3) -------------------------------------------------------------------

    /** The last week trades are open in the regular season: DeadlineFraction of its weeks. */
    UFUNCTION(BlueprintPure, Category = "Trades")
    int32 GetDeadlineWeek() const;

    UFUNCTION(BlueprintPure, Category = "Trades")
    bool IsOffseason() const;

    /** Open up to the deadline week and again in the off-season. */
    UFUNCTION(BlueprintPure, Category = "Trades")
    bool IsWindowOpen() const;

    /** 0 until DeadlineRampWeeks before the deadline, rising to 1 in its week; 0 past it. */
    UFUNCTION(BlueprintPure, Category = "Trades")
    float GetDeadlineUrgency() const;

    /** The draft whose picks are traded first: the prepared one not yet complete, else the next. */
    UFUNCTION(BlueprintPure, Category = "Trades")
    int32 GetNextDraftYear() const;

    /** The contracts' league year (0 without contracts). */
    UFUNCTION(BlueprintPure, Category = "Trades")
    int32 GetLeagueYear() const;

    /** TeamId's stance from its standing: a contender, a rebuilder, or balanced (before
     *  MinGamesForStance games too). */
    UFUNCTION(BlueprintPure, Category = "Trades")
    EPSTradeStance GetTeamStance(FName TeamId) const;

    // --- Value (88.1) --------------------------------------------------------------------------

    /** PlayerId's value to TeamId: to keep, for his own team; to get, for any other; the league's
     *  neutral value for None. 0 for a player on no registered roster. */
    UFUNCTION(BlueprintPure, Category = "Trades")
    float ValuePlayer(FName TeamId, FName PlayerId) const;

    /** A pick's value to TeamId (neutral for None). */
    UFUNCTION(BlueprintPure, Category = "Trades")
    float ValuePick(FName TeamId, int32 DraftYear, int32 Round, FName OriginalTeamId) const;

    /** The chart: a pick in Round at RoundPosition (0 its first pick, towards 1 its last). */
    UFUNCTION(BlueprintPure, Category = "Trades")
    float GetPickChartValue(int32 Round, float RoundPosition) const;

    /** Where in its round a pick is expected to fall, 0 (first) to under 1 (last): from the order
     *  once its draft's is set; for the coming draft from its original team's standing (worst
     *  first), blended from the middle as the season is played; the middle for a later draft. */
    UFUNCTION(BlueprintPure, Category = "Trades")
    float GetProjectedRoundPosition(int32 DraftYear, int32 Round, FName OriginalTeamId) const;

    /** Asset's values to the team giving it (GiverTeamId), to the team getting it, and neutral. */
    UFUNCTION(BlueprintPure, Category = "Trades")
    FPSTradeAssetValue ValueAsset(const FPSTradeAsset& Asset, FName GiverTeamId, FName ReceiverTeamId) const;

    /** What TeamId can trade: its rostered players, then the picks it holds in the tradable drafts. */
    UFUNCTION(BlueprintPure, Category = "Trades")
    TArray<FPSTradeAsset> GetTeamAssets(FName TeamId) const;

    /** Asset as a line: "BEARS_WR_02 (WR, 30)", "2027 round 2 (Hawks)". */
    UFUNCTION(BlueprintPure, Category = "Trades")
    FString DescribeAsset(const FPSTradeAsset& Asset) const;

    // --- Proposals (88.2) ----------------------------------------------------------------------

    /** How ToTeamId answers Proposal, as the CPU does, with its reasons and values; nothing
     *  changes. The guardrails and rules come first. A Counter carries the trade it would accept. */
    UFUNCTION(BlueprintPure, Category = "Trades")
    FPSTradeEvaluation EvaluateTrade(const FPSTradeProposal& Proposal) const;

    /** Puts Proposal to its CPU team (the player's team answers its offers through
     *  GetIncomingOffers): accepted, the trade is made at once. Counted in the telemetry. */
    UFUNCTION(BlueprintCallable, Category = "Trades")
    FPSTradeEvaluation ProposeTrade(const FPSTradeProposal& Proposal);

    /** Up to Max trades BuyerTeamId would propose this week, the best first: for another team's
     *  player worth more to it than to his team, the package his team values enough. With
     *  bToUserTeam, only for the player's team's players; otherwise never them. Open to the
     *  player's team too, as suggestions. */
    UFUNCTION(BlueprintPure, Category = "Trades")
    TArray<FPSTradeProposal> GenerateProposals(FName BuyerTeamId, int32 Max, bool bToUserTeam) const;

    /** The CPU's offers to the player's team this week. */
    UFUNCTION(BlueprintPure, Category = "Trades")
    const TArray<FPSTradeOffer>& GetIncomingOffers() const { return State.IncomingOffers; }

    /** The player's team takes offer OfferId: made when it still stands (the rules and guardrails
     *  checked again). The offer is gone either way. */
    UFUNCTION(BlueprintCallable, Category = "Trades")
    FPSTradeEvaluation AcceptOffer(int32 OfferId);

    UFUNCTION(BlueprintCallable, Category = "Trades")
    bool DeclineOffer(int32 OfferId);

    // --- The week (88.3) -----------------------------------------------------------------------

    /** The CPU teams' trading this week, while the window is open in the regular season: each
     *  contender and rebuilder looks for a trade at its deadline chance (a balanced team at the
     *  base chance), proposes its best and makes it when the other team accepts (or it takes the
     *  counter); CPU offers to the player's team are made. Last week's offers expire. Returns the
     *  trades made. */
    UFUNCTION(BlueprintCallable, Category = "Trades")
    TArray<FPSTradeRecord> RunWeek();

    // --- Telemetry (88.4) ----------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Trades")
    const TArray<FPSTradeRecord>& GetHistory() const { return State.History; }

    UFUNCTION(BlueprintPure, Category = "Trades")
    const FPSTradeTelemetry& GetTelemetry() const { return State.Telemetry; }

    /** Trades TeamId has made in this league year. */
    UFUNCTION(BlueprintPure, Category = "Trades")
    int32 CountTeamTrades(FName TeamId) const;

    /** The telemetry as lines, for a log or a tuning report. */
    UFUNCTION(BlueprintPure, Category = "Trades")
    TArray<FString> DescribeTelemetry() const;

    /** A trade was made. */
    FPSTradeCompletedMC OnTradeCompleted;

    // --- Persistence ---------------------------------------------------------------------------

    /** Writes the trades, offers and telemetry into the franchise save. */
    void SaveTo(UPSFranchiseSaveGame* Save) const;

    /** Reads them back; false (keeping the current state) when the save has none. */
    bool LoadFrom(const UPSFranchiseSaveGame* Save);

    const FPSTradeMarketState& GetState() const { return State; }

private:
    /** The team whose roster has PlayerId, and his row; null when none does. */
    const FPlayerAttributes* FindRosteredPlayer(FName PlayerId, FName& OutTeamId) const;

    UPSRoster* FindRoster(FName TeamId) const;

    /** The week trades are recorded in: the season's, 0 in the off-season. */
    int32 GetMarketWeek() const;

    /** The regular season's last week (0 without a season). */
    int32 GetFinalWeek() const;

    /** Player's value to ViewerId (None: neutral); OwnerId is his team. */
    float ValuePlayerFor(const FPlayerAttributes& Player, FName OwnerId, FName ViewerId) const;

    /** A pick's value to ViewerId (None: neutral). */
    float ValuePickFor(int32 DraftYear, int32 Round, FName OriginalTeamId, FName ViewerId) const;

    /** A season of a player rated Rating at Role. */
    float SeasonTalent(float Rating, EPlayerRole Role) const;

    /** How short TeamId is at Role with Delta players more (or fewer), 0-1. */
    float GetNeed(FName TeamId, EPlayerRole Role, int32 Delta) const;

    /** The ratio TeamId accepts at: AcceptRatio, less a contender's deadline premium. */
    float GetAcceptRatio(FName TeamId) const;

    /** Whether TeamId holds Asset and can trade it now. */
    bool HoldsAsset(FName TeamId, const FPSTradeAsset& Asset) const;

    /** Whether PlayerId was traded too recently to move again. */
    bool IsCoolingDown(FName PlayerId) const;

    /** The evaluation; with bAllowCounter, a near miss looks for a counter. With bAnswerForUser,
     *  the player's team answers as the CPU would (its offers, and the packages built for it). */
    FPSTradeEvaluation EvaluateInternal(const FPSTradeProposal& Proposal, bool bAllowCounter, bool bAnswerForUser) const;

    /** The counter ToTeamId would accept, from a near miss: the trade, why, and the asset it
     *  adds or keeps. False when none would do. */
    bool FindCounter(const FPSTradeProposal& Proposal, const FPSTradeEvaluation& Evaluation, bool bAnswerForUser,
        FPSTradeProposal& OutCounter, EPSTradeReason& OutReason, FString& OutDetail) const;

    /** The cheapest package (to the buyer) of BuyerTeamId's assets that SellerTeamId would accept
     *  for Target, when the buyer still likes the trade. */
    bool BuildPackage(FName BuyerTeamId, FName SellerTeamId, const FPSTradeAsset& Target, FPSTradeProposal& OutProposal) const;

    /** Makes the trade: rosters, contracts and picks move; recorded, counted and announced. */
    FPSTradeRecord ExecuteTrade(const FPSTradeProposal& Proposal, const FPSTradeEvaluation& Evaluation);

    /** Adds Reason (and its text) to Evaluation. */
    void AddReason(FPSTradeEvaluation& Evaluation, EPSTradeReason Reason, FName TeamId, const FString& Detail) const;

    /** Counts an answer in the telemetry. */
    void CountAnswer(const FPSTradeEvaluation& Evaluation);

    UPROPERTY(Transient)
    FPSTradeTuning Tuning;

    UPROPERTY(Transient)
    FPSTradeMarketState State;

    UPROPERTY(Transient)
    UPSFranchiseSeason* Season = nullptr;

    UPROPERTY(Transient)
    UPSContractManager* Contracts = nullptr;

    UPROPERTY(Transient)
    UPSDraft* Draft = nullptr;

    UPROPERTY(Transient)
    TMap<FName, UPSRoster*> Rosters;

    TWeakObjectPtr<const UPSLockerRoom> LockerRoom;
    TWeakObjectPtr<const UPSPlayerAging> Aging;
    TWeakObjectPtr<UPSTelemetryBus> BoundBus;

    FName UserTeamId;
    bool bOffseason = false;
};
