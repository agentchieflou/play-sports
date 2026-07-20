#include "PSHealthComponent.h"

UPSHealthComponent::UPSHealthComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UPSHealthComponent::Initialize(float InMaxHitPoints)
{
    MaxHitPoints = FMath::Max(0.f, InMaxHitPoints);
    CurrentHitPoints = MaxHitPoints;
    bIsDowned = false;
}

bool UPSHealthComponent::ApplyDamage(float Amount)
{
    if (bIsDowned)
    {
        return true;
    }

    CurrentHitPoints = FMath::Max(0.f, CurrentHitPoints - FMath::Max(0.f, Amount));
    bIsDowned = CurrentHitPoints <= 0.f;
    return bIsDowned;
}

void UPSHealthComponent::Kill()
{
    CurrentHitPoints = 0.f;
    bIsDowned = true;
}

void UPSHealthComponent::Respawn()
{
    CurrentHitPoints = MaxHitPoints;
    bIsDowned = false;
}
