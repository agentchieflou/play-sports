#include "PSBallResolutionHelpers.h"

float PSBallResolutionHelpers::ComputeCatchChance(const FPlayerAttributes& Attributes, const FCatchTuningRow& Tuning)
{
    float Chance = Tuning.CatchBaseChance + (Attributes.Agility + Attributes.Awareness) * Tuning.CatchAttributeScalar;
    return FMath::Clamp(Chance, Tuning.CatchChanceMin, Tuning.CatchChanceMax);
}

float PSBallResolutionHelpers::ComputeInterceptionChance(const FPlayerAttributes& Attributes, const FCatchTuningRow& Tuning)
{
    float Chance = Tuning.InterceptionBaseChance + (Attributes.Agility + Attributes.Awareness) * Tuning.InterceptionAttributeScalar;
    return FMath::Clamp(Chance, Tuning.InterceptionChanceMin, Tuning.InterceptionChanceMax);
}

float PSBallResolutionHelpers::ComputeFumbleRecoveryChance(const FPlayerAttributes& Attributes, const FCatchTuningRow& Tuning)
{
    float Chance = Tuning.FumbleRecoveryBaseChance + (Attributes.Agility + Attributes.Awareness) * Tuning.FumbleRecoveryAttributeScalar;
    return FMath::Clamp(Chance, Tuning.FumbleRecoveryChanceMin, Tuning.FumbleRecoveryChanceMax);
}

bool PSBallResolutionHelpers::ResolveCatch(const FPlayerAttributes& Attributes, float Roll, const FCatchTuningRow& Tuning)
{
    return Roll <= ComputeCatchChance(Attributes, Tuning);
}

float PSBallResolutionHelpers::ComputeTackleChance(const FPlayerAttributes& Carrier, const FPlayerAttributes& Defender, float CarrierSpeed, float DefenderSpeed,
    float CarrierMoveMultiplier, const FTackleTuningRow& Tuning)
{
    const float DefenderPower = Defender.Strength * Tuning.DefenderStrengthWeight + DefenderSpeed * Tuning.DefenderSpeedWeight;
    const float CarrierPower = Carrier.Strength * Tuning.CarrierStrengthWeight + Carrier.Agility * Tuning.CarrierAgilityWeight + CarrierSpeed * Tuning.CarrierSpeedWeight;
    const float Chance = FMath::Clamp(Tuning.TackleBaseChance + (DefenderPower - CarrierPower) * Tuning.PowerScalar, Tuning.TackleChanceMin, Tuning.TackleChanceMax);
    return FMath::Clamp(Chance * FMath::Max(0.f, CarrierMoveMultiplier), 0.f, Tuning.TackleChanceMax);
}
