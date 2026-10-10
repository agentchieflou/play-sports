// PSGameIntelligenceSubsystem.h - Epic 82: the game's hooks for outside models, gated on the bridge
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSCoachingData.h"
#include "PSCoachingSuggestionProvider.h"
#include "PSGameIntelligenceTypes.h"
#include "PSGameStateSerializer.h"
#include "PSPlaybookData.h"
#include "PSTelemetryBus.h"
#include "PSGameIntelligenceSubsystem.generated.h"

class UPSPersonnelManager;
class UPSStatsEngine;

DECLARE_MULTICAST_DELEGATE_OneParam(FPSIntelRequestMC, const FPSIntelRequest& /* Request */);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSIntelRequestSignature, const FPSIntelRequest&, Request);

/**
 * UPSGameIntelligenceSubsystem is where outside models meet the game (Epic 82). Everything is
 * gated on the Epic 25 bridge: while AgenticLink's MCP server isn't serving (IsBridgeOnline),
 * every hook is refused and the game plays on exactly as without it.
 *
 *  - The game-state contract (82.1): GetGameStateJson is PSGameStateSerializer's compact JSON of
 *    the situation (the play simulation's GameState on the bus), the personnel
 *    (UPSPersonnelManager), the human's tendencies (UPSOpponentModel) and the game's statistics
 *    (UPSStatsEngine), within ContextBudgetChars.
 *  - Play-call consultation (82.2, Epic 18's hook made real): an agent turns it on per side
 *    (SetConsultation). When UPSPlayCallSubsystem opens a call window for a CPU side it opens a
 *    PlayCall request: the game state, and the side's plays this down as the only answers taken.
 *    The side's call waits (WaitForConsultation, from the play-call poll) until the request is
 *    answered or PlayCallTimeoutSeconds pass. This subsystem is the coaching AI's suggestion
 *    provider: an answered play is the CPU's call, unless the clock or special teams call one
 *    outright; with no answer the CPU calls its own. An answer that isn't one of the choices is
 *    refused and the request stays open.
 *  - Post-game analysis (82.3): the drives come from the GameState events; the key plays are Epic
 *    42's highlights (UPSHighlightSubsystem), the most important first. At the final whistle,
 *    with the bridge online, a DriveSummary and a GameAnalysis request ask a model to write them.
 *  - Model-slot routing (82.4): each request names the model router's task it is for (Epic 119's
 *    tools/orchestrator/routing.json: strategy for play calls, summary and analysis after the
 *    game); `python -m tools.orchestrator game-hooks` relays them through the router.
 *
 * An agent polls GetPendingRequestsJson and answers with AnswerRequest, both through
 * AgenticLink's call_function on this subsystem; in-process code can bind OnRequestOpenedMC and
 * answer the same way. Tuning: Data/game_intelligence.json.
 */
