// PSNarrativeTypes.h - Epic 93: the league's storylines, its weekly news and its awards
#pragma once

#include "CoreMinimal.h"
#include "PSTelemetryBus.h"
#include "PSNarrativeTypes.generated.h"

/** What a storyline is about (Epic 93). */
UENUM(BlueprintType)
enum class EPSStorylineKind : uint8
{
    /** A team that keeps winning. */
    WinStreak,
    /** A team that keeps losing. */
    LosingStreak,
    /** A first-year player among the league's best in a category. */
    RookieSurge,
    /** Next week's game against the team that beat it earlier this season. */
    RevengeGame,
    /** A record in the record book fell this week (Epic 92). */
    RecordBroken,
    /** The most valuable player race is close. */
    AwardRace,
    /** A trade this week (Epic 88): the team that got its headline player, and the other. */
    Trade
};

/** An honor the league hands out (Epic 93). Week honors each week; the rest at the season's end
 *  by a vote. */
UENUM(BlueprintType)
enum class EPSAwardKind : uint8
{
    OffensivePlayerOfWeek,
    DefensivePlayerOfWeek,
    MostValuablePlayer,
    OffensivePlayerOfYear,
    DefensivePlayerOfYear,
    RookieOfYear
};

/** How much one kind of storyline matters: the news and the broadcast lead with the heaviest. */
USTRUCT(BlueprintType)
struct FPSStorylineKindDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    EPSStorylineKind Kind = EPSStorylineKind::WinStreak;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    float Weight = 1.f;
};

/** One statistic's worth in an award score. */
USTRUCT(BlueprintType)
struct FPSAwardStatWeight
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    EPSStatCategory Category = EPSStatCategory::PassingYards;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    float Weight = 0.f;
};

/**
 * Tuning for the league's narrative (Data/league_narrative.json, Epic 93): when a run of results
 * or a player's numbers become a story, how the news is put together, and how awards are scored
 * and voted. Defaults equal the JSON.
 */
USTRUCT(BlueprintType)
struct FPSNarrativeTuning
{
    GENERATED_BODY()

    /** Wins (or losses) in a row that make a streak. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    int32 StreakMin = 3;

    /** A rookie in a category's top this many is surging. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    int32 RookieSurgeTopN = 3;

    /** The MVP race is news from this week on ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    int32 AwardRaceFromWeek = 4;

    /** ... when the second's score is within this share of the leader's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    float AwardRaceMargin = 0.15f;

    /** Each kind of storyline once, with its weight. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    TArray<FPSStorylineKindDef> StorylineKinds;

    /** Items in a week's news digest, the heaviest first. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    int32 MaxDigestItems = 6;

    /** Weekly digests kept (and saved), the latest. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    int32 DigestsKept = 20;

    /** Storylines a game's broadcast shows as chyrons. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    int32 MaxBroadcastStorylines = 2;

    /** An offensive player's award score: each statistic times its weight. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    TArray<FPSAwardStatWeight> OffenseScoring;

    /** A defensive player's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    TArray<FPSAwardStatWeight> DefenseScoring;

    /** The MVP's score adds his team's winning share times this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    float MvpWinWeight = 20.f;

    /** Season awards are voted: this many voters ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    int32 VoterCount = 50;

    /** ... each ranking this many players, worth these points from first place down ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    TArray<int32> BallotPoints;

    /** ... each seeing every score off by up to this share, so close races split the vote. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    float VoterNoise = 0.15f;

    /** The vote's seed (with the season), so a season's vote is the same every time. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    int32 VotingSeed = 9301;

    /** With Epic 82's bridge online, each digest asks a model to write it up: the model-router
     *  task (tools/orchestrator/routing.json), what it is asked, and the most characters of facts
     *  it is given. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    FString DigestTask = TEXT("narration");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    FString DigestInstructions = TEXT("Write this week's league news as a short sports-desk column: lead with the heaviest storyline, then the week's honors. Use only the facts given.");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative")
    int32 DigestContextChars = 4000;

    FPSNarrativeTuning()
    {
        const TPair<EPSStorylineKind, float> Kinds[] = {
            { EPSStorylineKind::WinStreak, 3.f }, { EPSStorylineKind::LosingStreak, 2.f }, { EPSStorylineKind::RookieSurge, 2.5f },
            { EPSStorylineKind::RevengeGame, 2.f }, { EPSStorylineKind::RecordBroken, 4.f }, { EPSStorylineKind::AwardRace, 3.5f },
            { EPSStorylineKind::Trade, 3.f } };
        for (const TPair<EPSStorylineKind, float>& Kind : Kinds)
        {
            FPSStorylineKindDef& Def = StorylineKinds.AddDefaulted_GetRef();
            Def.Kind = Kind.Key;
            Def.Weight = Kind.Value;
        }
        const TPair<EPSStatCategory, float> Offense[] = {
            { EPSStatCategory::PassingYards, 0.04f }, { EPSStatCategory::PassingTouchdowns, 4.f }, { EPSStatCategory::InterceptionsThrown, -2.f },
            { EPSStatCategory::RushingYards, 0.1f }, { EPSStatCategory::RushingTouchdowns, 6.f }, { EPSStatCategory::Receptions, 0.5f },
            { EPSStatCategory::ReceivingYards, 0.1f }, { EPSStatCategory::ReceivingTouchdowns, 6.f } };
        for (const TPair<EPSStatCategory, float>& Stat : Offense)
        {
            FPSAwardStatWeight& Weight = OffenseScoring.AddDefaulted_GetRef();
            Weight.Category = Stat.Key;
            Weight.Weight = Stat.Value;
        }
        const TPair<EPSStatCategory, float> Defense[] = {
            { EPSStatCategory::Tackles, 1.f }, { EPSStatCategory::Sacks, 4.f }, { EPSStatCategory::Interceptions, 5.f } };
        for (const TPair<EPSStatCategory, float>& Stat : Defense)
        {
            FPSAwardStatWeight& Weight = DefenseScoring.AddDefaulted_GetRef();
            Weight.Category = Stat.Key;
            Weight.Weight = Stat.Value;
        }
        BallotPoints = { 10, 5, 3, 1 };
    }
};

/** A story the league is telling (Epic 93). */
USTRUCT(BlueprintType)
struct FPSStoryline
{
    GENERATED_BODY()

