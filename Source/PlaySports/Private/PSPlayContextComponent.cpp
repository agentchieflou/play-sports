#include "PSPlayContextComponent.h"
#include "PSKickMeterComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "Engine/World.h"

UPSPlayContextComponent::UPSPlayContextComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    PreSnapContextId = TEXT("PreSnap");
    PassingContextId = TEXT("Passing");
    BallCarrierContextId = TEXT("BallCarrier");
    DefenseContextId = TEXT("Defense");
    KickingContextId = TEXT("Kicking");
}

void UPSPlayContextComponent::BeginPlay()
{
    Super::BeginPlay();
    BindToBus();
}

void UPSPlayContextComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnbindFromBus();
    Super::EndPlay(EndPlayReason);
}

void UPSPlayContextComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    Refresh();
}

void UPSPlayContextComponent::BindToBus()
{
    UWorld* World = GetWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }
    UnbindFromBus();
    Bus->OnSnapMC.AddUObject(this, &UPSPlayContextComponent::HandleSnap);
    Bus->OnPhaseChangeMC.AddUObject(this, &UPSPlayContextComponent::HandlePhaseChange);
    BoundBus = Bus;
}

void UPSPlayContextComponent::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSPlayContextComponent::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    bPlayLive = true;
    LineOfScrimmage = Event.LineOfScrimmage;
}

void UPSPlayContextComponent::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("Scoring") || Event.NewPhase == TEXT("PreSnap"))
    {
        bPlayLive = false;
    }
    bKickPhase = UPSKickMeterComponent::IsKickPhase(Event.NewPhase);
}

FName UPSPlayContextComponent::ComputeContext() const
{
    const APSPlayerController* Controller = GetPlayerController();
    const APSPlayerPawn* Controlled = Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
    if (!Controlled)
    {
        return NAME_None;
    }
    if (bKickPhase)
    {
        return Controlled->TeamSide == EPSTeamSide::Offense ? KickingContextId : NAME_None;
    }
    if (!bPlayLive)
    {
        return PreSnapContextId;
    }
    if (Controlled->HasPossession())
    {
        const bool bPasser = Controlled->GetAttributes().Role == EPlayerRole::Quarterback
            && Controlled->GetActorLocation().X <= LineOfScrimmage.X;
        return bPasser ? PassingContextId : BallCarrierContextId;
    }
    return Controlled->TeamSide == EPSTeamSide::Defense ? DefenseContextId : NAME_None;
}

void UPSPlayContextComponent::Refresh()
{
    if (APSPlayerController* Controller = GetPlayerController())
    {
        Controller->SetDepthContext(ComputeContext());
    }
}

APSPlayerController* UPSPlayContextComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}
