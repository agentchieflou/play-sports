// PSContractManager.h - Epic 87: every contract in the league and the salary cap over them
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/Function.h"
#include "PSContractData.h"
#include "PSPlayerAttributes.h"
#include "PSContractManager.generated.h"

class UPSFranchiseSaveGame;
class UPSRoster;

/**
 * UPSContractManager is the one authority on contracts and the salary cap (Epic 87). Rosters
 * (UPSRoster) say who is on a team; this says what each player is paid and what the team's cap
 * can bear. Its ledger (FPSContractLedger) persists in the franchise save; the economics are data
 * (Data/contracts.json).
 *
 *  - Contract model: consecutive league years, each a base salary (part of it guaranteed) and a
 *    share of the bonuses paid on the deal. A year's cap hit is its base plus that share; cutting
 *    a player charges what is guaranteed or already paid as dead money.
 *  - Cap engine: a team's cap space is the league year's cap, plus space carried over from last
 *    year, less its contracts' cap hits and its dead money. No signing may take a team over it.
 *    The league-year rollover grows the cap, carries unused space over (up to
 *    MaxCarryoverFraction of the cap), lets deals that ran out expire, and reports teams over the
 *    new cap; EnforceCompliance cuts a CPU team back under it.
 *  - Cap tools: cut (dead money now, or spread over two years), restructure (base salary turned
 *    into a bonus spread over the deal's remaining years) and extension (more years and a new
 *    bonus). Each has a preview that shows its cap consequences without making the move.
 *  - Negotiation: a player's demand and his answer to an offer come from
 *    UPSContractNegotiation, against this league's cap and market.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSContractManager : public UObject
{
    GENERATED_BODY()

public:
    /** Data/contracts.json under the project directory. */
    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion. False, with the
     *  tuning unchanged, on a missing or malformed file. */
    UFUNCTION(BlueprintCallable, Category = "Contracts")
    bool LoadTuningFromJson(const FString& JsonFilePath);

    void SetTuning(const FPSContractTuning& InTuning) { Tuning = InTuning; }

    UFUNCTION(BlueprintPure, Category = "Contracts")
    const FPSContractTuning& GetTuning() const { return Tuning; }

    /** Problems with a tuning, one line each; empty when it is sound. */
    static TArray<FString> ValidateTuning(const FPSContractTuning& InTuning);

    /** A new league: the tuning's first league year and salary cap, and no contracts. */
    UFUNCTION(BlueprintCallable, Category = "Contracts")
    void StartLeague();

    /** Adds TeamId to the league's teams (signing a contract adds its team too). */
    UFUNCTION(BlueprintCallable, Category = "Contracts")
    void RegisterTeam(FName TeamId);

    UFUNCTION(BlueprintPure, Category = "Contracts")
    int32 GetLeagueYear() const { return Ledger.LeagueYear; }

    UFUNCTION(BlueprintPure, Category = "Contracts")
    int32 GetSalaryCap() const { return Ledger.SalaryCap; }

    /** The cap in a league year: this year's, grown at CapGrowthRate a year for a later one. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    int32 GetProjectedSalaryCap(int32 LeagueYear) const;

    UFUNCTION(BlueprintPure, Category = "Contracts")
    const FPSContractLedger& GetLedger() const { return Ledger; }

    // --- Contracts -------------------------------------------------------------------------

    /** PlayerId's contract, or null. */
    const FPSContract* FindContract(FName PlayerId) const;

    UFUNCTION(BlueprintPure, Category = "Contracts")
    bool GetContract(FName PlayerId, FPSContract& OutContract) const;

    UFUNCTION(BlueprintPure, Category = "Contracts")
    TArray<FPSContract> GetTeamContracts(FName TeamId) const;

    /** Offer as a contract for PlayerId starting this league year: flat base salaries, the
     *  signing bonus spread over up to MaxProrationYears, and the guaranteed share (less the
     *  bonus) on the earliest base salaries. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    FPSContract MakeContract(FName PlayerId, const FPSContractOffer& Offer) const;

    /** Whether Contract could be signed now, and if not, why. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    EPSCapResult CanSign(const FPSContract& Contract) const;

    /** Signs Contract when CanSign allows it. */
    UFUNCTION(BlueprintCallable, Category = "Contracts")
    EPSCapResult SignContract(const FPSContract& Contract);

    /** A new franchise's contracts: each of Players (a team's roster) signed at his demand at his
     *  age (GetPlayerAge), their lengths staggered (1 year, 2 years, ...) so they don't all run out
     *  together. A player who doesn't fit under the cap is left unsigned. Returns those signed. */
    UFUNCTION(BlueprintCallable, Category = "Contracts")
    int32 SignRosterAtDemand(FName TeamId, const TArray<FPlayerAttributes>& Players);

    // --- The cap ---------------------------------------------------------------------------

    /** PlayerId's cap hit in LeagueYear; 0 without a contract that year. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    int32 GetCapHit(FName PlayerId, int32 LeagueYear) const;

    UFUNCTION(BlueprintPure, Category = "Contracts")
    int32 GetDeadMoney(FName TeamId, int32 LeagueYear) const;

    /** Space TeamId carried into this league year. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    int32 GetCarryover(FName TeamId) const;

    /** Contracts' cap hits plus dead money for TeamId in LeagueYear. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    int32 GetCapUsed(FName TeamId, int32 LeagueYear) const;

    /** TeamId's cap space this league year; negative when over the cap. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    int32 GetCapSpace(FName TeamId) const;

    /** TeamId's cap space in LeagueYear, against that year's projected cap. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    int32 GetCapSpaceInYear(FName TeamId, int32 LeagueYear) const;

    UFUNCTION(BlueprintPure, Category = "Contracts")
    bool IsCompliant(FName TeamId) const { return GetCapSpace(TeamId) >= 0; }

    UFUNCTION(BlueprintPure, Category = "Contracts")
    TArray<FName> GetTeamsOverCap() const;

    /** The market: the league's teams' average cap space this year, as a fraction of the cap. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    float GetLeagueCapSpaceFraction() const;

    // --- Negotiation -----------------------------------------------------------------------

    /** Player's age for negotiation: his Age when it is above 0, else the tuning's
     *  DefaultPlayerAge (a roster that gives no ages). */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    int32 GetPlayerAge(const FPlayerAttributes& Player) const;

    /** What Player brings to a negotiation in this league: his rating, the market. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    FPSNegotiationContext MakeNegotiationContext(const FPlayerAttributes& Player, int32 Age, float Morale, FName CurrentTeamId) const;

    /** What he asks for under this year's cap. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    FPSContractDemand GetDemand(const FPSNegotiationContext& Context) const;

    /** Offers PlayerId's team an extension (Offer's years added to his deal). He answers as to any
     *  offer; when he accepts, the extension is made. OutPreview shows its cap consequences. */
    UFUNCTION(BlueprintCallable, Category = "Contracts")
    FPSNegotiationResult ProposeExtension(FName PlayerId, const FPSNegotiationContext& Context, const FPSContractOffer& Offer, FPSCapPreview& OutPreview);

    // --- Cap tools -------------------------------------------------------------------------

    /** Cutting PlayerId: his remaining guarantees and bonus shares become dead money, all this
     *  league year, or (bSpreadDeadMoney) this year's share now and the rest next year. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    FPSCapPreview PreviewCut(FName PlayerId, bool bSpreadDeadMoney) const;

    /** Cuts PlayerId as PreviewCut shows. The caller takes him off the roster. */
    UFUNCTION(BlueprintCallable, Category = "Contracts")
    FPSCapPreview CutPlayer(FName PlayerId, bool bSpreadDeadMoney);

    /** Turning up to Amount of this year's base salary (keeping the minimum) into a bonus spread
     *  over his remaining years: this year's cap hit falls, later years' rise. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    FPSCapPreview PreviewRestructure(FName PlayerId, int32 Amount) const;

    UFUNCTION(BlueprintCallable, Category = "Contracts")
    FPSCapPreview RestructureContract(FName PlayerId, int32 Amount);

    /** Adding Extension's years after his deal, at its annual value; its signing bonus spreads
     *  over all his remaining years. The deal may run at most MaxContractYears from now. */
    UFUNCTION(BlueprintPure, Category = "Contracts")
    FPSCapPreview PreviewExtension(FName PlayerId, const FPSContractOffer& Extension) const;

    UFUNCTION(BlueprintCallable, Category = "Contracts")
    FPSCapPreview ExtendContract(FName PlayerId, const FPSContractOffer& Extension);

    // --- League year -----------------------------------------------------------------------

    /** Cuts TeamId's contracts until it is under the cap: each time the one that saves the most
     *  cap per rating point (players Roster doesn't know first), dead money spread. Cut players
     *  leave Roster (when given) and go to OutReleased. Returns the cut PlayerIds. */
    UFUNCTION(BlueprintCallable, Category = "Contracts")
    TArray<FName> EnforceCompliance(FName TeamId, UPSRoster* Roster, TArray<FPlayerAttributes>& OutReleased);

    /** The next league year: unused space carries over, the cap grows, expired deals and past dead
     *  money drop. Returns the expired contracts and the teams over the new cap. */
    UFUNCTION(BlueprintCallable, Category = "Contracts")
    FPSLeagueYearRollover RolloverLeagueYear();

    // --- Persistence -----------------------------------------------------------------------

    /** Writes the ledger into the franchise save. */
    void SaveTo(UPSFranchiseSaveGame* Save) const;

    /** Reads it back; false (keeping the current ledger) when the save has none. */
    bool LoadFrom(const UPSFranchiseSaveGame* Save);

private:
    /** Problems with Contract against the rules and this league year; empty when valid. */
    FString CheckContract(const FPSContract& Contract) const;

    /** Builds a preview of a move: Move applies it to a copy of the ledger (false with a problem
     *  when it can't). The ledger itself is untouched. */
    FPSCapPreview PreviewMove(FName PlayerId, TFunctionRef<bool(FPSContractLedger&, FString&)> Move, FPSContractLedger& OutAfter) const;

    bool ApplyCut(FPSContractLedger& InOutLedger, FName PlayerId, bool bSpreadDeadMoney, FString& OutProblem) const;
    bool ApplyRestructure(FPSContractLedger& InOutLedger, FName PlayerId, int32 Amount, FString& OutProblem) const;
    bool ApplyExtension(FPSContractLedger& InOutLedger, FName PlayerId, const FPSContractOffer& Extension, FString& OutProblem) const;

    UPROPERTY(Transient)
    FPSContractTuning Tuning;

    UPROPERTY(Transient)
    FPSContractLedger Ledger;
};
