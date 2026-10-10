// PSOverlayPlayArtSubsystem.h - Epic 27: the offense's routes drawn on the field before the snap
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Stats/Stats.h"
#include "PSPlatformTiers.h"
#include "PSPlayArtTypes.h"
#include "PSTelemetryBus.h"
#include "PSOverlayPlayArtSubsystem.generated.h"

class APlayerController;
class UPSSettingsSubsystem;

/**
 * UPSOverlayPlayArtSubsystem is the pre-snap play art's model (Epic 27): the reference frame's
 * route ribbons with a ring where each route ends, as primitives (PSPlayArtTypes.h) a renderer
 * draws.
 *
 *  - What: the offense's call (UPSPlayCallSubsystem, the authority on it) resolved by
 *    PSPlayResolution, the same resolution UPSPlayOrchestrator hands the AI at the snap, against
 *    the line the GameState event announces. So the ribbons are the routes the AI will run,
 *    hot routes and protection changes included (Epic 66). PSPlayArt::CompileRouteArt makes the
 *    primitives: cuts by the route-running model's break angle (Data/route_running.json),
 *    fakes, option branches, and color by the play's ReadOrder (primary read vs. check-down).
 *  - When: from the offense's call to the snap. It is rebuilt on the tick after each PlayCall,
 *    PreSnap and GameState event (once every system has heard it), and PlayArtRefreshHz times a
 *    second (the platform tier's) to follow motion. At the snap it fades over SnapFadeSeconds on
 *    a Full tier and goes at once on others; a Minimal tier draws none. The next down starts
 *    afresh.
 *  - Who sees it: the RouteArt setting (Data/ui_settings.json) turns it off. Head to head, the
 *    house rules decide (UPSVersusSubsystem::ShouldShowOverlay, Epic 107). Otherwise it is the
 *    offense's art, like its call: a player on defense doesn't see it, a spectator does.
 *  - Drawing: until the editor-made ribbon exists (Specs/Route_Ribbons_Spec.md), development
 *    builds draw debug shapes (PSPlayArt::DrawDebug) for the first local player.
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

    /** The setting that turns route art on and off (Gameplay, Data/ui_settings.json). */
    static const FName RouteArtSettingId;

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

    /** How opaque the art is drawn now (0-1): 1 before the snap, running down to 0 as it fades. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    float GetOpacity() const;

    /** True from the snap until the art has faded. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    bool IsFading() const { return bSnapped && RouteArt.Num() > 0; }

    /** The play the art is of; none without art. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    FName GetPlayId() const { return PlayId; }

    /** Whether Viewer's view draws the art: there is art and, head to head, the versus rules
     *  let Viewer see it; otherwise Viewer doesn't play defense. A null viewer is a spectator. */
    bool IsVisibleTo(const APlayerController* Viewer) const;

    UFUNCTION(BlueprintCallable, Category = "Overlay")
    void SetOverlayDetail(EPSOverlayDetail InDetail);

    UFUNCTION(BlueprintPure, Category = "Overlay")
    EPSOverlayDetail GetOverlayDetail() const { return OverlayDetail; }

    /** How often a second the art follows the players before the snap; 0 on events only. */
    void SetRefreshHz(float InRefreshHz);

    /** Reads the RouteArt setting from these settings instead of the game instance's (headless
     *  tests, where there is no game instance). */
    void SetSettings(UPSSettingsSubsystem* InSettings) { SettingsOverride = InSettings; }

    /** Whether the RouteArt setting is on (on when there are no settings or no such setting). */
    bool IsSettingOn() const;

    /** Resolves the offense's call again and rebuilds the art now; nothing after the snap until
     *  the next down. AdvanceTime does this after an event and at the refresh rate. */
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
    void HandleGameState(const FPSTelemetryGameStateEvent& Event);
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    UPSSettingsSubsystem* GetSettings() const;
    float GetBreakMinAngleDegrees();
    void Clear();

    FPSPlayArtStyle Style;
    EPSOverlayDetail OverlayDetail = EPSOverlayDetail::Full;
    float RefreshHz = 30.f;

    UPROPERTY(Transient)
    TArray<FPSPlayArtPrimitive> RouteArt;

    UPROPERTY(Transient)
    UPSSettingsSubsystem* SettingsOverride = nullptr;

    FName PlayId;
    /** The line the GameState event last announced. */
    FVector LineOfScrimmage = FVector::ZeroVector;
    bool bHasLine = false;
    /** From the snap to the next down. */
    bool bSnapped = false;
    /** An event changed what the art shows; rebuilt on the next AdvanceTime. */
    bool bStale = false;
    float FadeRemaining = 0.f;
    float RefreshClock = 0.f;
    /** The setting's value at the last tick, to see it change. */
    bool bSettingWasOn = true;
    /** What a cut is, from the route-running tuning, loaded on first use. */
    float BreakMinAngleDegrees = 30.f;
    bool bBreakAngleLoaded = false;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
};
