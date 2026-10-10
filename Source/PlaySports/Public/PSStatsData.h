// PSStatsData.h - Epic 92: box scores, season and career lines, the record book
#pragma once

#include "CoreMinimal.h"
#include "PSTelemetryBus.h"
#include "PSStatsData.generated.h"

/** One player's numbers: a game's, a season's or a career's (Epic 92). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSPlayerStatLine
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    FName PlayerId;

    /** The team he played for (the latest one, for a career). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Games = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 PassAttempts = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Completions = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 PassingYards = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 PassingTouchdowns = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 InterceptionsThrown = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 TimesSacked = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 RushAttempts = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 RushingYards = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 RushingTouchdowns = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Targets = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Receptions = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 ReceivingYards = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 ReceivingTouchdowns = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Tackles = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Sacks = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Interceptions = 0;

    /** Adds Other's counts to these (the ids stay; the team becomes Other's when it has one). */
    void Accumulate(const FPSPlayerStatLine& Other);

    /** The count a player category reads; 0 for a team category. */
    int32 GetValue(EPSStatCategory Category) const;
};

/** A team's numbers in one situation: a down, the red zone (Epic 92's splits). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSSplitLine
{
    GENERATED_BODY()

    /** Down1 .. Down4, or RedZone (snapped inside the opponent's 20). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    FName Situation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Plays = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Yards = 0;

    /** Plays that reached the line to gain or scored. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Conversions = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Touchdowns = 0;
};

/** A team's numbers: a game's, a season's or its whole history (Epic 92). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTeamStatLine
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Games = 0;

    /** Points scored and allowed, play by play. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Points = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 PointsAllowed = 0;

    /** Scrimmage plays (not kicks). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Plays = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 TotalYards = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 PassingYards = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 RushingYards = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 FirstDowns = 0;

    /** Interceptions thrown. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Turnovers = 0;

    /** Interceptions made. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Takeaways = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 FieldGoalsMade = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 FieldGoalsAttempted = 0;

    /** Accepted penalties this team committed, and the yards they cost it. Penalty yards are
     *  the team's, never a passer's, rusher's or receiver's: the play they wiped out counts for
     *  nobody. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Penalties = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 PenaltyYards = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    TArray<FPSSplitLine> Splits;

    void Accumulate(const FPSTeamStatLine& Other);

    /** The split for Situation, added when missing. */
    FPSSplitLine& FindOrAddSplit(FName Situation);

    const FPSSplitLine* FindSplit(FName Situation) const;

    /** The count a team category reads; 0 for a player category. */
    int32 GetValue(EPSStatCategory Category) const;
};

/** One game: both teams' lines and every player who did something (Epic 92). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSBoxScore
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Season = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Week = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    FPSTeamStatLine Home;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    FPSTeamStatLine Away;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    TArray<FPSPlayerStatLine> Players;

    /** Plays recorded, kicks included. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 PlayCount = 0;

    /** True once the game is over. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    bool bFinal = false;

    const FPSPlayerStatLine* FindPlayer(FName PlayerId) const
    {
        return Players.FindByPredicate([PlayerId](const FPSPlayerStatLine& Line) { return Line.PlayerId == PlayerId; });
    }
};

/** A finished season's totals, kept once its box scores are let go. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSSeasonStats
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Season = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    TArray<FPSPlayerStatLine> Players;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    TArray<FPSTeamStatLine> Teams;
};

/** A record in the record book. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSRecordEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    EPSStatCategory Category = EPSStatCategory::PassingYards;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    EPSStatScope Scope = EPSStatScope::Game;

    /** A PlayerId, or a TeamId for a team category. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    FName HolderId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Value = 0;

    /** When it was set. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Season = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Week = 0;
};

/** Every statistic the league keeps, as the franchise save holds it (UPSFranchiseSaveGame). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSStatBook
{
    GENERATED_BODY()

    /** The season being played; 0 before the first. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 Season = 0;

    /** This season's box scores. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    TArray<FPSBoxScore> Games;

    /** Past seasons' totals, oldest first. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    TArray<FPSSeasonStats> History;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    TArray<FPSRecordEntry> Records;
};

/** One place on a leaderboard. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSLeaderEntry
{
    GENERATED_BODY()

    /** A PlayerId, or a TeamId for a team category. */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    FName Id;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    FName TeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 Value = 0;

    /** For a single-game leaderboard, the game's week. */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 Week = 0;
};

/** A player's rates, derived from his counts (Epic 92's advanced metrics). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSPlayerMetrics
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float CompletionPercentage = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float YardsPerAttempt = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float TouchdownPercentage = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float InterceptionPercentage = 0.f;

    /** The NFL passer rating, 0 to 158.3. */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float PasserRating = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float YardsPerCarry = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float YardsPerReception = 0.f;

    /** Receptions per target. */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float CatchRate = 0.f;
};

/** A team's rates, derived from its counts and splits. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTeamMetrics
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float YardsPerPlay = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float PointsPerGame = 0.f;

    /** Third-down plays that converted. */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float ThirdDownConversionRate = 0.f;

    /** Red-zone plays that scored a touchdown. */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float RedZoneTouchdownRate = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float FieldGoalPercentage = 0.f;

    /** Takeaways less turnovers. */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 TurnoverMargin = 0;
};
