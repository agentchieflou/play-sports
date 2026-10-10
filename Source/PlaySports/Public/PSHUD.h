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
 * position badges over the players (Epic 28).
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

protected:
    virtual void BeginPlay() override;
};
