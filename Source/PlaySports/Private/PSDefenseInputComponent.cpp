#include "PSDefenseInputComponent.h"
#include "PSDefenderTechniqueComponent.h"
#include "PSInputBufferComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "Engine/World.h"

UPSDefenseInputComponent::UPSDefenseInputComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UPSDefenseInputComponent::BeginPlay()
{
    Super::BeginPlay();
    BindToController();
    BindToBus();
}

void UPSDefenseInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    APSPlayerController* Controller = GetPlayerController();
    if (UPSInputBufferComponent* Buffer = Controller ? Controller->GetInputBufferComponent() : nullptr)
    {
        Buffer->OnActionPressed.RemoveDynamic(this, &UPSDefenseInputComponent::HandleActionPressed);
        Buffer->RemoveBusyChecks(this);
    }
    UnbindFromBus();
    Super::EndPlay(EndPlayReason);
}

void UPSDefenseInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    AdvanceTime(DeltaTime);
}

void UPSDefenseInputComponent::BindToController()
{
    APSPlayerController* Controller = GetPlayerController();
    UPSInputBufferComponent* Buffer = Controller ? Controller->GetInputBufferComponent() : nullptr;
    if (!Buffer)
    {
        return;
    }
    Buffer->BindToController();
    Buffer->OnActionPressed.AddUniqueDynamic(this, &UPSDefenseInputComponent::HandleActionPressed);
    Buffer->RemoveBusyChecks(this);
    Buffer->AddBusyCheck(FPSInputBusyCheck::CreateUObject(this, &UPSDefenseInputComponent::IsDefenseActionBusy));
}

void UPSDefenseInputComponent::BindToBus()
{
    UWorld* World = GetWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }
    UnbindFromBus();
    Bus->OnSnapMC.AddUObject(this, &UPSDefenseInputComponent::HandleSnap);
    Bus->OnPhaseChangeMC.AddUObject(this, &UPSDefenseInputComponent::HandlePhaseChange);
    BoundBus = Bus;
}

void UPSDefenseInputComponent::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSDefenseInputComponent::HandleActionPressed(FName ActionId, float HeldSeconds)
{
    UPSDefenderTechniqueComponent* Technique = GetControlledTechnique();
    if (!Technique)
    {
        return;
    }
    const FDefensiveTechniqueTuningRow& Settings = Technique->GetTuning();
    if (ActionId == Settings.JumpSnapAction)
    {
        PressJumpSnap();
    }
    else if (ActionId == Settings.StripAction)
    {
        PressStrip();
    }
}

bool UPSDefenseInputComponent::IsDefenseActionBusy(FName ActionId)
{
    UPSDefenderTechniqueComponent* Technique = GetControlledTechnique();
    return Technique && ActionId == Technique->GetTuning().StripAction && Technique->IsStripBusy();
}

bool UPSDefenseInputComponent::PressJumpSnap()
{
    APSPlayerPawn* Defender = GetControlledDefender();
    if (!Defender || bSnapped || JumpPawn.IsValid())
    {
        return false;
    }
    // The first move is the jump: pressing again can't take it back.
    JumpPawn = Defender;
    JumpPressedAt = Clock;
    return true;
}

bool UPSDefenseInputComponent::PressStrip()
{
    UPSDefenderTechniqueComponent* Technique = GetControlledTechnique();
    return Technique && Technique->TryStrip();
}

void UPSDefenseInputComponent::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    bSnapped = true;
    APSPlayerPawn* Jumper = JumpPawn.Get();
    JumpPawn.Reset();
    // A jump counts for the defender still under control at the snap.
    if (!Jumper || Jumper != GetControlledDefender())
    {
        return;
    }
    UPSDefenderTechniqueComponent* Technique = Jumper->GetDefenderTechniqueComponent();
    if (!Technique)
    {
        return;
    }

    FPSTelemetryJumpSnapEvent Jump;
    Jump.DefenderName = Jumper->GetAttributes().DisplayName;
    Jump.LeadSeconds = Clock - JumpPressedAt;
    Jump.bOffside = Jump.LeadSeconds > Technique->GetTuning().JumpWindowSeconds;
    if (!Jump.bOffside)
    {
        Technique->GetOff(Event.LineOfScrimmage);
    }
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->PublishJumpSnap(Jump);
    }
}

void UPSDefenseInputComponent::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    // Back to the line: a new snap to time.
    if (Event.NewPhase == TEXT("PreSnap"))
    {
        bSnapped = false;
        JumpPawn.Reset();
    }
}

void UPSDefenseInputComponent::AdvanceTime(float DeltaSeconds)
{
    Clock += DeltaSeconds;
}

APSPlayerPawn* UPSDefenseInputComponent::GetControlledDefender() const
{
    const APSPlayerController* Controller = GetPlayerController();
    APSPlayerPawn* Controlled = Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
    return Controlled && Controlled->TeamSide == EPSTeamSide::Defense ? Controlled : nullptr;
}

UPSDefenderTechniqueComponent* UPSDefenseInputComponent::GetControlledTechnique() const
{
    const APSPlayerPawn* Defender = GetControlledDefender();
    return Defender ? Defender->GetDefenderTechniqueComponent() : nullptr;
}

APSPlayerController* UPSDefenseInputComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}
