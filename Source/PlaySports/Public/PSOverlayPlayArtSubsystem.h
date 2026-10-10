// PSOverlayPlayArtSubsystem.h - Epics 27 and 31: both sides' calls drawn on the field before the snap
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Stats/Stats.h"
#include "UObject/ObjectKey.h"
#include "PSPlatformTiers.h"
#include "PSPlayArtTypes.h"
#include "PSPlayResolution.h"
#include "PSTelemetryBus.h"
#include "PSVersusTypes.h"
#include "PSOverlayPlayArtSubsystem.generated.h"

class APlayerController;
class APSPlayerPawn;
class UPSSettingsSubsystem;

/**
 * UPSOverlayPlayArtSubsystem is the pre-snap play art's model, as primitives (PSPlayArtTypes.h)
 * a renderer draws:
 *
 *  - Route art (Epic 27): the offense's call (UPSPlayCallSubsystem, the authority on it)
 *    resolved by PSPlayResolution -- the same resolution UPSPlayOrchestrator hands the AI at the
 *    snap -- against the line the GameState event announces. So the ribbons are the routes the AI
 *    will run, hot routes and protection changes included (Epic 66). PSPlayArt::CompileRouteArt
 *    makes them: a ribbon from each receiver's feet with a ring at its end, cuts by the
 *    route-running model's break angle (Data/route_running.json), fakes, option branches, and
 *    color by the play's ReadOrder (primary read vs. check-down).
 *  - Defensive icons (Epic 31): the defense's call as it will run (its adjustment applied),
 *    resolved the same way, its man matchups taken as the defense AI will take them at the snap
 *    (PSPlayResolution::ResolveManMatchups: a shadow, a press, the nearest open receiver).
 *    PSPlayArt::CompileDefenseArt makes them: a star at each zone landmark, a line from each man
 *    defender to his receiver, an arrow from each rusher through the line.
 *  - When: from a side's call to the snap. The art is rebuilt on the tick after each PlayCall,
 *    PreSnap, DefensivePreSnap and GameState event (once every system has heard it), and
 *    PlayArtRefreshHz times a second (the platform tier's) to follow shifts, motion and the
 *    defense lining up. At the snap it fades over SnapFadeSeconds on a Full tier and goes at once
 *    on others; a Minimal tier draws none. The next down starts afresh.
 *  - Who sees it: the RouteArt and DefenseIcons settings (Data/ui_settings.json) turn each off.
 *    Head to head, the house rules decide (UPSVersusSubsystem::ShouldShowOverlay, Epic 107).
 *    Otherwise each side's art is its own, like its call: a player on the other side doesn't see
 *    it -- except the defense's icons in study mode (the StudyMode setting), which shows the
 *    defense's call to the offense to learn to read coverages. A spectator sees both.
 *  - Annotations (Epic 35): each assignment's Art block in the play data -- a color, emphasis, a
 *    badge letter -- is layered on as the art compiles (PSPlayArt::CompilePlayArt); the letters
 *    reach the position badges through GetBadgeLetter, under the same visibility as the art.
 *  - Drawing: until the editor-made renderer exists (Specs/Route_Ribbons_Spec.md,
 *    Specs/Defensive_Icons_Spec.md), development builds draw debug shapes (PSPlayArt::DrawDebug)
 *    for the first local player.
 *
 * The style is Data/play_art.json. It ticks with its world; headless tests call AdvanceTime.
 */
