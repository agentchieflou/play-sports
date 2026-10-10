#include "PSSettingsSubsystem.h"
#include "PSDataIngestion.h"
#include "PSProfileSaveGame.h"
#include "PSSaveSubsystem.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/Paths.h"

UPSSettingsSubsystem::UPSSettingsSubsystem()
{
    WindowModeSettingId = TEXT("WindowMode");
    ResolutionScaleSettingId = TEXT("ResolutionScale");
    QualitySettingId = TEXT("Quality");
    VSyncSettingId = TEXT("VSync");
    FrameRateLimitSettingId = TEXT("FrameRateLimit");
    MasterVolumeSettingId = TEXT("MasterVolume");
}

void UPSSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    GetCatalog();
    // A running game's settings: read the profile, then hand the engine its part.
    bApplyToEngine = true;
    LoadFromProfile(Collection.InitializeDependency<UPSSaveSubsystem>(), UPSProfileSaveGame::GetDefaultSlotName());
    ApplyEngineSettings();
}

UPSSettingsSubsystem* UPSSettingsSubsystem::Get(const UObject* WorldContext)
{
    const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    return GameInstance ? GameInstance->GetSubsystem<UPSSettingsSubsystem>() : nullptr;
}

FString UPSSettingsSubsystem::GetDefaultCatalogPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/ui_settings.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSSettingsCatalog& UPSSettingsSubsystem::GetCatalog()
{
    if (!bCatalogLoaded)
    {
        LoadCatalogFromJson(GetDefaultCatalogPath());
    }
    return Catalog;
}

bool UPSSettingsSubsystem::LoadCatalogFromJson(const FString& JsonFilePath)
{
    bCatalogLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSSettingsCatalog Loaded;
    if (!Ingestion->LoadSettingsCatalogFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSSettingsSubsystem: Could not load the settings from %s; there are none."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateCatalog(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSSettingsSubsystem: %s"), *Problem);
    }
    Catalog = Loaded;
    return true;
}

TArray<FString> UPSSettingsSubsystem::ValidateCatalog(const FPSSettingsCatalog& InCatalog)
{
    TArray<FString> Problems;
    TSet<FName> CategoryIds;
    for (int32 Index = 0; Index < InCatalog.Categories.Num(); ++Index)
    {
        const FName CategoryId = InCatalog.Categories[Index].CategoryId;
        if (CategoryId.IsNone() || CategoryIds.Contains(CategoryId))
        {
            Problems.Add(FString::Printf(TEXT("Categories[%d]: CategoryId is empty or used twice"), Index));
        }
        CategoryIds.Add(CategoryId);
    }

    TSet<FName> SettingIds;
    for (int32 Index = 0; Index < InCatalog.Settings.Num(); ++Index)
    {
        const FPSSettingDef& Def = InCatalog.Settings[Index];
        const FString Where = FString::Printf(TEXT("Settings[%d] '%s'"), Index, *Def.SettingId.ToString());
        if (Def.SettingId.IsNone() || SettingIds.Contains(Def.SettingId))
        {
            Problems.Add(Where + TEXT(": SettingId is empty or used twice"));
        }
        SettingIds.Add(Def.SettingId);
        if (!CategoryIds.Contains(Def.Category))
        {
            Problems.Add(Where + FString::Printf(TEXT(": unknown category '%s'"), *Def.Category.ToString()));
        }
        switch (Def.Kind)
        {
        case EPSSettingKind::Toggle:
            if (Def.Default != 0.f && Def.Default != 1.f)
            {
                Problems.Add(Where + TEXT(": a toggle's Default is 0 or 1"));
            }
            break;
        case EPSSettingKind::Choice:
            if (Def.Choices.Num() < 2)
            {
                Problems.Add(Where + TEXT(": a choice needs at least two Choices"));
            }
            if (Def.Values.Num() > 0 && Def.Values.Num() != Def.Choices.Num())
            {
                Problems.Add(Where + TEXT(": Values must be empty or one per choice"));
            }
            if (Def.Default < 0.f || Def.Default >= Def.Choices.Num() || Def.Default != FMath::RoundToFloat(Def.Default))
            {
                Problems.Add(Where + TEXT(": a choice's Default is the index of one of its Choices"));
            }
            break;
        case EPSSettingKind::Slider:
            if (Def.Step <= 0.f || Def.Max <= Def.Min)
            {
                Problems.Add(Where + TEXT(": a slider needs Min below Max and a positive Step"));
            }
            else if (Def.Default < Def.Min || Def.Default > Def.Max)
            {
                Problems.Add(Where + TEXT(": Default is outside Min..Max"));
            }
            break;
        default:
            break;
        }
    }
    return Problems;
}

