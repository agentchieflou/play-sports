#pragma once

#include "CoreMinimal.h"
#include "PSSaveGame.h"
#include "PSLeagueData.h"
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
};
