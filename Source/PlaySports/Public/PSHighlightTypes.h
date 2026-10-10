// PSHighlightTypes.h - Epic 42: what makes a play a highlight, and the reel made of them
#pragma once

#include "CoreMinimal.h"
#include "PSCameraDirectorComponent.h"
#include "PSReplayFormat.h"
#include "PSSeasonHighlights.h"
#include "PSHighlightTypes.generated.h"

/** The angle a kind of highlight is shown from, in the camera director's vocabulary (Epic 38). */
USTRUCT(BlueprintType)
struct FPSHighlightShot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    EPSHighlightKind Kind = EPSHighlightKind::Score;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    EPSDirectorShot Shot = EPSDirectorShot::EndZone;
};

/**
 * The home team's chance of winning from a game state: a logistic of the score margin, plus what
 * having the ball is worth where it is, sharpened as time runs out. Its swing across a play is
 * one measure of how much the play mattered.
 */
USTRUCT(BlueprintType)
struct FPSWinProbabilityTuning
{
    GENERATED_BODY()

    /** How much one point of margin moves the odds with the whole game to play. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float MarginScale = 0.12f;

    /** Points the ball is worth to the side holding it on the opponent's goal line; on its own
     *  goal line, none (scaled by YardLine / 100). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float PossessionPoints = 3.f;

    /** The share of the game always counted as left, so the last seconds don't divide by zero. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float TimeFloor = 0.02f;

    /** A regulation game's length (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float GameSeconds = 3600.f;

    /** A quarter's length (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float QuarterSeconds = 900.f;
};

/**
 * What makes a highlight and how the reel plays (Data/highlights.json; Architecture rule 4).
 * Defaults equal the file.
 */
USTRUCT(BlueprintType)
struct FPSHighlightTuning
{
    GENERATED_BODY()

    /** Importance per yard gained (or lost). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float YardWeight = 0.1f;

    /** Importance per point scored. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float PointsWeight = 1.f;

    /** Importance of the ball changing hands. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float TurnoverWeight = 6.f;

    /** Importance per tackle the ball carrier shrugged off. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float BrokenTackleWeight = 1.5f;

    /** Importance per unit of win-probability swing (0 to 1). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float WinProbabilityWeight = 20.f;

    /** A play less important than this is never a highlight. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float MinImportance = 3.f;

    /** How many plays a game's reel keeps: its most important. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    int32 ReelSize = 8;

    /** The angle each kind opens on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    TArray<FPSHighlightShot> KindShots;

    /** The slow-motion beat: it starts this long before the play's key moment... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float BeatLeadSeconds = 0.5f;

    /** ...lasts this long (in clip time)... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float BeatSeconds = 1.5f;

    /** ...at this speed; the rest of the clip plays in real time. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float BeatPlaybackRate = 0.25f;

    /** Seconds each clip holds its last frame before the next. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float ClipGapSeconds = 1.f;

    /** A play counts as settled this long after its whistle if no game state has said so
     *  (the score and the ball are read by then). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float SettleAfterWhistleSeconds = 6.f;

    /** Play the reel by itself when the game ends... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    bool bPlayReelAtGameEnd = true;

    /** ...this long after the final whistle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float GameEndReelDelaySeconds = 3.f;

    /** How many highlights a franchise season keeps: its most important. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    int32 SeasonHighlightsKept = 20;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    FPSWinProbabilityTuning WinProbability;

    FPSHighlightTuning()
    {
        FPSHighlightShot& ScoreShot = KindShots.AddDefaulted_GetRef();
        ScoreShot.Kind = EPSHighlightKind::Score;
        ScoreShot.Shot = EPSDirectorShot::EndZone;
        FPSHighlightShot& TurnoverShot = KindShots.AddDefaulted_GetRef();
        TurnoverShot.Kind = EPSHighlightKind::Turnover;
        TurnoverShot.Shot = EPSDirectorShot::Skycam;
        FPSHighlightShot& BigPlayShot = KindShots.AddDefaulted_GetRef();
        BigPlayShot.Kind = EPSHighlightKind::BigPlay;
        BigPlayShot.Shot = EPSDirectorShot::TightFollow;
    }
};

/** What a play did, as importance scoring reads it. */
USTRUCT(BlueprintType)
struct FPSPlayImpact
{
    GENERATED_BODY()

    /** Yards the offense gained (negative for a loss). */
    UPROPERTY(BlueprintReadOnly, Category = "Highlights")
    int32 Yards = 0;

    /** Points scored in the play, by either side. */
    UPROPERTY(BlueprintReadOnly, Category = "Highlights")
    int32 Points = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Highlights")
    bool bTurnover = false;

    UPROPERTY(BlueprintReadOnly, Category = "Highlights")
    int32 BrokenTackles = 0;

    /** How far the home team's chance of winning moved (0 to 1). */
    UPROPERTY(BlueprintReadOnly, Category = "Highlights")
    float WinProbabilitySwing = 0.f;
};

/** One play of a game's highlights: why, how much, and its clip with how to show it. */
USTRUCT(BlueprintType)
struct FPSPlayHighlight
{
    GENERATED_BODY()

    /** The play's place in the game, from 1. */
    UPROPERTY(BlueprintReadOnly, Category = "Highlights")
    int32 PlayNumber = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Highlights")
    EPSHighlightKind Kind = EPSHighlightKind::BigPlay;

    UPROPERTY(BlueprintReadOnly, Category = "Highlights")
    float Importance = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Highlights")
    FPSPlayImpact Impact;

    /** The game state when it was snapped. */
    UPROPERTY(BlueprintReadOnly, Category = "Highlights")
    FPlayState Situation;

    /** The angle it opens on. */
    UPROPERTY(BlueprintReadOnly, Category = "Highlights")
    EPSDirectorShot Shot = EPSDirectorShot::None;

    /** Where on the clip (seconds from its start) the slow-motion beat begins and ends. */
    UPROPERTY(BlueprintReadOnly, Category = "Highlights")
    float BeatStartSeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Highlights")
    float BeatEndSeconds = 0.f;

    /** The play, as the replay system cuts it (Epic 41). */
    UPROPERTY(BlueprintReadOnly, Category = "Highlights")
    FPSReplayRecording Clip;
};