float UPSSettingsSubsystem::Snap(const FPSSettingDef& Def, float Value) const
{
    switch (Def.Kind)
    {
    case EPSSettingKind::Toggle:
        return Value >= 0.5f ? 1.f : 0.f;
    case EPSSettingKind::Choice:
        return FMath::Clamp(FMath::RoundToFloat(Value), 0.f, FMath::Max(0.f, float(Def.Choices.Num() - 1)));
    case EPSSettingKind::Slider:
    {
        const float Steps = Def.Step > 0.f ? FMath::RoundToFloat((Value - Def.Min) / Def.Step) : 0.f;
        return FMath::Clamp(Def.Min + Steps * Def.Step, Def.Min, Def.Max);
    }
    default:
        return Value;
    }
}

float UPSSettingsSubsystem::GetValue(FName SettingId)
{
    const FPSSettingDef* Def = GetCatalog().FindSetting(SettingId);
    if (!Def)
    {
        return 0.f;
    }
    const float* Stored = Values.Find(SettingId);
    return Stored ? *Stored : Def->Default;
}

float UPSSettingsSubsystem::GetNumber(FName SettingId)
{
    const FPSSettingDef* Def = GetCatalog().FindSetting(SettingId);
    const float Value = GetValue(SettingId);
    if (Def && Def->Kind == EPSSettingKind::Choice && Def->Values.IsValidIndex(FMath::RoundToInt(Value)))
    {
        return Def->Values[FMath::RoundToInt(Value)];
    }
    return Value;
}

bool UPSSettingsSubsystem::GetBool(FName SettingId)
{
    return GetValue(SettingId) >= 0.5f;
}

bool UPSSettingsSubsystem::SetValue(FName SettingId, float Value)
{
    const FPSSettingDef* Def = GetCatalog().FindSetting(SettingId);
    if (!Def)
    {
        return false;
    }
    const float Snapped = Snap(*Def, Value);
    if (FMath::IsNearlyEqual(GetValue(SettingId), Snapped))
    {
        return true;
    }
    Values.Add(SettingId, Snapped);
    if (Saves)
    {
        SaveToProfile(Saves, Slot);
    }
    const bool bEngineSetting = SettingId == WindowModeSettingId || SettingId == ResolutionScaleSettingId || SettingId == QualitySettingId
        || SettingId == VSyncSettingId || SettingId == FrameRateLimitSettingId || SettingId == MasterVolumeSettingId;
    if (bEngineSetting)
    {
        ApplyEngineSettings();
    }
    OnSettingChanged.Broadcast(SettingId, Snapped);
    OnSettingChangedMC.Broadcast(SettingId, Snapped);
    return true;
}

bool UPSSettingsSubsystem::StepSetting(FName SettingId)
{
    const FPSSettingDef* Def = GetCatalog().FindSetting(SettingId);
    if (!Def)
    {
        return false;
    }
    const float Value = GetValue(SettingId);
    float Next = Value;
    switch (Def->Kind)
    {
    case EPSSettingKind::Toggle:
        Next = Value >= 0.5f ? 0.f : 1.f;
        break;
    case EPSSettingKind::Choice:
        Next = Def->Choices.Num() > 0 ? float((FMath::RoundToInt(Value) + 1) % Def->Choices.Num()) : 0.f;
        break;
    case EPSSettingKind::Slider:
        Next = Value + Def->Step > Def->Max + KINDA_SMALL_NUMBER ? Def->Min : Value + Def->Step;
        break;
    default:
        break;
    }
    return SetValue(SettingId, Next);
}

