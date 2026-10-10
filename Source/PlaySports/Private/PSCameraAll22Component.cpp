#include "PSCameraAll22Component.h"
#include "PSDataIngestion.h"
#include "PSPlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

UPSCameraAll22Component::UPSCameraAll22Component()
{
    PrimaryComponentTick.bCanEverTick = false;
    ToggleActionId = TEXT("FilmView");
}

FString UPSCameraAll22Component::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/camera_all22.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

FString UPSCameraAll22Component::GetDefaultExportDirectory()
{
    return FPaths::ProjectSavedDir() / TEXT("Film");
}

const FPSAll22CameraTuning& UPSCameraAll22Component::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSCameraAll22Component::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSAll22CameraTuning Loaded;
    if (!Ingestion->LoadAll22CameraTuningFromJson(JsonFilePath, Loaded) || Loaded.All22Rigs.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCameraAll22Component: Could not load the all-22 rigs from %s; using one default sideline rig."), *JsonFilePath);
        Tuning = FPSAll22CameraTuning();
        Tuning.All22Rigs.Add(FPSAll22RigDef());
        return false;
    }
    for (const FString& Problem : UPSCameraFraming::ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCameraAll22Component: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

void UPSCameraAll22Component::BeginPlay()
{
    Super::BeginPlay();
    BindToBus();
}

void UPSCameraAll22Component::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    const TArray<TWeakObjectPtr<APSPlayerController>> Bound = BoundControllers;
    for (const TWeakObjectPtr<APSPlayerController>& Weak : Bound)
    {
        UnbindFromController(Weak.Get());
    }
    BoundControllers.Reset();
    UnbindFromBus();
    Super::EndPlay(EndPlayReason);
}

void UPSCameraAll22Component::BindToController(APSPlayerController* Controller)
{
    if (!Controller)
    {
        return;
    }
    Controller->OnCatalogActionStarted.AddUniqueDynamic(this, &UPSCameraAll22Component::HandleActionStarted);
    BoundControllers.AddUnique(TWeakObjectPtr<APSPlayerController>(Controller));
}

void UPSCameraAll22Component::UnbindFromController(APSPlayerController* Controller)
{
    if (!Controller)
    {
        return;
    }
    Controller->OnCatalogActionStarted.RemoveDynamic(this, &UPSCameraAll22Component::HandleActionStarted);
    BoundControllers.Remove(TWeakObjectPtr<APSPlayerController>(Controller));
}

void UPSCameraAll22Component::BindToBus()
{
    UWorld* World = GetWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }
    UnbindFromBus();
    Bus->OnSnapMC.AddUObject(this, &UPSCameraAll22Component::HandleSnap);
    BoundBus = Bus;
}

void UPSCameraAll22Component::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSCameraAll22Component::HandleActionStarted(FName ActionId)
{
    if (ActionId == ToggleActionId)
    {
        CycleView();
    }
}

void UPSCameraAll22Component::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    // The formation at the snap says which way the offense attacks. It holds for the play (only
    // a cut or the next snap reads it again), so the end-zone rig never swaps ends mid-play.
    if (IsFilmViewActive())
    {
        CutToActiveRig();
        return;
    }
    TArray<APSPlayerPawn*> Players;
    GatherPlayers(Players);
    AttackDirection = ReadAttackDirection(Players, AttackDirection);
}

FName UPSCameraAll22Component::CycleView()
{
    const int32 RigCount = GetTuning().All22Rigs.Num();
    const int32 NextIndex = ActiveRigIndex + 1;
    if (NextIndex >= RigCount)
    {
        LeaveFilmView();
    }
    else
    {
        EnterRig(NextIndex);
    }
    return GetActiveRigId();
}

bool UPSCameraAll22Component::SetFilmView(FName RigId)
{
    if (RigId.IsNone())
    {
        LeaveFilmView();
        return true;
    }
    const int32 RigIndex = GetTuning().All22Rigs.IndexOfByPredicate(
        [RigId](const FPSAll22RigDef& Rig) { return Rig.RigId == RigId; });
    if (RigIndex == INDEX_NONE)
    {
        return false;
    }
    EnterRig(RigIndex);
    return true;
}

FName UPSCameraAll22Component::GetActiveRigId() const
{
    return Tuning.All22Rigs.IsValidIndex(ActiveRigIndex) ? Tuning.All22Rigs[ActiveRigIndex].RigId : NAME_None;
}

FPSCameraShot UPSCameraAll22Component::GetCurrentShot() const
{
    FPSCameraShot Shot;
    Shot.AspectRatio = ResolveAspectRatio();
    const ACameraActor* Camera = GetCamera();
    const UCameraComponent* CameraComponent = Camera ? Camera->GetCameraComponent() : nullptr;
    if (CameraComponent)
    {
        Shot.Location = CameraComponent->GetComponentLocation();
        Shot.Rotation = CameraComponent->GetComponentRotation();
        Shot.FieldOfView = CameraComponent->FieldOfView;
    }
    else if (Camera)
    {
        Shot.Location = Camera->GetActorLocation();
        Shot.Rotation = Camera->GetActorRotation();
    }
    return Shot;
}

