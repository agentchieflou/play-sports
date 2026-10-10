// PSContractData.h - Epic 87: contracts, the salary cap, negotiation and free agency
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSContractData.generated.h"

// Money throughout is in thousands of dollars (int32): a 255,000 cap is $255 million.

/** What a role is worth on the open market, and how many of it a front office wants. */
USTRUCT(BlueprintType)
struct FPSPositionMarket
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    EPlayerRole Role = EPlayerRole::Quarterback;

    /** An elite player at the role asks this fraction of the salary cap a year. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    float TopCapFraction = 0.1f;

    /** Players of the role a team wants on its roster; fewer is a need free agency fills. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 RosterTarget = 2;
};

/** The league's economics and its negotiators (Data/contracts.json, Epic 87). */
USTRUCT(BlueprintType)
struct FPSContractTuning
{
    GENERATED_BODY()

    /** The league year a new franchise starts in. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Cap")
    int32 FirstLeagueYear = 2026;

    /** The first league year's salary cap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Cap")
    int32 SalaryCap = 255000;

    /** How much the cap grows at each league-year rollover (0.05 = 5%). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Cap")
    float CapGrowthRate = 0.05f;

    /** The least base salary a contract year may pay. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Cap")
    int32 MinimumSalary = 900;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Cap")
    int32 MaxContractYears = 5;

    /** A bonus spreads over at most this many contract years on the cap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Cap")
    int32 MaxProrationYears = 5;

    /** Unused cap space a team carries into the next league year, at most this fraction of the cap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Cap")
    float MaxCarryoverFraction = 0.1f;

    /** A player rated this (UPSContractNegotiation::RatePlayer) asks the minimum ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Demand")
    float ReplacementRating = 65.f;

    /** ... and one rated this asks his role's top of the market. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Demand")
    float EliteRating = 99.f;

    /** The demand curve between them: above 1, only the best players ask near the top. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Demand")
    float DemandCurveExponent = 3.f;

    /** Up to this age a player asks for the longest deal; past it, fewer years ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Demand")
    int32 PrimeAge = 26;

    /** ... this many fewer per year older. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Demand")
    float YearsLostPerYearPastPrime = 0.5f;

    /** Past this age the market pays less ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Demand")
    int32 DeclineAge = 30;

    /** ... this fraction less per year ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Demand")
    float AgeDiscountPerYear = 0.12f;

    /** ... down to this fraction of his value. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Demand")
    float MinAgeMultiplier = 0.3f;

    /** The guaranteed share of a deal a replacement player asks, and an elite one. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Demand")
    float MinGuaranteeFraction = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Demand")
    float MaxGuaranteeFraction = 0.6f;

    /** The market: when the league's teams have more cap space (as a fraction of the cap) than
     *  NeutralCapSpaceFraction, demands rise by MarketSpaceWeight times the difference, at most
     *  MaxMarketAdjustment either way. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Demand")
    float MarketSpaceWeight = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Demand")
    float NeutralCapSpaceFraction = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Demand")
    float MaxMarketAdjustment = 0.15f;

    /** Morale (0-1, 0.5 neutral; Epic 91 supplies it) and his own team: a happy player values its
     *  offers up to this much more, an unhappy one this much less. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Offer")
    float MoraleLoyaltyWeight = 0.15f;

    /** How much an offer's guaranteed share above (or below) his ask raises (or lowers) its value. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Offer")
    float GuaranteeValueWeight = 0.5f;

    /** An offer's value falls this fraction for each year it differs from the years he asks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Offer")
    float YearsMismatchPenalty = 0.05f;

    /** He accepts an offer worth this fraction of his ask ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Offer")
    float AcceptRatio = 1.f;

    /** ... counters one worth at least this, and rejects anything less. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|Offer")
    float WalkAwayRatio = 0.85f;

    /** Free agency lasts this many days. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|FreeAgency")
    int32 FreeAgencyDays = 7;

    /** A free agent weighs his offers this many days, then signs the best one he would counter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|FreeAgency")
    int32 DecisionDays = 2;

    /** An offer worth this fraction of his ask is signed on the spot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|FreeAgency")
    float InstantAcceptRatio = 1.15f;

    /** An unsigned free agent's ask falls this fraction a day ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|FreeAgency")
    float DemandDecayPerDay = 0.05f;

    /** ... to no less than this fraction of his opening ask. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|FreeAgency")
    float DemandFloorFraction = 0.6f;

    /** A CPU team bids this fraction of a player's ask, plus AINeedPremium times how short of
     *  its roster target the role is (0-1). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|FreeAgency")
    float AIBidRatio = 0.9f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|FreeAgency")
    float AINeedPremium = 0.15f;

    /** A CPU team keeps this fraction of the cap free when it bids. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|FreeAgency")
    float AICapCushionFraction = 0.05f;

    /** New offers a CPU team makes a day. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|FreeAgency")
    int32 AIOffersPerDay = 1;

    /** The age of a player whose FPlayerAttributes::Age is 0 (unknown). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts|FreeAgency")
    int32 DefaultPlayerAge = 27;

    /** One entry per EPlayerRole. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    TArray<FPSPositionMarket> PositionMarkets;

    const FPSPositionMarket* FindMarket(EPlayerRole Role) const
    {
        return PositionMarkets.FindByPredicate([Role](const FPSPositionMarket& Market) { return Market.Role == Role; });
    }
};

/** One league year of a contract. Its cap hit is BaseSalary plus ProratedBonus. */
USTRUCT(BlueprintType)
struct FPSContractYear
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 LeagueYear = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 BaseSalary = 0;

