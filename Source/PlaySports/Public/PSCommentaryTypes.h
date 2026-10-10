// PSCommentaryTypes.h - Epics 23.5 and 96: the commentary hooks, the booth's line library and its use
#pragma once

#include "CoreMinimal.h"
#include "PSTelemetryBus.h"
#include "PSCommentaryTypes.generated.h"

/**
 * Tuning for the commentary hooks (Data/commentary_hooks.json, Epics 23.5 and 96.1; Architecture
 * rule 4): what makes a moment worth describing, how its stakes and novelty are weighed, how many
 * are kept, and which ones are offered to outside models through Epic 82's bridge. Defaults equal
 * the JSON.
 */
USTRUCT(BlueprintType)
struct FPSCommentaryHookTuning
{
    GENERATED_BODY()

    /** A hit of this much damage (Epic 139) or more is a BigHit moment. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float BigHitDamage = 30.f;

    /** A pass thrown this far (cm, start to target) or farther is Deep; shorter is Short. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float DeepPassCm = 2500.f;

    /** The two-minute warning: the first game state of the second or fourth quarter at or under
     *  this many seconds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float TwoMinuteWarningSeconds = 120.f;

    /** Moments kept (GetMoments), the newest last; also the model lines kept. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 MaxMomentsKept = 64;

    /** Stakes (Epic 96.1), each added and the sum capped at 1: from LateGameQuarter on; a margin
     *  of CloseGameMargin points or fewer; third or fourth down; at or past RedZoneYardLine; points
     *  scored; the ball changing hands. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 LateGameQuarter = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float LateQuarterStakes = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 CloseGameMargin = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float CloseGameStakes = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float CriticalDownStakes = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 RedZoneYardLine = 80;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float RedZoneStakes = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float ScoreStakes = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float TurnoverStakes = 0.35f;

    /** Novelty (Epic 96.1): a moment's first of its kind this game is 1, falling to 0 at the
     *  NoveltyHorizon-th; a play of BigPlayYards or more adds BigPlayNovelty; a record is 1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 NoveltyHorizon = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 BigPlayYards = 20;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float BigPlayNovelty = 0.3f;

    /** Offer moments to outside models while Epic 82's bridge is online. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    bool bOfferToModels = true;

    /** The moments offered: each becomes a Commentary request with the moment as its context. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    TArray<EPSCommentaryMoment> ModelMoments;

    /** The model router's task the requests name (tools/orchestrator/routing.json). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    FString ModelTask = TEXT("narration");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    FString ModelInstructions = TEXT("You are a football play-by-play announcer. Describe this moment in one short spoken sentence, using only the facts given.");

    /** The most characters of a moment's facts a request carries. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 ModelContextChars = 1024;
};

/** A line an outside model wrote for a moment through Epic 82's bridge. */
USTRUCT(BlueprintType)
struct FPSCommentaryModelLine
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    int32 RequestId = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    FPSTelemetryCommentaryEvent Moment;

    /** The model's text, as it answered; not ours to translate. */
    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    FString Text;
};

/** The booth's two voices (Epic 96.3). */
UENUM(BlueprintType)
enum class EPSCommentaryVoice : uint8
{
    /** Calls the action as it happens. */
    PlayByPlay,
    /** The analyst: speaks between plays. */
    Color
};

/** Where a spoken line came from (Epic 96.4). */
UENUM(BlueprintType)
enum class EPSCommentarySource : uint8
{
    /** The line library's template (Data/commentary_lines.json, the string table's text). */
    Template,
    /** A storyline of the league's (Epic 93) as a talking point. */
    TalkingPoint,
    /** An outside model's line, through Epic 82's bridge. */
    Model
};

/**
 * One line of the booth's library (Data/commentary_lines.json, Epic 96). Its text is the string
 * table's Commentary.Line.<LineId> row (Data/ui_text.csv), with {placeholders} for the moment's
 * facts. It is said for its Moment when every condition holds.
 */
USTRUCT(BlueprintType)
struct FPSCommentaryLineDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    FName LineId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    EPSCommentaryVoice Voice = EPSCommentaryVoice::PlayByPlay;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    EPSCommentaryMoment Moment = EPSCommentaryMoment::Snap;

    /** 0-100: how much it matters to say it. A line cuts off the one being said when it is at
     *  least InterruptMargin above it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 Priority = 50;

    /** The line isn't said again sooner than this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float CooldownSeconds = 0.f;

    /** At most this many times a game, and a season (0: no cap). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 MaxPerGame = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 MaxPerSeason = 0;

    /** Conditions: the moment's Detail (None: any), its down, yards and stakes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    FName Detail;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 MinDown = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 MaxDown = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 MinYards = -100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 MaxYards = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float MinStakes = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    bool bRequireFirstDown = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    bool bRequireTurnover = false;

    /** The text names the primary player ({Player}), the other one ({Other}), or his game total
     *  ({Total} {Stat}): the line is said only when the moment has them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    bool bNeedsPrimary = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    bool bNeedsSecondary = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    bool bNeedsTotal = false;
};

/**
 * The booth (Data/commentary_lines.json, Epic 96; Architecture rule 4): how lines are picked,
 * paced, interrupted and capped, and the line library. Defaults equal the JSON's numbers; the
 * lines come from the file.
 */
