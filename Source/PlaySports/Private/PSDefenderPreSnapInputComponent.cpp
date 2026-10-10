#include "PSDefenderPreSnapInputComponent.h"
#include "PSDefenderPreSnapSubsystem.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UPSDefenderPreSnapInputComponent::UPSDefenderPreSnapInputComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UPSDefenderPreSnapInputComponent::BeginPlay()
{
    Super::BeginPlay();
    BindToController();
}

void UPSDefenderPreSnapInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (APSPlayerController* Controller = GetPlayerController())
    {
        Controller->OnCatalogActionStarted.RemoveDynamic(this, &UPSDefenderPreSnapInputComponent::HandleActionStarted);
    }
    Super::EndPlay(EndPlayReason);
}

void UPSDefenderPreSnapInputComponent::BindToController()
{
    if (APSPlayerController* Controller = GetPlayerController())
    {
        Controller->OnCatalogActionStarted.AddUniqueDynamic(this, &UPSDefenderPreSnapInputComponent::HandleActionStarted);
    }
}

void UPSDefenderPreSnapInputComponent::HandleActionStarted(FName ActionId)
{
    PressAction(ActionId);
}

TArray<APSPlayerPawn*> UPSDefenderPreSnapInputComponent::GetSelectableReceivers() const
{
    TArray<APSPlayerPawn*> Receivers;
    const APSPlayerController* Controller = GetPlayerController();
    const APSPlayerPawn* Controlled = Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
    if (!Controlled || Controlled->TeamSide != EPSTeamSide::Defense || !GetWorld())
    {
        return Receivers;
    }
    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        APSPlayerPawn* Candidate = *It;
        const EPlayerRole Role = Candidate->GetAttributes().Role;
        if (Candidate->TeamSide == EPSTeamSide::Offense
            && (Role == EPlayerRole::WideReceiver || Role == EPlayerRole::TightEnd || Role == EPlayerRole::RunningBack))
        {
            Receivers.Add(Candidate);
        }
    }
    // Across the field as the offense faces it: -Y first, the same order its pre-snap uses.
    Receivers.Sort([](const APSPlayerPawn& A, const APSPlayerPawn& B)
    {
        return A.GetActorLocation().Y < B.GetActorLocation().Y;
    });
    return Receivers;
}

APSPlayerPawn* UPSDefenderPreSnapInputComponent::GetSelectedReceiver() const
{
    const TArray<APSPlayerPawn*> Receivers = GetSelectableReceivers();
    return Receivers.Num() > 0 ? Receivers[SelectedIndex % Receivers.Num()] : nullptr;
}

APSPlayerPawn* UPSDefenderPreSnapInputComponent::FindNearestBack(const APSPlayerPawn* Receiver) const
{
    APSPlayerPawn* Nearest = nullptr;
    float NearestLateral = TNumericLimits<float>::Max();
    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        APSPlayerPawn* Candidate = *It;
        if (Candidate->TeamSide != EPSTeamSide::Defense || Candidate->IsUserControlled() || Candidate->GetAttributes().Role != EPlayerRole::DefensiveBack)
        {
            continue;
        }
        const float Lateral = FMath::Abs(Candidate->GetActorLocation().Y - Receiver->GetActorLocation().Y);
        if (Lateral < NearestLateral)
        {
            NearestLateral = Lateral;
            Nearest = Candidate;
        }
    }
    return Nearest;
}

bool UPSDefenderPreSnapInputComponent::PressAction(FName ActionId)
{
    UPSDefenderPreSnapSubsystem* DefensePreSnap = GetDefensePreSnap();
    if (!DefensePreSnap || ActionId.IsNone() || GetSelectableReceivers().Num() == 0)
    {
        return false;
    }
    const FPSDefensivePreSnapTuning& Settings = DefensePreSnap->GetTuning();
    if (ActionId == Settings.AudibleAction)
    {
        return DefensePreSnap->AudibleToNext(true);
    }
    if (ActionId == Settings.SelectAction)
    {
        SelectedIndex = (SelectedIndex + 1) % GetSelectableReceivers().Num();
        return true;
    }
    if (ActionId == Settings.ShowBlitzAction)
    {
        return DefensePreSnap->ToggleShowBlitz(true);
    }
    if (ActionId == Settings.DisguiseAction)
    {
        return DefensePreSnap->ToggleShellDisguise(true);
    }
    if (ActionId == Settings.CreepAction)
    {
        return DefensePreSnap->ToggleCreep(true);
    }
    if (ActionId == Settings.ShadowAction)
    {
        APSPlayerPawn* Receiver = GetSelectedReceiver();
        if (!Receiver || !DefensePreSnap->IsAdjustable())
        {
            return false;
        }
        // Pressed on a receiver someone already shadows: let him go.
        if (const APSPlayerPawn* Shadowing = DefensePreSnap->GetShadowingDefender(Receiver))
        {
            DefensePreSnap->ClearShadow(Shadowing);
            DefensePreSnap->EnsureAligned();
            return true;
        }
        return DefensePreSnap->SetShadow(FindNearestBack(Receiver), Receiver, true);
    }
    return false;
}

APSPlayerController* UPSDefenderPreSnapInputComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}

UPSDefenderPreSnapSubsystem* UPSDefenderPreSnapInputComponent::GetDefensePreSnap() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSDefenderPreSnapSubsystem>() : nullptr;
}
