// PSCommentaryEventModel.h - Epic 23.5: the game's moments, structured for commentary
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSCommentaryTypes.h"
#include "PSGameIntelligenceTypes.h"
#include "PSTelemetryBus.h"
#include "PSCommentaryEventModel.generated.h"

class UPSGameIntelligenceSubsystem;
class UPSStatsEngine;

DECLARE_MULTICAST_DELEGATE_TwoParams(FPSCommentaryModelLineMC, const FPSTelemetryCommentaryEvent& /* Moment */, const FString& /* Text */);

/**
 * UPSCommentaryEventModel is the commentary hooks (Epic 23.5): it reads the game's moments off the
 * telemetry bus and publishes each one back as a structured Commentary event -- what happened
 * (EPSCommentaryMoment and its Detail), who (the players' names and ids), and the situation (the
 * quarter, clock, down, distance, spot and score). It never polls the game or asks the game mode;
 * the play's result is the simulation's PlayResult (the outcome authority), the names come from the
 * play's own live events (the throw, the catch, the tackle).
 *
 * Epic 96's event model adds what the booth weighs: Stakes (late, close, a big down, the red zone,
 * points, a turnover) and Novelty (the first of its kind this game, from the moments told so far;
 * a big play; a record) by the tuning's weights, and for a play's result the primary player's
 * statistic and his total in it this game, from Epic 92's box score (SetStats).
 *
 *  - Moments: the game's start; the snap; a pass (Deep at DeepPassCm); a catch; an interception; a
 *    sack; a hit of BigHitDamage or more; a fumble; a receiver running free; a carrier crossing
 *    the goal line; a flag; the play's result and its score; a timeout; the two-minute warning; a
 *    quarter's end; the final whistle; a record broken (Epic 92).
 *  - The booth: the commentary engine (Epic 96) subscribes to these Commentary events.
 *  - Outside models, bridge-gated like Epic 18's hook: while Epic 82's bridge is online
 *    (UPSGameIntelligenceSubsystem::IsBridgeOnline) each moment in ModelMoments is offered as a
 *    Commentary request (ModelTask's routing, the moment's facts within ModelContextChars as its
 *    context). An answer is kept (GetModelLines) and told to OnModelLineMC. With the bridge
 *    offline nothing is asked and the game is unchanged.
 *
 * In a match it binds to the world's bus and its game-intelligence hooks when the world begins
 * play; headless tests call BindToBus and SetIntelligence. Tuning: Data/commentary_hooks.json.
 */
UCLASS()
class PLAYSPORTS_API UPSCommentaryEventModel : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

    // --- Tuning ----------------------------------------------------------------------------

    static FString GetDefaultTuningPath();

    /** The tuning, loaded from the default path on first use. */
    const FPSCommentaryHookTuning& GetTuning();

    /** Replaces the tuning with JsonFilePath's (through UPSDataIngestion); a file that can't be
     *  read or fails ValidateTuning is refused and the current tuning kept. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Applies InTuning when it passes ValidateTuning. */
    bool SetTuning(const FPSCommentaryHookTuning& InTuning);

    /** Problems with InTuning, one line each (empty when sound): thresholds above 0, a moment
     *  kept, a model moment listed twice, an empty task or instructions, a context under 512. */
    static TArray<FString> ValidateTuning(const FPSCommentaryHookTuning& InTuning);

    // --- Sources -----------------------------------------------------------------------------

    /** Hears Bus's events and publishes the moments on it. OnWorldBeginPlay binds the world's. */
    void BindToBus(UPSTelemetryBus* Bus);

    void UnbindFromBus();

    /** Epic 82's hooks, through which moments are offered to outside models. OnWorldBeginPlay
     *  takes the world's. */
    void SetIntelligence(UPSGameIntelligenceSubsystem* InIntelligence);

    /** The match's statistics (Epic 92), which the game mode owns: a play's primary player's game
     *  total comes from its box score. */
    void SetStats(UPSStatsEngine* InStats);

    // --- What the booth weighs (Epic 96.1) -----------------------------------------------------

    /** How much Moment matters, 0-1, by InTuning's stakes. */
    static float ComputeStakes(const FPSTelemetryCommentaryEvent& Moment, const FPSCommentaryHookTuning& InTuning);

    /** How unusual Moment is, 0-1, when PriorOfKind moments of its kind came before it this game. */
    static float ComputeNovelty(const FPSTelemetryCommentaryEvent& Moment, int32 PriorOfKind, const FPSCommentaryHookTuning& InTuning);

    /** Moments of Kind told this game. */
    int32 CountThisGame(EPSCommentaryMoment Kind) const;

    // --- Moments -----------------------------------------------------------------------------

    /** The moments published, oldest first (the latest MaxMomentsKept). */
    const TArray<FPSTelemetryCommentaryEvent>& GetMoments() const { return Moments; }

    /** The latest moment of Kind still kept. */
    bool FindLatestMoment(EPSCommentaryMoment Kind, FPSTelemetryCommentaryEvent& OutMoment) const;

    /** Plays snapped this game. */
    UFUNCTION(BlueprintPure, Category = "Commentary")
    int32 GetPlayNumber() const { return PlayNumber; }

    /** Moment's facts as an outside model reads them: one compact JSON object, at most MaxChars
     *  (names are shortened, then dropped, to fit). */
    static FString DescribeForModel(const FPSTelemetryCommentaryEvent& Moment, int32 MaxChars);

    /** The lines outside models wrote, oldest first. */
    const TArray<FPSCommentaryModelLine>& GetModelLines() const { return ModelLines; }

    /** An outside model answered a moment's request. */
    FPSCommentaryModelLineMC OnModelLineMC;

