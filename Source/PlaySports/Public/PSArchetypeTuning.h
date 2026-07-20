// PSArchetypeTuning.h - Epic 139: character archetype hitpoint/damage tuning
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.h"
#include "PSArchetypeTuning.generated.h"

/** Hitpoint/damage tuning per archetype class. Tuning lives in data, not code
 *  (Architecture rule 4). */
USTRUCT(BlueprintType)
struct FPSArchetypeTuning : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxHitPointsOffenseSkill = 60.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxHitPointsDefenseSkill = 70.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxHitPointsLineman = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageMultiplierOffenseSkill = 1.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageMultiplierDefenseSkill = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageMultiplierLineman = 0.7f;
};

/** Maps the fine-grained position (EPlayerRole) to the broad combat archetype class. */
PLAYSPORTS_API EPlayerArchetypeClass GetArchetypeClassForRole(EPlayerRole Role);

PLAYSPORTS_API float GetMaxHitPointsForClass(EPlayerArchetypeClass ArchetypeClass, const FPSArchetypeTuning& Tuning);

PLAYSPORTS_API float GetDamageMultiplierForClass(EPlayerArchetypeClass ArchetypeClass, const FPSArchetypeTuning& Tuning);