UCLASS()
class PLAYSPORTS_API UPSGameIntelligenceSubsystem : public UWorldSubsystem, public IPSCoachingSuggestionProvider
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // --- Tuning ----------------------------------------------------------------------------

    static FString GetDefaultTuningPath();

    /** The tuning, loaded from the default path on first use. */
    const FPSGameIntelligenceTuning& GetTuning();

    /** Replaces the tuning with JsonFilePath's (through UPSDataIngestion); a file that can't be
     *  read or fails ValidateTuning is refused and the current tuning kept. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Applies InTuning when it passes ValidateTuning. */
    bool SetTuning(const FPSGameIntelligenceTuning& InTuning);

    /** Problems with InTuning, one line each (empty when sound). */
    static TArray<FString> ValidateTuning(const FPSGameIntelligenceTuning& InTuning);

    // --- The bridge ------------------------------------------------------------------------

    /** The modular feature AgenticLink registers while its MCP server serves
     *  (FAgenticLinkModule::GetBridgeFeatureName): "AgenticLinkBridge". */
    static FName GetBridgeFeatureName();

    /** True while the Epic 25 bridge serves. */
    UFUNCTION(BlueprintPure, Category = "GameIntelligence")
    static bool IsBridgeOnline();

    // --- The game-state contract (82.1) ------------------------------------------------------

    /** The match's personnel manager and statistics, which the game mode owns. */
    void SetStateSources(UPSPersonnelManager* InPersonnel, UPSStatsEngine* InStats);

    /** The sources the contract reads now. */
    FPSGameStateSources GatherSources();

    /** The game state as PSGameStateSerializer writes it, within ContextBudgetChars. */
    UFUNCTION(BlueprintCallable, Category = "GameIntelligence")
    FString GetGameStateJson();

    /** True once the bus has announced a game state. */
    bool HasGameState() const { return bHaveState; }

    // --- Play-call consultation (82.2) -------------------------------------------------------

    /** An agent asks to call the plays of the CPU's offense and/or defense. False (nothing
     *  changes) while the bridge is offline. */
    UFUNCTION(BlueprintCallable, Category = "GameIntelligence")
    bool SetConsultation(bool bOffense, bool bDefense);

    /** True while the bridge is online and an agent asked to call that side's plays. */
    UFUNCTION(BlueprintPure, Category = "GameIntelligence")
    bool IsConsulting(bool bOffense) const;

    /** A call window opened for a CPU side: opens its PlayCall request with Candidates (the
     *  side's plays this down) as the only answers. Returns the request's id, or 0 when refused
     *  (bridge offline, the side not consulted, no plays). Replaces the side's earlier request. */
    int32 OpenPlayConsultation(bool bOffense, const FPSSituationContext& Situation, const TArray<FPSPlayDefinition>& Candidates);

    /** The play-call poll asks whether the side's call should wait: true while its request is
     *  open and DeltaSeconds more stay within the timeout. Past it the request times out and the
     *  CPU calls its own. */
    bool WaitForConsultation(bool bOffense, float DeltaSeconds);

    /** The side's answered play, or None. */
    FName GetAnsweredPlay(bool bOffense) const;

    /** The snap: each side's request is settled against the play it runs (Used, Overruled) or
     *  closed unanswered. */
    void CloseConsultations(FName OffensePlayId, FName DefensePlayId);

    /** IPSCoachingSuggestionProvider: the side's answered play, which the coaching AI runs if it
     *  is among its candidates; None otherwise. */
    virtual FName SuggestPlay_Implementation(const FPSSituationContext& Situation, bool bOffense) override;

    // --- Requests --------------------------------------------------------------------------

    /** {"bridge":bool,"requests":[...open requests: id, kind, task, instructions, side,
     *  secondsLeft, choices, context]}: what an agent polls. */
    UFUNCTION(BlueprintCallable, Category = "GameIntelligence")
    FString GetPendingRequestsJson();

    /** Answers an open request. False with OutReason when the bridge is offline, the request is
     *  unknown or no longer open, the answer is empty or too long, or (for a request with
     *  choices) it isn't one of them; the request then stays as it was. */
    UFUNCTION(BlueprintCallable, Category = "GameIntelligence")
    bool AnswerRequest(int32 RequestId, const FString& Answer, FString& OutReason);

    /** A free-text request from another system (Epic 93's news digest): Context (compact JSON
     *  the caller kept within its own budget) for the model-router Task, answered with any text.
     *  Nothing waits on it. Returns its id, or 0 when the bridge is offline or a field is empty;
     *  the answer arrives on OnRequestAnsweredMC. */
    int32 OpenTextRequest(EPSIntelRequestKind Kind, const FString& Task, const FString& Instructions, const FString& Context);

    /** Every request still kept, oldest first. */
    const TArray<FPSIntelRequest>& GetRequests() const { return Requests; }

    bool FindRequest(int32 RequestId, FPSIntelRequest& OutRequest) const;

    /** A request opened, for a listener that answers in process. */
    FPSIntelRequestMC OnRequestOpenedMC;

    UPROPERTY(BlueprintAssignable, Category = "GameIntelligence")
    FPSIntelRequestSignature OnRequestOpened;

    /** A request was answered. */
    FPSIntelRequestMC OnRequestAnsweredMC;

    // --- Post-game analysis (82.3) -----------------------------------------------------------

    /** The game's analysis as it stands: the score, the drives so far and the key plays, with
     *  any texts a model has written. */
    UFUNCTION(BlueprintCallable, Category = "GameIntelligence")
    FPSGameAnalysis BuildPostGameAnalysis();

    /** The analysis built at the final whistle (or by the last BuildPostGameAnalysis). */
    const FPSGameAnalysis& GetPostGameAnalysis() const { return Analysis; }

    /** BuildPostGameAnalysis as PSGameStateSerializer::SerializeAnalysis writes it. */
    UFUNCTION(BlueprintCallable, Category = "GameIntelligence")
    FString GetPostGameAnalysisJson();

    /** The game's finished drives, oldest first. */
    const TArray<FPSDriveRecap>& GetDrives() const { return Drives; }

    UFUNCTION(BlueprintPure, Category = "GameIntelligence")
    bool IsGameOver() const { return bGameOver; }

    /** Follows the play simulation's GameState events. Initialize binds the world's bus. */
    void BindToBus(UPSTelemetryBus* Bus);

    void UnbindFromBus();

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandleGameState(const FPSTelemetryGameStateEvent& Event);

    /** Forgets the game: its drives, its analysis and its requests. */
    void ResetGame();

    /** Adds Request (with a new id), closing the oldest request nothing waits on when too many
     *  are open, and tells the listeners. Returns the id. */
    int32 AddRequest(FPSIntelRequest Request);

    FPSIntelRequest* FindRequestMutable(int32 RequestId);

    /** At the final whistle: the drive summary and game analysis requests. */
    void OpenPostGameRequests();

    UPROPERTY(Transient)
    FPSGameIntelligenceTuning Tuning;

    bool bTuningLoaded = false;

    TWeakObjectPtr<UPSPersonnelManager> Personnel;
    TWeakObjectPtr<UPSStatsEngine> Stats;

    /** The latest game state on the bus. */
    FPSTelemetryGameStateEvent LastState;
    bool bHaveState = false;
    bool bGameOver = false;

    UPROPERTY(Transient)
    TArray<FPSDriveRecap> Drives;

    UPROPERTY(Transient)
    FPSGameAnalysis Analysis;

    UPROPERTY(Transient)
    TArray<FPSIntelRequest> Requests;

    int32 NextRequestId = 1;

    /** Each side's PlayCall request for the coming snap ([0] defense, [1] offense); 0 for none. */
    int32 PlayCallRequestIds[2] = { 0, 0 };

    /** Which sides an agent asked to call ([0] defense, [1] offense). */
    bool bConsultSide[2] = { false, false };

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    FDelegateHandle GameStateHandle;
};
