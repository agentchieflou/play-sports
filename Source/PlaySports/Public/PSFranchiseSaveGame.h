#pragma once

#include "CoreMinimal.h"
#include "PSSaveGame.h"
#include "PSLeagueData.h"
#include "PSStaffData.h"
#include "PSContractData.h"
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
};
