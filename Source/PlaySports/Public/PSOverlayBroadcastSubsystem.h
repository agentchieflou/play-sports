// PSOverlayBroadcastSubsystem.h - Epic 33: the score bug's view of the game and the chyron queue
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSOverlayBroadcastTypes.h"
#include "PSPlatformTiers.h"
#include "PSTelemetryBus.h"
#include "PSUITeamCatalog.h"
#include "PSOverlayBroadcastSubsystem.generated.h"

/**
 * UPSOverlayBroadcastSubsystem is the broadcast package's model (Epic 33): what the score bug
 * and the lower-third chyrons show. The widgets (UPSOverlayScoreBugWidget,
 * UPSOverlayChyronWidget, made by APSHUD) only draw it.
 *
 *  - Score bug: the teams, the score, possession, timeouts, the quarter and both clocks, and
 *    down and distance, from UPSPlaySimulation's GameState events: the simulation is the
 *    authority (rule 6) and the bus the only way in (rule 5). Between events the clocks run on
 *    by themselves while the event says they run. The red-zone and two-minute states are
 *    worked out from the theme's thresholds.
 *  - Chyrons: one at a time, from a queue ordered by the theme's priority for each kind, first
 *    come first served within a priority. A higher priority one cuts in once the one on
 *    screen has been up ChyronMinShowSeconds; the queue holds ChyronMaxQueued at most. The
 *    bus feeds it: points (score alerts), finished drives (drive summaries), sacks, runs and
 *    interceptions (play lines). PushStatLine is the door for a stats source (Epic 92).
 *  - Detail: the platform tier's OverlayDetail. Minimal keeps the score bug and turns chyrons
 *    off.
 *
 * Team identity: SetTeams, and at the start of play the level's team= (home) and away=
 * options. The theme (Data/broadcast_overlay.json) is the skin.
 *
 * It ticks with its world; headless tests call AdvanceTime.
 */
UCLASS()
class PLAYSPORTS_API UPSOverlayBroadcastSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultThemePath();

    /** Replaces the theme with JsonFilePath's, read through UPSDataIngestion. A theme that
     *  fails ValidateTheme is refused and the current one kept. */
    bool LoadThemeFromJson(const FString& JsonFilePath);

    void SetTheme(const FPSBroadcastOverlayTheme& NewTheme);

    const FPSBroadcastOverlayTheme& GetTheme() const { return Theme; }

    /** Problems with a theme, one line each (empty when sound). */
    static TArray<FString> ValidateTheme(const FPSBroadcastOverlayTheme& InTheme);

    /** Names the teams (Data/sample_teams.json IDs); NAME_None leaves a side to the theme's
     *  label and color. */
    UFUNCTION(BlueprintCallable, Category = "Broadcast")
    void SetTeams(FName InHomeTeamId, FName InAwayTeamId);

    UFUNCTION(BlueprintCallable, Category = "Broadcast")
    void SetOverlayDetail(EPSOverlayDetail InDetail);

    UFUNCTION(BlueprintPure, Category = "Broadcast")
    EPSOverlayDetail GetOverlayDetail() const { return OverlayDetail; }

    UFUNCTION(BlueprintPure, Category = "Broadcast")
    FPSScoreBugState GetScoreBug() const { return ScoreBug; }

    /** The chyron on screen; false while none is. */
    UFUNCTION(BlueprintCallable, Category = "Broadcast")
    bool GetCurrentChyron(FPSChyron& OutChyron) const;

    /** How long the chyron on screen has been up. */
    UFUNCTION(BlueprintPure, Category = "Broadcast")
    float GetChyronShownSeconds() const { return bChyronShowing ? ChyronElapsed : 0.f; }

    UFUNCTION(BlueprintPure, Category = "Broadcast")
    int32 GetQueuedChyronCount() const { return ChyronQueue.Num(); }

    /** Queues a chyron with its kind's priority and time from the theme. False when chyrons
     *  are off (a Minimal tier) or the text is empty. */
    UFUNCTION(BlueprintCallable, Category = "Broadcast")
    bool PushChyron(EPSChyronKind Kind, const FString& Headline, const FString& Detail);

    /** A player's line from a stats source, e.g. "5 catches, 72 yards" (Epic 92). */
    UFUNCTION(BlueprintCallable, Category = "Broadcast")
    bool PushStatLine(const FString& PlayerName, const FString& StatText);

    /** One step: the clocks run on, chyrons come and go. The tick calls this; headless tests
     *  call it directly. */
    void AdvanceTime(float DeltaSeconds);

private:
    void HandleGameState(const FPSTelemetryGameStateEvent& Event);
    void HandleScore(const FPSTelemetryScoreEvent& Event);
    void HandleCatch(const FPSTelemetryCatchEvent& Event);
    void HandleTackle(const FPSTelemetryTackleEvent& Event);

    /** Texts and special states from the numbers in ScoreBug. */
    void RefreshDerived();
    /** Labels and colors from the teams and the theme. */
    void RefreshTeams();
    const FPSTeamSummary* FindTeam(FName TeamId);
    FString ScoreLine() const;

    void ShowNextChyron();

    FPSBroadcastOverlayTheme Theme;
    FPSScoreBugState ScoreBug;

    FPSTelemetryGameStateEvent LastGameState;
    bool bHasGameState = false;

    /** The score a Score event already announced, so the GameState that follows doesn't
     *  announce it again. */
    int32 AnnouncedHomeScore = 0;
    int32 AnnouncedAwayScore = 0;

    FName HomeTeamId;
    FName AwayTeamId;
    TArray<FPSTeamSummary> Teams;
    bool bTeamsLoaded = false;

    /** Waiting chyrons, best first. */
    TArray<FPSChyron> ChyronQueue;
    FPSChyron CurrentChyron;
    bool bChyronShowing = false;
    float ChyronElapsed = 0.f;
    float GapRemaining = 0.f;
    int32 NextChyronOrder = 0;

    EPSOverlayDetail OverlayDetail = EPSOverlayDetail::Full;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
};
