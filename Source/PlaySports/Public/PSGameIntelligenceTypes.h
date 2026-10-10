// PSGameIntelligenceTypes.h - Epic 82: what the game asks outside models, and what it takes back
#pragma once

#include "CoreMinimal.h"
#include "PSSeasonHighlights.h"
#include "PSGameIntelligenceTypes.generated.h"

/** What an outside model is asked for (Epic 82). */
UENUM(BlueprintType)
enum class EPSIntelRequestKind : uint8
{
    /** A CPU side's play call: the answer must be one of the request's choices (Epic 18's hook). */
    PlayCall,
    /** A finished game's drives, a sentence each. */
    DriveSummary,
    /** A finished game explained from its key plays. */
    GameAnalysis,
    /** A franchise week's league news written up from its storylines (Epic 93). */
    NewsDigest
};

/** Where a request stands. */
UENUM(BlueprintType)
enum class EPSIntelRequestState : uint8
{
    /** Waiting for an answer. */
    Open,
    /** Answered; a play call's answer waits for its side's call. */
    Answered,
    /** The side ran the answered play. */
    Used,
    /** The side ran another play: the clock or special teams called it outright. */
    Overruled,
    /** No answer in time: the CPU made its own call. */
    TimedOut,
    /** Closed unanswered: the snap came first, a newer request replaced it, or too many were open. */
    Closed
};

/**
 * Tuning for the game's hooks for outside models (Epic 82, Data/game_intelligence.json): how
 * much game state a model is given, how long a play call waits for one, and which task of the
 * model router (tools/orchestrator/routing.json, Epic 119) each request names. Defaults equal
 * the JSON.
 */
USTRUCT(BlueprintType)
struct FPSGameIntelligenceTuning
{
    GENERATED_BODY()

    /** The most characters a request's game state may take (a model reads about four to a
     *  token). At least PSGameStateSerializer::MinBudgetChars. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameIntelligence")
    int32 ContextBudgetChars = 6000;

    /** How long a CPU side waits for an outside model's play call before making its own. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameIntelligence")
    float PlayCallTimeoutSeconds = 6.f;

    /** Requests open at once; past it the oldest that blocks nothing is closed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameIntelligence")
    int32 MaxOpenRequests = 8;

    /** The longest answer taken, in characters. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameIntelligence")
    int32 MaxAnswerChars = 4000;

    /** The game's leaders in the game state: this many per stat category (0 for none). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameIntelligence")
    int32 LeadersPerCategory = 1;

    /** Key plays in a post-game analysis, the most important first (Epic 42's scoring). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameIntelligence")
    int32 MaxKeyPlays = 5;

    /** At the final whistle, while the bridge is online, ask for the drive summary and the
     *  game analysis. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameIntelligence")
    bool bPostGameRequests = true;

    /** The model-router task each request names: better models for strategy, cheaper ones for
     *  summaries (routing.json's tasks). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameIntelligence")
    FString PlayCallTask = TEXT("strategy");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameIntelligence")
    FString DriveSummaryTask = TEXT("summary");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameIntelligence")
    FString GameAnalysisTask = TEXT("analysis");

    /** What each request asks of the model. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameIntelligence")
    FString PlayCallInstructions = TEXT("You call plays for the side named in 'side'. Read the game state (situation, personnel, the human's tendencies, the game's stats) and answer with exactly one id from 'choices', nothing else.");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameIntelligence")
    FString DriveSummaryInstructions = TEXT("Summarize each drive of this finished game in one sentence, in order, one line per drive.");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameIntelligence")
    FString GameAnalysisInstructions = TEXT("Explain how this game was won in a short paragraph, built on its key plays (the most important first).");
};

/** One answer a request takes, with a note for the model. */
USTRUCT(BlueprintType)
struct FPSIntelChoice
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    FName Id;

    /** "Run, I-Form: I-Form Dive". */
    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    FString Note;
};

/** One question for an outside model, polled through the bridge and answered back (Epic 82). */
USTRUCT(BlueprintType)
struct FPSIntelRequest
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 RequestId = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    EPSIntelRequestKind Kind = EPSIntelRequestKind::PlayCall;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    EPSIntelRequestState State = EPSIntelRequestState::Open;

    /** The model-router task to send it as (routing.json). */
    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    FString Task;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    FString Instructions;

    /** The facts, as compact JSON within the tuning's budget: the game state for a play call,
     *  the post-game analysis for the others. */
    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    FString Context;

    /** The only answers taken (a play call's plays); empty takes any text. */
    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    TArray<FPSIntelChoice> Choices;

    /** A play call's side. */
    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    bool bOffense = true;

    /** How long the game waits for it; 0 when nothing waits. */
    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    float TimeoutSeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    float WaitedSeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    FString Answer;

    bool IsOpen() const { return State == EPSIntelRequestState::Open; }
};

/** One finished drive of the game, as the play simulation announced it. */
USTRUCT(BlueprintType)
struct FPSDriveRecap
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    bool bHomeTeam = true;

    /** The quarter it ended in. */
    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 Quarter = 1;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 Plays = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 Yards = 0;

    /** "Touchdown", "Turnover on Downs", ... */
    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    FString Result;
};

/** One of the game's key plays, by Epic 42's importance scoring. */
USTRUCT(BlueprintType)
struct FPSKeyPlay
{
    GENERATED_BODY()

    /** The play's place in the game, from 1. */
    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 PlayNumber = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    EPSHighlightKind Kind = EPSHighlightKind::BigPlay;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    float Importance = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 Yards = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 Points = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    bool bTurnover = false;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 BrokenTackles = 0;

    /** The situation it was snapped in. */
    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 Quarter = 1;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 Down = 1;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 Distance = 10;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 YardLine = 20;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    bool bHomeOffense = true;
};

/** A finished game, analysed: its drives, its key plays and, once a model answered, the
 *  summary and the analysis it wrote (Epic 82). */
USTRUCT(BlueprintType)
struct FPSGameAnalysis
{
    GENERATED_BODY()

    /** True once the game is over (built before that, it is the game so far). */
    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    bool bFinal = false;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 HomeScore = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 AwayScore = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    TArray<FPSDriveRecap> Drives;

    /** The most important first. */
    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    TArray<FPSKeyPlay> KeyPlays;

    /** The model's drive summary and analysis, once answered (empty before). */
    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    FString DriveSummaryText;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    FString GameAnalysisText;

    /** The requests that asked for them (0 when none was opened). */
    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 DriveSummaryRequestId = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GameIntelligence")
    int32 GameAnalysisRequestId = 0;
};
