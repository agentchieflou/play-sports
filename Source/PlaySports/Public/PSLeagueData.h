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

    /** Short on-screen name, e.g. "HAW" (Epic 101 team select). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Abbreviation;

    /** Team colors as "#RRGGBB" (Epic 101 team select; Track L's identity generator fills
     *  these at scale). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString PrimaryColor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString SecondaryColor;

    /** Soft object path of the team logo texture; empty until an editor session imports
     *  logos (the abbreviation stands in). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString LogoPath;
};

/** Top-level league configuration (season length, bye weeks, playoff field size).
 *  Not a DataTable row -- loaded as a single JSON object by
 *  UPSDataIngestion::LoadLeagueConfigFromJson (Epic 21). */
USTRUCT(BlueprintType)
struct FPSLeagueConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString LeagueName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 NumWeeks = 17;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<int32> ByeWeekNumbers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 NumPlayoffTeams = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString TeamsDataTablePath;
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
