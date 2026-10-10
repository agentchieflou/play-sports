#include "PSPlayCallComponent.h"
#include "PSMenuComponent.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "Engine/World.h"

UPSPlayCallComponent::UPSPlayCallComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    ConfirmActionId = TEXT("Confirm");
    TempoActionId = TEXT("Tempo");
    TimeoutActionId = TEXT("Timeout");
}

void UPSPlayCallComponent::BeginPlay()
{
    Super::BeginPlay();
    BindToPlayCall();
}

void UPSPlayCallComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnbindFromPlayCall();
    Super::EndPlay(EndPlayReason);
}

void UPSPlayCallComponent::BindToPlayCall()
{
    UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    if (!PlayCall || BoundPlayCall.Get() == PlayCall)
    {
        return;
    }

    UnbindFromPlayCall();
    CallNeededHandle = PlayCall->OnHumanCallNeeded.AddUObject(this, &UPSPlayCallComponent::HandleHumanCallNeeded);
    BoundPlayCall = PlayCall;

    if (APSPlayerController* Controller = GetOwningController())
    {
        Controller->OnCatalogActionStarted.AddUniqueDynamic(this, &UPSPlayCallComponent::HandleCatalogAction);
    }
    if (UPSTelemetryBus* Bus = GetWorld() ? GetWorld()->GetSubsystem<UPSTelemetryBus>() : nullptr)
    {
        Bus->OnPlayCallMC.AddUObject(this, &UPSPlayCallComponent::HandlePlayCall);
        BoundBus = Bus;
    }
}

void UPSPlayCallComponent::UnbindFromPlayCall()
{
    if (UPSPlayCallSubsystem* PlayCall = BoundPlayCall.Get())
    {
        PlayCall->OnHumanCallNeeded.Remove(CallNeededHandle);
    }
    CallNeededHandle.Reset();
    BoundPlayCall.Reset();

    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnPlayCallMC.RemoveAll(this);
    }
    BoundBus.Reset();

    if (APSPlayerController* Controller = GetOwningController())
    {
        Controller->OnCatalogActionStarted.RemoveDynamic(this, &UPSPlayCallComponent::HandleCatalogAction);
    }
}

bool UPSPlayCallComponent::IsCallingForOffense() const
{
    const APSPlayerController* Controller = GetOwningController();
    if (!Controller)
    {
        return true;
    }
    if (const APSPlayerPawn* PlayerPawn = Cast<APSPlayerPawn>(Controller->GetPawn()))
    {
        return PlayerPawn->TeamSide == EPSTeamSide::Offense;
    }
    return Controller->HumanSide == EPSTeamSide::Offense;
}

bool UPSPlayCallComponent::OpenCallScreen()
{
    APSPlayerController* Controller = GetOwningController();
    UPSMenuComponent* Menu = Controller ? Controller->GetMenuComponent() : nullptr;
    if (!Menu || Menu->IsMenuOpen())
    {
        return false;
    }
    return Menu->OpenScreen(Menu->GetCatalog().PlayCallScreen);
}

void UPSPlayCallComponent::HandleHumanCallNeeded(bool bOffense)
{
    if (bOffense == IsCallingForOffense())
    {
        OpenCallScreen();
    }
}

void UPSPlayCallComponent::HandlePlayCall(const FPSTelemetryPlayCallEvent& Event)
{
    // The player's own call already closed the screens; this is the clock calling for them.
    if (Event.bHumanCall || Event.bOffense != IsCallingForOffense())
    {
        return;
    }
    APSPlayerController* Controller = GetOwningController();
    UPSMenuComponent* Menu = Controller ? Controller->GetMenuComponent() : nullptr;
    if (Menu && Menu->IsPlayCallScreenOpen())
    {
        Menu->Resume();
    }
}

void UPSPlayCallComponent::HandleCatalogAction(FName ActionId)
{
    UPSPlayCallSubsystem* PlayCall = BoundPlayCall.Get();
    if (!PlayCall)
    {
        return;
    }

    // The clock controls (Epic 76): the tempo holds from down to down; a timeout needs the
    // call window open.
    const bool bOffense = IsCallingForOffense();
    if (ActionId == TempoActionId && bOffense)
    {
        PlayCall->CycleHumanTempo();
        return;
    }
    if (ActionId == TimeoutActionId)
    {
        PlayCall->RequestTimeout(bOffense, true);
        return;
    }
    if (ActionId != ConfirmActionId || !PlayCall->IsCallWindowOpen())
    {
        return;
    }

    if (PlayCall->IsWaitingForHuman(bOffense))
    {
        OpenCallScreen();
    }
    else if (bOffense)
    {
        PlayCall->RequestSnap();
    }
}

APSPlayerController* UPSPlayCallComponent::GetOwningController() const
{
    return Cast<APSPlayerController>(GetOwner());
}

UPSPlayCallSubsystem* UPSPlayCallComponent::GetPlayCall() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
}
