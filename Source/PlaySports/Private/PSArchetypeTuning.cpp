#include "PSArchetypeTuning.h"

EPlayerArchetypeClass GetArchetypeClassForRole(EPlayerRole Role)
{
    switch (Role)
    {
    case EPlayerRole::Quarterback:
    case EPlayerRole::RunningBack:
    case EPlayerRole::WideReceiver:
    case EPlayerRole::TightEnd:
        return EPlayerArchetypeClass::OffenseSkill;
    case EPlayerRole::Linebacker:
    case EPlayerRole::DefensiveBack:
        return EPlayerArchetypeClass::DefenseSkill;
    case EPlayerRole::OffensiveLineman:
    case EPlayerRole::DefensiveLineman:
        return EPlayerArchetypeClass::Lineman;
    default:
        return EPlayerArchetypeClass::OffenseSkill;
    }
}

float GetMaxHitPointsForClass(EPlayerArchetypeClass ArchetypeClass, const FPSArchetypeTuning& Tuning)
{
    switch (ArchetypeClass)
    {
    case EPlayerArchetypeClass::OffenseSkill: return Tuning.MaxHitPointsOffenseSkill;
    case EPlayerArchetypeClass::DefenseSkill: return Tuning.MaxHitPointsDefenseSkill;
    case EPlayerArchetypeClass::Lineman:      return Tuning.MaxHitPointsLineman;
    default:                                  return Tuning.MaxHitPointsOffenseSkill;
    }
}

float GetDamageMultiplierForClass(EPlayerArchetypeClass ArchetypeClass, const FPSArchetypeTuning& Tuning)
{
    switch (ArchetypeClass)
    {
    case EPlayerArchetypeClass::OffenseSkill: return Tuning.DamageMultiplierOffenseSkill;
    case EPlayerArchetypeClass::DefenseSkill: return Tuning.DamageMultiplierDefenseSkill;
    case EPlayerArchetypeClass::Lineman:      return Tuning.DamageMultiplierLineman;
    default:                                  return Tuning.DamageMultiplierOffenseSkill;
    }
}