void UPSSettingsSubsystem::ResetToDefaults(FName Category)
{
    for (const FPSSettingDef& Def : GetCatalog().Settings)
    {
        if (Category.IsNone() || Def.Category == Category)
        {
            SetValue(Def.SettingId, Def.Default);
        }
    }
}

FString UPSSettingsSubsystem::FormatValue(FName SettingId)
{
    const FPSSettingDef* Def = GetCatalog().FindSetting(SettingId);
    if (!Def)
    {
        return FString();
    }
    const float Value = GetValue(SettingId);
    switch (Def->Kind)
    {
    case EPSSettingKind::Toggle:
        return Value >= 0.5f ? TEXT("On") : TEXT("Off");
    case EPSSettingKind::Choice:
        return Def->Choices.IsValidIndex(FMath::RoundToInt(Value)) ? Def->Choices[FMath::RoundToInt(Value)] : FString();
    case EPSSettingKind::Slider:
        return FString::SanitizeFloat(Value, 0) + Def->Unit;
    default:
        return FString();
    }
}

bool UPSSettingsSubsystem::LoadFromProfile(UPSSaveSubsystem* InSaves, const FString& InSlot)
{
    // Changes from now on are saved here.
    Saves = InSaves;
    Slot = InSlot;
    const UPSProfileSaveGame* Profile = InSaves && InSaves->DoesSlotExist(InSlot) ? Cast<UPSProfileSaveGame>(InSaves->LoadFromSlot(InSlot)) : nullptr;
    if (!Profile)
    {
        return false;
    }
    Values.Reset();
    for (const TPair<FName, float>& Stored : Profile->Settings)
    {
        // A setting the catalog dropped is forgotten; the rest are snapped into today's range.
        if (const FPSSettingDef* Def = GetCatalog().FindSetting(Stored.Key))
        {
            Values.Add(Stored.Key, Snap(*Def, Stored.Value));
        }
    }
    return true;
}

bool UPSSettingsSubsystem::SaveToProfile(UPSSaveSubsystem* InSaves, const FString& InSlot)
{
    if (!InSaves)
    {
        return false;
    }
    // Keep whatever else the profile holds; only the settings change.
    UPSProfileSaveGame* Profile = InSaves->DoesSlotExist(InSlot) ? Cast<UPSProfileSaveGame>(InSaves->LoadFromSlot(InSlot)) : nullptr;
    if (!Profile)
    {
        Profile = NewObject<UPSProfileSaveGame>(this);
    }
    Profile->Settings = Values;
    if (!InSaves->SaveToSlot(Profile, InSlot))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSSettingsSubsystem: Could not save the settings to %s."), *InSlot);
        return false;
    }
    return true;
}

void UPSSettingsSubsystem::ApplyEngineSettings()
{
    if (!bApplyToEngine || !GEngine)
    {
        return;
    }

    // Window mode, resolution and quality would change the editor itself; only a game applies them.
    UGameUserSettings* UserSettings = GEngine->GetGameUserSettings();
    if (UserSettings && !GIsEditor)
    {
        UserSettings->SetFullscreenMode(EWindowMode::ConvertIntToWindowMode(FMath::RoundToInt(GetNumber(WindowModeSettingId))));
        UserSettings->SetResolutionScaleNormalized(FMath::Clamp(GetNumber(ResolutionScaleSettingId) / 100.f, 0.f, 1.f));
        UserSettings->SetOverallScalabilityLevel(FMath::RoundToInt(GetNumber(QualitySettingId)));
        UserSettings->SetVSyncEnabled(GetBool(VSyncSettingId));
        UserSettings->SetFrameRateLimit(GetNumber(FrameRateLimitSettingId));
        UserSettings->ApplySettings(false);
    }

    if (FAudioDevice* AudioDevice = GEngine->GetMainAudioDeviceRaw())
    {
        AudioDevice->SetTransientPrimaryVolume(FMath::Clamp(GetNumber(MasterVolumeSettingId) / 100.f, 0.f, 1.f));
    }
}
