#include "PSPlayerLeveling.h"
#include "PSArchetypeTuning.h"

float UPSPlayerLeveling::ComputeXpForPlay(bool bScoredTouchdown, const FPSLevelingTuning& Tuning) const
{
    float Xp = Tuning.XpPerPlaySurvived;
    if (bScoredTouchdown)
    {
        Xp += Tuning.XpBonusForTouchdown;
    }
    return Xp;
}

void UPSPlayerLeveling::ApplyLevelUpGrowth(FPlayerAttributes& Attributes, const FPSLevelingTuning& Tuning) const
{
    const EPlayerArchetypeClass ArchetypeClass = GetArchetypeClassForRole(Attributes.Role);
    if (ArchetypeClass == EPlayerArchetypeClass::Lineman)
    {
        Attributes.Strength += Tuning.AttributeGrowthPerLevel;
    }
    else
    {
        Attributes.Speed += Tuning.AttributeGrowthPerLevel * 0.5f;
        Attributes.Agility += Tuning.AttributeGrowthPerLevel * 0.5f;
    }
}

bool UPSPlayerLeveling::AwardXpForPlay(UPSRoster* Roster, FName PlayerId, float XpAmount, const FPSLevelingTuning& Tuning)
{
    if (!Roster || PlayerId.IsNone())
    {
        return false;
    }

    Roster->AwardXp(PlayerId, XpAmount);

    FPSPlayerLiveState State;
    if (!Roster->FindLiveState(PlayerId, State))
    {
        return false;
    }

    bool bLeveledUp = false;
    while (Tuning.XpPerLevel > 0.f && State.CurrentXp >= Tuning.XpPerLevel)
    {
        State.CurrentXp -= Tuning.XpPerLevel;
        State.Level++;
        bLeveledUp = true;

        for (FPlayerAttributes& Attributes : Roster->GetMutableFullRoster())
        {
            if (Attributes.PlayerId == PlayerId)
            {
                ApplyLevelUpGrowth(Attributes, Tuning);
                break;
            }
        }
    }

    if (bLeveledUp)
    {
        Roster->SetLevel(PlayerId, State.Level, State.CurrentXp);
    }

    return bLeveledUp;
}
