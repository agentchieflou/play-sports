// PSTradeData.h - Epic 88: trade logic and the league market
#pragma once

#include "CoreMinimal.h"
#include "PSTradeData.generated.h"

/** Where a team's season has it in the market (UPSTradeMarket::GetTeamStance): a contender buys
 *  for now, a rebuilder sells for later. Its standings decide. */
UENUM(BlueprintType)
enum class EPSTradeStance : uint8
{
    Contender,
    Balanced,
    Rebuilder
};

/** How a stance values the seasons ahead and the draft. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTradeStanceTuning
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades")
    EPSTradeStance Stance = EPSTradeStance::Balanced;

    /** Each later season of a player's value weighs this much of the season before it: low, the
     *  team lives for now; high, it builds for later. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades")
    float FutureYearWeight = 0.7f;

    /** Draft picks are worth this many times the chart to it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades")
    float PickMultiplier = 1.f;
};

/**
 * The trade market's tuning (Data/trades.json, Epic 88). Values are trade points: the first
 * overall pick is worth PickRoundValues[0]. Ratings are the contract market's (contracts.json's
 * ReplacementRating to EliteRating), positions weigh as its PositionMarkets' TopCapFraction does,
 * and players age along Epic 94's role curves (legacy.json). Defaults equal the file's.
 */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTradeTuning
{
    GENERATED_BODY()

    /** A season of a player at the elite rating, at the position the market pays most. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Players")
    float MaxPlayerValue = 1500.f;

    /** A season's value grows as his place between the replacement and elite ratings to this power. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Players")
    float TalentCurveExponent = 2.f;

    /** A position weighs its top-of-market cap share against the highest, to this power. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Players")
    float RoleWeightExponent = 0.6f;

    /** Seasons of a player's future his value looks at, this one first. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Players")
    int32 ValueHorizonYears = 3;

    /** A season after his contract runs out counts this share: he may walk. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Players")
    float UncontrolledYearWeight = 0.5f;

    /** Each season his worth (the contract market's demand for him) beats his base salary by the
     *  whole cap would add this many points. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Players")
    float SurplusValuePerCap = 1000.f;

    /** A team short at his position (contracts.json's RosterTarget) values him up to this share more. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Players")
    float NeedValueWeight = 0.3f;

    /** His own team values a player who asked to be traded (Epic 91) at this share. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Players")
    float TradeRequestDiscount = 0.8f;

    /** A team that has played MinGamesForStance games is a contender at this win percentage or
     *  better, a rebuilder at RebuilderWinPercentage or worse. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Stances")
    float ContenderWinPercentage = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Stances")
    float RebuilderWinPercentage = 0.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Stances")
    int32 MinGamesForStance = 3;

    /** One per EPSTradeStance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Stances")
    TArray<FPSTradeStanceTuning> Stances;

    /** The pick chart: the value of each round's first pick, falling evenly (in ratio) to the
     *  next round's first; the last round falls to LastPickValue. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Picks")
    TArray<float> PickRoundValues;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Picks")
    float LastPickValue = 2.f;

    /** Drafts whose picks trade: the coming one and the ones after, this many in all. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Picks")
    int32 TradablePickYears = 2;

    /** A pick a draft further off is worth this share per year. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Picks")
    float FuturePickDiscount = 0.8f;

    /** A team accepts what it values at this many times what it gives ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Answers")
    float AcceptRatio = 1.05f;

    /** ... counters from this share, and rejects below it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Answers")
    float CounterRatio = 0.85f;

    /** Guardrail: by the league's neutral values, neither side may give more than this share more
     *  than it gets (the gap over the larger side), once the gap is LopsidedMinGap points or more. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Guardrails")
    float MaxValueImbalance = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Guardrails")
    float LopsidedMinGap = 40.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Guardrails")
    int32 MaxAssetsPerSide = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Guardrails")
    int32 MaxTradesPerTeamPerSeason = 3;

    /** A traded player can't be traded again for this many weeks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Guardrails")
    int32 RetradeCooldownWeeks = 4;

    /** No trade leaves a team with fewer players at a position than this (or fewer than it had). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Guardrails")
    int32 MinPlayersAtRole = 1;

    /** The trade deadline: after this share of the regular season's weeks (rounded); trades open
     *  again once the season is over. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Deadline")
    float DeadlineFraction = 0.6f;

    /** The deadline's urgency (0-1) builds over this many weeks before it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Deadline")
    int32 DeadlineRampWeeks = 3;

    /** The chance a CPU team looks for a trade in a week; a contender's or a rebuilder's rises to
     *  DeadlineTradeChance with the urgency. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Deadline")
    float BaseTradeChance = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Deadline")
    float DeadlineTradeChance = 0.6f;

    /** At full urgency a contender accepts this much less (off AcceptRatio) ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Deadline")
    float DeadlineBuyerPremium = 0.1f;

    /** ... and a rebuilder's season now weighs this much less against the seasons ahead. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|Deadline")
    float DeadlineSellerDiscount = 0.3f;

    /** A CPU team goes after another team's player only when he is worth this many points more
     *  to it than to his team. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|AI")
    float MinTargetGain = 25.f;

    /** The players a CPU team prices a package for when it looks for a trade, the biggest gains
     *  first; and, of its own assets, the cheapest and the biggest it considers paying with. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|AI")
    int32 TargetsPerAttempt = 8;

    /** CPU offers the player's team may get a week. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|AI")
    int32 MaxOffersToUserPerWeek = 1;

    /** The CPU's weekly draws (with the league year and week), so a week trades the same way every time. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades|AI")
    int32 RandomSeed = 8801;

    FPSTradeTuning()
    {
        const EPSTradeStance DefaultStances[] = { EPSTradeStance::Contender, EPSTradeStance::Balanced, EPSTradeStance::Rebuilder };
        const float DefaultFutureWeights[] = { 0.4f, 0.7f, 0.9f };
        const float DefaultPickMultipliers[] = { 0.8f, 1.f, 1.25f };
        for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(DefaultStances)); ++Index)
        {
            FPSTradeStanceTuning& Entry = Stances.AddDefaulted_GetRef();
            Entry.Stance = DefaultStances[Index];
            Entry.FutureYearWeight = DefaultFutureWeights[Index];
            Entry.PickMultiplier = DefaultPickMultipliers[Index];
        }
        PickRoundValues = { 1000.f, 190.f, 85.f, 40.f, 18.f, 8.f, 4.f };
    }

    const FPSTradeStanceTuning* FindStance(EPSTradeStance Stance) const
    {
        return Stances.FindByPredicate([Stance](const FPSTradeStanceTuning& Entry) { return Entry.Stance == Stance; });
    }
};

UENUM(BlueprintType)
enum class EPSTradeAssetKind : uint8
{
    Player,
    DraftPick
};

/** Something a team gives in a trade: a rostered player, or a draft pick (the draft's year, the
 *  round and the team it first belonged to). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTradeAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades")
    EPSTradeAssetKind Kind = EPSTradeAssetKind::Player;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades")
    FName PlayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades")
    int32 DraftYear = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades")
    int32 Round = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades")
    FName OriginalTeamId;

    static FPSTradeAsset MakePlayer(FName InPlayerId)
    {
        FPSTradeAsset Asset;
        Asset.Kind = EPSTradeAssetKind::Player;
        Asset.PlayerId = InPlayerId;
        return Asset;
    }

    static FPSTradeAsset MakePick(int32 InDraftYear, int32 InRound, FName InOriginalTeamId)
    {
        FPSTradeAsset Asset;
        Asset.Kind = EPSTradeAssetKind::DraftPick;
        Asset.DraftYear = InDraftYear;
        Asset.Round = InRound;
        Asset.OriginalTeamId = InOriginalTeamId;
        return Asset;
    }

    bool IsSameAs(const FPSTradeAsset& Other) const
    {
        return Kind == Other.Kind && (Kind == EPSTradeAssetKind::Player
            ? PlayerId == Other.PlayerId
            : DraftYear == Other.DraftYear && Round == Other.Round && OriginalTeamId == Other.OriginalTeamId);
    }
};

/** A trade put to a team: FromTeamId proposes, ToTeamId answers. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTradeProposal
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades")
    FName FromTeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades")
    FName ToTeamId;

    /** What FromTeamId gives. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades")
    TArray<FPSTradeAsset> FromAssets;

    /** What ToTeamId gives. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trades")
    TArray<FPSTradeAsset> ToAssets;
};

UENUM(BlueprintType)
enum class EPSTradeResponse : uint8
{
    Accept,
    Counter,
    Reject
};

/** Why a trade was answered as it was. Each has a Trade.Reason.<Name> line in Data/ui_text.csv. */
UENUM(BlueprintType)
enum class EPSTradeReason : uint8
{
    /** It gets what it wants for what it gives. */
    GoodValue,
    /** It values what it would get under what it would give. */
    NotEnoughValue,
    /** It would do it with one more of the proposer's assets ... */
    CounterAddAsset,
    /** ... or keeping one of its own. */
    CounterKeepAsset,
    /** The league's guardrail: one side would give far more than it gets. */
    Lopsided,
    /** Past the deadline, before the season is over. */
    WindowClosed,
    UnknownTeam,
    SameTeam,
    EmptySide,
    TooManyAssets,
    DuplicateAsset,
    /** A side doesn't hold an asset it would give (or a pick isn't tradable). */
    AssetNotOwned,
    OverCap,
    RosterTooThin,
    TradeLimit,
    /** A player traded too recently to move again. */
    Cooldown,
    /** The player's team answers for itself (GetIncomingOffers). */
    UserTeamDecides
};

/** One asset of a trade as each side values it. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTradeAssetValue
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    FPSTradeAsset Asset;

    /** To the team giving it, and to the team getting it. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float ValueToGiver = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float ValueToReceiver = 0.f;

    /** The league's view: no team's stance, need or grudge. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float NeutralValue = 0.f;

    /** "BEARS_QB_01 (QB, 31)", "2027 round 2 (Hawks)". */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    FString Label;
};

