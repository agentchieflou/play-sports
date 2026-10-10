// PSFranchiseFlow.h - a franchise between games: the week's matches and the season's end
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSStaffData.h"
#include "PSContractData.h"
#include "PSFranchiseFlow.generated.h"

class UPSContractManager;
class UPSFranchiseSeason;
class UPSFreeAgency;
class UPSMatchSetup;
class UPSRoster;
class UPSStaffManager;

/**
 * UPSFranchiseFlow runs a franchise from week to week. It owns no league facts itself: the
 * season (UPSFranchiseSeason) is the authority on the schedule and results, each team's roster
 * (UPSRoster) on its players, the staff manager (UPSStaffManager, Epic 89) on its coaches. The
 * flow puts them together:
 *
 *  - BuildUserMatch: the player's game this week, from the schedule, as a UPSMatchSetup.
 *  - SimulateWeek: the week's unplayed games through the quick sim (UPSQuickSimRunner), each
 *    team playing with its coaching staff's scheme fit (UPSMatchSetup::ApplyStaffs), results
 *    recorded in the season.
 *  - AdvanceWeek: on to the next week; once the last week's games are all played, the season
 *    ends.
 *  - EndSeason: the off-season, once per season. The coaching carousel
 *    (UPSStaffManager::RunCarousel) runs on the final standings; then, with a contract manager
 *    (UPSContractManager, Epic 87), the league year rolls over, CPU teams over the new cap cut
 *    back under it, and free agency (UPSFreeAgency) opens with every player whose deal ran out or
 *    who was cut. The player's team bids there; GetFreeAgency()->AdvanceDay() runs its days.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSFranchiseFlow : public UObject
{
    GENERATED_BODY()

public:
    /** The season to run, the league's coaching staffs and the team the player runs. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    void Initialize(UPSFranchiseSeason* InSeason, UPSStaffManager* InStaffs, FName InUserTeamId);

    /** The league's contracts and cap (Epic 87); the off-season rolls its league year over and
     *  opens free agency. Optional: without one the season ends with the carousel alone. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    void SetContracts(UPSContractManager* InContracts) { Contracts = InContracts; }

    UFUNCTION(BlueprintPure, Category = "Franchise")
    UPSContractManager* GetContracts() const { return Contracts; }

    /** A new franchise's contracts: every team with a roster signs its players at their demands
     *  (UPSContractManager::SignRosterAtDemand). Returns the contracts signed. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    int32 SignLeagueContracts();

    /** The roster TeamId plays its quick-sim games with. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    void SetTeamRoster(FName TeamId, UPSRoster* Roster);

    /** Gives every team in TeamsJsonPath (Data/sample_teams.json) its roster, read through
     *  UPSDataIngestion with a default depth chart. Returns how many teams got one. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    int32 LoadLeagueRosters(const FString& TeamsJsonPath);

    UFUNCTION(BlueprintPure, Category = "Franchise")
    UPSRoster* GetTeamRoster(FName TeamId) const;

    UFUNCTION(BlueprintPure, Category = "Franchise")
    UPSFranchiseSeason* GetSeason() const { return Season; }

    UFUNCTION(BlueprintPure, Category = "Franchise")
    UPSStaffManager* GetStaffManager() const { return Staffs; }

    UFUNCTION(BlueprintPure, Category = "Franchise")
    FName GetUserTeamId() const { return UserTeamId; }

    /** Sets up the player's game this week from the schedule (UPSMatchSetup::InitializeForSeasonGame).
     *  False on a bye week, past the season, or without a setup. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    bool BuildUserMatch(UPSMatchSetup* Setup) const;

    /** Quick-sims this week's unplayed games, the player's own too when bIncludeUserGame, and
     *  records them. A game whose team has no roster is left unplayed. Returns the games played. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    int32 SimulateWeek(bool bIncludeUserGame);

    /** The last week with a game. */
    UFUNCTION(BlueprintPure, Category = "Franchise")
    int32 GetFinalWeek() const;

    /** True once every scheduled game has a result. */
    UFUNCTION(BlueprintPure, Category = "Franchise")
    bool IsRegularSeasonComplete() const;

    /** Moves the season on a week. Past the final week with every game played, the season ends
     *  (EndSeason). Returns true when this call ended the season. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    bool AdvanceWeek();

    /** The off-season: the coaching carousel on the final standings. Runs once, and only once the
     *  regular season is complete; false otherwise. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    bool EndSeason();

    UFUNCTION(BlueprintPure, Category = "Franchise")
    bool HasSeasonEnded() const { return bSeasonEnded; }

    /** The free agency the season's end opened; null before then or without contracts. */
    UFUNCTION(BlueprintPure, Category = "Franchise")
    UPSFreeAgency* GetFreeAgency() const { return FreeAgency; }

    /** What the season-end league-year rollover did (empty before then). */
    UFUNCTION(BlueprintPure, Category = "Franchise")
    const FPSLeagueYearRollover& GetLastRollover() const { return LastRollover; }

    /** What the carousel did at the season's end, in order (empty before then). */
    UFUNCTION(BlueprintPure, Category = "Franchise")
    const TArray<FPSCarouselEvent>& GetCarouselEvents() const { return CarouselEvents; }

private:
    UPROPERTY(Transient)
    UPSFranchiseSeason* Season = nullptr;

    UPROPERTY(Transient)
    UPSStaffManager* Staffs = nullptr;

    UPROPERTY(Transient)
    TMap<FName, UPSRoster*> RostersByTeam;

    UPROPERTY(Transient)
    UPSContractManager* Contracts = nullptr;

    UPROPERTY(Transient)
    UPSFreeAgency* FreeAgency = nullptr;

    UPROPERTY(Transient)
    FPSLeagueYearRollover LastRollover;

    UPROPERTY(Transient)
    TArray<FPSCarouselEvent> CarouselEvents;

    FName UserTeamId;
    bool bSeasonEnded = false;
};