    /** The part of BaseSalary owed even if he is cut: dead money then. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 GuaranteedSalary = 0;

    /** This year's share of bonuses already paid (signing, restructure, extension): dead money if he is cut. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 ProratedBonus = 0;

    int32 GetCapHit() const { return BaseSalary + ProratedBonus; }
};

/** A player's deal with the team that pays him (Epic 87). UPSContractManager holds every one. */
USTRUCT(BlueprintType)
struct FPSContract
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    FName PlayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    FName TeamId;

    /** Bonus money paid on it so far, in all. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 BonusPaid = 0;

    /** Consecutive league years, earliest first. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    TArray<FPSContractYear> Years;

    const FPSContractYear* FindYear(int32 LeagueYear) const
    {
        return Years.FindByPredicate([LeagueYear](const FPSContractYear& Year) { return Year.LeagueYear == LeagueYear; });
    }

    int32 GetLastYear() const { return Years.Num() > 0 ? Years.Last().LeagueYear : 0; }
};

/** Cap charged to a team for a player it no longer pays to play. */
USTRUCT(BlueprintType)
struct FPSDeadMoneyEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    FName PlayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 LeagueYear = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 Amount = 0;
};

/** Cap space a team carried into this league year. */
USTRUCT(BlueprintType)
struct FPSCapCarryover
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 Amount = 0;
};

/** Everything the cap engine knows, as the franchise save keeps it (UPSFranchiseSaveGame). */
USTRUCT(BlueprintType)
struct FPSContractLedger
{
    GENERATED_BODY()

    /** 0 until a league starts. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 LeagueYear = 0;

    /** This league year's cap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 SalaryCap = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    TArray<FName> TeamIds;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    TArray<FPSContract> Contracts;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    TArray<FPSDeadMoneyEntry> DeadMoney;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    TArray<FPSCapCarryover> Carryover;
};

/** A team's offer to a player: Years at AnnualValue a year, SigningBonus of it paid up front,
 *  and GuaranteedFraction of the whole guaranteed (the bonus counts toward it). */
USTRUCT(BlueprintType)
struct FPSContractOffer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 Years = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 AnnualValue = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 SigningBonus = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    float GuaranteedFraction = 0.f;
};

