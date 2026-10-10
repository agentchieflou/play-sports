// PSDefenderGapOverlaySubsystem.h - Epic 81: run-gap integrity shown live on the field
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Stats/Stats.h"
#include "PSDefenderGapOverlayTypes.h"
#include "PSTelemetryBus.h"
#include "PSDefenderGapOverlaySubsystem.generated.h"

/**
 * UPSDefenderGapOverlaySubsystem shows the run defense's gap integrity while a play is live
 * (Epic 81): a debug and coaching view of what UPSDefenderGapSubsystem, the one authority on gap
 * ownership, already knows.
 *
 *  - Markers: one per gap, D left to D right, on the gap's spot on the line. Each says whether
 *    its owner is in it (Filled), in it but engaged with a blocker (Blocked), somewhere else
 *    (Open), or whether nobody owns it (Unowned). They are read again on each GapIntegrity
 *    event on the bus and every RefreshSeconds, so they follow the line as it moves.
 *  - Emphasis: the owner of an open gap is emphasized through UPSOverlayEmphasisSubsystem
 *    (Track A's player emphasis, source "GapIntegrity"), until he fills it or the play ends.
 *  - Drawing: GetMarkers is the model a world-space marker draws from. Until that marker is made
 *    in the editor (Specs/Gap_Integrity_Overlay_Spec.md), development builds draw debug shapes.
 *
 * Off unless the style turns it on, ps.Overlay.GapIntegrity 1 at the console, or SetEnabled.
 * The style is Data/gap_overlay.json. It ticks with its world; headless tests call AdvanceTime.
 */
UCLASS()
class PLAYSPORTS_API UPSDefenderGapOverlaySubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    /** The emphasis source the overlay's requests carry. */
    static const FName EmphasisSource;

    static FString GetDefaultStylePath();

    /** Replaces the style with JsonFilePath's, read through UPSDataIngestion. A style that fails
     *  ValidateStyle is refused and the current one kept. */
    bool LoadStyleFromJson(const FString& JsonFilePath);

    void SetStyle(const FPSGapOverlayStyle& InStyle);

    const FPSGapOverlayStyle& GetStyle() const { return Style; }

    /** Problems with a style, one line each (empty when sound). */
    static TArray<FString> ValidateStyle(const FPSGapOverlayStyle& InStyle);

    /** Shows or hides the overlay. Shown, it reads the gaps' integrity at once. */
    UFUNCTION(BlueprintCallable, Category = "Overlay")
    void SetEnabled(bool bInEnabled);

    UFUNCTION(BlueprintPure, Category = "Overlay")
    bool IsEnabled() const { return bEnabled; }

    /** The markers now, D left to D right; empty while hidden, and from the whistle to the next
     *  snap's first integrity update. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    TArray<FPSGapMarker> GetMarkers() const { return Markers; }

    /** The marker for Gap, or null. */
    const FPSGapMarker* FindMarker(EPSRunGap Gap) const;

    /** Reads the markers from the gaps' integrity again, and emphasizes the open gaps' owners.
     *  A GapIntegrity event and the RefreshSeconds timer do this. */
    void Refresh();

    /** Moves the overlay's clock on: it refreshes every RefreshSeconds. The tick calls this;
     *  headless tests call it directly. */
    void AdvanceTime(float DeltaSeconds);

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandleGapIntegrity(const FPSTelemetryGapIntegrityEvent& Event);
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    /** Drops the markers and takes back the overlay's emphasis. */
    void Clear();

    /** Emphasizes exactly Owners, asking the emphasis subsystem again only when they change. */
    void SyncEmphasis(const TArray<APSPlayerPawn*>& Owners);

    FLinearColor ColorFor(EPSGapMarkerState State) const;
    void DrawDebugMarkers() const;

    FPSGapOverlayStyle Style;
    TArray<FPSGapMarker> Markers;
    /** The open gaps' owners the overlay has emphasized. */
    TArray<TWeakObjectPtr<APSPlayerPawn>> EmphasizedOwners;
    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    float RefreshClock = 0.f;
    bool bEnabled = false;
    /** From the snap to the whistle. */
    bool bPlayLive = false;
    /** The console toggle's value at the last tick, to see it change. */
    bool bConsoleEnabled = false;
};
