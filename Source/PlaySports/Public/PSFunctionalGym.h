#pragma once

#include "CoreMinimal.h"
#include "FunctionalTest.h"
#include "PSFunctionalGym.generated.h"

UCLASS(Blueprintable)
class PLAYSPORTS_API APSFunctionalGym : public AFunctionalTest
{
    GENERATED_BODY()

public:
    APSFunctionalGym();

    virtual void BeginPlay() override;

    UFUNCTION(BlueprintCallable, Category = "Gym")
    void RunBlockSimulationTest();
};
