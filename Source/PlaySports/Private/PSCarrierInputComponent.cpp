#include "PSCarrierInputComponent.h"
#include "PSCarrierMoveComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"

UPSCarrierInputComponent::UPSCarrierInputComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UPSCarrierInputComponent::BeginPlay()
{
    Super::BeginPlay();
    BindToController();
}

void UPSCarrierInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (APSPlayerController* Controller = GetPlayerController())
    {
        Controller->OnCatalogActionStarted.RemoveDynamic(this, &UPSCarrierInputComponent::HandleActionStarted);
    }
    Super::EndPlay(EndPlayReason);
}

void UPSCarrierInputComponent::BindToController()
{
    if (APSPlayerController* Controller = GetPlayerController())
    {
        Controller->OnCatalogActionStarted.AddUniqueDynamic(this, &UPSCarrierInputComponent::HandleActionStarted);
    }
}

void UPSCarrierInputComponent::HandleActionStarted(FName ActionId)
{
    PressMoveAction(ActionId);
}

bool UPSCarrierInputComponent::PressMoveAction(FName ActionId)
{
    const APSPlayerController* Controller = GetPlayerController();
    const APSPlayerPawn* Carrier = Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
    UPSCarrierMoveComponent* Moves = Carrier ? Carrier->GetCarrierMoveComponent() : nullptr;
    if (!Moves)
    {
        return false;
    }
    const EPSCarrierMove Move = Moves->FindMoveForAction(ActionId);
    return Move != EPSCarrierMove::None && Moves->TryMove(Move, Controller->GetMoveInput());
}

APSPlayerController* UPSCarrierInputComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}
