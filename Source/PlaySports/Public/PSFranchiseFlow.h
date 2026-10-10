// PSFranchiseFlow.h - a franchise between games: the week's matches and the season's end
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSStaffData.h"
#include "PSContractData.h"
#include "PSEconomyData.h"
#include "PSLockerRoomData.h"
#include "PSTrainingData.h"
#include "PSFranchiseFlow.generated.h"

class UPSContractManager;
class UPSFranchiseSeason;
class UPSFreeAgency;
class UPSLockerRoom;
class UPSOwnerEconomy;
class UPSMatchSetup;
class UPSRoster;
class UPSStaffManager;
class UPSStatsEngine;
class UPSWeeklyPreparation;

/**
 * UPSFranchiseFlow runs a franchise from week to week. It owns no league facts itself: the
 * season (UPSFranchiseSeason) is the authority on the schedule and results, each team's roster
 * (UPSRoster) on its players, the staff manager (UPSStaffManager, Epic 89) on its coaches. The
 * flow puts them together:
 *
 *  - BuildUserMatch: the player's game this week, from the schedule, as a UPSMatchSetup.
 *  - PrepareWeek: with weekly preparation (Epic 90), every team's practice week before its game:
 *    development, fatigue, practice injuries and a gameplan for its opponent.
 *  - SimulateWeek: the week's unplayed games through the quick sim (UPSQuickSimRunner), each
 *    team playing with its coaching staff's scheme fit (UPSMatchSetup::ApplyStaffs), results
 *    recorded in the season and, with a statistics engine (Epic 92), every play in its box
 *    score.
 *  - AdvanceWeek: on to the next week; once the last week's games are all played, the season
 *    ends.
 *  - EndSeason: the off-season, once per season. The coaching carousel
 *    (UPSStaffManager::RunCarousel) runs on the final standings, the statistics engine (Epic 92)
 *    archives the season, the owner economy (Epic 95) closes the books; then, with a contract manager
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

    /** The league's statistics (Epic 92): every simulated game's plays are recorded in it, and the
     *  season is archived when it ends. A book with no season yet starts at the contract
     *  manager's league year (set contracts first), else season 1. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    void SetStats(UPSStatsEngine* InStats);

    UFUNCTION(BlueprintPure, Category = "Franchise")
    UPSStatsEngine* GetStats() const { return Stats; }

    /** The league's business (Epic 95): every simulated game's gate and fans, and the books at
     *  the season's end (with the contract manager's payroll when there is one). */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    void SetEconomy(UPSOwnerEconomy* InEconomy) { Economy = InEconomy; }

    UFUNCTION(BlueprintPure, Category = "Franchise")
    UPSOwnerEconomy* GetEconomy() const { return Economy; }

    /** The league's locker rooms (Epic 91): every simulated game's lineups make chemistry and its
     *  players play at their morale and cohesion (a holdout sits); each week's AdvanceWeek
     *  re-evaluates morale; at season end released players take their morale into free agency
     *  and the new league year's holdouts begin. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    void SetLockerRoom(UPSLockerRoom* InLockerRoom) { LockerRoom = InLockerRoom; }

    UFUNCTION(BlueprintPure, Category = "Franchise")
    UPSLockerRoom* GetLockerRoom() const { return LockerRoom; }

    /** Re-evaluates every team's locker room with its record from the standings; bNewLeagueYear
     *  lets holdouts begin. Returns (and keeps) what happened. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    TArray<FPSLockerRoomEvent> EvaluateLockerRooms(bool bNewLeagueYear);

    /** Everything that has happened in the locker rooms, in order. */
    UFUNCTION(BlueprintPure, Category = "Franchise")
    const TArray<FPSLockerRoomEvent>& GetLockerRoomEvents() const { return LockerRoomEvents; }

    /** The league's practice weeks (Epic 90): each week's practice runs before its games
     *  (PrepareWeek); in every simulated game the injured sit and everyone plays at his freshness,
     *  with his team's gameplan against that opponent; each game tires the players who played; the
     *  season's end heals everyone. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    void SetPreparation(UPSWeeklyPreparation* InPreparation) { Preparation = InPreparation; }

    UFUNCTION(BlueprintPure, Category = "Franchise")
    UPSWeeklyPreparation* GetPreparation() const { return Preparation; }

    /** This week's practice for every team with a roster (UPSWeeklyPreparation::PrepareTeam),
     *  against its opponent this week, once a week; SimulateWeek runs it when it hasn't run. Set
     *  the player's allocation and focus before. Returns (and keeps) what happened. */
    UFUNCTION(BlueprintCallable, Category = "Franchise")
    TArray<FPSTrainingEvent> PrepareWeek();

    /** Everything that has happened in practice, in order. */
    UFUNCTION(BlueprintPure, Category = "Franchise")
    const TArray<FPSTrainingEvent>& GetTrainingEvents() const { return TrainingEvents; }

    /** Every team's books for the season that just ended (empty before then). */
    UFUNCTION(BlueprintPure, Category = "Franchise")
    const TArray<FPSEconomySeasonReport>& GetEconomyReports() const { return EconomyReports; }

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
    UPSStatsEngine* Stats = nullptr;

    UPROPERTY(Transient)
    UPSOwnerEconomy* Economy = nullptr;

    UPROPERTY(Transient)
    TArray<FPSEconomySeasonReport> EconomyReports;

    UPROPERTY(Transient)
    UPSLockerRoom* LockerRoom = nullptr;

    UPROPERTY(Transient)
    TArray<FPSLockerRoomEvent> LockerRoomEvents;

    UPROPERTY(Transient)
    UPSWeeklyPreparation* Preparation = nullptr;

    UPROPERTY(Transient)
    TArray<FPSTrainingEvent> TrainingEvents;

    UPROPERTY(Transient)
    FPSLeagueYearRollover LastRollover;

    UPROPERTY(Transient)
    TArray<FPSCarouselEvent> CarouselEvents;

    FName UserTeamId;
    bool bSeasonEnded = false;
};
