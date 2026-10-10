#include "PSKickMeterComponent.h"
#include "PSDataIngestion.h"
#include "PSInputBufferComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

UPSKickMeterComponent::UPSKickMeterComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

FString UPSKickMeterComponent::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/kick_meter.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FKickMeterTuningRow& UPSKickMeterComponent::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSKickMeterComponent::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FKickMeterTuningRow Loaded;
    if (!Ingestion->LoadKickMeterTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSKickMeterComponent: Could not load the kick meter from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSKickMeterComponent: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

TArray<FString> UPSKickMeterComponent::ValidateTuning(const FKickMeterTuningRow& InTuning)
{
    TArray<FString> Problems;
    if (InTuning.KickAction.IsNone())
    {
        Problems.Add(TEXT("KickAction is empty"));
    }
    if (InTuning.LineUpSeconds <= 0.f || InTuning.PowerFillSeconds <= 0.f || InTuning.AccuracySweepSeconds <= 0.f)
    {
        Problems.Add(TEXT("LineUpSeconds, PowerFillSeconds and AccuracySweepSeconds must be positive"));
    }
    if (InTuning.PowerWeight < 0.f || InTuning.AccuracyWeight < 0.f || InTuning.PowerWeight + InTuning.AccuracyWeight <= 0.f)
    {
        Problems.Add(TEXT("PowerWeight and AccuracyWeight must be 0 or more, and not both 0"));
    }
    return Problems;
}

float UPSKickMeterComponent::ComputeRoll(float Power, float Needle, const FKickMeterTuningRow& InTuning)
{
    const float Shortfall = 1.f - FMath::Clamp(Power, 0.f, 1.f);
    const float Miss = FMath::Abs(FMath::Clamp(Needle, -1.f, 1.f));
    return FMath::Clamp(InTuning.PowerWeight * Shortfall + InTuning.AccuracyWeight * Miss, 0.f, 1.f);
}

bool UPSKickMeterComponent::IsKickPhase(const FString& PhaseName)
{
    return PhaseName == TEXT("Kickoff") || PhaseName == TEXT("Punt") || PhaseName == TEXT("FieldGoal");
}

void UPSKickMeterComponent::BeginPlay()
{
    Super::BeginPlay();
    GetTuning();
    BindToController();
    BindToBus();
}

void UPSKickMeterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    APSPlayerController* Controller = GetPlayerController();
    if (UPSInputBufferComponent* Buffer = Controller ? Controller->GetInputBufferComponent() : nullptr)
    {
        Buffer->OnActionPressed.RemoveDynamic(this, &UPSKickMeterComponent::HandleActionPressed);
        Buffer->OnActionReleased.RemoveDynamic(this, &UPSKickMeterComponent::HandleActionReleased);
    }
    UnbindFromBus();
    Super::EndPlay(EndPlayReason);
}

void UPSKickMeterComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    AdvanceTime(DeltaTime);
}

void UPSKickMeterComponent::BindToController()
{
    APSPlayerController* Controller = GetPlayerController();
    UPSInputBufferComponent* Buffer = Controller ? Controller->GetInputBufferComponent() : nullptr;
    if (!Buffer)
    {
        return;
    }
    Buffer->BindToController();
    Buffer->OnActionPressed.AddUniqueDynamic(this, &UPSKickMeterComponent::HandleActionPressed);
    Buffer->OnActionReleased.AddUniqueDynamic(this, &UPSKickMeterComponent::HandleActionReleased);
}

void UPSKickMeterComponent::BindToBus()
{
    UWorld* World = GetWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }
    UnbindFromBus();
    Bus->OnPhaseChangeMC.AddUObject(this, &UPSKickMeterComponent::HandlePhaseChange);
    BoundBus = Bus;
}

void UPSKickMeterComponent::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnPhaseChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSKickMeterComponent::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (IsKickPhase(Event.NewPhase))
    {
        LineUp(Event.NewPhase);
    }
    else
    {
        CancelKick();
    }
}

