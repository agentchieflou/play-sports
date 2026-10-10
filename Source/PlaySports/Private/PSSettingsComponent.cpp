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
        InSettings->OnInputRemapsChangedMC.AddUObject(this, &UPSSettingsComponent::ApplyRemaps);
        BoundSettings = InSettings;
    }
}

void UPSSettingsComponent::Unbind()
{
    if (UPSSettingsSubsystem* Settings = BoundSettings.Get())
    {
        Settings->OnSettingChangedMC.RemoveAll(this);
        Settings->OnInputRemapsChangedMC.RemoveAll(this);
    }
    BoundSettings.Reset();
}

void UPSSettingsComponent::ApplyAll()
{
    Apply(VibrationSettingId);
    Apply(VibrationStrengthSettingId);
    Apply(StickDeadZoneSettingId);
    Apply(InputBufferingSettingId);
    ApplyRemaps();
}

void UPSSettingsComponent::ApplyRemaps()
{
    UPSSettingsSubsystem* Settings = GetSettings();
    APSPlayerController* Controller = GetPlayerController();
    UPSInputConfig* Config = Controller ? Controller->GetInputConfig() : nullptr;
    if (!Settings || !Config)
    {
        return;
    }
    // Keep each saved remap the catalog still accepts alongside the ones before it.
    TArray<FPSInputRemap> Accepted;
    TArray<FString> Problems;
    for (const FPSInputRemap& Remap : Settings->GetInputRemaps())
    {
        TArray<FPSInputRemap> Trial = Accepted;
        Trial.Add(Remap);
        if (Config->ApplyRemaps(Trial, Problems))
        {
            Accepted = MoveTemp(Trial);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSSettingsComponent: Dropped the saved key for '%s': %s"),
                *Remap.ActionId.ToString(), Problems.Num() > 0 ? *Problems[0] : TEXT(""));
        }
    }
    Config->ApplyRemaps(Accepted, Problems);
    Controller->RefreshInputMappings();
}

bool UPSSettingsComponent::RequestRemap(FName ActionId, bool bGamepad, FName Key, FString& OutProblem)
{
    OutProblem.Reset();
    UPSSettingsSubsystem* Settings = GetSettings();
    APSPlayerController* Controller = GetPlayerController();
    UPSInputConfig* Config = Controller ? Controller->GetInputConfig() : nullptr;
    if (!Settings || !Config)
    {
        OutProblem = TEXT("Settings aren't available");
        return false;
    }

    TArray<FPSInputRemap> Wanted = Settings->GetInputRemaps();
    Wanted.RemoveAll([ActionId, bGamepad](const FPSInputRemap& Existing) { return Existing.ActionId == ActionId && Existing.bGamepad == bGamepad; });
    FPSInputRemap Remap;
    Remap.ActionId = ActionId;
    Remap.bGamepad = bGamepad;
    Remap.Key = Key;
    Wanted.Add(Remap);

    // Try it on this player's config first: a refused remap changes nothing.
    TArray<FString> Problems;
    if (!Config->ApplyRemaps(Wanted, Problems))
    {
        OutProblem = Problems.Num() > 0 ? Problems[0] : FString(TEXT("The key was refused"));
        return false;
    }
    Settings->SetInputRemaps(Wanted);
    return true;
}

void UPSSettingsComponent::ResetRemaps()
{
    if (UPSSettingsSubsystem* Settings = GetSettings())
    {
        Settings->SetInputRemaps(TArray<FPSInputRemap>());
    }
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
