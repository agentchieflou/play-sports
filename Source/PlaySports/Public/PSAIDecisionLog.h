// PSAIDecisionLog.h - Epic 85: every AI decision inspectable -- the log, the overlay and the play post-mortem
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSAIDecisionTypes.h"
#include "PSOverlayBadgeTypes.h"
#include "PSTelemetryBus.h"
#include "PSAIDecisionLog.generated.h"

/**
 * UPSAIDecisionLog is the AI's debugging surface (Epic 85):
 *
 *  - Decision log: while it is logging, every AI decision is recorded as an FPSAIDecisionRecord --
 *    the player, the play and the time, his assignment, what he did, at what, why, and the options
 *    he weighed. UPSSkillPlayerAIComponent, UPSDefenderAIComponent and UPSRushMoveComponent record
 *    each decision tick, UPSPlayCallSubsystem each CPU play call. The records of the current play
 *    are kept, and the latest per player.
 *  - On-field overlay: with the console variable ps.AI.DebugOverlay at 1 (or SetOverlayEnabled),
 *    each player's latest decision is shown above him every frame (DescribeForOverlay), with a
 *    line to his target. UPSAIDebugOverlayWidget draws it as cards laid out by Epic 28's badge
 *    rules (LayoutOverlay); where no such layer is up, it is debug text in the world, which
 *    shipping builds leave out.
 *  - Play post-mortem: when a play ends (the Scoring phase), or the next one is snapped, one JSON
 *    file under Saved/<PostMortemDirectory> holds the play's situation, both calls, its bus events
 *    and every player's decision stream. The bus sequence numbers of its first and last event link
 *    it to the event history a replay is built from (Epic 41).
 *
 * Logging costs nothing while off: the AI checks IsLogging before it builds a record. It is on
 * with Data/ai_debug.json's bLogDecisions, the console variable ps.AI.DecisionLog, or SetLogging;
 * post-mortems likewise (bWritePostMortems, ps.AI.PostMortem, SetWritePostMortems), and they
 * turn logging on. Recording never changes a decision.
 */
