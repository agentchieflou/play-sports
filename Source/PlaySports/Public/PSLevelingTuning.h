// PSLevelingTuning.h - Epic 141: player leveling/XP tuning
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSLevelingTuning.generated.h"

/** XP curve and per-play XP awards. Tuning lives in data, not code (Architecture
 *  rule 4). Flat XpPerLevel keeps the curve simple and predictable; a future story
 *  can make it scale with level without touching any calling code. */
USTRUCT(BlueprintType)
struct FPSLevelingTuning : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float XpPerLevel = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float XpPerPlaySurvived = 5.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float XpBonusForTouchdown = 50.f;

    /** Attribute points added on each level-up, split across the archetype's
     *  signature attributes (Speed/Agility for skill players, Strength for
     *  linemen). This is the seam Epic 141's future microtransaction layer would
     *  hook into (e.g. XP boosts), without any store integration here. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float AttributeGrowthPerLevel = 1.f;
};