USTRUCT(BlueprintType)
struct FPSCommentaryLibrary
{
    GENERATED_BODY()

    /** Seeds the pick among equally good lines each game, so a game's booth can be replayed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 Seed = 1993;

    /** A line takes its words over WordsPerSecond, within MinLineSeconds..MaxLineSeconds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float WordsPerSecond = 2.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float MinLineSeconds = 1.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float MaxLineSeconds = 6.f;

    /** A queued play-by-play line older than this is dropped: the moment has passed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float MaxDelaySeconds = 3.f;

    /** ...and a queued analyst's line older than this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float ColorMaxDelaySeconds = 12.f;

    /** A line cuts off the one being said when its priority is at least this much higher. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 InterruptMargin = 20;

    /** Lines each voice keeps waiting; past it the least important is dropped. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 QueueLength = 3;

    /** The analyst's window opens this long after the play is over (its result, a timeout, a
     *  quarter's end) and closes at the snap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float ColorWindowDelaySeconds = 1.5f;

    /** A candidate line's score: its Priority, plus StakesWeight x the moment's stakes and
     *  NoveltyWeight x its novelty, less RepeatPenalty x its uses this game. Lines within
     *  VarietyBand of the best are picked among at random. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float StakesWeight = 30.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float NoveltyWeight = 20.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float RepeatPenalty = 15.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float VarietyBand = 5.f;

    /** Storylines (Epic 93) the analyst brings up a game, at TalkingPointPriority, at least
     *  TalkingPointGapSeconds apart, when he has nothing else to say. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 MaxTalkingPointsPerGame = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 TalkingPointPriority = 25;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float TalkingPointGapSeconds = 30.f;

    /** Voice an outside model's line for the play (Epic 82's bridge, through the commentary
     *  hooks) as the analyst's, at ModelLinePriority, in place of his template line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    bool bUseModelLines = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 ModelLinePriority = 35;

    /** Spoken lines kept (GetSpoken), the newest last. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 MaxSpokenKept = 64;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    TArray<FPSCommentaryLineDef> Lines;
};

/** A line the booth said, or is waiting to say. */
USTRUCT(BlueprintType)
struct FPSCommentaryUtterance
{
    GENERATED_BODY()

    /** The library's line; None for a talking point or a model's line. */
    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    FName LineId;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    EPSCommentaryVoice Voice = EPSCommentaryVoice::PlayByPlay;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    EPSCommentarySource Source = EPSCommentarySource::Template;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    int32 Priority = 0;

    /** The voice's name and the line, localized (UPSLocalization), as the caption shows them. */
    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    FString Speaker;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    FString Text;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    float DurationSeconds = 0.f;

    /** The moment it is about. */
    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    EPSCommentaryMoment Moment = EPSCommentaryMoment::Snap;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    int32 PlayNumber = 0;

    /** On the booth's clock: when it was picked, when it may start (the analyst's waits for his
     *  window), when it started. */
    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    float CreatedAt = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    float NotBefore = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    float StartedAt = 0.f;

    /** A more important line cut it off. */
    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    bool bInterrupted = false;
};

/** How often a line has been said. */
USTRUCT(BlueprintType)
struct FPSCommentaryLineCount
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Commentary")
    FName LineId;

    UPROPERTY(BlueprintReadWrite, Category = "Commentary")
    int32 Uses = 0;
};

/** A season's line use, so its caps hold across games (Epic 96.6); kept in the franchise save. */
USTRUCT(BlueprintType)
struct FPSCommentarySeasonUsage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Commentary")
    int32 Season = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Commentary")
    TArray<FPSCommentaryLineCount> Lines;
};

/** How repetitive the booth has been this game (Epic 96.6). */
USTRUCT(BlueprintType)
struct FPSCommentaryRepetitionReport
{
    GENERATED_BODY()

    /** Lines said from the library this game, and how many different ones. */
    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    int32 Spoken = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    int32 DistinctLines = 0;

    /** 0-1: the share of lines said that repeated one said before this game. */
    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    float RepeatShare = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    FName MostUsedLineId;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    int32 MostUsedCount = 0;

    /** Lines cut off, queued lines dropped as stale, and picks a cap ruled out. */
    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    int32 Interrupted = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    int32 DroppedStale = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    int32 SuppressedByCap = 0;

    /** The lines that reached a cap this game. */
    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    TArray<FName> LinesAtCap;
};