UCLASS()
class PLAYSPORTS_API UPSAIDecisionLog : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultTuningPath();

    /** World's decision log, or null. */
    static UPSAIDecisionLog* Get(const UWorld* World);

    /** The tuning in use, loaded from the default path on first use. */
    const FPSAIDebugTuning& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    void SetTuning(const FPSAIDebugTuning& InTuning);

    /** True while decisions are recorded. */
    UFUNCTION(BlueprintPure, Category = "AIDebug")
    bool IsLogging();

    /** Turns logging on or off over the tuning and the console variable. */
    UFUNCTION(BlueprintCallable, Category = "AIDebug")
    void SetLogging(bool bOn);

    /** True while each play's post-mortem is written. */
    UFUNCTION(BlueprintPure, Category = "AIDebug")
    bool IsWritingPostMortems();

    UFUNCTION(BlueprintCallable, Category = "AIDebug")
    void SetWritePostMortems(bool bOn);

    /** Records a decision for the current play (when logging). A CPU play call (System
     *  PlayCall) is for the coming snap and joins that play, unless it is made at a snap (one
     *  outside a call window), when it joins the play that snap starts. */
    void Record(const FPSAIDecisionRecord& InRecord);

    /** The play the records belong to: 1 from the first snap, 0 before. */
    UFUNCTION(BlueprintPure, Category = "AIDebug")
    int32 GetPlayIndex() const { return PlayIndex; }

    const TArray<FPSAIDecisionRecord>& GetPlayRecords() const { return PlayRecords; }

    /** One player's decisions this play, in order. */
    UFUNCTION(BlueprintPure, Category = "AIDebug")
    TArray<FPSAIDecisionRecord> GetAgentStream(FName AgentId) const;

    /** A player's latest decision this play; false when he has none. */
    UFUNCTION(BlueprintPure, Category = "AIDebug")
    bool GetLatest(FName AgentId, FPSAIDecisionRecord& OutRecord) const;

    /** The overlay's text for a decision: "QB [Route] Throw -> WR_1", then the reason. */
    UFUNCTION(BlueprintPure, Category = "AIDebug")
    static FString DescribeForOverlay(const FPSAIDecisionRecord& InRecord);

    // --- The on-field overlay (Epic 85.2) --------------------------------------------------

    /** True while the overlay is on: ps.AI.DebugOverlay, unless SetOverlayEnabled decided. It
     *  turns logging on, so there are decisions to show. */
    UFUNCTION(BlueprintPure, Category = "AIDebug")
    bool IsOverlayOn() const;

    /** Turns the overlay on or off over the console variable. */
    UFUNCTION(BlueprintCallable, Category = "AIDebug")
    void SetOverlayEnabled(bool bOn);

    /**
     * The overlay's cards for View (PSAIDebugOverlay::LayoutCards): one for each player on the field
     * with a decision this play, his latest, over his head, with the screen points of the line to
     * his target. BadgeStyle is Epic 28's (its distance scaling and overlap rule); PixelsPerUnit
     * is the viewport's DPI scale. Empty while the overlay is off.
     */
    TArray<FPSAIDebugCard> LayoutOverlay(const FPSBadgeView& View, const FPSOverlayBadgeStyle& BadgeStyle, float PixelsPerUnit);

    /** A drawing layer (UPSAIDebugOverlayWidget) shows the overlay from now on, or stops. While
     *  one does, the debug text drawn in the world stands down. */
    void RegisterOverlayLayer() { ++OverlayLayers; }
    void UnregisterOverlayLayer() { OverlayLayers = FMath::Max(0, OverlayLayers - 1); }

    /** True when the overlay is on and no drawing layer shows it: the world's debug text then
     *  draws it (development builds), as before the layer existed. */
    UFUNCTION(BlueprintPure, Category = "AIDebug")
    bool ShouldDrawWorldText() const { return IsOverlayOn() && OverlayLayers == 0; }

    /** The current play's post-mortem as JSON. */
    FString BuildPostMortemJson() const;

    /** Writes the current play's post-mortem and prunes the oldest beyond MaxPostMortemFiles.
     *  Returns the file's path, or empty when nothing was written. */
    FString WritePostMortem();

    /** Where post-mortems go: Saved/<PostMortemDirectory>. */
    FString GetPostMortemDirectory();

    /** The play is over: its post-mortem is written when that is on. Once per play. */
    void FinishPlay();

    /** Listens for the snap, the end of the play and the calls. Initialize binds the world's. */
    void BindToBus(UPSTelemetryBus* Bus);

    void UnbindFromBus();

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);
    void HandlePlayCall(const FPSTelemetryPlayCallEvent& Event);
    void DrawOverlay();

    /** True while nothing but play calls has gone on the bus since the current play's snap. */
    bool IsAtSnap() const;

    UPROPERTY(Transient)
    FPSAIDebugTuning Tuning;

    UPROPERTY(Transient)
    TArray<FPSAIDecisionRecord> PlayRecords;

    /** CPU play calls made for the coming snap, which belong to the play it starts. */
    UPROPERTY(Transient)
    TArray<FPSAIDecisionRecord> PendingRecords;

    /** The latest record per player this play. */
    TMap<FName, FPSAIDecisionRecord> Latest;

    /** The calls announced for the coming snap, and the ones the current play was run with. */
    TArray<FPSTelemetryPlayCallEvent> PendingCalls;
    TArray<FPSTelemetryPlayCallEvent> PlayCalls;

    FPSTelemetrySnapEvent Snap;
    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    int32 PlayIndex = 0;
    int32 SnapSequence = 0;
    /** -1: the tuning decides; 0 or 1: SetLogging / SetWritePostMortems did. */
    int32 LoggingOverride = -1;
    int32 PostMortemOverride = -1;
    /** -1: the console variable decides; 0 or 1: SetOverlayEnabled did. */
    int32 OverlayOverride = -1;
    /** Drawing layers showing the overlay now. */
    int32 OverlayLayers = 0;
    bool bPlayOpen = false;
    bool bTuningLoaded = false;
};
