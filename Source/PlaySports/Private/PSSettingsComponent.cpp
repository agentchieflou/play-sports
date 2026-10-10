#include "PSSettingsComponent.h"
#include "PSForceFeedbackComponent.h"
#include "PSInputBufferComponent.h"
#include "PSInputConfig.h"
#include "PSPlayerController.h"
#include "PSSettingsSubsystem.h"

UPSSettingsComponent::UPSSettingsComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    VibrationSettingId = TEXT("Vibration");
    VibrationStrengthSettingId = TEXT("VibrationStrength");
    StickDeadZoneSettingId = TEXT("StickDeadZone");
    InputBufferingSettingId = TEXT("InputBuffering");
}

void UPSSettingsComponent::BeginPlay()
{
    Super::BeginPlay();
    if (UPSSettingsSubsystem* GameSettings = UPSSettingsSubsystem::Get(this))
    {
        SetSettings(GameSettings);
    }
}

void UPSSettingsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Unbind();
    Super::EndPlay(EndPlayReason);
}

UPSSettingsSubsystem* UPSSettingsComponent::GetSettings() const
{
    return BoundSettings.IsValid() ? BoundSettings.Get() : UPSSettingsSubsystem::Get(this);
}

void UPSSettingsComponent::SetSettings(UPSSettingsSubsystem* InSettings)
{
    Bind(InSettings);
    ApplyAll();
}

void UPSSettingsComponent::Bind(UPSSettingsSubsystem* InSettings)
{
    if (BoundSettings.Get() == InSettings)
    {
        return;
    }
    Unbind();
    if (InSettings)
    {
        InSettings->OnSettingChangedMC.AddUObject(this, &UPSSettingsComponent::HandleSettingChanged);
        BoundSettings = InSettings;
    }
}

void UPSSettingsComponent::Unbind()
{
    if (UPSSettingsSubsystem* Settings = BoundSettings.Get())
    {
        Settings->OnSettingChangedMC.RemoveAll(this);
    }
    BoundSettings.Reset();
}

void UPSSettingsComponent::ApplyAll()
{
    Apply(VibrationSettingId);
    Apply(VibrationStrengthSettingId);
    Apply(StickDeadZoneSettingId);
    Apply(InputBufferingSettingId);
}

void UPSSettingsComponent::HandleSettingChanged(FName SettingId, float Value)
{
    Apply(SettingId);
}

void UPSSettingsComponent::Apply(FName SettingId)
{
    UPSSettingsSubsystem* Settings = GetSettings();
    APSPlayerController* Controller = GetPlayerController();
    if (!Settings || !Controller)
    {
        return;
    }

    if (SettingId == VibrationSettingId)
    {
        if (UPSForceFeedbackComponent* Rumble = Controller->GetForceFeedbackComponent())
        {
            Rumble->bEnabled = Settings->GetBool(SettingId);
        }
    }
    else if (SettingId == VibrationStrengthSettingId)
    {
        Controller->ForceFeedbackScale = FMath::Clamp(Settings->GetNumber(SettingId) / 100.f, 0.f, 1.f);
    }
    else if (SettingId == StickDeadZoneSettingId)
    {
        if (UPSInputConfig* Config = Controller->GetInputConfig())
        {
            Config->SetStickDeadZoneScale(Settings->GetNumber(SettingId));
            Controller->RefreshInputMappings();
        }
    }
    else if (SettingId == InputBufferingSettingId)
    {
        if (UPSInputBufferComponent* Buffer = Controller->GetInputBufferComponent())
        {
            Buffer->bBufferingEnabled = Settings->GetBool(SettingId);
        }
    }
}

APSPlayerController* UPSSettingsComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}
