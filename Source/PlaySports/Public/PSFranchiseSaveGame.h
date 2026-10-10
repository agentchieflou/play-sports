#pragma once

#include "CoreMinimal.h"
#include "PSSaveGame.h"
#include "PSLeagueData.h"
#include "PSSeasonHighlights.h"
#include "PSStaffData.h"
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

    /** The season's highlights so far, its most important (Epic 42; UPSHighlightSubsystem::
     *  ArchiveForSeason). Track G shows them. Empty in a save from before highlights. */
    UPROPERTY(BlueprintReadWrite, Category = "Franchise")
    TArray<FPSSeasonHighlight> SeasonHighlights;
};
