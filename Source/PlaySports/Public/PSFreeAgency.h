// PSFreeAgency.h - Epic 87: the free-agency period
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSContractData.h"
#include "PSPlayerAttributes.h"
#include "PSFreeAgency.generated.h"

class UPSContractManager;
class UPSRoster;

/** A team in free agency: the roster its signings join, and whether the player runs it. */
struct FPSFreeAgencyTeam
{
    TWeakObjectPtr<UPSRoster> Roster;
    bool bUserControlled = false;
};

/**
 * UPSFreeAgency runs the free-agency period (Epic 87), day by day. The pool is the record of every
 * unsigned player until he signs; a signing moves him onto his new team's roster (UPSRoster) and
 * his contract into UPSContractManager, the cap's authority, which refuses anything over the cap.
 *
 *  - Opening: each free agent's ask (UPSContractNegotiation) against the league's cap and market.
 *  - Offers: a team bids with a contract offer. The player turns down one below what he would
 *    counter, signs one far above his ask on the spot, and otherwise weighs it.
 *  - Each day (AdvanceDay): every CPU team bids for its needs (roles short of the tuning's roster
 *    target) within its cap space, the best players it can afford first. Then each free agent
 *    with offers counts a day; after DecisionDays he signs the best offer he would at least
 *    counter (his old team's offers carry his morale). Unsigned players' asks then cool, to a
 *    floor.
 *  - The user's team bids with SubmitOffer, under the same rules and timers as the CPU teams.
 *  - Deterministic: no random rolls, so the same pool and offers sign the same way.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSFreeAgency : public UObject
{
    GENERATED_BODY()

public:
    /** The cap authority every signing goes through. Call first. */
    UFUNCTION(BlueprintCallable, Category = "Contracts|FreeAgency")
    void Initialize(UPSContractManager* InContracts);

    /** A team taking part, with the roster its signings join. bUserControlled: the player makes
     *  its offers; the CPU doesn't bid for it. */
    UFUNCTION(BlueprintCallable, Category = "Contracts|FreeAgency")
    void RegisterTeam(FName TeamId, UPSRoster* Roster, bool bUserControlled);

    /** Adds a player to the pool; false when he is already in it or under contract. */
    UFUNCTION(BlueprintCallable, Category = "Contracts|FreeAgency")
    bool AddFreeAgent(const FPlayerAttributes& Player, int32 Age, float Morale, FName PreviousTeamId);

    /** Moves every player on a registered roster who has no contract (his deal expired at the
     *  league-year rollover, or he never had one) into the pool, at his age in AgeByPlayerId (the
     *  tuning's DefaultPlayerAge when missing). Returns how many moved. */
    UFUNCTION(BlueprintCallable, Category = "Contracts|FreeAgency")
    int32 ReleaseUnsignedToPool(const TMap<FName, int32>& AgeByPlayerId);

    /** Opens free agency on day 1: every free agent's ask is set. */
    UFUNCTION(BlueprintCallable, Category = "Contracts|FreeAgency")
    void BeginPeriod();

    /** Offer (its TeamId bidding) to PlayerId. Replaces the team's earlier offer to him. */
    UFUNCTION(BlueprintCallable, Category = "Contracts|FreeAgency")
    FPSFreeAgencyOfferResponse SubmitOffer(FName PlayerId, const FPSContractOffer& Offer);

    UFUNCTION(BlueprintCallable, Category = "Contracts|FreeAgency")
    bool WithdrawOffer(FName TeamId, FName PlayerId);

    /** One day of free agency: CPU bids, decisions, cooling asks. Returns the day's signings. The
     *  period closes after the tuning's FreeAgencyDays. */
    UFUNCTION(BlueprintCallable, Category = "Contracts|FreeAgency")
    TArray<FPSFreeAgentSigning> AdvanceDay();

    /** Advances until the period closes; returns every signing on the way. */
    UFUNCTION(BlueprintCallable, Category = "Contracts|FreeAgency")
    TArray<FPSFreeAgentSigning> RunToEnd();

    UFUNCTION(BlueprintPure, Category = "Contracts|FreeAgency")
    bool IsOpen() const { return bOpen; }

    /** The day it is (1 on opening). */
    UFUNCTION(BlueprintPure, Category = "Contracts|FreeAgency")
    int32 GetDay() const { return Day; }

    UFUNCTION(BlueprintPure, Category = "Contracts|FreeAgency")
    const TArray<FPSFreeAgent>& GetPool() const { return Pool; }

    UFUNCTION(BlueprintPure, Category = "Contracts|FreeAgency")
    bool GetFreeAgent(FName PlayerId, FPSFreeAgent& OutFreeAgent) const;

    /** The roles TeamId's roster is short of its targets in, the most short (as a fraction of the
     *  target) first. */
    UFUNCTION(BlueprintPure, Category = "Contracts|FreeAgency")
    TArray<EPlayerRole> GetTeamNeeds(FName TeamId) const;

    UFUNCTION(BlueprintPure, Category = "Contracts|FreeAgency")
    const TArray<FPSFreeAgentSigning>& GetSignings() const { return Signings; }

private:
    FPSFreeAgent* FindMutable(FName PlayerId);

    /** What the free agent brings to the table today. */
    FPSNegotiationContext MakeContext(const FPSFreeAgent& FreeAgent) const;

    /** The first-year cap hit of Offer as a contract. */
    int32 FirstYearCapHit(FName PlayerId, const FPSContractOffer& Offer) const;

    /** The cap the team has promised in offers still pending, other than to ExceptPlayerId. */
    int32 PendingCommitments(FName TeamId, FName ExceptPlayerId) const;

    /** Signs PoolIndex's free agent to Offer: contract, roster, out of the pool. */
    bool Sign(int32 PoolIndex, const FPSContractOffer& Offer, TArray<FPSFreeAgentSigning>& OutSignings);

    /** How short of its target TeamId's roster is at Role, 0-1, counting pending offers. */
    float GetShortfall(FName TeamId, EPlayerRole Role) const;

    void MakeCpuOffers();
    void DecideOffers(TArray<FPSFreeAgentSigning>& OutSignings);

    UPROPERTY(Transient)
    UPSContractManager* Contracts = nullptr;

    UPROPERTY(Transient)
    TArray<FPSFreeAgent> Pool;

    UPROPERTY(Transient)
    TArray<FPSFreeAgentSigning> Signings;

    /** Teams in registration order (the order CPU teams bid in). */
    TArray<FName> TeamOrder;
    TMap<FName, FPSFreeAgencyTeam> Teams;

    int32 Day = 0;
    bool bOpen = false;
};
