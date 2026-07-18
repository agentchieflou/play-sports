#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Blueprint/UserWidget.h"
#include "PSHUD.generated.h"

/**
 * Custom HUD class for PlaySports which spawns and manages scoreboard widgets.
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

protected:
    virtual void BeginPlay() override;
};