void UPSCameraAll22Component::EnterRig(int32 RigIndex)
{
    if (!GetTuning().All22Rigs.IsValidIndex(RigIndex) || RigIndex == ActiveRigIndex)
    {
        return;
    }

    ACameraActor* Camera = GetCamera();
    UCameraComponent* CameraComponent = Camera ? Camera->GetCameraComponent() : nullptr;
    if (!IsFilmViewActive() && Camera)
    {
        ReturnTransform = Camera->GetActorTransform();
        if (CameraComponent)
        {
            ReturnFieldOfView = CameraComponent->FieldOfView;
            bReturnMotionBlurOverride = CameraComponent->PostProcessSettings.bOverride_MotionBlurAmount != 0;
            ReturnMotionBlurAmount = CameraComponent->PostProcessSettings.MotionBlurAmount;
            CameraComponent->PostProcessSettings.bOverride_MotionBlurAmount = true;
            CameraComponent->PostProcessSettings.MotionBlurAmount = 0.f;
        }
    }

    ActiveRigIndex = RigIndex;
    CutToActiveRig();
    UE_LOG(LogTemp, Display, TEXT("UPSCameraAll22Component: Film view on the %s rig."), *GetActiveRigId().ToString());
    OnFilmViewChanged.Broadcast(GetActiveRigId());
}

void UPSCameraAll22Component::LeaveFilmView()
{
    if (!IsFilmViewActive())
    {
        return;
    }
    ActiveRigIndex = INDEX_NONE;
    FramedBox = FBox(ForceInit);

    if (ACameraActor* Camera = GetCamera())
    {
        Camera->SetActorTransform(ReturnTransform);
        if (UCameraComponent* CameraComponent = Camera->GetCameraComponent())
        {
            CameraComponent->SetFieldOfView(ReturnFieldOfView);
            CameraComponent->PostProcessSettings.bOverride_MotionBlurAmount = bReturnMotionBlurOverride;
            CameraComponent->PostProcessSettings.MotionBlurAmount = ReturnMotionBlurAmount;
        }
    }
    UE_LOG(LogTemp, Display, TEXT("UPSCameraAll22Component: Back to the broadcast view."));
    OnFilmViewChanged.Broadcast(NAME_None);
}

void UPSCameraAll22Component::CutToActiveRig()
{
    if (!Tuning.All22Rigs.IsValidIndex(ActiveRigIndex))
    {
        return;
    }
    TArray<APSPlayerPawn*> Players;
    GatherPlayers(Players);
    AttackDirection = ReadAttackDirection(Players, AttackDirection);
    FramedBox = UPSCameraFraming::ComputePlayerBox(GetLocations(Players), Tuning);
    ApplyShot(UPSCameraFraming::FrameBox(Tuning.All22Rigs[ActiveRigIndex], FramedBox, AttackDirection, ResolveAspectRatio()));
}

void UPSCameraAll22Component::StepFraming(float DeltaTime)
{
    if (!Tuning.All22Rigs.IsValidIndex(ActiveRigIndex))
    {
        return;
    }

    TArray<APSPlayerPawn*> Players;
    GatherPlayers(Players);
    const FBox Target = UPSCameraFraming::ComputePlayerBox(GetLocations(Players), Tuning);
    if (!Target.IsValid || !FramedBox.IsValid)
    {
        FramedBox = Target;
    }
    else
    {
        // Each face of the framed box eases toward the players' box but never inside it: the
        // frame widens at once to take in a breakaway and closes in gently once play bunches.
        const double Alpha = Tuning.ReframeSpeed > 0.f ? FMath::Clamp<double>(DeltaTime * Tuning.ReframeSpeed, 0.0, 1.0) : 1.0;
        const FVector EasedMin = FramedBox.Min + (Target.Min - FramedBox.Min) * Alpha;
        const FVector EasedMax = FramedBox.Max + (Target.Max - FramedBox.Max) * Alpha;
        FramedBox = FBox(Target.Min.ComponentMin(EasedMin), Target.Max.ComponentMax(EasedMax));
    }
    ApplyShot(UPSCameraFraming::FrameBox(Tuning.All22Rigs[ActiveRigIndex], FramedBox, AttackDirection, ResolveAspectRatio()));
}

void UPSCameraAll22Component::ApplyShot(const FPSCameraShot& Shot)
{
    ACameraActor* Camera = GetCamera();
    if (!Camera)
    {
        return;
    }
    Camera->SetActorLocationAndRotation(Shot.Location, Shot.Rotation);
    if (UCameraComponent* CameraComponent = Camera->GetCameraComponent())
    {
        CameraComponent->SetFieldOfView(Shot.FieldOfView);
    }
}

