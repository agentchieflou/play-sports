// PSContractNegotiation.h - Epic 87: what a player asks for, and how he answers an offer
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSContractData.h"
#include "PSPlayerAttributes.h"
#include "PSContractNegotiation.generated.h"

/**
 * The player's side of a contract talk (Epic 87). Pure functions of the tuning
 * (Data/contracts.json), the salary cap and what the player brings (FPSNegotiationContext):
 *
 *  - Performance: his rating sets his place between ReplacementRating (the minimum salary) and
 *    EliteRating (his role's top of the market, a fraction of the cap) along a curve.
 *  - Age: past PrimeAge he asks for fewer years; past DeclineAge the market pays less.
 *  - Market: when the league's teams have more cap space than usual, demands rise (and fall
 *    when it is tight), within MaxMarketAdjustment.
 *  - Morale (Epic 91 supplies it): his own team's offers are worth more to a happy player and
 *    less to an unhappy one.
 *
 * An offer's value to him weighs its annual value by its guarantees and its years against his
 * ask: at AcceptRatio he accepts, at WalkAwayRatio he counters with his ask, below that he walks.
 */
UCLASS()
class PLAYSPORTS_API UPSContractNegotiation : public UObject
{
    GENERATED_BODY()

public:
    /** A player's rating, 0-100: his average skill attribute (Speed, Agility, Strength,
     *  Acceleration, Awareness), as team select rates rosters. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    static float RatePlayer(const FPlayerAttributes& Player);

    /** The market multiplier for a league whose teams have MarketCapSpaceFraction of the cap free. */
    static float GetMarketMultiplier(const FPSContractTuning& Tuning, float MarketCapSpaceFraction);

    /** What he asks for under a cap of SalaryCap. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    static FPSContractDemand ComputeDemand(const FPSContractTuning& Tuning, int32 SalaryCap, const FPSNegotiationContext& Context);

    /** What Offer is worth to him over Demand (1 = his ask): its annual value, more for guarantees
     *  above his ask, less per year it differs from his ask, and his morale on his own team's. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    static float ValueOffer(const FPSContractTuning& Tuning, const FPSContractDemand& Demand, const FPSNegotiationContext& Context, const FPSContractOffer& Offer);

    /** His answer to Offer: accept, counter with Demand, or reject. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    static FPSNegotiationResult EvaluateOffer(const FPSContractTuning& Tuning, const FPSContractDemand& Demand, const FPSNegotiationContext& Context, const FPSContractOffer& Offer);
};
