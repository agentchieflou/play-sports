#include "PSInjuryModel.h"

void UPSInjuryModel::SeedDeterminism(int32 Seed)
{
    DeterminismStream.Initialize(Seed);
}

FPSInjuryResult UPSInjuryModel::RollForInjury(float StaminaRatio, const FPSInjuryTuning& Tuning)
{
    FPSInjuryResult Result;

    const float ClampedRatio = FMath::Clamp(StaminaRatio, 0.f, 1.f);
    const float FatigueMultiplier = FMath::Lerp(Tuning.MaxFatigueMultiplier, 1.f, ClampedRatio);
    const float InjuryChance = Tuning.BaseInjuryChance * FatigueMultiplier;

    const float Roll = DeterminismStream.FRandRange(0.f, 1.f);
    Result.bInjured = Roll < InjuryChance;

    if (Result.bInjured)
    {
        Result.RecoveryWeeks = DeterminismStream.RandRange(Tuning.MinRecoveryWeeks, Tuning.MaxRecoveryWeeks);
    }

    return Result;
}
