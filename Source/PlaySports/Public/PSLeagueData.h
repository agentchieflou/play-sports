#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSLeagueData.generated.h"

USTRUCT(BlueprintType)
struct FPSTeamInfo : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Division;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString RosterDataTablePath;
};

USTRUCT(BlueprintType)
struct FPSTeamStanding
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Wins = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Losses = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Ties = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 PointsFor = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 PointsAgainst = 0;

    UFUNCTION(BlueprintPure, Category = "League")
    float GetWinPercentage() const
    {
        const int32 TotalGames = Wins + Losses + Ties;
        return TotalGames > 0 ? (Wins + (Ties * 0.5f)) / static_cast<float>(TotalGames) : 0.f;
    }
};

USTRUCT(BlueprintType)
struct FPSWeekMatchup
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 WeekNumber = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName HomeTeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName AwayTeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bPlayed = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 HomeScore = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 AwayScore = 0;
};

USTRUCT(BlueprintType)
struct FPSPlayoffMatchup
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Round = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 HigherSeed = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 LowerSeed = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName HigherSeedTeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName LowerSeedTeamId;
};