private:
    void HandleGameState(const FPSTelemetryGameStateEvent& Event);
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandleThrow(const FPSTelemetryThrowEvent& Event);
    void HandleCatch(const FPSTelemetryCatchEvent& Event);
    void HandleTackle(const FPSTelemetryTackleEvent& Event);
    void HandleDamage(const FPSTelemetryDamageEvent& Event);
    void HandleFumble(const FPSTelemetryFumbleEvent& Event);
    void HandleBlownCoverage(const FPSTelemetryBlownCoverageEvent& Event);
    void HandleBoundaryCrossed(const FPSTelemetryBoundaryCrossedEvent& Event);
    void HandlePenalty(const FPSTelemetryPenaltyEvent& Event);
    void HandleTimeout(const FPSTelemetryTimeoutEvent& Event);
    void HandlePlayResult(const FPSTelemetryPlayResultEvent& Event);
    void HandleRecordBroken(const FPSTelemetryRecordBrokenEvent& Event);
    void HandleRequestAnswered(const FPSIntelRequest& Request);

    /** A moment of Kind in the latest situation, from the bus's latest event of SourceType. */
    FPSTelemetryCommentaryEvent MakeMoment(EPSCommentaryMoment Kind, EPSTelemetryEventType SourceType) const;

    /** Weighs Moment's stakes and novelty, keeps and publishes it, and offers it to outside
     *  models. */
    void Publish(FPSTelemetryCommentaryEvent Moment);

    /** PlayerId's total in Category this game (the box score, with ThisPlay added when the play
     *  isn't in it yet); -1 without statistics. */
    int32 GameTotalFor(FName PlayerId, EPSStatCategory Category, int32 InPlayNumber, int32 ThisPlay) const;

    /** Forgets the game: its plays and its moments' counts. */
    void ResetGame();

    /** Forgets the play's players. */
    void ResetPlay();

    UPROPERTY(Transient)
    FPSCommentaryHookTuning Tuning;

    bool bTuningLoaded = false;

    UPROPERTY(Transient)
    TArray<FPSTelemetryCommentaryEvent> Moments;

    UPROPERTY(Transient)
    TArray<FPSCommentaryModelLine> ModelLines;

    /** Open model requests, by id, with the moment each describes. */
    TMap<int32, FPSTelemetryCommentaryEvent> OpenRequests;

    /** The game, as the bus last told it. */
    FPSTelemetryGameStateEvent LastState;
    bool bHaveState = false;
    bool bGameOver = false;
    int32 PlayNumber = 0;

    /** The play's players, from its live events. */
    FString PasserName;
    FString TargetName;
    FString ReceiverName;
    FString CarrierName;
    FString TacklerName;
    FString InterceptorName;
    bool bTurnoverThisPlay = false;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    TWeakObjectPtr<UPSGameIntelligenceSubsystem> Intelligence;
    FDelegateHandle AnsweredHandle;

    TWeakObjectPtr<UPSStatsEngine> Stats;

    /** Moments told this game, by kind. */
    TMap<EPSCommentaryMoment, int32> KindCounts;
};
