// PSDraft.h - Epic 86: the draft, scouting and the combine
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSDraftData.h"
#include "PSLeagueGeneratorData.h"
#include "PSPlayerAttributes.h"
#include "PSDraft.generated.h"

class UPSContractManager;
class UPSFranchiseSaveGame;
class UPSFreeAgency;
class UPSRoster;

/**
 * UPSDraft is the one authority on a year's draft (Epic 86), tuned in Data/draft.json: its class,
 * what each team's scouts know, and the picks. Every team, the player's or the CPU's, sees only
 * what it has scouted, and the CPU's choices are open to the player too (AutoScout, AutoPick).
 *
 *  - The class: the league generator's draft-class mode (Epic 122) makes the prospects: ratings,
 *    ages and DNA (Epic 79). Each prospect's true grade (the contract market's overall rating) is
 *    hidden behind a public projection with an error of its own: wider for one who skipped the
 *    combine for a pro day, and a big swing either way for the class's busts and booms.
 *  - The combine and pro days: each drill reads a true rating with a little noise. Combine results
 *    are public and make the projection surer; a pro day's are seen only by the teams that scout
 *    the prospect.
 *  - Scouting: each team's budget of reports grows with its scouting funding (the owner economy's
 *    index, Epic 95). Every report reads the prospect's true grade with noise, and now and then
 *    misleads by much more. A team's estimate weighs the projection and its reports by how sure
 *    each is, so its range narrows with every report.
 *  - The draft: rounds in the order the caller gives (the worst team first). The team on the clock
 *    takes the prospect its board values most, its estimate plus its need at his role; the
 *    player's team picks for itself (MakePick) or as the CPU would (AutoPick).
 *  - Rookies: each pick joins his team's roster with his true ratings and signs a rookie-scale
 *    contract with the contract manager (Epic 87). When the draft ends, the undrafted go to free
 *    agency if it is open.
 *
 * Picks change hands through Epic 88's trades (UPSTradeMarket): the draft is the authority on who
 * holds each one (TransferPick, GetPickOwner), this year's and later years', and a traded pick
 * takes its place in the order with it. The draft persists in the franchise save.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSDraft : public UObject
{
    GENERATED_BODY()

public:
    /** Data/draft.json under the project directory. */
    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion. False, with the
     *  tuning unchanged, on a missing or malformed file. */
    UFUNCTION(BlueprintCallable, Category = "Draft")
    bool LoadTuningFromJson(const FString& JsonFilePath);

    void SetTuning(const FPSDraftTuning& InTuning) { Tuning = InTuning; }

    UFUNCTION(BlueprintPure, Category = "Draft")
    const FPSDraftTuning& GetTuning() const { return Tuning; }

    /** Problems with a tuning, one line each; empty when it is sound. */
    static TArray<FString> ValidateTuning(const FPSDraftTuning& InTuning);

    // --- The class and scouting ---------------------------------------------------------------

    /** Class's prospects to scout: each one's true grade, combine or pro day, measurables and
     *  public projection, all drawn from Seed. Clears any earlier draft and every team's scouting.
     *  False for an empty class. */
    UFUNCTION(BlueprintCallable, Category = "Draft")
    bool PrepareClass(const FPSDraftClass& Class, int32 Seed);

    /** TeamId joins the draft: its picks go onto Roster. A team new to this class gets its scouting
     *  budget, PointsPerSeason times the funding multiplier of ScoutingFundingIndex; a team already
     *  in it (after a load) keeps what it has. */
    UFUNCTION(BlueprintCallable, Category = "Draft")
    void RegisterTeam(FName TeamId, UPSRoster* Roster, bool bUserControlled, float ScoutingFundingIndex);

    /** What a team's scouting funding (its index against the league's average) multiplies its
     *  budget by. */
    UFUNCTION(BlueprintPure, Category = "Draft")
    float GetFundingMultiplier(float FundingIndex) const;

    /** TeamId files a report on PlayerId, for ReportCost of its points. False without the points,
     *  for a prospect not in the class or already drafted, or once the draft has ended. */
    UFUNCTION(BlueprintCallable, Category = "Draft")
    bool Scout(FName TeamId, FName PlayerId);

    /** TeamId spends the points it has left as the CPU does: evenly over the AIScoutTargets
     *  best-projected prospects still undrafted. Returns the reports filed. */
    UFUNCTION(BlueprintCallable, Category = "Draft")
    int32 AutoScout(FName TeamId);

    /** PlayerId as TeamId sees him (an empty view for a prospect not in the class). */
    UFUNCTION(BlueprintPure, Category = "Draft")
    FPSProspectView GetProspectView(FName TeamId, FName PlayerId) const;

    /** TeamId's board: the undrafted prospects by its estimate, best first. */
    UFUNCTION(BlueprintPure, Category = "Draft")
    TArray<FPSProspectView> GetBoard(FName TeamId) const;

    UFUNCTION(BlueprintPure, Category = "Draft")
    bool GetTeamScouting(FName TeamId, FPSTeamScouting& OutTeam) const;

    // --- The draft -----------------------------------------------------------------------------

    /** The draft opens: NumRounds rounds in FirstRoundOrder's order (each team once, the worst
     *  first), as long as prospects last. CPU teams spend the points they have left (AutoScout).
     *  InContracts sign the rookies and InFreeAgency takes the undrafted at the end (either may be
     *  null). False without a class, a registered team in the order, or once it has opened. */
    UFUNCTION(BlueprintCallable, Category = "Draft")
    bool BeginDraft(const TArray<FName>& FirstRoundOrder, UPSContractManager* InContracts, UPSFreeAgency* InFreeAgency);

    UFUNCTION(BlueprintPure, Category = "Draft")
    bool IsOpen() const { return State.bOpen; }

    UFUNCTION(BlueprintPure, Category = "Draft")
    bool IsComplete() const { return State.bComplete; }

    /** The team on the clock; None when the draft isn't open. */
    UFUNCTION(BlueprintPure, Category = "Draft")
    FName GetTeamOnTheClock() const;

    /** The overall number of the pick on the clock (1 = the first); 0 when none is. */
    UFUNCTION(BlueprintPure, Category = "Draft")
    int32 GetCurrentPick() const;

    /** The team on the clock takes PlayerId. False, with nothing changed, for a prospect not in the
     *  class or already taken, or with the draft closed. */
    UFUNCTION(BlueprintCallable, Category = "Draft")
    bool MakePick(FName PlayerId);

    /** The team on the clock takes the prospect its board values most (GetPickValue). False with the
     *  draft closed. */
    UFUNCTION(BlueprintCallable, Category = "Draft")
    bool AutoPick();

    /** CPU teams pick until the player's team is on the clock or the draft ends. Returns the picks. */
    UFUNCTION(BlueprintCallable, Category = "Draft")
    TArray<FPSDraftPick> AdvanceDraft();

    /** Every pick left, the player's made as the CPU would. Returns the picks. */
    UFUNCTION(BlueprintCallable, Category = "Draft")
    TArray<FPSDraftPick> RunToEnd();

    /** What TeamId's board makes of PlayerId: its estimate plus NeedWeight times its need at his
     *  role. */
    UFUNCTION(BlueprintPure, Category = "Draft")
    float GetPickValue(FName TeamId, FName PlayerId) const;

    /** TeamId's need at Role, 0-1: how far its roster is short of the contract market's
     *  RosterTarget (0 without a contract manager). */
    UFUNCTION(BlueprintPure, Category = "Draft")
    float GetNeed(FName TeamId, EPlayerRole Role) const;

    /** The rookie scale's annual value for the Overall-th pick of NumPicks, falling to
     *  MinimumSalary at the last. */
    UFUNCTION(BlueprintPure, Category = "Draft")
    int32 GetRookieSalary(int32 Overall, int32 NumPicks, int32 MinimumSalary) const;

    /** The share of the Overall-th pick's contract that is guaranteed. */
    UFUNCTION(BlueprintPure, Category = "Draft")
    float GetRookieGuarantee(int32 Overall, int32 NumPicks) const;

    UFUNCTION(BlueprintPure, Category = "Draft")
    const TArray<FPSDraftPick>& GetPicks() const { return State.Picks; }

    // --- Who holds which pick (Epic 88: trades) --------------------------------------------

    /** The team holding OriginalTeamId's pick in Round of DraftYear's draft: the team it was
     *  traded to, else OriginalTeamId. */
    UFUNCTION(BlueprintPure, Category = "Draft")
    FName GetPickOwner(int32 DraftYear, int32 Round, FName OriginalTeamId) const;

    /** Whether that pick is still to be made: a round of NumRounds, in this class's draft or a
     *  later one, not yet made (nor cut off by a class too small for every pick). */
    UFUNCTION(BlueprintPure, Category = "Draft")
    bool IsPickAvailable(int32 DraftYear, int32 Round, FName OriginalTeamId) const;

    /** Gives that pick to NewOwnerTeamId; in the open draft its place in the order goes with it.
     *  False, with nothing changed, for a pick not available or no new owner. */
    UFUNCTION(BlueprintCallable, Category = "Draft")
    bool TransferPick(int32 DraftYear, int32 Round, FName OriginalTeamId, FName NewOwnerTeamId);

    /** The picks TeamId holds in DraftYear's draft and can still make: its own not traded away,
     *  then those traded to it. */
    UFUNCTION(BlueprintPure, Category = "Draft")
    TArray<FPSDraftPickRight> GetTeamPicks(FName TeamId, int32 DraftYear) const;

    /** That pick's overall number (1 = the first) once its draft's order is set; 0 before then. */
    UFUNCTION(BlueprintPure, Category = "Draft")
    int32 GetPickSlot(int32 DraftYear, int32 Round, FName OriginalTeamId) const;

    /** Picks in a round of the set order (the teams in it); 0 before the order is set. */
    UFUNCTION(BlueprintPure, Category = "Draft")
    int32 GetRoundSize() const;

    /** The draft so far, a line a pick. */
    UFUNCTION(BlueprintPure, Category = "Draft")
    TArray<FString> DescribeDraft() const;

    UFUNCTION(BlueprintPure, Category = "Draft")
    const FPSDraftState& GetState() const { return State; }

    /** Writes the draft into the franchise save. */
    void SaveTo(UPSFranchiseSaveGame* Save) const;

    /** Reads it back; false (keeping the current one) when the save has none. Teams register again
     *  (RegisterTeam keeps their scouting) to give their picks a roster. */
    bool LoadFrom(const UPSFranchiseSaveGame* Save);

