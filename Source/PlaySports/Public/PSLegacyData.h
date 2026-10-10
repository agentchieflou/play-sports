// PSLegacyData.h - Epic 94: the league's history, retirements and the hall of fame
#pragma once

#include "CoreMinimal.h"
#include "PSLeagueData.h"
#include "PSPlayerAttributes.h"
#include "PSStatsData.h"
#include "PSTelemetryBus.h"
#include "PSLegacyData.generated.h"

/** A career total that alone makes a hall of famer. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSHallOfFameThreshold
{
    GENERATED_BODY()

    /** A player category (not TeamPoints or TeamTotalYards). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    EPSStatCategory Category = EPSStatCategory::PassingYards;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    int32 CareerValue = 1;
};

/** Who goes into the hall of fame (Data/legacy.json). A retired player's hall score is his best
 *  category: the highest of his career totals over their thresholds' values, so reaching any one
 *  threshold is a score of 1. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSHallOfFameTuning
{
    GENERATED_BODY()

    /** Seasons a player waits after retiring before he can be voted in. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    int32 WaitSeasons = 3;

    /** Seasons he must have played. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    int32 MinSeasons = 4;

    /** The hall score that makes him a hall of famer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    float InductionScore = 1.f;

    /** The most voted in after a season, the best scores first; the rest wait. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    int32 MaxInducteesPerSeason = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    TArray<FPSHallOfFameThreshold> Thresholds;
};

/** The league's history (Data/legacy.json, Epic 94). The defaults equal the file's, but for the
 *  arrays. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSLegacyTuning
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    FPSHallOfFameTuning HallOfFame;

    /** The player categories whose season leader each season's archive keeps. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    TArray<EPSStatCategory> LeaderCategories;
};

/** A season's leader in one category. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSSeasonLeader
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    EPSStatCategory Category = EPSStatCategory::PassingYards;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    FName PlayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    int32 Value = 0;
};

/** One finished season, as the archive keeps it. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSSeasonArchive
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    int32 Season = 0;

    /** The team that finished first (no playoffs are played yet: the best regular season). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    FName ChampionTeamId;

    /** The final standings, in finishing order. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    TArray<FPSTeamStanding> Standings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    TArray<FPSSeasonLeader> Leaders;

    /** The players who retired after it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    TArray<FName> Retired;

    /** The hall of fame class voted in after it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    TArray<FName> Inducted;
};

/** A retired player: his career, kept once he has left the league. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSRetiredPlayer
{
    GENERATED_BODY()

    /** His ratings when he retired. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    FPlayerAttributes Player;

    /** The team he retired from, and the one he played the most games for. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    FName LastTeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    FName PrimaryTeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    int32 RetiredAfterSeason = 0;

    /** "Age 36", "Declining", ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    FString Reason;

    /** His career totals (Epic 92), and the seasons he played. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    FPSPlayerStatLine Career;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    int32 Seasons = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    float HallScore = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    bool bHallOfFame = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    int32 InductedAfterSeason = 0;
};

/** The league's past, as the franchise save keeps it (UPSFranchiseSaveGame). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSLeagueHistoryState
{
    GENERATED_BODY()

    /** Every finished season, oldest first. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    TArray<FPSSeasonArchive> Seasons;

    /** Every retired player, in the order they retired. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legacy")
    TArray<FPSRetiredPlayer> Retired;
};

/** One of a team's seasons in its history. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSFranchiseSeasonRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Legacy")
    int32 Season = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Legacy")
    FPSTeamStanding Record;

    /** Where it finished (1 = first). */
    UPROPERTY(BlueprintReadOnly, Category = "Legacy")
    int32 Finish = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Legacy")
    bool bChampion = false;
};

/** A franchise's history: its seasons, titles and legends. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSFranchiseHistory
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Legacy")
    FName TeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Legacy")
    TArray<FPSFranchiseSeasonRecord> Seasons;

    UPROPERTY(BlueprintReadOnly, Category = "Legacy")
    int32 Championships = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Legacy")
    int32 Wins = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Legacy")
    int32 Losses = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Legacy")
    int32 Ties = 0;

    /** Its hall of famers (those who played the most games for it), the best hall score first. */
    UPROPERTY(BlueprintReadOnly, Category = "Legacy")
    TArray<FPSRetiredPlayer> Legends;
};
