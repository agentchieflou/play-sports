#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSCoachingData.generated.h"

/** Per-opponent tendency profile (Architecture rule 4: tuning lives in DataTables,
 *  not hardcoded magic numbers). AggressionScore biases 4th-down/2-point/deep-shot
 *  decisions; CategoryWeights biases play-category selection ("Run", "ShortPass",
 *  "DeepPass", "PlayAction", "Screen" for offense, "Base"/"Blitz"/"Prevent" for
 *  defense) relative to the situational base weight. */
USTRUCT(BlueprintType)
struct FPSTendencyProfile : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName TeamId;

    /** 0 = conservative (favors the percentage play, punts/kicks by the book),
     *  1 = aggressive (goes for it, attempts 2-pointers, dials up deep shots/blitzes). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float AggressionScore = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TMap<FString, float> CategoryWeights;
};

/** Down/distance/clock/score snapshot the coaching AI reads to weight play
 *  categories and make situational decisions. Not a DataTable row -- built
 *  per-play from the live FPlayState (Epic 9's UPSPlaySimulation). */
USTRUCT(BlueprintType)
struct FPSSituationContext
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Down = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Distance = 10;

    /** 0 = own goal line, 100 = opponent's goal line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 YardLine = 20;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Quarter = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float GameClockSeconds = 900.f;

    /** Possessing team's score minus the opponent's score. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 ScoreDifferential = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 TimeoutsRemaining = 3;
};
