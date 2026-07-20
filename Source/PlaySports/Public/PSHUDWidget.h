// PSHUDWidget.h - Epic 5: Scoreboard and Play Result Widget C++ base classes
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSTelemetryBus.h"
#include "PSHUDWidget.generated.h"

/**
 * UPSScoreboardWidget is the C++ base class for WBP_Scoreboard.
 * Subscribes to the TelemetryBus to receive updates dynamically (Rule 5).
 */
UCLASS(Abstract, Blueprintable)
class PLAYSPORTS_API UPSScoreboardWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;

protected:
    UPROPERTY(BlueprintReadOnly, Category = "Scoreboard")
    int32 HomeScore = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Scoreboard")
    int32 AwayScore = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Scoreboard")
    int32 Down = 1;

    UPROPERTY(BlueprintReadOnly, Category = "Scoreboard")
    int32 Distance = 10;

    UPROPERTY(BlueprintReadOnly, Category = "Scoreboard")
    int32 YardLine = 20;

    UPROPERTY(BlueprintReadOnly, Category = "Scoreboard")
    FText GameClockText;

    UPROPERTY(BlueprintReadOnly, Category = "Scoreboard")
    FText PlayClockText;

    UPROPERTY(BlueprintReadOnly, Category = "Scoreboard")
    FText PlayPhaseText;

    UFUNCTION()
    void HandleOnSnap(const FPSTelemetrySnapEvent& Event);

    UFUNCTION()
    void HandleOnScore(const FPSTelemetryScoreEvent& Event);

    UFUNCTION()
    void HandleOnPhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

private:
    void FormatGameClock(float GameClockSeconds);
    void FormatPlayClock(float PlayClockSeconds);
};

/**
 * UPSPlayResultWidget is the C++ base class for WBP_PlayResult.
 * Subscribes to the TelemetryBus to display the play outcomes dynamically.
 */
UCLASS(Abstract, Blueprintable)
class PLAYSPORTS_API UPSPlayResultWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;

protected:
    UPROPERTY(BlueprintReadOnly, Category = "PlayResult")
    FText BannerText;

    UFUNCTION()
    void HandleOnTackle(const FPSTelemetryTackleEvent& Event);

    UFUNCTION()
    void HandleOnScore(const FPSTelemetryScoreEvent& Event);

    UFUNCTION()
    void HandleOnPhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    /** Blueprint event triggered when a result should be shown visually (e.g. play animations) */
    UFUNCTION(BlueprintImplementableEvent, Category = "PlayResult")
    void OnShowPlayResultBanner();
};