    /** Stable for as long as the story runs, e.g. "WinStreak.HOM". */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    FName StorylineId;

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    EPSStorylineKind Kind = EPSStorylineKind::WinStreak;

    /** The team it is about, and (a revenge game) the opponent. */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    FName TeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    FName OtherTeamId;

    /** The player it is about, and (the MVP race) his rival. */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    FName PlayerId;

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    FName OtherPlayerId;

    /** The statistic it is about (a rookie's, a record's). */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    EPSStatCategory Category = EPSStatCategory::PassingYards;

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    EPSStatScope Scope = EPSStatScope::Game;

    /** The streak's length, the rookie's total, the record, the revenge game's margin, the race's
     *  gap, the trade's number of pieces. */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    int32 Value = 0;

    /** The rookie's rank in his category. */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    int32 Rank = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    int32 Season = 0;

    /** The week after which it was told. */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    int32 Week = 0;

    /** Its kind's weight: the heaviest leads. */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    float Weight = 0.f;

    bool Involves(FName InTeamId) const { return !InTeamId.IsNone() && (TeamId == InTeamId || OtherTeamId == InTeamId); }
};

/** An honor given (Epic 93). Epic 94's hall of fame reads them. */
USTRUCT(BlueprintType)
struct FPSAwardRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    EPSAwardKind Award = EPSAwardKind::MostValuablePlayer;

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    int32 Season = 0;

    /** The week of a week's honor; 0 for a season award. */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    int32 Week = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    FName PlayerId;

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    FName TeamId;

    /** His award score: the game's for a week's honor, the season's for a season award. */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    float Score = 0.f;

    /** A season award's vote: his points and first-place votes (0 for a week's honor). */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    int32 VotePoints = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    int32 FirstPlaceVotes = 0;
};

/** One item of the league news, written from the string tables (UPSLocalization). */
USTRUCT(BlueprintType)
struct FPSNewsItem
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    FString Headline;

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    FString Body;

    /** The storyline it tells, or None for an award. */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    FName StorylineId;

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    float Weight = 0.f;
};

/** A week's league news (Epic 93). */
USTRUCT(BlueprintType)
struct FPSNewsDigest
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    int32 Season = 0;

    /** The week it covers; the week after the last for the season's awards. */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    int32 Week = 0;

    /** True for the season's-end digest, the awards. */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    bool bSeasonEnd = false;

    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    TArray<FPSNewsItem> Items;

    /** A model's write-up through Epic 82's bridge, once answered (empty otherwise). */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    FString ModelText;

    /** The request that asked for it; 0 when none was opened. */
    UPROPERTY(BlueprintReadOnly, Category = "Narrative")
    int32 ModelRequestId = 0;
};

/** What the narrative keeps in the franchise save. */
USTRUCT(BlueprintType)
struct FPSNarrativeState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Narrative")
    TArray<FPSStoryline> ActiveStorylines;

    UPROPERTY(BlueprintReadWrite, Category = "Narrative")
    TArray<FPSNewsDigest> Digests;

    UPROPERTY(BlueprintReadWrite, Category = "Narrative")
    TArray<FPSAwardRecord> Awards;
};
