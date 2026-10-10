#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PSPlayerAttributes.h"
#include "PSSituationData.h"
#include "PSSpecialTeamsData.h"
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

    /** The coming snap is a kickoff (Epic 75): the possessing team kicks from YardLine. Set by
     *  a score, cleared when the kick is resolved. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bKickoff = false;
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
    Holding,
    /** Defensive pass interference, drawn by the coverage contest (Epic 69): a spot foul. */
    PassInterference
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

class UPSSpecialTeamsModel;

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

    /** A pass-interference flag's spot: yards past the line of scrimmage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    int32 PassInterferenceYards = 0;

    // Bus subscriber handlers (C2) -- public so tests can call them directly
    UFUNCTION()
    void OnBusCatchEvent(const FPSTelemetryCatchEvent& Event);

    UFUNCTION()
    void OnBusTackleEvent(const FPSTelemetryTackleEvent& Event);

    UFUNCTION()
    void OnBusScoreEvent(const FPSTelemetryScoreEvent& Event);

    /** A pass in the air (Epic 92): who threw it to whom, for the play's result. */
    UFUNCTION()
    void OnBusThrowEvent(const FPSTelemetryThrowEvent& Event);

    /** Every play's result as this simulation, the outcome authority, resolves it (Epic 92): the
     *  situation at the snap, the result, the points and who threw, caught, ran and tackled.
     *  Fires with or without a world, so quick-sim games are counted; with a world the result is
     *  also published on the bus (PlayResult). */
    FPSTelemetryPlayResultMC OnPlayResolved;

    /** A human kicker lined up or kicked (Epic 104.5, UPSKickMeterComponent). While one is lined
     *  up, the kick phase waits for him up to the event's HoldSeconds; his Roll then stands in
     *  for the CPU kicker's random number. Ignored outside a kick phase. */
    UFUNCTION()
    void OnBusKickEvent(const FPSTelemetryKickEvent& Event);

    /** A human defender's jump at the snap was timed (Epic 104.5); an offside jump is flagged
     *  as Offsides. */
    UFUNCTION()
    void OnBusJumpSnapEvent(const FPSTelemetryJumpSnapEvent& Event);

    /** Pass interference the coverage contest drew (Epic 69, UPSCoverageMatchupSubsystem):
     *  flagged as a spot foul at the event's yards past the line, unless a flag is already down
     *  or the ball is dead. */
    UFUNCTION()
    void OnBusCoverageEvent(const FPSTelemetryCoverageEvent& Event);

    /** A blocked kick's loose ball the players play out (Epic 17.4, UPSLooseBallSubsystem): once
     *  it is taken live the play waits for it, and its dead ball -- the spot, the defense's
     *  touchdown -- becomes the kick's outcome. */
    UFUNCTION()
    void OnBusLooseBallEvent(const FPSTelemetryLooseBallEvent& Event);

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

    /** The kicking game's model (Epic 75), created on first use with Data/special_teams.json. */
    UFUNCTION(BlueprintCallable, Category = "Simulation|SpecialTeams")
    UPSSpecialTeamsModel* GetSpecialTeams();

    /** The last kick's outcome, as the special-teams model resolved it. */
    const FPSSpecialTeamsOutcome& GetLastSpecialTeamsOutcome() const { return LastSpecialTeamsOutcome; }

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

    /** Both sides' special-teams calls for the coming snap, and the last kick's outcome (Epic 75). */
    FPSSpecialTeamsCall PendingSpecialTeams;
    FPSSpecialTeamsOutcome LastSpecialTeamsOutcome;

    UPROPERTY(Transient)
    UPSSpecialTeamsModel* SpecialTeams = nullptr;

    /** Resolves the kickoff, punt or field goal the play is in through the special-teams model.
     *  KickRoll (0 = perfect .. 1) is the kick's quality; negative lets the model draw it. */
    void ResolveKick(float KickRoll = -1.f);

    /** The whistle has blown, or the special-teams model decides the play: physical tackles and
     *  catches no longer change the result. */
    bool IsBallDead() const;

    /** A scrimmage play is under way: from the snap until the whistle (not before the snap, not
     *  on a kick). Offensive holding is called only then. */
    bool IsBallLive() const;

    /** A blocked kick's loose ball is being played out on the field (Epic 17.4). */
    bool bLooseBallLive = false;

    void ResolvePlayResult();

    /** The play in progress (Epic 92): opened at the snap, filled in as players throw, catch and
     *  tackle, announced (OnPlayResolved, the bus) when the play is resolved. */
    FPSTelemetryPlayResultEvent PlayLog;
    bool bPlayLogOpen = false;
    int32 PlaysAnnounced = 0;

    /** Starts the play log at the snap, from the situation. */
    void OpenPlayLog();

    /** The PlayerId of the player on either side with this display name (the bus names players
     *  by it); None for nobody. */
    FName FindPlayerIdByName(const FString& DisplayName) const;

    /** Completes the play log with the play's result (AtSnap: the state the play was resolved
     *  from) and announces it. */
    void AnnouncePlayResult(const FPlayState& AtSnap, bool bTurnover);

    /** True when the kick phase resolves this frame: a human's kick has arrived, the wait for a
     *  lined-up human has run out, or (no human) the CPU kicker's time has come. */
    bool IsKickReady() const;

    /** The kick's roll, 0 (perfect) .. 1: the human kicker's if he kicked, else random. Clears
     *  the human kick. */
    float ConsumeKickRoll();

    bool bHumanKickLinedUp = false;
    float HumanKickHoldSeconds = 0.f;
    float HumanKickRoll = -1.f;

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
