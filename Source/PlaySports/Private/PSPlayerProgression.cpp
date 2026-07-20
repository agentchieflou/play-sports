#include "PSPlayerProgression.h"

float UPSPlayerProgression::ClampAttribute(float Value)
{
    return FMath::Clamp(Value, 0.f, 100.f);
}

void UPSPlayerProgression::ApplyOffseasonProgression(FPlayerAttributes& Attributes, int32 Age, float SeasonSnapShare, const FPSProgressionTuning& Tuning) const
{
    float Delta = 0.f;

    if (Age < Tuning.PeakAgeStart)
    {
        Delta = Tuning.GrowthPerYear;
        if (SeasonSnapShare < Tuning.LowSnapShareThreshold)
        {
            Delta *= 0.5f;
        }
    }
    else if (Age > Tuning.PeakAgeEnd)
    {
        Delta = -Tuning.DeclinePerYear;
    }
    // Within the peak window: no change.

    Attributes.Speed = ClampAttribute(Attributes.Speed + Delta);
    Attributes.Agility = ClampAttribute(Attributes.Agility + Delta);
    Attributes.Acceleration = ClampAttribute(Attributes.Acceleration + Delta);
    Attributes.Awareness = ClampAttribute(Attributes.Awareness + (Delta > 0.f ? Delta * 0.5f : Delta * 0.25f));
    // Strength declines more slowly than speed/agility and can still rise past PeakAgeEnd.
    Attributes.Strength = ClampAttribute(Attributes.Strength + (Delta > 0.f ? Delta : Delta * 0.5f));
}
