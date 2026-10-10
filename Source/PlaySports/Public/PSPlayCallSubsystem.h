// PSPlayCallSubsystem.h - Epic 102: the one authority on the play each side runs next
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSCoachingData.h"
#include "PSMenuTypes.h"
#include "PSPlayCallTypes.h"
#include "PSPlaybookData.h"
#include "PSTelemetryBus.h"
#include "PSPlayCallSubsystem.generated.h"

class APSPlayerPawn;
class UDataTable;
class UPSSaveSubsystem;
class UPSCoachingAI;
class UPSPlayOrchestrator;
struct FPlayState;

DECLARE_MULTICAST_DELEGATE_OneParam(FPSHumanCallNeededMC, bool /* bOffense */);

/**
 * UPSPlayCallSubsystem holds each side's call for the coming snap -- the single source of
 * truth for "which play is being run" (Architecture rule 6) -- and makes the calls real:
 *
 *  - The playbook (Data/sample_playbook.json, routes in Data/sample_routes.json) loads
 *    through UPSPlaybookIngestion.
 *  - APSGameMode opens a call window at every scrimmage down (OpenPlayCall) and snaps when
 *    PollReadyToSnap says so. A side no human controls is called by UPSCoachingAI; a side a
 *    human controls waits for that player's call (the play-call screens, UPSPlayCallComponent).
 *    A CPU offense snaps CpuSnapDelaySeconds after both calls are in; a human offense snaps
 *    when its player hikes (RequestSnap).
 *  - At the Snap event both calls go to UPSPlayOrchestrator, which hands every AI pawn its
 *    assignment.
 *
 * Which pawns humans control comes from the bus (ControlChange); every call is announced on
 * it (PlayCall).
 */
