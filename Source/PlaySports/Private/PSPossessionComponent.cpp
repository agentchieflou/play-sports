// PSPossessionComponent.cpp - Epic C3
#include "PSPossessionComponent.h"
#include "PSPlayerPawn.h"

UPSPossessionComponent::UPSPossessionComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    bHasPossession = false;
}

void UPSPossessionComponent::GainPossession()
{
    bHasPossession = true;
    UE_LOG(LogTemp, Display, TEXT("UPSPossessionComponent: %s gained possession."),
        GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
}

void UPSPossessionComponent::LosePossession()
{
    bHasPossession = false;
    UE_LOG(LogTemp, Display, TEXT("UPSPossessionComponent: %s lost possession."),
        GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
}

bool UPSPossessionComponent::TransferPossessionTo(APSPlayerPawn* TargetPawn)
{
    if (!TargetPawn)
    {
        return false;
    }

    LosePossession();
    TargetPawn->GainPossession();

    UE_LOG(LogTemp, Display, TEXT("UPSPossessionComponent: Transferred possession from %s to %s."),
        GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"),
        *TargetPawn->GetName());

    return true;
}
