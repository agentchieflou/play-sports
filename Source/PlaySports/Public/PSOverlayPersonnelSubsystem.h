// PSOverlayPersonnelSubsystem.h - Epic 29: what the offense and defense personnel panels show
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSOverlayPersonnelTypes.h"
#include "PSPlatformTiers.h"
#include "PSRosterData.h"
#include "PSTelemetryBus.h"
#include "PSOverlayPersonnelSubsystem.generated.h"

/**
 * UPSOverlayPersonnelSubsystem is the personnel panels' model (Epic 29): the broadcast frame's
 * "RB 1 | TE 3 | WR 1" for the offense and "DL 3 | LB 4 | DB 4" for the defense, with each
 * package's name. UPSOverlayPersonnelWidget (made by APSHUD) only draws it.
 *
 *  - The counts are read live from the roles of the players on the field, by side; the pawns are
 *    the one record of who is on (rule 6).
 *  - The name: the personnel catalog's (Data/personnel_packages.json, Epic 19.5) for a package
 *    it lists with those exact counts -- the one UPSPersonnelManager just announced first --
 *    else the style's rules: "{RB}{TE} Personnel" on offense, by defensive backs on defense.
 *  - The teams: the side with the ball is the offense, with its label and color as the score bug
 *    shows them (UPSOverlayBroadcastSubsystem, Epic 33).
 *  - A substitution (the bus's Personnel event) flashes its side's panel and the counts it
 *    changed, on a Full tier. The panels show before the snap only (the style can keep them
 *    up), and not at all on a Minimal tier.
 *
 * The style is Data/personnel_panel.json. It counts again on every Personnel and GameState
 * event; it ticks with its world to fade the flash, and headless tests call AdvanceTime.
 */
UCLASS()
class PLAYSPORTS_API UPSOverlayPersonnelSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultStylePath();

    /** Replaces the style with JsonFilePath's, read through UPSDataIngestion. A style that fails
     *  ValidateStyle is refused and the current one kept. */
    bool LoadStyleFromJson(const FString& JsonFilePath);

    void SetStyle(const FPSPersonnelPanelStyle& InStyle);

    const FPSPersonnelPanelStyle& GetStyle() const { return Style; }

    /** Problems with a style, one line each (empty when sound). */
    static TArray<FString> ValidateStyle(const FPSPersonnelPanelStyle& InStyle);

    /** The packages named by exact role counts; loaded from the personnel catalog's default
     *  path on first use. */
    const FPSPersonnelCatalog& GetCatalog();

    void SetCatalog(const FPSPersonnelCatalog& InCatalog);

    /**
     * The name of the package Counts make on a side: PreferredPackage's when the catalog lists
     * it with exactly these counts, else any catalog package of the side with these counts, else
     * the style's rule. Counts holds every role on the side (a missing role is 0).
     */
    static FString NamePackage(const TMap<EPlayerRole, int32>& Counts, bool bOffense, const FPSPersonnelCatalog& Catalog,
        const FPSPersonnelPanelStyle& InStyle, FName PreferredPackage = NAME_None);

    /** The players of each role on the side, from the pawns on the field. */
    static TMap<EPlayerRole, int32> CountSide(const UWorld* World, bool bOffense);

    // The panels' words, localized (Epic 106).

    /** A counted role's label ("RB"), from the style through the generated data table. */
    static FString LocalizedRoleLabel(const FPSPersonnelRoleLabel& Entry);

    /** One count, "RB 1"; Label arrives localized. */
    static FString FormatCount(const FString& Label, int32 Count);

    /** What goes between two counts, " | ". */
    static FString CountSeparator();

    UFUNCTION(BlueprintPure, Category = "Overlay")
    FPSPersonnelPanel GetPanel(bool bOffense) const { return bOffense ? OffensePanel : DefensePanel; }

    UFUNCTION(BlueprintCallable, Category = "Overlay")
    void SetOverlayDetail(EPSOverlayDetail InDetail);

    UFUNCTION(BlueprintPure, Category = "Overlay")
    EPSOverlayDetail GetOverlayDetail() const { return OverlayDetail; }

    /** Counts both sides again and works out the panels. */
    void Refresh();

    /** Fades the flash. The tick calls this; headless tests call it directly. */
    void AdvanceTime(float DeltaSeconds);

private:
    void HandlePersonnel(const FPSTelemetryPersonnelEvent& Event);
    void HandleGameState(const FPSTelemetryGameStateEvent& Event);
    void BuildPanel(FPSPersonnelPanel& Panel, bool bOffense, const TArray<FPSPersonnelRoleLabel>& Roles, float FlashRemaining);

    FPSPersonnelPanelStyle Style;
    FPSPersonnelCatalog Catalog;
    bool bCatalogLoaded = false;
    EPSOverlayDetail OverlayDetail = EPSOverlayDetail::Full;

    FPSPersonnelPanel OffensePanel;
    FPSPersonnelPanel DefensePanel;

    /** The packages UPSPersonnelManager last announced. */
    FName OffensePackageId;
    FName DefensePackageId;

    float OffenseFlashRemaining = 0.f;
    float DefenseFlashRemaining = 0.f;

    FString Phase;
    bool bHomeHasPossession = true;
    bool bHasGameState = false;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
};
