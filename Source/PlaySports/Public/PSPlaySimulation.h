#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PSPlayerAttributes.h"
#include "PSSituationData.h"
#include "PSTelemetryBus.h"
#include "PSPlaySimulation.generated.h"

UENUM(BlueprintType)
enum class EPlayPhase : uint8
{
    PreSnap,
    Snap,
    PassRush,
    BallCarrierMovement,
    Scoring,
    Kickoff,
    Punt,
    FieldGoal
};

USTRUCT(BlueprintType)
struct FPlayState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPlayPhase Phase = EPlayPhase::PreSnap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float GameTimeSeconds = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Down = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Distance = 10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 YardLine = 20;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 YardLineToGain = 30;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Quarter = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float GameClockSeconds = 900.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PlayClockSeconds = 40.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHomeHasPossession = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 HomeScore = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 AwayScore = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsClockRunning = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 HomeTimeoutsRemaining = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 AwayTimeoutsRemaining = 3;
};

USTRUCT(BlueprintType)
struct FDriveSummary
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Plays = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Yards = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Result = TEXT("");
};

UENUM(BlueprintType)
enum class EPSPenaltyType : uint8
{
    None,
    Offsides,
    Holding
};

UENUM(BlueprintType)
enum class EPlayResultType : uint8
{
    Incomplete,
    Tackle,
    Touchdown,
    Safety,
    FieldGoalGood,
    FieldGoalMissed,
    KickoffResult,
    PuntResult
};

USTRUCT(BlueprintType)
struct FPlayResult
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 YardsGained = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPlayResultType ResultType = EPlayResultType::Incomplete;

    /** The carrier went out of bounds (Epic 76): late in a half the clock stays stopped. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bOutOfBounds = false;
};

UCLASS(Blueprintable)
class PLAYSPORTS_API UPSPlaySimulation : public UObject
{
    GENERATED_BODY()

public:
    UPSPlaySimulation();

    UFUNCTION(BlueprintCallable)
    void InitializePlay(const TArray<FPlayerAttributes>& Offense, const TArray<FPlayerAttributes>& Defense);

    UFUNCTION(BlueprintCallable)
    void AdvancePlay(float DeltaSeconds);

    UFUNCTION(BlueprintCallable)
    FPlayState GetPlayState() const;

    UFUNCTION(BlueprintCallable)
    FPlayResult GetPlayResult() const;

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void TriggerSnap();

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void SetPlayPhase(EPlayPhase NewPhase);

    /** The carrier is down in bounds. Ignored once the play is over (Scoring). */
    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void RecordTackle(int32 YardsGained);

    /** The carrier ran out of bounds (Epic 76): a tackle that stops the clock until the snap
     *  inside DoesOutOfBoundsStopClock's window. Ignored once the play is over. */
    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void RecordOutOfBounds(int32 YardsGained);

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void EndPlayAndPrepareNext();

    /**
     * Call after NewObject<UPSPlaySimulation> so the sim can cache the world
     * and subscribe to the TelemetryBus. Must be called before AdvancePlay.
     */
    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void InitializeWithWorld(UWorld* InWorld);

    /**
     * When true, ResolvePlayResult runs its statistical rolls (headless quick-sim).
     * When false (default), play outcomes are driven by physical bus events (catch/tackle/score).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    bool bQuickSimMode = false;

    UPROPERTY(BlueprintReadOnly, Category = "Simulation")
    FDriveSummary CurrentDriveSummary;

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    FDriveSummary GetDriveSummary() const { return CurrentDriveSummary; }

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void RecordTouchdown();

    UFUNCTION(BlueprintPure, Category = "Simulation|Clock")
    FString GetFormattedGameClock() const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    EPSPenaltyType ActivePenalty;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    bool bPenaltyDeclined;

    // Bus subscriber handlers (C2) -- public so tests can call them directly
    UFUNCTION()
    void OnBusCatchEvent(const FPSTelemetryCatchEvent& Event);

    UFUNCTION()
    void OnBusTackleEvent(const FPSTelemetryTackleEvent& Event);

    UFUNCTION()
    void OnBusScoreEvent(const FPSTelemetryScoreEvent& Event);

    /** The offense's call before its snap (Epic 76): its tempo's play-clock mark, and whether
     *  it is a spike or a kneel, which this resolves at the snap. */
    UFUNCTION()
    void OnBusPlayCallEvent(const FPSTelemetryPlayCallEvent& Event);

    /** A side's timeout: charged through CallTimeout. */
    UFUNCTION()
    void OnBusTimeoutEvent(const FPSTelemetryTimeoutEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
    bool CallTimeout(bool bHomeTeam);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Config")
    class UPSRulesConfig* RulesConfig = nullptr;

    const TArray<FPlayerAttributes>& GetOffenseRoster() const { return OffenseRoster; }
    const TArray<FPlayerAttributes>& GetDefenseRoster() const { return DefenseRoster; }

private:
    FPlayState CurrentState;
    TArray<FPlayerAttributes> OffenseRoster;
    TArray<FPlayerAttributes> DefenseRoster;
    FPlayResult CurrentPlayResult;
    float PhaseTimer;

    /** Cached world -- set via InitializeWithWorld; never assumed valid in headless tests. */
    UPROPERTY(Transient)
    UWorld* CachedWorld = nullptr;

    /** From the offense's call for the coming snap (Epic 76). */
    EPSClockPlay PendingClockPlay = EPSClockPlay::None;
    float PendingSnapPlayClock = -1.f;

    void ResolvePlayResult();

    /** Announces the game state on the bus (Epic 33) when any of it changed other than the
     *  running clocks, which listeners run on themselves between announcements. */
    void PublishGameStateIfChanged();

    FPSTelemetryGameStateEvent LastPublishedGameState;
    bool bHasPublishedGameState = false;
    FDriveSummary LastCompletedDrive;
    int32 CompletedDrives = 0;

    /** Ends a spike or a kneel at the snap: nothing physical decides it. */
    void ResolveClockPlay();
};
