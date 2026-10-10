#include "PSUIAccessibilitySubsystem.h"
#include "PSDataIngestion.h"
#include "PSLocalization.h"
#include "PSSettingsSubsystem.h"
#include "PSUICaptionWidget.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"

UPSUIAccessibilitySubsystem::UPSUIAccessibilitySubsystem()
{
    CaptionsSettingId = TEXT("Captions");
    CaptionSizeSettingId = TEXT("CaptionSize");
    NarrationSettingId = TEXT("Narration");
    ColorblindSettingId = TEXT("ColorblindMode");
}

void UPSUIAccessibilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>())
    {
        Bus->OnSpeechMC.AddUObject(this, &UPSUIAccessibilitySubsystem::HandleSpeech);
        BoundBus = Bus;
    }
}

void UPSUIAccessibilitySubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSpeechMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

bool UPSUIAccessibilitySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSUIAccessibilitySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    // The local player reads the captions at the bottom of the screen.
    APlayerController* Player = InWorld.GetFirstPlayerController();
    if (Player && Player->IsLocalController() && !CaptionWidget)
    {
        CaptionWidget = CreateWidget<UPSUICaptionWidget>(Player, UPSUICaptionWidget::StaticClass());
        if (CaptionWidget)
        {
            CaptionWidget->AddToViewport(50);
        }
    }
}

FString UPSUIAccessibilitySubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/ui_accessibility.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSUIAccessibilityTuning& UPSUIAccessibilitySubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSUIAccessibilitySubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSUIAccessibilityTuning Loaded;
    if (!Ingestion->LoadUIAccessibilityTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSUIAccessibilitySubsystem: Could not load %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSUIAccessibilitySubsystem: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

TArray<FString> UPSUIAccessibilitySubsystem::ValidateTuning(const FPSUIAccessibilityTuning& InTuning)
{
    TArray<FString> Problems;
    if (InTuning.CaptionMinSeconds <= 0.f || InTuning.CaptionMaxSeconds < InTuning.CaptionMinSeconds)
    {
        Problems.Add(TEXT("CaptionMinSeconds must be positive and CaptionMaxSeconds no less"));
    }
    if (InTuning.CaptionWordsPerSecond <= 0.f)
    {
        Problems.Add(TEXT("CaptionWordsPerSecond must be positive"));
    }
    if (InTuning.CaptionMaxLines < 1)
    {
        Problems.Add(TEXT("CaptionMaxLines must be 1 or more"));
    }
    if (InTuning.MinMatchupColorDistance < 0.f)
    {
        Problems.Add(TEXT("MinMatchupColorDistance must be 0 or more"));
    }
    return Problems;
}

UPSSettingsSubsystem* UPSUIAccessibilitySubsystem::GetSettings() const
{
    return SettingsOverride ? SettingsOverride : UPSSettingsSubsystem::Get(this);
}

EPSColorblindMode UPSUIAccessibilitySubsystem::GetColorblindMode(UPSSettingsSubsystem* Settings)
{
    if (!Settings)
    {
        return EPSColorblindMode::Off;
    }
    const int32 Index = FMath::RoundToInt(Settings->GetValue(GetDefault<UPSUIAccessibilitySubsystem>()->ColorblindSettingId));
    return Index >= 0 && Index <= int32(EPSColorblindMode::Tritanopia) ? EPSColorblindMode(Index) : EPSColorblindMode::Off;
}

bool UPSUIAccessibilitySubsystem::AreCaptionsOn()
{
    UPSSettingsSubsystem* Settings = GetSettings();
    // Without settings (no game instance) captions follow the setting's usual default: on.
    return !Settings || Settings->GetBool(CaptionsSettingId);
}

bool UPSUIAccessibilitySubsystem::IsNarrationOn()
{
    UPSSettingsSubsystem* Settings = GetSettings();
    return Settings && Settings->GetBool(NarrationSettingId);
}

int32 UPSUIAccessibilitySubsystem::GetCaptionFontSize()
{
    UPSSettingsSubsystem* Settings = GetSettings();
    const int32 Size = Settings ? FMath::RoundToInt(Settings->GetNumber(CaptionSizeSettingId)) : 0;
    return Size > 0 ? Size : 24;
}

float UPSUIAccessibilitySubsystem::GetCaptionSeconds(const FString& Text)
{
    TArray<FString> Words;
    Text.ParseIntoArrayWS(Words);
    const FPSUIAccessibilityTuning& Timing = GetTuning();
    return FMath::Clamp(Words.Num() / FMath::Max(Timing.CaptionWordsPerSecond, KINDA_SMALL_NUMBER), Timing.CaptionMinSeconds, Timing.CaptionMaxSeconds);
}

bool UPSUIAccessibilitySubsystem::ShowCaption(const FString& Speaker, const FString& Text, FName Channel, float DurationSeconds, float Now)
{
    if (Text.IsEmpty() || !AreCaptionsOn())
    {
        return false;
    }
    FPSCaptionLine& Line = Lines.AddDefaulted_GetRef();
    Line.Speaker = Speaker;
    Line.Text = Text;
    Line.Channel = Channel;
    Line.ShownAt = Now;
    Line.ExpiresAt = Now + (DurationSeconds > 0.f ? DurationSeconds : GetCaptionSeconds(Text));

    // Forget what has come down, and the oldest beyond the screen's lines.
    Lines.RemoveAll([Now](const FPSCaptionLine& Old) { return Old.ExpiresAt <= Now; });
    const int32 MaxLines = FMath::Max(1, GetTuning().CaptionMaxLines);
    if (Lines.Num() > MaxLines)
    {
        Lines.RemoveAt(0, Lines.Num() - MaxLines);
    }
    return true;
}

TArray<FPSCaptionLine> UPSUIAccessibilitySubsystem::GetActiveCaptions(float Now) const
{
    TArray<FPSCaptionLine> Active = Lines.FilterByPredicate([Now](const FPSCaptionLine& Line) { return Line.ShownAt <= Now && Now < Line.ExpiresAt; });
    const int32 MaxLines = FMath::Max(1, Tuning.CaptionMaxLines);
    if (Active.Num() > MaxLines)
    {
        Active.RemoveAt(0, Active.Num() - MaxLines);
    }
    return Active;
}

FText UPSUIAccessibilitySubsystem::FormatCaptionText(const FPSCaptionLine& Line)
{
    if (Line.Speaker.IsEmpty())
    {
        return UPSLocalization::FromLocalized(Line.Text);
    }
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Speaker"), UPSLocalization::FromLocalized(Line.Speaker));
    Arguments.Add(TEXT("Text"), UPSLocalization::FromLocalized(Line.Text));
    return UPSLocalization::Format(TEXT("Caption.Line"), Arguments);
}

FString UPSUIAccessibilitySubsystem::FormatCaption(const FPSCaptionLine& Line)
{
    return FormatCaptionText(Line).ToString();
}

void UPSUIAccessibilitySubsystem::Narrate(const FString& Text)
{
    if (Text.IsEmpty() || !IsNarrationOn())
    {
        return;
    }
    OnNarration.Broadcast(Text);
    OnNarrationMC.Broadcast(Text);
}

void UPSUIAccessibilitySubsystem::HandleSpeech(const FPSTelemetrySpeechEvent& Event)
{
    const UWorld* World = GetWorld();
    ShowCaption(Event.Speaker, Event.Text, Event.Channel, Event.DurationSeconds, World ? World->GetTimeSeconds() : 0.f);
}
