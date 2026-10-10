#include "PSDefenderTechniqueComponent.h"
#include "PSDataIngestion.h"
#include "PSPlayerPawn.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Misc/Paths.h"

UPSDefenderTechniqueComponent::UPSDefenderTechniqueComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

FString UPSDefenderTechniqueComponent::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/defensive_techniques.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FDefensiveTechniqueTuningRow& UPSDefenderTechniqueComponent::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSDefenderTechniqueComponent::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FDefensiveTechniqueTuningRow Loaded;
    if (!Ingestion->LoadDefensiveTechniquesFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSDefenderTechniqueComponent: Could not load defensive techniques from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSDefenderTechniqueComponent: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

TArray<FString> UPSDefenderTechniqueComponent::ValidateTuning(const FDefensiveTechniqueTuningRow& InTuning)
{
    TArray<FString> Problems;
    if (InTuning.JumpSnapAction.IsNone() || InTuning.StripAction.IsNone() || InTuning.JumpSnapAction == InTuning.StripAction)
    {
        Problems.Add(TEXT("JumpSnapAction and StripAction must name two different actions"));
    }
    if (InTuning.JumpWindowSeconds < 0.f || InTuning.GetOffSpeed < 0.f || InTuning.StripWindowSeconds < 0.f || InTuning.StripCooldownSeconds < 0.f)
    {
        Problems.Add(TEXT("JumpWindowSeconds, GetOffSpeed, StripWindowSeconds and StripCooldownSeconds must be 0 or more"));
    }
    if (InTuning.StripTackleScale < 0.f || InTuning.StripTackleScale > 1.f || InTuning.StripFumbleChance < 0.f || InTuning.StripFumbleChance > 1.f)
    {
        Problems.Add(TEXT("StripTackleScale and StripFumbleChance must be between 0 and 1"));
    }
    return Problems;
}

bool UPSDefenderTechniqueComponent::GetOff(const FVector& LineOfScrimmage)
{
    APSPlayerPawn* Defender = GetDefender();
    UFloatingPawnMovement* Movement = Defender ? Defender->GetFloatingMovementComponent() : nullptr;
    if (!Movement || Defender->TeamSide != EPSTeamSide::Defense)
    {
        return false;
    }
    // Upfield is +X, so the line is ahead of a defender along -X; straight at it either way.
    const float ToLine = LineOfScrimmage.X - Defender->GetActorLocation().X;
    const FVector Direction(FMath::IsNearlyZero(ToLine) ? -1.f : FMath::Sign(ToLine), 0.f, 0.f);
    Movement->Velocity += Direction * GetTuning().GetOffSpeed;
    return true;
}

bool UPSDefenderTechniqueComponent::TryStrip()
{
    const APSPlayerPawn* Defender = GetDefender();
    if (!Defender || Defender->TeamSide != EPSTeamSide::Defense || Defender->HasPossession() || Clock < StripReadyAt)
    {
        return false;
    }
    const FDefensiveTechniqueTuningRow& Settings = GetTuning();
    StripUntil = Clock + Settings.StripWindowSeconds;
    StripReadyAt = Clock + Settings.StripCooldownSeconds;
    ActiveTackleScale = Settings.StripTackleScale;
    ActiveFumbleBonus = Settings.StripFumbleChance * FMath::Clamp(Defender->GetAttributes().Strength, 0.f, 100.f) / 100.f;
    return true;
}

bool UPSDefenderTechniqueComponent::IsStripBusy() const
{
    const APSPlayerPawn* Defender = GetDefender();
    return Defender && Defender->TeamSide == EPSTeamSide::Defense && !Defender->HasPossession() && Clock < StripReadyAt;
}

void UPSDefenderTechniqueComponent::ResetTechniques()
{
    StripUntil = -1.f;
    StripReadyAt = Clock;
}

void UPSDefenderTechniqueComponent::AdvanceTime(float DeltaSeconds)
{
    Clock += DeltaSeconds;
}

void UPSDefenderTechniqueComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    AdvanceTime(DeltaTime);
}

APSPlayerPawn* UPSDefenderTechniqueComponent::GetDefender() const
{
    return Cast<APSPlayerPawn>(GetOwner());
}
