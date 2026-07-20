#include "PSCombatRulesModel.h"

void UPSCombatRulesModel::SeedDeterminism(int32 Seed)
{
    DeterminismStream.Initialize(Seed);
}

float UPSCombatRulesModel::ResolveTackleDamage(const FPlayerAttributes& Carrier, const FPlayerAttributes& Tackler, const FPSArchetypeTuning& Tuning)
{
    const float BaseDamage = 15.f + (Tackler.Strength - (Carrier.Strength + Carrier.Agility) * 0.5f) * 0.2f;
    const float ArchetypeMultiplier = GetDamageMultiplierForClass(GetArchetypeClassForRole(Carrier.Role), Tuning);
    const float Spread = DeterminismStream.FRandRange(0.8f, 1.2f);

    return FMath::Max(1.f, BaseDamage * ArchetypeMultiplier * Spread);
}
