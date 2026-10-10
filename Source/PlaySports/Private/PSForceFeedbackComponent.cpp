#include "PSForceFeedbackComponent.h"
#include "PSDataIngestion.h"
#include "PSPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"

UPSForceFeedbackComponent::UPSForceFeedbackComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    bEnabled = true;
    ActiveDevice = EPSInputDevice::KeyboardMouse;
    bTuningLoaded = false;
}

FString UPSForceFeedbackComponent::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/force_feedback.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSForceFeedbackTuning& UPSForceFeedbackComponent::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
        bTuningLoaded = true;
    }
    return Tuning;
}

bool UPSForceFeedbackComponent::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSForceFeedbackTuning Loaded;
    if (!Ingestion->LoadForceFeedbackTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSForceFeedbackComponent: Could not load force feedback patterns from %s; the controller will not rumble."), *JsonFilePath);
        return false;
    }

    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSForceFeedbackComponent: %s"), *Problem);
    }

    Tuning = MoveTemp(Loaded);
    bTuningLoaded = true;
    return true;
}

TArray<FString> UPSForceFeedbackComponent::ValidateTuning(const FPSForceFeedbackTuning& InTuning)
{
    TArray<FString> Problems;

    if (InTuning.MasterIntensity < 0.f || InTuning.MasterIntensity > 1.f)
    {
        Problems.Add(FString::Printf(TEXT("MasterIntensity %.2f is outside 0-1."), InTuning.MasterIntensity));
    }

    const UEnum* CueEnum = StaticEnum<EPSForceFeedbackCue>();
    TSet<EPSForceFeedbackCue> Seen;
    for (const FForceFeedbackTuningRow& Row : InTuning.Cues)
    {
        const FString CueName = CueEnum->GetNameStringByValue(static_cast<int64>(Row.Cue));
        bool bAlreadySeen = false;
        Seen.Add(Row.Cue, &bAlreadySeen);
        if (bAlreadySeen)
        {
            Problems.Add(FString::Printf(TEXT("Cue '%s' has more than one pattern."), *CueName));
        }
        if (Row.Intensity < 0.f || Row.Intensity > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("Cue '%s': Intensity %.2f is outside 0-1."), *CueName, Row.Intensity));
        }
        if (Row.Duration <= 0.f || Row.Duration > MaxCueDurationSeconds)
        {
            Problems.Add(FString::Printf(TEXT("Cue '%s': Duration %.2f must be above 0 and at most %.1f seconds."), *CueName, Row.Duration, MaxCueDurationSeconds));
        }
        if (Row.Intensity > 0.f && !Row.bLeftLarge && !Row.bLeftSmall && !Row.bRightLarge && !Row.bRightSmall)
        {
            Problems.Add(FString::Printf(TEXT("Cue '%s' rumbles no motor."), *CueName));
        }
    }

    // NumEnums() includes the generated _MAX entry.
    for (int32 Index = 0; Index < CueEnum->NumEnums() - 1; ++Index)
    {
        const EPSForceFeedbackCue Cue = static_cast<EPSForceFeedbackCue>(CueEnum->GetValueByIndex(Index));
        if (!Seen.Contains(Cue))
        {
            Problems.Add(FString::Printf(TEXT("Cue '%s' has no pattern."), *CueEnum->GetNameStringByIndex(Index)));
        }
    }
    return Problems;
}

void UPSForceFeedbackComponent::BeginPlay()
{
    Super::BeginPlay();

    GetTuning();
    BindToBus();
}

void UPSForceFeedbackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnbindFromBus();

    Super::EndPlay(EndPlayReason);
}

void UPSForceFeedbackComponent::BindToBus()
{
    UWorld* World = GetWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }

    UnbindFromBus();
    Bus->OnDamageMC.AddUObject(this, &UPSForceFeedbackComponent::HandleDamage);
    Bus->OnTackleMC.AddUObject(this, &UPSForceFeedbackComponent::HandleTackle);
    Bus->OnCatchMC.AddUObject(this, &UPSForceFeedbackComponent::HandleCatch);
    Bus->OnFumbleMC.AddUObject(this, &UPSForceFeedbackComponent::HandleFumble);
    Bus->OnScoreMC.AddUObject(this, &UPSForceFeedbackComponent::HandleScore);
    Bus->OnControlChangeMC.AddUObject(this, &UPSForceFeedbackComponent::HandleControlChange);
    Bus->OnInputDeviceChangeMC.AddUObject(this, &UPSForceFeedbackComponent::HandleInputDeviceChange);
    BoundBus = Bus;
}

void UPSForceFeedbackComponent::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnDamageMC.RemoveAll(this);
        Bus->OnTackleMC.RemoveAll(this);
        Bus->OnCatchMC.RemoveAll(this);
        Bus->OnFumbleMC.RemoveAll(this);
        Bus->OnScoreMC.RemoveAll(this);
        Bus->OnControlChangeMC.RemoveAll(this);
        Bus->OnInputDeviceChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

