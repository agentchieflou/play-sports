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