UCLASS()
class PLAYSPORTS_API UPSPlayCallSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    static FString GetDefaultPlaybookPath();
    static FString GetDefaultRoutesPath();
    static FString GetDefaultTuningPath();
    static FString GetDefaultAdjustmentsPath();

    /** Replaces the playbook with these files, read through UPSPlaybookIngestion. */
    bool LoadPlaybook(const FString& PlaysJsonPath, const FString& RoutesJsonPath);

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** The tuning in use, loaded from the default path on first use. */
    const FPlayCallTuningRow& GetTuning();

    /** A side's plays in playbook order (the default playbook loads on first use). */
    TArray<FPSPlayDefinition> GetPlays(bool bOffense);

    /** A side's formations, each once, in playbook order. */
    TArray<FString> GetFormations(bool bOffense);

    TArray<FPSPlayDefinition> GetPlaysInFormation(const FString& Formation, bool bOffense);

    bool FindPlay(FName PlayId, FPSPlayDefinition& OutPlay);

    /** The route library plays resolve against. */
    const UDataTable* GetRouteLibrary();

    /** Menu options for the play-call screens (Epic 101's screen stack): one per formation,
     *  opening PlaysScreenId with the formation as payload ... */
    TArray<FPSMenuOptionDef> BuildFormationOptions(bool bOffense, FName PlaysScreenId);

    /** ... and one per play in Formation, calling it. */
    TArray<FPSMenuOptionDef> BuildPlayOptions(const FString& Formation, bool bOffense);

    /** The top-ranked play for the side in the current situation, as one option that calls
     *  it, with the coaching AI's reasons as its detail (102.2). Empty with no plays. */
    TArray<FPSMenuOptionDef> BuildSuggestionOptions(bool bOffense);

    /** One option per recent human call for the side, most recent first (102.3). */
    TArray<FPSMenuOptionDef> BuildRecentOptions(bool bOffense);

    /** The call screen's body: the situation, and the player's tendencies once they have
     *  called plays (102.3), e.g. "3rd & 7 at own 35" / "Your calls: Run 50% ...". */
    FString BuildCallScreenBody(bool bOffense) const;

    /** The side's plays ranked for the current situation, with reasons (UPSCoachingAI). */
    TArray<FPSPlaySuggestion> RankPlays(bool bOffense);

    /** "3rd & 7 at own 35", from the situation the window opened with. */
    static FString DescribeSituation(const FPSSituationContext& InSituation);

    /** Distinct plays the human called and ran for the side, most recent first. */
    TArray<FName> GetRecentCalls(bool bOffense, int32 MaxCount) const;

    /** "Your calls: Run 67% / Short pass 33%" over the side's history; empty without one. */
    FString DescribeTendencies(bool bOffense) const;

    const TArray<FPSPlayCallRecord>& GetCallHistory() const { return CallHistory; }

    /** The pre-snap defensive adjustments (102.4), loaded on first use. */
    const TArray<FPSDefensiveAdjustmentDef>& GetAdjustments();

    /** Replaces the adjustments with JsonFilePath's, read through UPSDataIngestion. */
    bool LoadAdjustmentsFromJson(const FString& JsonFilePath);

    /** Problems with an adjustment table, one line each: empty or duplicate IDs, no label,
     *  a role that isn't a defender's, or a kind that isn't a defensive assignment. */
    static TArray<FString> ValidateAdjustments(const FPSDefensiveAdjustmentCatalog& InCatalog);

    /** Applies AdjustmentId over the defense's call for this snap; NAME_None clears it. False
     *  when the window is closed, the defense hasn't called, or the ID is unknown. */
    bool SetDefensiveAdjustment(FName AdjustmentId);

    FName GetDefensiveAdjustment() const { return DefensiveAdjustment; }

    /** The defense's call as it will run: the play with the adjustment applied. */
    bool GetDefensivePlayToRun(FPSPlayDefinition& OutPlay);

    /** The adjustments screen: "No adjustment", then one option per adjustment. */
    TArray<FPSMenuOptionDef> BuildAdjustmentOptions();

    /** "Offense lines up in Trips Right" once the offense has called. */
    FString BuildAdjustmentScreenBody() const;

    /** Favourite plays (102.3), kept in the player's profile save when a game instance has
     *  UPSSaveSubsystem (in memory otherwise, e.g. headless tests). */
    bool IsFavorite(FName PlayId);

    /** Stars or unstars PlayId and saves; returns whether it is now a favourite. */
    bool ToggleFavorite(FName PlayId);

    /** The side's favourite plays, in the order they were starred. */
    TArray<FName> GetFavorites(bool bOffense);

    /** One option per favourite play of the side, each calling it. */
    TArray<FPSMenuOptionDef> BuildFavoriteOptions(bool bOffense);

    /** A one-line text stand-in for play art until Track A's art pipeline (Epic 35) exists:
     *  "WR Slant, RB Flat, TE pass block" (dot-separated). */
    static FString DescribePlay(const FPSPlayDefinition& Play);

    /** The coaching AI's view of a play state, from the possessing team's side. */
    static FPSSituationContext MakeSituation(const FPlayState& State);

    /** A new scrimmage down: clears both calls and opens the window. Sides a human controls
     *  are announced on OnHumanCallNeeded; the others are called on the next poll. */
    void OpenPlayCall(const FPSSituationContext& InSituation);

    bool IsCallWindowOpen() const { return bWindowOpen; }

    const FPSSituationContext& GetSituation() const { return Situation; }

    /** The game mode passes the live play clock every pre-snap tick (102.5). */
    void SetPlayClock(float Seconds) { PlayClockSeconds = Seconds; }

    /** The live play clock, or a negative number before the game mode has set one. */
    float GetPlayClockSeconds() const { return PlayClockSeconds; }

    /** Calls PlayId for its side (the play says which). False when the window is closed or
     *  the play is unknown. A new call replaces the side's earlier one. */
    bool CallPlay(FName PlayId, EPSPlayCaller Caller);

    const FPSPlayCall& GetCall(bool bOffense) const { return bOffense ? OffenseCall : DefenseCall; }

    /** True while a human controls a pawn on that side. */
    bool IsHumanSide(bool bOffense) const;

    /** True while the window is open and that side waits for its human's call. */
    bool IsWaitingForHuman(bool bOffense) const;

    /** The human offense hikes. False unless the window is open and a human made the
     *  offense's call. */
    bool RequestSnap();

    /** Called every pre-snap tick by the game mode: fills CPU calls for sides no human
     *  controls, quick-calls a human's side that ran low on play clock, then says whether
     *  the offense snaps now. */
    bool PollReadyToSnap(float DeltaSeconds);

    /** A side now waits for its human's call (bOffense says which). */
    FPSHumanCallNeededMC OnHumanCallNeeded;

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandleControlChange(const FPSTelemetryControlChangeEvent& Event);

    void EnsurePlaybookLoaded();
    void EnsureFavoritesLoaded();
    void SaveFavorites();
    UPSSaveSubsystem* GetSaveSubsystem() const;
    FPSMenuOptionDef MakePlayOption(const FPSPlayDefinition& Play, const FString& Label);
    void CallForCpu(bool bOffense);
    void QuickCall(bool bOffense);
    void SetCall(const FPSPlayDefinition& Play, EPSPlayCaller Caller);
    void Distribute(const FVector& LineOfScrimmage);
    APSPlayerPawn* FindPawnByPlayerId(FName PlayerId) const;

    UPROPERTY(Transient)
    UDataTable* PlaysTable;

    UPROPERTY(Transient)
    UDataTable* RoutesTable;

    /** PlaysTable's rows in playbook order. */
    UPROPERTY(Transient)
    TArray<FPSPlayDefinition> Plays;

    UPROPERTY(Transient)
    UPSCoachingAI* CoachingAI;

    UPROPERTY(Transient)
    UPSPlayOrchestrator* Orchestrator;

    UPROPERTY(Transient)
    FPlayCallTuningRow Tuning;

    UPROPERTY(Transient)
    FPSSituationContext Situation;

    UPROPERTY(Transient)
    FPSPlayCall OffenseCall;

    UPROPERTY(Transient)
    FPSPlayCall DefenseCall;

    UPROPERTY(Transient)
    FPSDefensiveAdjustmentCatalog AdjustmentCatalog;

    UPROPERTY(Transient)
    TArray<FName> FavoritePlays;

    /** Every play a human called and ran this game, oldest first. */
    UPROPERTY(Transient)
    TArray<FPSPlayCallRecord> CallHistory;

    /** PlayerId of each human-controlled pawn -> whether it plays offense. */
    TMap<FName, bool> HumanPawnSides;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    float TimeSinceCallsComplete = 0.f;
    float PlayClockSeconds = -1.f;
    FName DefensiveAdjustment;
    bool bAdjustmentsLoaded = false;
    bool bFavoritesLoaded = false;
    bool bWindowOpen = false;
    bool bSnapRequested = false;
    bool bPlaybookLoaded = false;
    bool bTuningLoaded = false;
};
