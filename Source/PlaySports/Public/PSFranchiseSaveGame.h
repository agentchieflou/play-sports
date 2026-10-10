#pragma once

#include "CoreMinimal.h"
#include "PSSaveGame.h"
#include "PSLeagueData.h"
#include "PSSeasonHighlights.h"
#include "PSStaffData.h"
#include "PSContractData.h"
#include "PSStatsData.h"
#include "PSEconomyData.h"
#include "PSLockerRoomData.h"
#include "PSFranchiseSaveGame.generated.h"

/** Persists a UPSFranchiseSeason snapshot (standings, matchups, current week)
 *  through the existing UPSSaveSubsystem (Epic 20; category Franchise). */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSFranchiseSaveGame : public UPSSaveGame
{
    GENERATED_BODY()

public:
    UPSFranchiseSaveGame()
    {
        Category = EPSSaveCategory::Franchise;
    }

    UPROPERTY(BlueprintReadWrite, Category = "Franchise")
    TArray<FPSTeamStanding> Standings;

    UPROPERTY(BlueprintReadWrite, Category = "Franchise")
    TArray<FPSWeekMatchup> Matchups;

    UPROPERTY(BlueprintReadWrite, Category = "Franchise")
    int32 CurrentWeek = 1;

    UPROPERTY(BlueprintReadWrite, Category = "Franchise")
    FName UserControlledTeamId;

    /** Every coach and each team's staff after the latest carousel (Epic 89; UPSStaffManager).
     *  Empty in a save from before coaching staffs: the data file's staffs stand. */
    UPROPERTY(BlueprintReadWrite, Category = "Franchise")
    TArray<FPSCoachDef> Coaches;

    UPROPERTY(BlueprintReadWrite, Category = "Franchise")
    TArray<FPSTeamStaffDef> Staffs;

    /** The league year, its salary cap, every contract, dead money and carried-over cap space
     *  (Epic 87; UPSContractManager). LeagueYear 0 in a save from before contracts. */
    UPROPERTY(BlueprintReadWrite, Category = "Franchise")
    FPSContractLedger ContractLedger;

    /** This season's box scores, past seasons' totals and the record book (Epic 92;
     *  UPSStatsEngine). Season 0 in a save from before statistics. */
    UPROPERTY(BlueprintReadWrite, Category = "Franchise")
    FPSStatBook StatBook;

    /** Every team's ticket price, fans, budget and revenue (Epic 95; UPSOwnerEconomy). No teams
     *  in a save from before owner mode. */
    UPROPERTY(BlueprintReadWrite, Category = "Franchise")
    FPSLeagueEconomy Economy;

    /** Every player's morale and flags, and each unit's lineup (Epic 91; UPSLockerRoom). Empty in
     *  a save from before the locker room. */
    UPROPERTY(BlueprintReadWrite, Category = "Franchise")
    FPSLockerRoomState LockerRoom;

    /** The season's highlights so far, its most important (Epic 42; UPSHighlightSubsystem::
     *  ArchiveForSeason). Track G shows them. Empty in a save from before highlights. */
    UPROPERTY(BlueprintReadWrite, Category = "Franchise")
    TArray<FPSSeasonHighlight> SeasonHighlights;
};