private:
    FPSProspect* FindMutableProspect(FName PlayerId);
    const FPSProspect* FindProspect(FName PlayerId) const;
    FPSTeamScouting* FindMutableTeam(FName TeamId);
    const FPSTeamScouting* FindTeam(FName TeamId) const;

    /** TeamId's estimate of Prospect and its uncertainty: the projection and its reports, weighed by
     *  how sure each is. */
    void EstimateGrade(FName TeamId, const FPSProspect& Prospect, float& OutEstimate, float& OutUncertainty) const;

    /** The place in the set order of OriginalTeamId's pick in Round of DraftYear's draft;
     *  INDEX_NONE before the order is set or for a pick not in it. */
    int32 FindPickIndex(int32 DraftYear, int32 Round, FName OriginalTeamId) const;

    /** The prospect TeamId's board values most; None when nobody is left. */
    FName ChooseProspect(FName TeamId) const;

    /** Makes the pick on the clock: PlayerId joins its team and signs; the draft ends after the last. */
    void TakePick(FName PlayerId);

    /** The undrafted go to free agency when it is open. */
    void FinishDraft();

    UPROPERTY(Transient)
    FPSDraftTuning Tuning;

    UPROPERTY(Transient)
    FPSDraftState State;

    UPROPERTY(Transient)
    TMap<FName, UPSRoster*> Rosters;

    UPROPERTY(Transient)
    UPSContractManager* Contracts = nullptr;

    UPROPERTY(Transient)
    UPSFreeAgency* FreeAgency = nullptr;
};
