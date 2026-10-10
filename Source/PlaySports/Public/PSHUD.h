#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Blueprint/UserWidget.h"
#include "PSHUD.generated.h"

/**
 * Custom HUD class for PlaySports which spawns and manages scoreboard widgets.
 *
 * By default it shows the broadcast package (Epic 33): UPSOverlayScoreBugWidget, which
 * replaces Epic 5's debug scoreboard, and UPSOverlayChyronWidget, both drawing
 * UPSOverlayBroadcastSubsystem. A designer can assign Widget Blueprints instead. The chyron
 * widget is left out on a tier whose OverlayDetail is Minimal. UPSOverlayBadgeWidget draws the
 * position badges over the players (Epic 28), and UPSTelestratorWidget is the telestrator's
 * drawing layer (Epic 44), on every tier: analysis is the player's own choice. In development
 * builds UPSAIDebugOverlayWidget shows the AI debug overlay's cards (Epic 85.2) when it is on.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSHUD : public AHUD
{
    GENERATED_BODY()

public:
    APSHUD();

    // The Scoreboard UMG Widget Class to spawn
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
    TSubclassOf<UUserWidget> ScoreboardWidgetClass;

    // Instance of the scoreboard widget
    UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD")
    UUserWidget* ScoreboardWidget;

    /** The lower-third chyron widget class (Epic 33). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
    TSubclassOf<UUserWidget> ChyronWidgetClass;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD")
    UUserWidget* ChyronWidget;

    /** The offense and defense personnel panels (Epic 29); left out on a Minimal tier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
    TSubclassOf<UUserWidget> PersonnelWidgetClass;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD")
    UUserWidget* PersonnelWidget;

    /** The position badge widget class (Epic 28). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
    TSubclassOf<UUserWidget> BadgeWidgetClass;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD")
    UUserWidget* BadgeWidget;

    /** The telestrator's drawing layer (Epic 44): idle until analysis mode is on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
    TSubclassOf<UUserWidget> TelestratorWidgetClass;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD")
    UUserWidget* TelestratorWidget;

    /** The on-field AI debug overlay (Epic 85.2); made in development builds only, and idle until
     *  ps.AI.DebugOverlay turns the overlay on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
    TSubclassOf<UUserWidget> AIDebugWidgetClass;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD")
    UUserWidget* AIDebugWidget;

protected:
    virtual void BeginPlay() override;
};