/** A team's answer to a trade, with its reasons and the values behind them. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTradeEvaluation
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    EPSTradeResponse Response = EPSTradeResponse::Reject;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    TArray<EPSTradeReason> Reasons;

    /** The reasons as the string table words them. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    TArray<FString> ReasonTexts;

    /** The answering team's values: what it would get and what it would give. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float ValueIn = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float ValueOut = 0.f;

    /** The proposing team's. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float ProposerValueIn = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float ProposerValueOut = 0.f;

    /** The league's neutral values of what each side gives, and their gap over the larger (the
     *  guardrail's imbalance, 0 = even). */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float NeutralFrom = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float NeutralTo = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float Imbalance = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    TArray<FPSTradeAssetValue> FromAssets;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    TArray<FPSTradeAssetValue> ToAssets;

    /** With a Counter: the trade the answering team would accept. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    bool bHasCounter = false;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    FPSTradeProposal Counter;

    /** The trade's id once it was made; 0 when it wasn't. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 TradeId = 0;
};

/** A trade made. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTradeRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 TradeId = 0;

    /** The contracts' league year it was made in, and the week (0 in the off-season). */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 LeagueYear = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 Week = 0;

    /** The deadline's urgency when it was made (0-1). */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float DeadlineUrgency = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    FPSTradeProposal Proposal;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    EPSTradeStance FromStance = EPSTradeStance::Balanced;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    EPSTradeStance ToStance = EPSTradeStance::Balanced;

    /** Neutral values of what each side gave, and their imbalance. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float NeutralFrom = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float NeutralTo = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float Imbalance = 0.f;

    /** Each side's own view: what it got and what it gave. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float FromValueIn = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float FromValueOut = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float ToValueIn = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float ToValueOut = 0.f;

    /** The player who headlined it: the most valuable player in it (None for picks only). */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    FName HeadlinePlayerId;

    /** The team that got him. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    FName HeadlineTeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    bool bUserTeam = false;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    FString Description;

    bool Involves(FName TeamId) const { return !TeamId.IsNone() && (Proposal.FromTeamId == TeamId || Proposal.ToTeamId == TeamId); }
};

/** A CPU team's offer to the player's team, open for the week it was made. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTradeOffer
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 OfferId = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 LeagueYear = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 Week = 0;

    /** The CPU team proposes (FromTeamId); the player's team answers. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    FPSTradeProposal Proposal;

    /** How the market values it for the player's team, as the CPU would. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    FPSTradeEvaluation Evaluation;
};

/** The league's trade activity, for tuning (Epic 88.4). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTradeTelemetry
{
    GENERATED_BODY()

    /** Trades proposed to a team that answers (the CPU's and the player's), and its answers. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 Proposals = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 Accepted = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 Countered = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 Rejected = 0;

    /** Rejections by the lopsided-trade guardrail. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 GuardrailBlocks = 0;

    /** Rejections by reason, indexed by EPSTradeReason. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    TArray<int32> RejectionsByReason;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 Trades = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 PlayersMoved = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 PicksMoved = 0;

    /** Trades in the deadline's ramp (urgency above 0). */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 DeadlineTrades = 0;

    /** Trades in which a contender got more player value than it gave ... */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 ContenderBuys = 0;

    /** ... and in which a rebuilder got more pick value than it gave. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 RebuilderSells = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    int32 OffersToUser = 0;

    /** The trades' neutral imbalances: summed (for the mean) and the largest. */
    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float SumImbalance = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Trades")
    float MaxImbalance = 0.f;
};

/** What the market keeps in the franchise save. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTradeMarketState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Trades")
    TArray<FPSTradeRecord> History;

    UPROPERTY(BlueprintReadWrite, Category = "Trades")
    FPSTradeTelemetry Telemetry;

    UPROPERTY(BlueprintReadWrite, Category = "Trades")
    TArray<FPSTradeOffer> IncomingOffers;

    UPROPERTY(BlueprintReadWrite, Category = "Trades")
    int32 NextTradeId = 1;

    UPROPERTY(BlueprintReadWrite, Category = "Trades")
    int32 NextOfferId = 1;
};

/** A trade was made. */
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTradeCompletedMC, const FPSTradeRecord& /* Trade */);
