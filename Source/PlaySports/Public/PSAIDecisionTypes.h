// PSAIDecisionTypes.h - Epic 85: what an AI decided and why, the debug tools' tuning, and scripted scenarios
#pragma once

#include "CoreMinimal.h"
#include "PSDefenseController.h"
#include "PSPlayerAttributes.h"
#include "PSAIDecisionTypes.generated.h"

/** One option an AI weighed in a decision: a receiver he read, a rush move, a play. */
USTRUCT(BlueprintType)
struct FPSAIDecisionOption
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    FString Option;

    /** What it was weighed by: a receiver's separation (cm), a move's score, a play's weight. */
    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    float Score = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    bool bChosen = false;

    /** Anything else that counted, e.g. "not his read yet". */
    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    FString Note;
};

/** One AI decision (Epic 85.1): who, when, what he did, at what, and why. */
USTRUCT(BlueprintType)
struct FPSAIDecisionRecord
{
    GENERATED_BODY()

    /** The player's PlayerId, or CPUOffense / CPUDefense for a play call. */
    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    FName AgentId;

    /** The system that decided: SkillAI, DefenderAI, RushMove or PlayCall. */
    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    FString System;

    /** The play it belongs to (UPSAIDecisionLog counts snaps from 1). */
    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    int32 PlayIndex = 0;

    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    float TimeSinceSnap = 0.f;

    /** His assignment this play, e.g. ManCoverage or Route. */
    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    FString Assignment;

    /** What he did: his state (RunRoute, Cover, ...) or the act (Throw, Scramble, HandOff, a move). */
    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    FString Action;

    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    FString Reason;

    /** Who or what he went at, and where. */
    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    FString Target;

    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    FVector TargetLocation = FVector::ZeroVector;

    /** The direction he was steered (unit, on the ground; zero standing). */
    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    FVector Direction = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "AIDebug")
    TArray<FPSAIDecisionOption> Options;
};

/** The AI debug tools' settings (Data/ai_debug.json; Architecture rule 4). Off by default: the
 *  console variables ps.AI.DecisionLog, ps.AI.DebugOverlay and ps.AI.PostMortem turn them on. */
USTRUCT(BlueprintType)
struct FPSAIDebugTuning
{
    GENERATED_BODY()

    /** Record every AI decision without the console variable. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIDebug")
    bool bLogDecisions = false;

    /** Write a post-mortem file at the end of every play without the console variable. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIDebug")
    bool bWritePostMortems = false;

    /** Under the project's Saved directory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIDebug")
    FString PostMortemDirectory = TEXT("AIPostMortems");

    /** The oldest post-mortems go once there are more than this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIDebug")
    int32 MaxPostMortemFiles = 50;

    /** A play keeps at most this many records (the earliest are kept). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIDebug")
    int32 MaxRecordsPerPlay = 5000;

    /** The overlay's text sits this far above a player (cm), at this scale. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIDebug")
    float OverlayHeightCm = 160.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIDebug")
    float OverlayFontScale = 1.f;
};

/** One player in a scripted scenario (Epic 85.4). */
USTRUCT(BlueprintType)
struct FPSAIScenarioPlayer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    FName PlayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    EPlayerRole Role = EPlayerRole::WideReceiver;

    /** Where he stands at the snap (the line of scrimmage is X = 0, the offense going +X). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    FVector Location = FVector::ZeroVector;

    /** Every rating he has (0-100). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    float Rating = 70.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    FPSPlayerDNA DNA;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    bool bHasBall = false;

    /** Offense: his route, as offsets from where he stands (none: he blocks, or reads as QB). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    TArray<FVector> Route;

    /** Defense: his assignment, the receiver he covers (PlayerId) and his zone (an offset). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    EPSDefensiveAssignmentType Assignment = EPSDefensiveAssignmentType::RunFit;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    FName CoverTarget;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    FVector ZoneOffset = FVector::ZeroVector;
};

/** What a player must have decided after the scenario's decision cycle. */
USTRUCT(BlueprintType)
struct FPSAIScenarioExpectation
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    FName PlayerId;

    /** His decision's Action (FPSAIDecisionRecord), e.g. Throw, Scramble, Rush, Cover. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    FString Action;

    /** Its Target, when given. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    FString Target;

    /** When not zero, he must be steered within MaxAngleDegrees of this heading. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    FVector Heading = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    float MaxAngleDegrees = 45.f;
};

/** A scripted AI scenario: players placed in a state, one decision cycle, asserted outputs. */
USTRUCT(BlueprintType)
struct FPSAIScenario
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    FName ScenarioId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    FString Description;

    /** The offense's call: Run, ShortPass, ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    FString OffenseCategory = TEXT("ShortPass");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    int32 Down = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    int32 Distance = 10;

    /** How long after the snap the decision cycle runs, and how many cycles. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    float StepSeconds = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    int32 Steps = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    TArray<FPSAIScenarioPlayer> Players;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    TArray<FPSAIScenarioExpectation> Expectations;
};

/** Top-level shape of Data/ai_scenarios.json. */
USTRUCT(BlueprintType)
struct FPSAIScenarioCatalog
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIScenario")
    TArray<FPSAIScenario> Scenarios;
};

/** How a scenario went: each player's last decision, and every expectation it missed. */
USTRUCT(BlueprintType)
struct FPSAIScenarioResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "AIScenario")
    FName ScenarioId;

    UPROPERTY(BlueprintReadOnly, Category = "AIScenario")
    bool bPassed = false;

    UPROPERTY(BlueprintReadOnly, Category = "AIScenario")
    TArray<FString> Failures;

    UPROPERTY(BlueprintReadOnly, Category = "AIScenario")
    TArray<FPSAIDecisionRecord> Decisions;
};