FPSFilmFrame UPSCameraAll22Component::CaptureFrame()
{
    FPSFilmFrame Frame;
    Frame.FrameIndex = NextFrameIndex++;
    Frame.TimestampSeconds = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f;
    Frame.RigId = GetActiveRigId();
    Frame.Shot = GetCurrentShot();

    TArray<APSPlayerPawn*> Players;
    GatherPlayers(Players);
    for (const APSPlayerPawn* Pawn : Players)
    {
        const FPlayerAttributes Attributes = Pawn->GetAttributes();
        FPSFilmFramePlayer Entry;
        Entry.PlayerId = Attributes.PlayerId;
        Entry.DisplayName = Attributes.DisplayName;
        Entry.TeamSide = Pawn->TeamSide;
        Entry.WorldLocation = Pawn->GetActorLocation();
        Entry.bInFrame = UPSCameraFraming::ProjectToShot(Frame.Shot, Entry.WorldLocation, Entry.ScreenPosition);
        Frame.Players.Add(Entry);
    }

    OnFrameCaptured.Broadcast(Frame);
    return Frame;
}

bool UPSCameraAll22Component::ExportFrame(const FString& Directory, FPSFilmFrame& OutFrame)
{
    OutFrame = CaptureFrame();

    const FString TargetDirectory = Directory.IsEmpty() ? GetDefaultExportDirectory() : Directory;
    const FString BaseName = FString::Printf(TEXT("film_%s_%04d"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")), OutFrame.FrameIndex);

    // The pixels need a rendered viewport; headless runs export the frame's data alone.
    UWorld* World = GetWorld();
    if (World && World->GetGameViewport())
    {
        OutFrame.ImageFile = TargetDirectory / (BaseName + TEXT(".png"));
        FScreenshotRequest::RequestScreenshot(OutFrame.ImageFile, false, false);
    }

    FString Json;
    if (!FJsonObjectConverter::UStructToJsonObjectString(OutFrame, Json))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCameraAll22Component: Could not serialize film frame %d."), OutFrame.FrameIndex);
        return false;
    }
    IFileManager::Get().MakeDirectory(*TargetDirectory, true);
    const FString JsonPath = TargetDirectory / (BaseName + TEXT(".json"));
    if (!FFileHelper::SaveStringToFile(Json, *JsonPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCameraAll22Component: Could not write %s."), *JsonPath);
        return false;
    }
    UE_LOG(LogTemp, Display, TEXT("UPSCameraAll22Component: Exported film frame %d to %s."), OutFrame.FrameIndex, *JsonPath);
    return true;
}

ACameraActor* UPSCameraAll22Component::GetCamera() const
{
    return Cast<ACameraActor>(GetOwner());
}

void UPSCameraAll22Component::GatherPlayers(TArray<APSPlayerPawn*>& OutPlayers) const
{
    OutPlayers.Reset();
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
    {
        if (IsValid(*It))
        {
            OutPlayers.Add(*It);
        }
    }
}

TArray<FVector> UPSCameraAll22Component::GetLocations(const TArray<APSPlayerPawn*>& Players)
{
    TArray<FVector> Locations;
    Locations.Reserve(Players.Num());
    for (const APSPlayerPawn* Pawn : Players)
    {
        Locations.Add(Pawn->GetActorLocation());
    }
    return Locations;
}

float UPSCameraAll22Component::ReadAttackDirection(const TArray<APSPlayerPawn*>& Players, float Fallback)
{
    double OffenseX = 0.0;
    double DefenseX = 0.0;
    int32 OffenseCount = 0;
    int32 DefenseCount = 0;
    for (const APSPlayerPawn* Pawn : Players)
    {
        if (Pawn->TeamSide == EPSTeamSide::Offense)
        {
            OffenseX += Pawn->GetActorLocation().X;
            ++OffenseCount;
        }
        else
        {
            DefenseX += Pawn->GetActorLocation().X;
            ++DefenseCount;
        }
    }
    if (OffenseCount == 0 || DefenseCount == 0)
    {
        return Fallback;
    }
    OffenseX /= OffenseCount;
    DefenseX /= DefenseCount;
    if (FMath::IsNearlyEqual(OffenseX, DefenseX))
    {
        return Fallback;
    }
    return DefenseX > OffenseX ? 1.f : -1.f;
}

float UPSCameraAll22Component::ResolveAspectRatio() const
{
    const UWorld* World = GetWorld();
    if (UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr)
    {
        FVector2D Size;
        Viewport->GetViewportSize(Size);
        if (Size.X > 0.0 && Size.Y > 0.0)
        {
            return static_cast<float>(Size.X / Size.Y);
        }
    }
    return Tuning.AspectRatio > 0.f ? Tuning.AspectRatio : FPSAll22CameraTuning().AspectRatio;
}
