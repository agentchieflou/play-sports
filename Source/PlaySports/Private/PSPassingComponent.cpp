#include "PSPassingComponent.h"
#include "PSBall.h"
#include "PSBallActionComponent.h"
#include "PSDataIngestion.h"
#include "PSFieldReads.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"

UPSPassingComponent::UPSPassingComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

FString UPSPassingComponent::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/passing_input.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPassingInputTuningRow& UPSPassingComponent::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSPassingComponent::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPassingInputTuningRow Loaded;
    if (!Ingestion->LoadPassingInputTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPassingComponent: Could not load passing tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    Tuning = Loaded;
    return true;
}

void UPSPassingComponent::BeginPlay()
{
    Super::BeginPlay();
    GetTuning();
    BindToController();
}

void UPSPassingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (APSPlayerController* Controller = GetPlayerController())
    {
        Controller->OnCatalogActionStarted.RemoveDynamic(this, &UPSPassingComponent::HandleActionStarted);
        Controller->OnCatalogActionCompleted.RemoveDynamic(this, &UPSPassingComponent::HandleActionCompleted);
    }
    Super::EndPlay(EndPlayReason);
}

void UPSPassingComponent::BindToController()
{
    if (APSPlayerController* Controller = GetPlayerController())
    {
        Controller->OnCatalogActionStarted.AddUniqueDynamic(this, &UPSPassingComponent::HandleActionStarted);
        Controller->OnCatalogActionCompleted.AddUniqueDynamic(this, &UPSPassingComponent::HandleActionCompleted);
    }
}

void UPSPassingComponent::HandleActionStarted(FName ActionId)
{
    const FPassingInputTuningRow& Settings = GetTuning();
    if (ActionId == Settings.PumpFakeAction)
    {
        PumpFake();
        return;
    }
    const int32 Slot = Settings.SlotActions.IndexOfByKey(ActionId);
    if (Slot == INDEX_NONE || !GetWorld())
    {
        return;
    }
    if (SlotPressedAt.Num() < Settings.SlotActions.Num())
    {
        SlotPressedAt.Init(-1.f, Settings.SlotActions.Num());
    }
    SlotPressedAt[Slot] = GetWorld()->GetTimeSeconds();
}

void UPSPassingComponent::HandleActionCompleted(FName ActionId)
{
    const int32 Slot = GetTuning().SlotActions.IndexOfByKey(ActionId);
    if (Slot == INDEX_NONE || !SlotPressedAt.IsValidIndex(Slot) || SlotPressedAt[Slot] < 0.f || !GetWorld())
    {
        return;
    }
    // The throw goes on release, so how long the button was held shapes it.
    const float HeldSeconds = GetWorld()->GetTimeSeconds() - SlotPressedAt[Slot];
    SlotPressedAt[Slot] = -1.f;
    const APSPlayerController* Controller = GetPlayerController();
    ThrowToSlot(Slot, HeldSeconds, Controller ? Controller->GetMoveInput() : FVector2D::ZeroVector);
}

bool UPSPassingComponent::CanPass() const
{
    const APSPlayerPawn* Passer = GetPasser();
    return Passer && Passer->HasPossession() && Passer->GetAttributes().Role == EPlayerRole::Quarterback;
}

TArray<APSPlayerPawn*> UPSPassingComponent::GetReceiverSlots() const
{
    TArray<APSPlayerPawn*> Slots;
    const APSPlayerPawn* Passer = GetPasser();
    if (!Passer || !GetWorld())
    {
        return Slots;
    }
    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        APSPlayerPawn* Candidate = *It;
        if (Candidate == Passer || Candidate->TeamSide != Passer->TeamSide)
        {
            continue;
        }
        const EPlayerRole Role = Candidate->GetAttributes().Role;
        if (Role == EPlayerRole::WideReceiver || Role == EPlayerRole::TightEnd || Role == EPlayerRole::RunningBack)
        {
            Slots.Add(Candidate);
        }
    }
    // Facing upfield (+X), left is -Y.
    Slots.Sort([](const APSPlayerPawn& A, const APSPlayerPawn& B)
    {
        return A.GetActorLocation().Y < B.GetActorLocation().Y;
    });
    return Slots;
}

bool UPSPassingComponent::ThrowToSlot(int32 Slot, float HeldSeconds, FVector2D Placement)
{
    if (!CanPass())
    {
        return false;
    }
    const TArray<APSPlayerPawn*> Slots = GetReceiverSlots();
    if (!Slots.IsValidIndex(Slot))
    {
        return false;
    }
    APSPlayerPawn* Passer = GetPasser();
    APSPlayerPawn* Receiver = Slots[Slot];
    UPSBallActionComponent* BallAction = Passer->GetBallActionComponent();
    APSBall* Ball = BallAction ? BallAction->GetCarriedBall() : nullptr;
    if (!Ball)
    {
        return false;
    }

    // The receiver's lead point, moved by the stick in the view's frame.
    const FPassingInputTuningRow& Settings = GetTuning();
    const APSPlayerController* Controller = GetPlayerController();
    const FRotationMatrix YawMatrix(FRotator(0.f, Controller ? Controller->GetControlRotation().Yaw : 0.f, 0.f));
    const FVector2D Stick(FMath::Clamp(Placement.X, -1.f, 1.f), FMath::Clamp(Placement.Y, -1.f, 1.f));
    FVector Target = PSFieldReads::LeadPoint(Passer->GetActorLocation(), Receiver, Settings.LeadSpeed);
    Target += YawMatrix.GetUnitAxis(EAxis::X) * (Stick.Y * Settings.PlacementDepth);
    Target += YawMatrix.GetUnitAxis(EAxis::Y) * (Stick.X * Settings.PlacementWidth);

    const bool bBullet = HeldSeconds >= Settings.BulletHoldSeconds;
    return Passer->ThrowPass(Ball, Target, false, Receiver, bBullet ? 1.f : Settings.TouchSpeedScale);
}

bool UPSPassingComponent::PumpFake()
{
    if (!CanPass())
    {
        return false;
    }
    UPSTelemetryBus* Bus = GetWorld() ? GetWorld()->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus)
    {
        return false;
    }
    const APSPlayerPawn* Passer = GetPasser();
    FPSTelemetryPumpFakeEvent Event;
    Event.PasserName = Passer->GetAttributes().DisplayName;
    Event.PasserLocation = Passer->GetActorLocation();
    Bus->PublishPumpFake(Event);
    return true;
}

APSPlayerController* UPSPassingComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}

APSPlayerPawn* UPSPassingComponent::GetPasser() const
{
    const APSPlayerController* Controller = GetPlayerController();
    return Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
}
