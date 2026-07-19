#pragma once

#include "CoreMinimal.h"
#include "FunctionalTest.h"
#include "PSPlaySimulation.h"
#include "PSFunctionalGym.generated.h"

/**
 * Scripted play-sequence gym test (Epic C4). Replaces the old
 * FinishTest(Succeeded, ...) stub, which asserted nothing, with a real
 * snap -> phase-progression -> Scoring check against a live APSGameMode/
 * UPSPlaySimulation in the level.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSFunctionalGym : public AFunctionalTest
{
    GENERATED_BODY()

public:
    APSFunctionalGym();

    virtual void BeginPlay() override;

    UFUNCTION(BlueprintCallable, Category = "Gym")
    void RunBlockSimulationTest();

private:
    /** Polls PlaySimulation after the snap and asserts it reached Scoring. */
    void CheckPlayProgress();

    FTimerHandle CheckProgressTimerHandle;
    EPlayPhase InitialPhase;
};
