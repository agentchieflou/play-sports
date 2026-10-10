#include "PSHumanTeamComponent.h"
#include "PSControlHandoffComponent.h"
#include "PSMatchSetup.h"
#include "PSMenuComponent.h"
#include "PSPlayerController.h"
#include "PSVersusSubsystem.h"
#include "Engine/World.h"

UPSHumanTeamComponent::UPSHumanTeamComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UPSHumanTeamComponent::BeginPlay()
{
    Super::BeginPlay();
    BindToBus();
}

void UPSHumanTeamComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnbindFromBus();
    Super::EndPlay(EndPlayReason);
}

void UPSHumanTeamComponent::BindToBus()
{
    UWorld* World = GetWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }
    UnbindFromBus();
    Bus->OnGameStateMC.AddUObject(this, &UPSHumanTeamComponent::HandleGameState);
    BoundBus = Bus;
}

void UPSHumanTeamComponent::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnGameStateMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSHumanTeamComponent::SetMatchSetup(const UPSMatchSetup* InMatchSetup)
{
    MatchSetup = InMatchSetup;
    ApplyTeamSide();
}

bool UPSHumanTeamComponent::PlaysForHome() const
{
    const UPSMatchSetup* Match = MatchSetup.Get();
    return !Match || Match->IsUserTeamHome();
}

EPSTeamSide UPSHumanTeamComponent::GetTeamSide() const
{
    return PlaysForHome() == bHomeHasPossession ? EPSTeamSide::Offense : EPSTeamSide::Defense;
}

void UPSHumanTeamComponent::HandleGameState(const FPSTelemetryGameStateEvent& Event)
{
    if (Event.bHomeHasPossession == bHomeHasPossession)
    {
        return;
    }
    bHomeHasPossession = Event.bHomeHasPossession;
    ApplyTeamSide();
}

void UPSHumanTeamComponent::ApplyTeamSide()
{
    APSPlayerController* Controller = GetPlayerController();
    if (!Controller)
    {
        return;
    }
    // A head-to-head seat's side is the versus subsystem's.
    const UWorld* World = GetWorld();
    const UPSVersusSubsystem* Versus = World ? World->GetSubsystem<UPSVersusSubsystem>() : nullptr;
    if (Versus && Versus->FindSeat(Controller) != INDEX_NONE)
    {
        return;
    }

    const EPSTeamSide Side = GetTeamSide();
    UPSControlHandoffComponent* Handoff = Controller->GetControlHandoffComponent();
    if (Handoff)
    {
        const FControlHandoffTuningRow& Tuning = Handoff->GetTuning();
        Controller->DefaultControlRole = Side == EPSTeamSide::Offense ? Tuning.OffenseControlRole : Tuning.DefenseControlRole;
    }
    Controller->HumanSide = Side;

    // Holding a player of the other side: let go (closing a call screen for that side first)
    // and take this side's control-role player.
    const APSPlayerPawn* Controlled = Cast<APSPlayerPawn>(Controller->GetPawn());
    if (!Controlled || Controlled->TeamSide == Side)
    {
        return;
    }
    UPSMenuComponent* Menu = Controller->GetMenuComponent();
    if (Menu && Menu->IsPlayCallScreenOpen())
    {
        Menu->Resume();
    }
    Controller->ReleaseControl();
    if (!Controller->TakeDefaultControl())
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSHumanTeamComponent: No %s on the %s to take; the AI keeps every pawn."),
            *UEnum::GetValueAsString(Controller->DefaultControlRole), *UEnum::GetValueAsString(Side));
        return;
    }
    UE_LOG(LogTemp, Display, TEXT("UPSHumanTeamComponent: The %s team is on %s; the human follows it."),
        PlaysForHome() ? TEXT("home") : TEXT("away"), Side == EPSTeamSide::Offense ? TEXT("offense") : TEXT("defense"));
}

APSPlayerController* UPSHumanTeamComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}