bool UPSKickMeterComponent::LineUp(const FString& InKickType)
{
    const APSPlayerController* Controller = GetPlayerController();
    const APSPlayerPawn* Kicker = Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
    UPSTelemetryBus* Bus = GetBus();
    if (!Kicker || Kicker->TeamSide != EPSTeamSide::Offense || !Bus)
    {
        CancelKick();
        return false;
    }

    const FKickMeterTuningRow& Settings = GetTuning();
    Stage = EPSKickMeterStage::LiningUp;
    KickType = InKickType;
    KickerName = Kicker->GetAttributes().DisplayName;
    LinedUpAt = Clock;
    StageStartedAt = Clock;
    LockedPower = 0.f;

    FPSTelemetryKickEvent Event;
    Event.KickerName = KickerName;
    Event.KickType = KickType;
    Event.bLiningUp = true;
    Event.HoldSeconds = Settings.LineUpSeconds;
    Bus->PublishKick(Event);
    return true;
}

void UPSKickMeterComponent::CancelKick()
{
    Stage = EPSKickMeterStage::Idle;
    KickType.Reset();
}

bool UPSKickMeterComponent::PressKick()
{
    if (Stage == EPSKickMeterStage::LiningUp)
    {
        Stage = EPSKickMeterStage::Power;
        StageStartedAt = Clock;
        return true;
    }
    if (Stage == EPSKickMeterStage::Accuracy)
    {
        Kick(NeedleAfter(Clock - StageStartedAt));
        return true;
    }
    return false;
}

bool UPSKickMeterComponent::ReleaseKick()
{
    if (Stage != EPSKickMeterStage::Power)
    {
        return false;
    }
    LockedPower = PowerAfter(Clock - StageStartedAt);
    Stage = EPSKickMeterStage::Accuracy;
    StageStartedAt = Clock;
    return true;
}

void UPSKickMeterComponent::AdvanceTime(float DeltaSeconds)
{
    Clock += DeltaSeconds;
    if (Stage == EPSKickMeterStage::Accuracy && Clock - StageStartedAt >= GetTuning().AccuracySweepSeconds)
    {
        // Nobody stopped the needle: it ends pushed right.
        Kick(1.f);
    }
    else if (Stage != EPSKickMeterStage::Idle && Clock - LinedUpAt >= GetTuning().LineUpSeconds)
    {
        // The play stopped waiting; the CPU kicks.
        CancelKick();
    }
}

float UPSKickMeterComponent::GetPower() const
{
    if (Stage == EPSKickMeterStage::Power)
    {
        return PowerAfter(Clock - StageStartedAt);
    }
    return Stage == EPSKickMeterStage::Accuracy ? LockedPower : 0.f;
}

float UPSKickMeterComponent::GetNeedle() const
{
    return Stage == EPSKickMeterStage::Accuracy ? NeedleAfter(Clock - StageStartedAt) : 0.f;
}

float UPSKickMeterComponent::PowerAfter(float HeldSeconds) const
{
    // Up to full in PowerFillSeconds, then back down, and so on.
    const float Cycle = FMath::Fmod(FMath::Max(0.f, HeldSeconds) / FMath::Max(Tuning.PowerFillSeconds, KINDA_SMALL_NUMBER), 2.f);
    return Cycle <= 1.f ? Cycle : 2.f - Cycle;
}

float UPSKickMeterComponent::NeedleAfter(float SweptSeconds) const
{
    const float Progress = FMath::Clamp(SweptSeconds / FMath::Max(Tuning.AccuracySweepSeconds, KINDA_SMALL_NUMBER), 0.f, 1.f);
    return -1.f + 2.f * Progress;
}

void UPSKickMeterComponent::Kick(float Needle)
{
    FPSTelemetryKickEvent Event;
    Event.KickerName = KickerName;
    Event.KickType = KickType;
    Event.bLiningUp = false;
    Event.Power = LockedPower;
    Event.Accuracy = FMath::Clamp(Needle, -1.f, 1.f);
    Event.Roll = ComputeRoll(Event.Power, Event.Accuracy, GetTuning());
    CancelKick();
    if (UPSTelemetryBus* Bus = GetBus())
    {
        Bus->PublishKick(Event);
    }
}

void UPSKickMeterComponent::HandleActionPressed(FName ActionId, float HeldSeconds)
{
    if (ActionId == GetTuning().KickAction)
    {
        PressKick();
    }
}

void UPSKickMeterComponent::HandleActionReleased(FName ActionId, float HeldSeconds)
{
    if (ActionId == GetTuning().KickAction)
    {
        ReleaseKick();
    }
}

UPSTelemetryBus* UPSKickMeterComponent::GetBus() const
{
    UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
}

APSPlayerController* UPSKickMeterComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}