bool UPSForceFeedbackComponent::ResolveCue(EPSForceFeedbackCue Cue, bool bInvolved, FPSForceFeedbackDispatch& OutDispatch)
{
    if (!bEnabled)
    {
        return false;
    }

    const FPSForceFeedbackTuning& Patterns = GetTuning();
    const FForceFeedbackTuningRow* Row = Patterns.FindCue(Cue);
    if (!Row || (Row->bOnlyWhenInvolved && !bInvolved))
    {
        return false;
    }

    const float Intensity = FMath::Clamp(Row->Intensity * Patterns.MasterIntensity, 0.f, 1.f);
    if (Intensity <= 0.f || Row->Duration <= 0.f)
    {
        return false;
    }

    OutDispatch = FPSForceFeedbackDispatch();
    OutDispatch.Cue = Cue;
    OutDispatch.Intensity = Intensity;
    OutDispatch.Duration = FMath::Min(Row->Duration, MaxCueDurationSeconds);
    OutDispatch.bLeftLarge = Row->bLeftLarge;
    OutDispatch.bLeftSmall = Row->bLeftSmall;
    OutDispatch.bRightLarge = Row->bRightLarge;
    OutDispatch.bRightSmall = Row->bRightSmall;
    return true;
}

bool UPSForceFeedbackComponent::IsControlled(const FString& Name) const
{
    return !ControlledPlayerName.IsEmpty() && Name == ControlledPlayerName;
}

void UPSForceFeedbackComponent::Play(EPSForceFeedbackCue Cue, bool bInvolved)
{
    FPSForceFeedbackDispatch Dispatch;
    if (!ResolveCue(Cue, bInvolved, Dispatch))
    {
        return;
    }

    // Only a gamepad has motors; on keyboard/mouse the cue is decided but goes nowhere. A
    // controller without a local player (headless worlds) accepts the call and plays nothing.
    APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
    if (ActiveDevice == EPSInputDevice::Gamepad && PlayerController)
    {
        PlayerController->PlayDynamicForceFeedback(Dispatch.Intensity, Dispatch.Duration,
            Dispatch.bLeftLarge, Dispatch.bLeftSmall, Dispatch.bRightLarge, Dispatch.bRightSmall);
        Dispatch.bPlayedOnGamepad = true;
    }

    if (RecentDispatches.Num() >= MaxRecentDispatches)
    {
        RecentDispatches.RemoveAt(0);
    }
    RecentDispatches.Add(Dispatch);
    OnDispatched.Broadcast(Dispatch);
}

void UPSForceFeedbackComponent::HandleDamage(const FPSTelemetryDamageEvent& Event)
{
    Play(EPSForceFeedbackCue::Hit, IsControlled(Event.TargetName));
}

void UPSForceFeedbackComponent::HandleTackle(const FPSTelemetryTackleEvent& Event)
{
    Play(Event.bIsSack ? EPSForceFeedbackCue::Sack : EPSForceFeedbackCue::Tackle,
        IsControlled(Event.TacklerName) || IsControlled(Event.BallCarrierName));
}

void UPSForceFeedbackComponent::HandleCatch(const FPSTelemetryCatchEvent& Event)
{
    Play(Event.bIsInterception ? EPSForceFeedbackCue::Interception : EPSForceFeedbackCue::Catch,
        IsControlled(Event.ReceiverName));
}

void UPSForceFeedbackComponent::HandleFumble(const FPSTelemetryFumbleEvent& Event)
{
    Play(EPSForceFeedbackCue::Fumble, IsControlled(Event.FumblerName) || IsControlled(Event.RecoveryName));
}

void UPSForceFeedbackComponent::HandleScore(const FPSTelemetryScoreEvent& Event)
{
    // A score names no player; only patterns every player feels play for it.
    Play(EPSForceFeedbackCue::Score, false);
}

bool UPSForceFeedbackComponent::IsOwnHuman(int32 HumanIndex) const
{
    const APSPlayerController* OwningController = Cast<APSPlayerController>(GetOwner());
    return HumanIndex == (OwningController ? OwningController->HumanIndex : 0);
}

void UPSForceFeedbackComponent::HandleControlChange(const FPSTelemetryControlChangeEvent& Event)
{
    if (!IsOwnHuman(Event.HumanIndex))
    {
        return;
    }
    if (Event.bHumanControlled)
    {
        ControlledPlayerName = Event.PlayerName;
    }
    else if (Event.PlayerName == ControlledPlayerName)
    {
        ControlledPlayerName.Empty();
    }
}

void UPSForceFeedbackComponent::HandleInputDeviceChange(const FPSTelemetryInputDeviceEvent& Event)
{
    if (IsOwnHuman(Event.HumanIndex))
    {
        ActiveDevice = Event.ActiveDevice;
    }
}
