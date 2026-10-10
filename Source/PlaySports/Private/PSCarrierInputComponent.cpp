#include "PSCarrierInputComponent.h"
#include "PSCarrierMoveComponent.h"
#include "PSInputBufferComponent.h"
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
    APSPlayerController* Controller = GetPlayerController();
    if (UPSInputBufferComponent* Buffer = Controller ? Controller->GetInputBufferComponent() : nullptr)
    {
        Buffer->OnActionPressed.RemoveDynamic(this, &UPSCarrierInputComponent::HandleActionPressed);
        Buffer->RemoveBusyChecks(this);
    }
    Super::EndPlay(EndPlayReason);
}

void UPSCarrierInputComponent::BindToController()
{
    APSPlayerController* Controller = GetPlayerController();
    UPSInputBufferComponent* Buffer = Controller ? Controller->GetInputBufferComponent() : nullptr;
    if (!Buffer)
    {
        return;
    }
    Buffer->BindToController();
    Buffer->OnActionPressed.AddUniqueDynamic(this, &UPSCarrierInputComponent::HandleActionPressed);
    Buffer->RemoveBusyChecks(this);
    Buffer->AddBusyCheck(FPSInputBusyCheck::CreateUObject(this, &UPSCarrierInputComponent::IsMoveActionBusy));
}

void UPSCarrierInputComponent::HandleActionPressed(FName ActionId, float HeldSeconds)
{
    PressMoveAction(ActionId);
}

bool UPSCarrierInputComponent::PressMoveAction(FName ActionId)
{
    UPSCarrierMoveComponent* Moves = GetControlledMoves();
    if (!Moves)
    {
        return false;
    }
    const EPSCarrierMove Move = Moves->FindMoveForAction(ActionId);
    return Move != EPSCarrierMove::None && Moves->TryMove(Move, GetPlayerController()->GetMoveInput());
}

bool UPSCarrierInputComponent::IsMoveActionBusy(FName ActionId)
{
    UPSCarrierMoveComponent* Moves = GetControlledMoves();
    return Moves && Moves->IsMoveBusy(Moves->FindMoveForAction(ActionId));
}

UPSCarrierMoveComponent* UPSCarrierInputComponent::GetControlledMoves() const
{
    const APSPlayerController* Controller = GetPlayerController();
    const APSPlayerPawn* Carrier = Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
    return Carrier ? Carrier->GetCarrierMoveComponent() : nullptr;
}

APSPlayerController* UPSCarrierInputComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}