UCLASS()
class PLAYSPORTS_API UPSOverlayPlayArtSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    /** The settings (Gameplay, Data/ui_settings.json): route art on or off ... */
    static const FName RouteArtSettingId;

    /** ... the defense's icons on or off ... */
    static const FName DefenseIconsSettingId;

    /** ... and study mode, which shows the defense's icons to the offense too. */
    static const FName StudyModeSettingId;

    static FString GetDefaultStylePath();

    /** Replaces the style with JsonFilePath's, read through UPSDataIngestion. A style that fails
     *  ValidateStyle is refused and the current one kept. */
    bool LoadStyleFromJson(const FString& JsonFilePath);

    void SetStyle(const FPSPlayArtStyle& InStyle);

    const FPSPlayArtStyle& GetStyle() const { return Style; }

    /** Problems with a style, one line each (empty when sound): PSPlayArt::ValidateStyle. */
    static TArray<FString> ValidateStyle(const FPSPlayArtStyle& InStyle);

    /** The route art now: empty before the offense calls, after the fade, on a Minimal tier,
     *  with the setting off, and for a kick or clock play. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    TArray<FPSPlayArtPrimitive> GetRouteArt() const { return RouteArt; }

    /** The defense's icons now: empty before the defense calls, after the fade, on a Minimal
     *  tier, with the setting off, and for a return or a kick block. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    TArray<FPSPlayArtPrimitive> GetDefenseArt() const { return DefenseArt; }

    /** How opaque the art is drawn now (0-1): 1 before the snap, running down to 0 as it fades. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    float GetOpacity() const;

    /** True from the snap until the art has faded. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    bool IsFading() const { return bSnapped && (RouteArt.Num() > 0 || DefenseArt.Num() > 0); }

    /** The offense's play the route art is of; none without art. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    FName GetPlayId() const { return PlayId; }

    /** The defense's play the icons are of; none without them. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    FName GetDefensePlayId() const { return DefensePlayId; }

    /** Whether Viewer's view draws the route art: there is some and, head to head, the versus
     *  rules let Viewer see it; otherwise Viewer doesn't play defense. A null viewer is a
     *  spectator. */
    bool IsVisibleTo(const APlayerController* Viewer) const;

    /** Whether Viewer's view draws the defense's icons: there are some and, head to head, the
     *  versus rules let Viewer see them; otherwise Viewer doesn't play offense, or study mode is
     *  on. A null viewer is a spectator. */
    bool IsDefenseArtVisibleTo(const APlayerController* Viewer) const;

    /** The letter the play's art gives Player for his position badge (Art.BadgeLetter, Epic 35),
     *  when Viewer may see his side's art; empty otherwise. UPSOverlayBadgeComponent shows it on a
     *  player without a pass button. */
    FString GetBadgeLetter(const APSPlayerPawn* Player, const APlayerController* Viewer) const;

    UFUNCTION(BlueprintCallable, Category = "Overlay")
    void SetOverlayDetail(EPSOverlayDetail InDetail);

    UFUNCTION(BlueprintPure, Category = "Overlay")
    EPSOverlayDetail GetOverlayDetail() const { return OverlayDetail; }

    /** How often a second the art follows the players before the snap; 0 on events only. */
    void SetRefreshHz(float InRefreshHz);

    /** Reads the settings from these instead of the game instance's (headless tests, where there
     *  is no game instance). */
    void SetSettings(UPSSettingsSubsystem* InSettings) { SettingsOverride = InSettings; }

    /** Whether a toggle setting is on; bWithoutIt when there are no settings or no such setting. */
    bool IsSettingOn(FName SettingId, bool bWithoutIt = true) const;

    /** Resolves both calls again and rebuilds the art now; nothing after the snap until the next
     *  down. AdvanceTime does this after an event and at the refresh rate. */
    void Refresh();

    /** Moves the art's clock on. Before the snap it rebuilds the art when an event has made it
     *  stale (so every system has heard the event first) or the refresh rate is due; after the
     *  snap it fades it. The tick calls this; headless tests call it directly (0 seconds just
     *  rebuilds stale art). */
    void AdvanceTime(float DeltaSeconds);

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandlePlayCall(const FPSTelemetryPlayCallEvent& Event);
    void HandlePreSnap(const FPSTelemetryPreSnapEvent& Event);
    void HandleDefensivePreSnap(const FPSTelemetryDefensivePreSnapEvent& Event);
    void HandleGameState(const FPSTelemetryGameStateEvent& Event);
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    void RebuildRouteArt();
    void RebuildDefenseArt();

    /** Each player's badge letter from his slot's annotation, kept beside the art. */
    static void KeepBadgeLetters(const TArray<FPSResolvedAssignment>& Resolved, TMap<FObjectKey, FString>& OutLetters);

    /** True in a head-to-head session, with bOutShown whether its house rules let Viewer see
     *  Overlay. */
    bool GetVersusVerdict(EPSVersusOverlay Overlay, const APlayerController* Viewer, bool& bOutShown) const;

    UPSSettingsSubsystem* GetSettings() const;
    float GetBreakMinAngleDegrees();
    void Clear();

    FPSPlayArtStyle Style;
    EPSOverlayDetail OverlayDetail = EPSOverlayDetail::Full;
    float RefreshHz = 30.f;

    UPROPERTY(Transient)
    TArray<FPSPlayArtPrimitive> RouteArt;

    UPROPERTY(Transient)
    TArray<FPSPlayArtPrimitive> DefenseArt;

    UPROPERTY(Transient)
    UPSSettingsSubsystem* SettingsOverride = nullptr;

    FName PlayId;
    FName DefensePlayId;
    /** Badge letters by player, for each side's art. */
    TMap<FObjectKey, FString> OffenseBadgeLetters;
    TMap<FObjectKey, FString> DefenseBadgeLetters;
    /** The line the GameState event last announced. */
    FVector LineOfScrimmage = FVector::ZeroVector;
    bool bHasLine = false;
    /** From the snap to the next down. */
    bool bSnapped = false;
    /** An event changed what the art shows; rebuilt on the next AdvanceTime. */
    bool bStale = false;
    float FadeRemaining = 0.f;
    float RefreshClock = 0.f;
    /** The settings' values at the last tick, to see them change. */
    bool bRouteSettingWasOn = true;
    bool bDefenseSettingWasOn = true;
    /** What a cut is, from the route-running tuning, loaded on first use. */
    float BreakMinAngleDegrees = 30.f;
    bool bBreakAngleLoaded = false;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
};
