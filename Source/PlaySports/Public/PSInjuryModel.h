#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSInjuryModel.generated.h"

USTRUCT(BlueprintType)
struct FPSInjuryResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    bool bInjured = false;

    UPROPERTY(BlueprintReadOnly)
    int32 RecoveryWeeks = 0;
};

/** Injury probability/severity tuning. Tuning lives in data, not code
 *  (Architecture rule 4). */
USTRUCT(BlueprintType)
struct FPSInjuryTuning : public FTableRowBase
{
    GENERATED_BODY()

    /** Base chance (0-1) of injury on a single play, before fatigue scaling. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseInjuryChance = 0.002f;

    /** Multiplier applied to BaseInjuryChance as stamina ratio drops toward 0
     *  (fully fatigued players are this many times more likely to get hurt). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxFatigueMultiplier = 4.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MinRecoveryWeeks = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxRecoveryWeeks = 8;
};

/** Rolls per-play injury probability/severity for a player (Epic 19). */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSInjuryModel : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Roster|Injury")
    void SeedDeterminism(int32 Seed);

    /** StaminaRatio is CurrentStamina/MaxStamina (0-1); lower ratio raises injury
     *  odds. Deterministic given the same seed + call order (replay-safe). */
    UFUNCTION(BlueprintCallable, Category = "Roster|Injury")
    FPSInjuryResult RollForInjury(float StaminaRatio, const FPSInjuryTuning& Tuning);

private:
    UPROPERTY(Transient)
    FRandomStream DeterminismStream;
};
