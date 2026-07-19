#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PSRulesConfig.generated.h"

UCLASS(Blueprintable)
class PLAYSPORTS_API UPSRulesConfig : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring")
    int32 TouchdownPoints = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring")
    int32 SafetyPoints = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring")
    float PATSuccessChance = 0.94f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring")
    float TwoPointSuccessChance = 0.45f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penalties")
    int32 OffsidesYardage = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penalties")
    int32 HoldingYardage = 10;
};
