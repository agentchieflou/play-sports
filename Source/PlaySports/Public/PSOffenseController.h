#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "PSTelemetryBus.h"
#include "PSOffenseController.generated.h"

class UBehaviorTree;

/**
 * APSOffenseController drives offensive skill players (QB, RB, WR, TE)
 * using Behavior Trees synchronized via UPSTelemetryBus events.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSOffenseController : public AAIController
{
    GENERATED_BODY()

public:
    APSOffenseController();

protected:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
    UBehaviorTree* BehaviorTreeAsset;

private:
    UFUNCTION()
    void OnPhaseChanged(const FPSTelemetryPhaseChangeEvent& Event);

    UFUNCTION()
    void OnSnapEvent(const FPSTelemetrySnapEvent& Event);

    UFUNCTION()
    void OnThrowEvent(const FPSTelemetryThrowEvent& Event);

    UFUNCTION()
    void OnCatchEvent(const FPSTelemetryCatchEvent& Event);

    UFUNCTION()
    void OnFumbleEvent(const FPSTelemetryFumbleEvent& Event);

    UFUNCTION()
    void OnTackleEvent(const FPSTelemetryTackleEvent& Event);

    void InitializeBlackboardState();
};
