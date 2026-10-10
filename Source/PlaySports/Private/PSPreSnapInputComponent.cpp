#include "PSPreSnapInputComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSPreSnapSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UPSPreSnapInputComponent::UPSPreSnapInputComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UPSPreSnapInputComponent::BeginPlay()
{
    Super::BeginPlay();
    BindToController();
}

void UPSPreSnapInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (APSPlayerController* Controller = GetPlayerController())
    {
        Controller->OnCatalogActionStarted.RemoveDynamic(this, &UPSPreSnapInputComponent::HandleActionStarted);
    }
    Super::EndPlay(EndPlayReason);
}

void UPSPreSnapInputComponent::BindToController()
{
    if (APSPlayerController* Controller = GetPlayerController())
    {
        Controller->OnCatalogActionStarted.AddUniqueDynamic(this, &UPSPreSnapInputComponent::HandleActionStarted);
    }
}

void UPSPreSnapInputComponent::HandleActionStarted(FName ActionId)
{
    PressAction(ActionId);
}

TArray<APSPlayerPawn*> UPSPreSnapInputComponent::GetSelectablePlayers() const
{
    TArray<APSPlayerPawn*> Players;
    const APSPlayerController* Controller = GetPlayerController();
    const APSPlayerPawn* Controlled = Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
    if (!Controlled || Controlled->TeamSide != EPSTeamSide::Offense || !GetWorld())
    {
        return Players;
    }
    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        APSPlayerPawn* Candidate = *It;
        const EPlayerRole Role = Candidate->GetAttributes().Role;
        if (Candidate != Controlled && Candidate->TeamSide == EPSTeamSide::Offense
            && (Role == EPlayerRole::WideReceiver || Role == EPlayerRole::TightEnd || Role == EPlayerRole::RunningBack))
        {
            Players.Add(Candidate);
        }
    }
    // Facing upfield (+X), left is -Y: the same order as the passing slots.
    Players.Sort([](const APSPlayerPawn& A, const APSPlayerPawn& B)
    {
        return A.GetActorLocation().Y < B.GetActorLocation().Y;
    });
    return Players;
}

APSPlayerPawn* UPSPreSnapInputComponent::GetSelectedPlayer() const
{
    const TArray<APSPlayerPawn*> Players = GetSelectablePlayers();
    return Players.Num() > 0 ? Players[SelectedIndex % Players.Num()] : nullptr;
}

bool UPSPreSnapInputComponent::PressAction(FName ActionId)
{
    UPSPreSnapSubsystem* PreSnap = GetPreSnap();
    if (!PreSnap || ActionId.IsNone() || GetSelectablePlayers().Num() == 0)
    {
        return false;
    }
    const FPreSnapTuningRow& Settings = PreSnap->GetTuning();
    if (ActionId == Settings.AudibleAction)
    {
        return PreSnap->AudibleToNext(true);
    }
    if (ActionId == Settings.SelectAction)
    {
        SelectedIndex = (SelectedIndex + 1) % GetSelectablePlayers().Num();
        return true;
    }
    if (ActionId == Settings.SlideAction)
    {
        return PreSnap->CycleSlide(true);
    }
    APSPlayerPawn* Selected = GetSelectedPlayer();
    if (ActionId == Settings.HotRouteAction)
    {
        return PreSnap->CycleHotRoute(Selected, true);
    }
    if (ActionId == Settings.MotionAction)
    {
        return PreSnap->StartMotion(Selected, true);
    }
    if (ActionId == Settings.ProtectionAction)
    {
        return PreSnap->ToggleProtection(Selected, true);
    }
    return false;
}

APSPlayerController* UPSPreSnapInputComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}

UPSPreSnapSubsystem* UPSPreSnapInputComponent::GetPreSnap() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSPreSnapSubsystem>() : nullptr;
}