/** What the player's side brings to a negotiation. */
USTRUCT(BlueprintType)
struct FPSNegotiationContext
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    EPlayerRole Role = EPlayerRole::Quarterback;

    /** How good he is, 0-100 (UPSContractNegotiation::RatePlayer; Epic 92's stats may weigh in later). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    float Rating = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 Age = 27;

    /** 0 (miserable) to 1 (delighted); 0.5 is neutral. Epic 91's morale model supplies it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    float Morale = 0.5f;

    /** The team he plays for, or played for last; its offers carry his morale. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    FName CurrentTeamId;

    /** The league's average cap space as a fraction of the cap: the market. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    float MarketCapSpaceFraction = 0.1f;
};

/** What a player asks for. */
USTRUCT(BlueprintType)
struct FPSContractDemand
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 Years = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 AnnualValue = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    float GuaranteedFraction = 0.f;

    /** Below this a year he walks away. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contracts")
    int32 WalkAwayValue = 0;
};

UENUM(BlueprintType)
enum class EPSNegotiationResponse : uint8
{
    Accept,
    Counter,
    Reject
};

/** A player's answer to an offer. */
USTRUCT(BlueprintType)
struct FPSNegotiationResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    EPSNegotiationResponse Response = EPSNegotiationResponse::Reject;

    /** The offer's value to him over his ask (1 = exactly his ask). */
    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    float ValueRatio = 0.f;

    /** His ask: the counter-offer. */
    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FPSContractDemand Demand;

    /** Why, for the negotiation screen: "Wants 4 years", "Below his ask", ... */
    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FString Reason;
};

UENUM(BlueprintType)
enum class EPSCapResult : uint8
{
    Ok,
    /** The contract breaks a rule: its years, salaries or guarantees. */
    InvalidContract,
    /** The player is already under contract. */
    AlreadySigned,
    /** Signing it would take the team over the cap. */
    OverCap
};

/** What a cut, restructure or extension does to a team's cap, before (and as) it happens. */
USTRUCT(BlueprintType)
struct FPSCapPreview
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    bool bValid = false;

    /** Why the move can't be made, when it can't. */
    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FString Problem;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FName PlayerId;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FName TeamId;

    /** What the player costs the cap this league year, before and after (a cut: his dead money). */
    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    int32 PlayerCapHitBefore = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    int32 PlayerCapHitAfter = 0;

    /** Dead money the move charges this league year and next. */
    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    int32 DeadMoneyThisYear = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    int32 DeadMoneyNextYear = 0;

    /** The team's cap space this league year and next, before and after. */
    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    int32 CapSpaceBefore = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    int32 CapSpaceAfter = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    int32 NextYearCapSpaceBefore = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    int32 NextYearCapSpaceAfter = 0;

    /** The team is under the cap after the move. */
    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    bool bCompliantAfter = false;
};

/** A contract that ran out at a league-year rollover: the player is a free agent. */
USTRUCT(BlueprintType)
struct FPSExpiredContract
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FName PlayerId;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FName TeamId;
};

/** What a league-year rollover did. */
USTRUCT(BlueprintType)
struct FPSLeagueYearRollover
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    int32 LeagueYear = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    int32 SalaryCap = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    TArray<FPSExpiredContract> Expired;

    /** Teams over the new year's cap: they must cut or restructure before they can sign. */
    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    TArray<FName> TeamsOverCap;
};

/** An offer a free agent is weighing. */
USTRUCT(BlueprintType)
struct FPSPendingOffer
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FPSContractOffer Offer;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    int32 DayMade = 0;
};

/** A player in the free-agent pool: the pool is his record until he signs. */
USTRUCT(BlueprintType)
struct FPSFreeAgent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FPlayerAttributes Player;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    int32 Age = 27;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    float Morale = 0.5f;

    /** The team he last played for; NAME_None for none. */
    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FName PreviousTeamId;

    /** His ask now, and when free agency opened. */
    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FPSContractDemand Demand;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FPSContractDemand OpeningDemand;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    TArray<FPSPendingOffer> Offers;

    /** Days he has had an offer to weigh. */
    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    int32 DaysConsidering = 0;
};

UENUM(BlueprintType)
enum class EPSFreeAgencyOfferResult : uint8
{
    /** He is weighing it. */
    Pending,
    /** He signed it on the spot. */
    Signed,
    /** Below what he would consider. */
    Rejected,
    /** The team can't fit its first year under the cap. */
    OverCap,
    /** Free agency is closed, the player isn't in the pool, or the team isn't in the league. */
    Unavailable
};

USTRUCT(BlueprintType)
struct FPSFreeAgencyOfferResponse
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    EPSFreeAgencyOfferResult Result = EPSFreeAgencyOfferResult::Unavailable;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FPSNegotiationResult Evaluation;
};

/** A free agent signed. */
USTRUCT(BlueprintType)
struct FPSFreeAgentSigning
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FName PlayerId;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FName TeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    int32 Day = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Contracts")
    FPSContractOffer Offer;
};
