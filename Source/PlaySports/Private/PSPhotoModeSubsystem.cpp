#include "PSPhotoModeSubsystem.h"
#include "PSBroadcastCamera.h"
#include "PSDataIngestion.h"
#include "PSMenuComponent.h"
#include "PSOverlayBallFlightActor.h"
#include "PSOverlayReticle.h"
#include "PSPlayerController.h"
#include "PSReplaySubsystem.h"
#include "PSUITeamCatalog.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HighResScreenshot.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace PSPhotoModeSubsystemPrivate
{
    /** Copies the post-process fields photo mode sets, and only those, from From to To. */
    void CopyPhotoFields(const FPostProcessSettings& From, FPostProcessSettings& To)
    {
        To.bOverride_ColorSaturation = From.bOverride_ColorSaturation;
        To.ColorSaturation = From.ColorSaturation;
        To.bOverride_ColorContrast = From.bOverride_ColorContrast;
        To.ColorContrast = From.ColorContrast;
        To.bOverride_ColorGain = From.bOverride_ColorGain;
        To.ColorGain = From.ColorGain;
        To.bOverride_WhiteTemp = From.bOverride_WhiteTemp;
        To.WhiteTemp = From.WhiteTemp;
        To.bOverride_VignetteIntensity = From.bOverride_VignetteIntensity;
        To.VignetteIntensity = From.VignetteIntensity;
        To.bOverride_DepthOfFieldFstop = From.bOverride_DepthOfFieldFstop;
        To.DepthOfFieldFstop = From.DepthOfFieldFstop;
        To.bOverride_DepthOfFieldFocalDistance = From.bOverride_DepthOfFieldFocalDistance;
        To.DepthOfFieldFocalDistance = From.DepthOfFieldFocalDistance;
    }

    /** White balance with no shift. */
    constexpr float NeutralWhiteTemp = 6500.f;
}

UPSPhotoModeSubsystem::UPSPhotoModeSubsystem()
{
    PhotoContextId = TEXT("PhotoMode");
    EnterActionId = TEXT("PhotoMode");
    TurnLeftActionId = TEXT("PhotoTurnLeft");
    TurnRightActionId = TEXT("PhotoTurnRight");
    TurnUpActionId = TEXT("PhotoTurnUp");
    TurnDownActionId = TEXT("PhotoTurnDown");
    RiseActionId = TEXT("PhotoRise");
    LowerActionId = TEXT("PhotoLower");
    ZoomInActionId = TEXT("PhotoZoomIn");
    ZoomOutActionId = TEXT("PhotoZoomOut");
    RollLeftActionId = TEXT("PhotoRollLeft");
    RollRightActionId = TEXT("PhotoRollRight");
    FocusNearActionId = TEXT("PhotoFocusNear");
    FocusFarActionId = TEXT("PhotoFocusFar");
    ApertureActionId = TEXT("PhotoAperture");
    FilterActionId = TEXT("PhotoFilter");
    GuidesActionId = TEXT("PhotoGuides");
    HideUIActionId = TEXT("PhotoHideUI");
    CaptureActionId = TEXT("PhotoCapture");
    ExitActionId = TEXT("PhotoExit");
}

void UPSPhotoModeSubsystem::Deinitialize()
{
    ExitPhotoMode();
    for (const TWeakObjectPtr<APSPlayerController>& Bound : BoundControllers)
    {
        if (APSPlayerController* Controller = Bound.Get())
        {
            Controller->OnCatalogActionStarted.RemoveDynamic(this, &UPSPhotoModeSubsystem::HandleActionStarted);
            Controller->OnCatalogActionCompleted.RemoveDynamic(this, &UPSPhotoModeSubsystem::HandleActionCompleted);
        }
    }
    BoundControllers.Reset();
    Super::Deinitialize();
}

bool UPSPhotoModeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSPhotoModeSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    AdvancePhotoMode(DeltaTime);
}

TStatId UPSPhotoModeSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSPhotoModeSubsystem, STATGROUP_Tickables);
}

// --- Tuning ------------------------------------------------------------------------------

FString UPSPhotoModeSubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/photo_mode.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSPhotoModeTuning& UPSPhotoModeSubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSPhotoModeSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSPhotoModeTuning Loaded;
    if (!Ingestion->LoadPhotoModeTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPhotoModeSubsystem: Could not load photo mode tuning from %s; keeping the current tuning."), *JsonFilePath);
        return false;
    }
    return SetTuning(Loaded);
}

bool UPSPhotoModeSubsystem::SetTuning(const FPSPhotoModeTuning& NewTuning)
{
    const TArray<FString> Problems = ValidateTuning(NewTuning);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPhotoModeSubsystem: Photo mode tuning refused: %s"), *Problem);
    }
    if (Problems.Num() > 0)
    {
        return false;
    }
    Tuning = NewTuning;
    bTuningLoaded = true;
    return true;
}

TArray<FString> UPSPhotoModeSubsystem::ValidateTuning(const FPSPhotoModeTuning& InTuning)
{
    TArray<FString> Problems;
    struct FPositiveField
    {
        const TCHAR* Name;
        float Value;
    };
    const FPositiveField Positives[] = {
        { TEXT("MoveCmPerSecond"), InTuning.MoveCmPerSecond },
        { TEXT("RiseCmPerSecond"), InTuning.RiseCmPerSecond },
        { TEXT("TurnDegreesPerSecond"), InTuning.TurnDegreesPerSecond },
        { TEXT("MaxPitchDegrees"), InTuning.MaxPitchDegrees },
        { TEXT("MaxDistanceCm"), InTuning.MaxDistanceCm },
        { TEXT("ZoomDegreesPerSecond"), InTuning.ZoomDegreesPerSecond },
        { TEXT("MaxRollDegrees"), InTuning.MaxRollDegrees },
        { TEXT("RollDegreesPerSecond"), InTuning.RollDegreesPerSecond },
        { TEXT("FocusDoublingsPerSecond"), InTuning.FocusDoublingsPerSecond },
    };
    for (const FPositiveField& Positive : Positives)
    {
        if (!(Positive.Value > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("%s must be above 0"), Positive.Name));
        }
    }
    if (InTuning.TurnStepDegrees < 0.f)
    {
        Problems.Add(TEXT("TurnStepDegrees must be 0 or more"));
    }
    if (InTuning.MaxPitchDegrees >= 90.f)
    {
        Problems.Add(TEXT("MaxPitchDegrees must be below 90"));
    }
    if (!(InTuning.MinFieldOfView > 0.f) || InTuning.MaxFieldOfView <= InTuning.MinFieldOfView || InTuning.MaxFieldOfView >= 180.f)
    {
        Problems.Add(TEXT("the field of view needs 0 < MinFieldOfView < MaxFieldOfView < 180"));
    }
    if (!(InTuning.MinFocusCm > 0.f) || InTuning.MaxFocusCm <= InTuning.MinFocusCm
        || InTuning.DefaultFocusCm < InTuning.MinFocusCm || InTuning.DefaultFocusCm > InTuning.MaxFocusCm)
    {
        Problems.Add(TEXT("the focus needs 0 < MinFocusCm < MaxFocusCm, with DefaultFocusCm between them"));
    }

    if (InTuning.Apertures.Num() == 0)
    {
        Problems.Add(TEXT("Apertures is empty"));
    }
    for (int32 Index = 0; Index < InTuning.Apertures.Num(); ++Index)
    {
        const float FStop = InTuning.Apertures[Index];
        if (FStop < 0.f || (Index > 0 && FStop <= InTuning.Apertures[Index - 1]))
        {
            Problems.Add(FString::Printf(TEXT("Apertures[%d] (%.2f) must be 0 or more and above the one before"), Index, FStop));
        }
    }

    TSet<FName> FilterIds;
    for (const FPSPhotoFilter& Filter : InTuning.Filters)
    {
        const FString Name = Filter.FilterId.ToString();
        if (Filter.FilterId.IsNone() || FilterIds.Contains(Filter.FilterId))
        {
            Problems.Add(FString::Printf(TEXT("filter '%s' has no id or is listed twice"), *Name));
        }
        FilterIds.Add(Filter.FilterId);
        FLinearColor Tint;
        if (!UPSUITeamCatalog::ParseHexColor(Filter.Tint, Tint))
        {
            Problems.Add(FString::Printf(TEXT("filter '%s': Tint '%s' is not #RRGGBB"), *Name, *Filter.Tint));
        }
        if (Filter.Saturation < 0.f || Filter.Contrast < 0.f)
        {
            Problems.Add(FString::Printf(TEXT("filter '%s': Saturation and Contrast must be 0 or more"), *Name));
        }
        if (!(Filter.WhiteTemp > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("filter '%s': WhiteTemp must be above 0"), *Name));
        }
        if (Filter.Vignette < 0.f || Filter.Vignette > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("filter '%s': Vignette must be 0 to 1"), *Name));
        }
    }

    if (InTuning.Presets.Num() == 0)
    {
        Problems.Add(TEXT("Presets is empty"));
    }
    TSet<FName> PresetIds;
    for (const FPSPhotoPreset& Preset : InTuning.Presets)
    {
        const FString Name = Preset.PresetId.ToString();
        if (Preset.PresetId.IsNone() || PresetIds.Contains(Preset.PresetId))
        {
            Problems.Add(FString::Printf(TEXT("preset '%s' has no id or is listed twice"), *Name));
        }
        PresetIds.Add(Preset.PresetId);
        for (const FName& FilterId : Preset.Filters)
        {
            if (!FilterIds.Contains(FilterId))
            {
                Problems.Add(FString::Printf(TEXT("preset '%s' names filter '%s', which isn't in Filters"), *Name, *FilterId.ToString()));
            }
        }
    }

    if (InTuning.CaptureResolutionMultiplier < 1.f)
    {
        Problems.Add(TEXT("CaptureResolutionMultiplier must be 1 or more"));
    }
    if (InTuning.MaxCaptureDimension < 1)
    {
        Problems.Add(TEXT("MaxCaptureDimension must be 1 or more"));
    }
    return Problems;
}

// --- Entering and leaving ----------------------------------------------------------------

APSBroadcastCamera* UPSPhotoModeSubsystem::FindBroadcastCamera() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }
    TActorIterator<APSBroadcastCamera> It(World);
    return It ? *It : nullptr;
}

UPSReplaySubsystem* UPSPhotoModeSubsystem::GetReplay() const
{
    UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSReplaySubsystem>() : nullptr;
}

bool UPSPhotoModeSubsystem::IsMenuOpen() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }
    for (TActorIterator<APSPlayerController> It(World); It; ++It)
    {
        const UPSMenuComponent* Menu = It->GetMenuComponent();
        if (Menu && Menu->IsMenuOpen())
        {
            return true;
        }
    }
    return false;
}

void UPSPhotoModeSubsystem::BindController(APSPlayerController* Controller)
{
    if (!Controller)
    {
        return;
    }
    Controller->OnCatalogActionStarted.AddUniqueDynamic(this, &UPSPhotoModeSubsystem::HandleActionStarted);
    Controller->OnCatalogActionCompleted.AddUniqueDynamic(this, &UPSPhotoModeSubsystem::HandleActionCompleted);
    BoundControllers.AddUnique(Controller);
}

void UPSPhotoModeSubsystem::UnbindController(APSPlayerController* Controller)
{
    if (!Controller)
    {
        return;
    }
    Controller->OnCatalogActionStarted.RemoveDynamic(this, &UPSPhotoModeSubsystem::HandleActionStarted);
    Controller->OnCatalogActionCompleted.RemoveDynamic(this, &UPSPhotoModeSubsystem::HandleActionCompleted);
    BoundControllers.Remove(Controller);
}

bool UPSPhotoModeSubsystem::EnterPhotoMode()
{
    UWorld* World = GetWorld();
    APSBroadcastCamera* Camera = FindBroadcastCamera();
    if (bActive || !World || !Camera || IsMenuOpen())
    {
        return false;
    }
    const FPSPhotoModeTuning& PhotoTuning = GetTuning();

    // The camera as it is, to give back.
    HeldCamera = Camera;
    SavedTransform = Camera->GetActorTransform();
    bSavedFreeCam = Camera->bIsFreeCam;
    bSavedFollowing = Camera->bIsFollowing;
    if (const UCameraComponent* View = Camera->GetCameraComponent())
    {
        SavedFieldOfView = View->FieldOfView;
        PSPhotoModeSubsystemPrivate::CopyPhotoFields(View->PostProcessSettings, SavedPostProcess);
    }
    Camera->bIsFreeCam = true;

    // The game and any replay hold still.
    UPSReplaySubsystem* Replay = GetReplay();
    bHeldReplay = Replay != nullptr;
    bResumeReplay = Replay && Replay->GetState() == EPSReplayState::Playing;
    if (Replay)
    {
        if (bResumeReplay)
        {
            Replay->SetPaused(true);
        }
        Replay->SetHeld(true);
        Replay->OnReplayEnded.AddUniqueDynamic(this, &UPSPhotoModeSubsystem::HandleReplayEnded);
    }
    bPausedGame = !World->IsPaused() && UGameplayStatics::SetGamePaused(World, true);

    // Photo mode's buttons over everything; the replay's off while it is on.
    ReplayContextControllers.Reset();
    for (TActorIterator<APSPlayerController> It(World); It; ++It)
    {
        APSPlayerController* Controller = *It;
        BindController(Controller);
        if (Replay && Controller->IsInputContextActive(Replay->ReplayContextId))
        {
            Controller->SetModeContextActive(Replay->ReplayContextId, false);
            ReplayContextControllers.Add(Controller);
        }
        Controller->SetModeContextActive(PhotoContextId, true);
    }
    HeldActions.Reset();

    // The free camera starts on the view as it was.
    const FRotator Rotation = Camera->GetActorRotation();
    Anchor = Camera->GetActorLocation();
    CameraLocation = Anchor;
    CameraPitch = FMath::Clamp(static_cast<float>(Rotation.Pitch), -PhotoTuning.MaxPitchDegrees, PhotoTuning.MaxPitchDegrees);
    CameraYaw = static_cast<float>(Rotation.Yaw);
    CameraRoll = 0.f;
    FieldOfView = FMath::Clamp(SavedFieldOfView, PhotoTuning.MinFieldOfView, PhotoTuning.MaxFieldOfView);
    const AActor* Subject = Camera->TargetActor;
    FocusDistance = FMath::Clamp(Subject ? static_cast<float>(FVector::Dist(Anchor, Subject->GetActorLocation())) : PhotoTuning.DefaultFocusCm,
        PhotoTuning.MinFocusCm, PhotoTuning.MaxFocusCm);
    ApertureIndex = 0;
    PresetIndex = 0;
    PresetId = PhotoTuning.Presets.Num() > 0 ? PhotoTuning.Presets[0].PresetId : NAME_None;
    FilterStack = PhotoTuning.Presets.Num() > 0 ? PhotoTuning.Presets[0].Filters : TArray<FName>();
    FilterStrength = 1.f;
    Guide = EPSPhotoGuide::None;
    bUIHidden = false;

    bActive = true;
    ApplyCamera();
    OnPhotoModeEntered.Broadcast();
    return true;
}

void UPSPhotoModeSubsystem::ExitPhotoMode()
{
    Leave(true);
}

void UPSPhotoModeSubsystem::HandleReplayEnded()
{
    // The replay gave the camera back as it was before the replay; photo mode leaves it there.
    if (bActive)
    {
        Leave(false);
    }
}

void UPSPhotoModeSubsystem::Leave(bool bRestoreCamera)
{
    if (!bActive)
    {
        return;
    }
    SetUIHidden(false);
    bActive = false;
    HeldActions.Reset();

    if (APSBroadcastCamera* Camera = HeldCamera.Get())
    {
        UCameraComponent* View = Camera->GetCameraComponent();
        if (View)
        {
            PSPhotoModeSubsystemPrivate::CopyPhotoFields(SavedPostProcess, View->PostProcessSettings);
        }
        if (bRestoreCamera)
        {
            Camera->SetActorTransform(SavedTransform);
            if (View)
            {
                View->SetFieldOfView(SavedFieldOfView);
            }
            Camera->bIsFreeCam = bSavedFreeCam;
            Camera->bIsFollowing = bSavedFollowing;
        }
    }
    HeldCamera.Reset();

    UPSReplaySubsystem* Replay = GetReplay();
    for (const TWeakObjectPtr<APSPlayerController>& Bound : BoundControllers)
    {
        if (APSPlayerController* Controller = Bound.Get())
        {
            Controller->SetModeContextActive(PhotoContextId, false);
        }
    }
    for (const TWeakObjectPtr<APSPlayerController>& Took : ReplayContextControllers)
    {
        APSPlayerController* Controller = Took.Get();
        if (Controller && Replay && Replay->IsReplaying())
        {
            Controller->SetModeContextActive(Replay->ReplayContextId, true);
        }
    }
    ReplayContextControllers.Reset();

    if (Replay && bHeldReplay)
    {
        Replay->OnReplayEnded.RemoveDynamic(this, &UPSPhotoModeSubsystem::HandleReplayEnded);
        Replay->SetHeld(false);
        if (bResumeReplay && Replay->IsReplaying())
        {
            Replay->SetPaused(false);
        }
    }
    bHeldReplay = false;
    bResumeReplay = false;

    // A pause menu still open keeps the game paused; its Resume unpauses it.
    UWorld* World = GetWorld();
    if (bPausedGame && World && !IsMenuOpen() && !(Replay && Replay->IsReplaying()))
    {
        UGameplayStatics::SetGamePaused(World, false);
    }
    bPausedGame = false;
    OnPhotoModeExited.Broadcast();
}

// --- The camera --------------------------------------------------------------------------

float UPSPhotoModeSubsystem::HeldAxis(FName Negative, FName Positive) const
{
    return (HeldActions.Contains(Positive) ? 1.f : 0.f) - (HeldActions.Contains(Negative) ? 1.f : 0.f);
}

void UPSPhotoModeSubsystem::Turn(float YawDegrees, float PitchDegrees)
{
    const float MaxPitch = GetTuning().MaxPitchDegrees;
    CameraYaw = static_cast<float>(FRotator::NormalizeAxis(static_cast<double>(CameraYaw + YawDegrees)));
    CameraPitch = FMath::Clamp(CameraPitch + PitchDegrees, -MaxPitch, MaxPitch);
}

void UPSPhotoModeSubsystem::AdvancePhotoMode(float DeltaSeconds)
{
    if (!bActive)
    {
        return;
    }
    UWorld* World = GetWorld();
    // A pause menu over photo mode holds it.
    if (IsMenuOpen())
    {
        return;
    }
    // The game stays paused under photo mode, whoever unpaused it.
    if (World && !World->IsPaused() && UGameplayStatics::SetGamePaused(World, true))
    {
        bPausedGame = true;
    }

    const float Step = FMath::Max(0.f, DeltaSeconds);
    const FPSPhotoModeTuning& PhotoTuning = GetTuning();

    // Turning, rising, zooming, rolling and focusing while their buttons are held.
    Turn(HeldAxis(TurnLeftActionId, TurnRightActionId) * PhotoTuning.TurnDegreesPerSecond * Step,
        HeldAxis(TurnDownActionId, TurnUpActionId) * PhotoTuning.TurnDegreesPerSecond * Step);
    SetFieldOfView(FieldOfView + HeldAxis(ZoomInActionId, ZoomOutActionId) * PhotoTuning.ZoomDegreesPerSecond * Step);
    SetRoll(CameraRoll + HeldAxis(RollLeftActionId, RollRightActionId) * PhotoTuning.RollDegreesPerSecond * Step);
    const float FocusAxis = HeldAxis(FocusNearActionId, FocusFarActionId);
    if (FocusAxis != 0.f)
    {
        SetFocusDistance(FocusDistance * FMath::Pow(2.f, FocusAxis * PhotoTuning.FocusDoublingsPerSecond * Step));
    }

    // The stick flies it level: forward along its heading, sideways across it.
    FVector2D Move = FVector2D::ZeroVector;
    for (const TWeakObjectPtr<APSPlayerController>& Bound : BoundControllers)
    {
        const APSPlayerController* Controller = Bound.Get();
        if (Controller && !Controller->GetMoveInput().IsNearlyZero())
        {
            Move = Controller->GetMoveInput();
            break;
        }
    }
    const FVector Forward = FRotator(0.f, CameraYaw, 0.f).Vector();
    const FVector Right = FRotator(0.f, CameraYaw + 90.f, 0.f).Vector();
    FVector Location = CameraLocation
        + (Forward * Move.Y + Right * Move.X) * (PhotoTuning.MoveCmPerSecond * Step)
        + FVector::UpVector * (HeldAxis(LowerActionId, RiseActionId) * PhotoTuning.RiseCmPerSecond * Step);

    // Within its leash of where photo mode began, and above the turf.
    Location = Anchor + (Location - Anchor).GetClampedToMaxSize(PhotoTuning.MaxDistanceCm);
    Location.Z = FMath::Max(Location.Z, static_cast<double>(PhotoTuning.MinHeightCm));
    CameraLocation = Location;
    ApplyCamera();
}

void UPSPhotoModeSubsystem::SetFieldOfView(float Degrees)
{
    const FPSPhotoModeTuning& PhotoTuning = GetTuning();
    FieldOfView = FMath::Clamp(Degrees, PhotoTuning.MinFieldOfView, PhotoTuning.MaxFieldOfView);
    ApplyCamera();
}

void UPSPhotoModeSubsystem::SetRoll(float Degrees)
{
    const float MaxRoll = GetTuning().MaxRollDegrees;
    CameraRoll = FMath::Clamp(Degrees, -MaxRoll, MaxRoll);
    ApplyCamera();
}

void UPSPhotoModeSubsystem::SetFocusDistance(float Centimetres)
{
    const FPSPhotoModeTuning& PhotoTuning = GetTuning();
    FocusDistance = FMath::Clamp(Centimetres, PhotoTuning.MinFocusCm, PhotoTuning.MaxFocusCm);
    ApplyCamera();
}

float UPSPhotoModeSubsystem::GetAperture() const
{
    return Tuning.Apertures.IsValidIndex(ApertureIndex) ? Tuning.Apertures[ApertureIndex] : 0.f;
}

float UPSPhotoModeSubsystem::CycleAperture()
{
    const int32 Count = GetTuning().Apertures.Num();
    ApertureIndex = Count > 0 ? (ApertureIndex + 1) % Count : 0;
    ApplyCamera();
    return GetAperture();
}

void UPSPhotoModeSubsystem::ApplyCamera()
{
    APSBroadcastCamera* Camera = HeldCamera.Get();
    if (!bActive || !Camera)
    {
        return;
    }
    Camera->SetActorLocationAndRotation(CameraLocation, GetCameraRotation());
    UCameraComponent* View = Camera->GetCameraComponent();
    if (!View)
    {
        return;
    }
    View->SetFieldOfView(FieldOfView);

    // The camera's own settings, with the look and the depth of field over them.
    FPostProcessSettings& Settings = View->PostProcessSettings;
    PSPhotoModeSubsystemPrivate::CopyPhotoFields(SavedPostProcess, Settings);
    const FPSPhotoLook Look = GetLook();
    if (Look.bColorGrade)
    {
        const double Saturation = Look.Saturation;
        const double Contrast = Look.Contrast;
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.0);
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = FVector4(Contrast, Contrast, Contrast, 1.0);
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = FVector4(static_cast<double>(Look.Tint.R), static_cast<double>(Look.Tint.G), static_cast<double>(Look.Tint.B), 1.0);
    }
    if (Look.bWhiteTemp)
    {
        Settings.bOverride_WhiteTemp = true;
        Settings.WhiteTemp = Look.WhiteTemp;
    }
    if (Look.bVignette)
    {
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = Look.Vignette;
    }
    const float FStop = GetAperture();
    if (FStop > 0.f)
    {
        Settings.bOverride_DepthOfFieldFstop = true;
        Settings.DepthOfFieldFstop = FStop;
        Settings.bOverride_DepthOfFieldFocalDistance = true;
        Settings.DepthOfFieldFocalDistance = FocusDistance;
    }
}

// --- Filters -----------------------------------------------------------------------------

bool UPSPhotoModeSubsystem::SetPreset(FName PresetName)
{
    const TArray<FPSPhotoPreset>& Presets = GetTuning().Presets;
    const int32 Index = Presets.IndexOfByPredicate([PresetName](const FPSPhotoPreset& Preset) { return Preset.PresetId == PresetName; });
    if (Index == INDEX_NONE)
    {
        return false;
    }
    PresetIndex = Index;
    PresetId = PresetName;
    FilterStack = Presets[Index].Filters;
    ApplyCamera();
    return true;
}

FName UPSPhotoModeSubsystem::CyclePreset()
{
    const TArray<FPSPhotoPreset>& Presets = GetTuning().Presets;
    if (Presets.Num() > 0)
    {
        SetPreset(Presets[(PresetIndex + 1) % Presets.Num()].PresetId);
    }
    return PresetId;
}

bool UPSPhotoModeSubsystem::PushFilter(FName FilterId)
{
    if (!GetTuning().Filters.ContainsByPredicate([FilterId](const FPSPhotoFilter& Filter) { return Filter.FilterId == FilterId; }))
    {
        return false;
    }
    FilterStack.Add(FilterId);
    PresetId = NAME_None;
    ApplyCamera();
    return true;
}

bool UPSPhotoModeSubsystem::PopFilter()
{
    if (FilterStack.Num() == 0)
    {
        return false;
    }
    FilterStack.Pop();
    PresetId = NAME_None;
    ApplyCamera();
    return true;
}

void UPSPhotoModeSubsystem::SetFilterStrength(float Strength)
{
    FilterStrength = FMath::Clamp(Strength, 0.f, 1.f);
    ApplyCamera();
}

FPSPhotoLook UPSPhotoModeSubsystem::GetLook() const
{
    return ComposeLook(Tuning, FilterStack, FilterStrength);
}

FPSPhotoLook UPSPhotoModeSubsystem::ComposeLook(const FPSPhotoModeTuning& InTuning, const TArray<FName>& Stack, float Strength)
{
    FPSPhotoLook Look;
    const float Amount = FMath::Clamp(Strength, 0.f, 1.f);
    for (const FName& FilterId : Stack)
    {
        const FPSPhotoFilter* Filter = InTuning.Filters.FindByPredicate([FilterId](const FPSPhotoFilter& Candidate) { return Candidate.FilterId == FilterId; });
        if (!Filter)
        {
            continue;
        }
        FLinearColor Tint = FLinearColor::White;
        UPSUITeamCatalog::ParseHexColor(Filter->Tint, Tint);
        if (Filter->Saturation != 1.f || Filter->Contrast != 1.f || !Tint.Equals(FLinearColor::White))
        {
            Look.bColorGrade = true;
            Look.Saturation *= FMath::Lerp(1.f, Filter->Saturation, Amount);
            Look.Contrast *= FMath::Lerp(1.f, Filter->Contrast, Amount);
            Look.Tint = FLinearColor(Look.Tint.R * FMath::Lerp(1.f, Tint.R, Amount), Look.Tint.G * FMath::Lerp(1.f, Tint.G, Amount),
                Look.Tint.B * FMath::Lerp(1.f, Tint.B, Amount), 1.f);
        }
        if (Filter->WhiteTemp != PSPhotoModeSubsystemPrivate::NeutralWhiteTemp)
        {
            Look.bWhiteTemp = true;
            Look.WhiteTemp += (Filter->WhiteTemp - PSPhotoModeSubsystemPrivate::NeutralWhiteTemp) * Amount;
        }
        if (Filter->Vignette > 0.f)
        {
            Look.bVignette = true;
            Look.Vignette = FMath::Max(Look.Vignette, Filter->Vignette * Amount);
        }
    }
    return Look;
}

// --- Framing and the UI ------------------------------------------------------------------

EPSPhotoGuide UPSPhotoModeSubsystem::CycleGuide()
{
    switch (Guide)
    {
    case EPSPhotoGuide::None:
        Guide = EPSPhotoGuide::Thirds;
        break;
    case EPSPhotoGuide::Thirds:
        Guide = EPSPhotoGuide::Center;
        break;
    default:
        Guide = EPSPhotoGuide::None;
        break;
    }
    return Guide;
}

TArray<FPSPhotoGuideLine> UPSPhotoModeSubsystem::GetGuideLines(EPSPhotoGuide InGuide)
{
    TArray<double> Splits;
    if (InGuide == EPSPhotoGuide::Thirds)
    {
        Splits = { 1.0 / 3.0, 2.0 / 3.0 };
    }
    else if (InGuide == EPSPhotoGuide::Center)
    {
        Splits = { 0.5 };
    }
    TArray<FPSPhotoGuideLine> Lines;
    for (const double Split : Splits)
    {
        FPSPhotoGuideLine& Vertical = Lines.AddDefaulted_GetRef();
        Vertical.Start = FVector2D(Split, 0.0);
        Vertical.End = FVector2D(Split, 1.0);
        FPSPhotoGuideLine& Horizontal = Lines.AddDefaulted_GetRef();
        Horizontal.Start = FVector2D(0.0, Split);
        Horizontal.End = FVector2D(1.0, Split);
    }
    return Lines;
}

void UPSPhotoModeSubsystem::SetUIHidden(bool bHidden)
{
    if (bHidden == bUIHidden || (bHidden && !bActive))
    {
        return;
    }
    bUIHidden = bHidden;
    UWorld* World = GetWorld();

    if (!bHidden)
    {
        for (const TPair<TWeakObjectPtr<UUserWidget>, ESlateVisibility>& Hidden : HiddenWidgets)
        {
            if (UUserWidget* Widget = Hidden.Key.Get())
            {
                Widget->SetVisibility(Hidden.Value);
            }
        }
        for (const TWeakObjectPtr<AActor>& Hidden : HiddenActors)
        {
            if (AActor* Actor = Hidden.Get())
            {
                Actor->SetActorHiddenInGame(false);
            }
        }
        for (const TWeakObjectPtr<AActor>& Hidden : HiddenHUDs)
        {
            if (AHUD* HUD = Cast<AHUD>(Hidden.Get()))
            {
                HUD->bShowHUD = true;
            }
        }
        HiddenWidgets.Reset();
        HiddenActors.Reset();
        HiddenHUDs.Reset();
        return;
    }
    if (!World)
    {
        return;
    }

    // Every widget on the viewport (the score bug, the chyron, the badges, captions).
    TArray<UUserWidget*> Widgets;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UUserWidget::StaticClass(), true);
    for (UUserWidget* Widget : Widgets)
    {
        const ESlateVisibility Visibility = Widget ? Widget->GetVisibility() : ESlateVisibility::Collapsed;
        if (Widget && Visibility != ESlateVisibility::Collapsed && Visibility != ESlateVisibility::Hidden)
        {
            HiddenWidgets.Emplace(Widget, Visibility);
            Widget->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    // The HUD's own drawing.
    for (TActorIterator<AHUD> It(World); It; ++It)
    {
        if (It->bShowHUD)
        {
            HiddenHUDs.Add(*It);
            It->bShowHUD = false;
        }
    }
    // The on-field overlays: the control reticle (Epic 28) and the ball-flight arc (Epic 30).
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if ((Actor->IsA<APSOverlayReticle>() || Actor->IsA<APSOverlayBallFlight>()) && !Actor->IsHidden())
        {
            HiddenActors.Add(Actor);
            Actor->SetActorHiddenInGame(true);
        }
    }
}

// --- Capture -----------------------------------------------------------------------------

FString UPSPhotoModeSubsystem::GetDefaultCaptureDirectory()
{
    return FPaths::ScreenShotDir() / TEXT("Photo");
}

FIntPoint UPSPhotoModeSubsystem::GetCaptureSize(const FIntPoint& ViewportSize, const FPSPhotoModeTuning& InTuning)
{
    const int32 Longer = FMath::Max(ViewportSize.X, ViewportSize.Y);
    if (Longer <= 0)
    {
        return FIntPoint::ZeroValue;
    }
    double Scale = FMath::Max(1.0, static_cast<double>(InTuning.CaptureResolutionMultiplier));
    Scale = FMath::Min(Scale, static_cast<double>(InTuning.MaxCaptureDimension) / Longer);
    Scale = FMath::Max(Scale, 1.0);
    return FIntPoint(FMath::RoundToInt(ViewportSize.X * Scale), FMath::RoundToInt(ViewportSize.Y * Scale));
}

bool UPSPhotoModeSubsystem::Capture(const FString& Directory, FPSPhotoCapture& OutCapture)
{
    OutCapture = FPSPhotoCapture();
    if (!bActive)
    {
        return false;
    }
    const FString TargetDirectory = Directory.IsEmpty() ? GetDefaultCaptureDirectory() : Directory;
    OutCapture.FilePath = TargetDirectory / FString::Printf(TEXT("photo_%s_%03d.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")), CaptureCount++);

    UWorld* World = GetWorld();
    UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr;
    FViewport* Viewport = ViewportClient ? ViewportClient->Viewport : nullptr;
    if (!Viewport)
    {
        return false;
    }
    const FIntPoint Size = GetCaptureSize(Viewport->GetSizeXY(), GetTuning());
    OutCapture.Width = Size.X;
    OutCapture.Height = Size.Y;
    if (Size.X <= 0 || Size.Y <= 0)
    {
        return false;
    }

    IFileManager::Get().MakeDirectory(*TargetDirectory, true);
    FHighResScreenshotConfig& ShotConfig = GetHighResScreenshotConfig();
    ShotConfig.SetResolution(static_cast<uint32>(Size.X), static_cast<uint32>(Size.Y));
    ShotConfig.SetFilename(OutCapture.FilePath);
    OutCapture.bRequested = Viewport->TakeHighResScreenShot();
    if (OutCapture.bRequested)
    {
        OnPhotoCaptured.Broadcast(OutCapture);
    }
    return OutCapture.bRequested;
}

// --- Controls ----------------------------------------------------------------------------

void UPSPhotoModeSubsystem::HandleActionStarted(FName ActionId)
{
    if (!bActive)
    {
        if (ActionId == EnterActionId)
        {
            EnterPhotoMode();
        }
        return;
    }

    const FPSPhotoModeTuning& PhotoTuning = GetTuning();
    if (ActionId == ExitActionId)
    {
        ExitPhotoMode();
    }
    else if (ActionId == TurnLeftActionId || ActionId == TurnRightActionId || ActionId == TurnUpActionId || ActionId == TurnDownActionId)
    {
        // A step at once (one swipe on touch), then steadily while held.
        HeldActions.Add(ActionId);
        const float Yaw = ActionId == TurnLeftActionId ? -1.f : (ActionId == TurnRightActionId ? 1.f : 0.f);
        const float Pitch = ActionId == TurnDownActionId ? -1.f : (ActionId == TurnUpActionId ? 1.f : 0.f);
        Turn(Yaw * PhotoTuning.TurnStepDegrees, Pitch * PhotoTuning.TurnStepDegrees);
        ApplyCamera();
    }
    else if (ActionId == RiseActionId || ActionId == LowerActionId || ActionId == ZoomInActionId || ActionId == ZoomOutActionId
        || ActionId == RollLeftActionId || ActionId == RollRightActionId || ActionId == FocusNearActionId || ActionId == FocusFarActionId)
    {
        HeldActions.Add(ActionId);
    }
    else if (ActionId == ApertureActionId)
    {
        CycleAperture();
    }
    else if (ActionId == FilterActionId)
    {
        CyclePreset();
    }
    else if (ActionId == GuidesActionId)
    {
        CycleGuide();
    }
    else if (ActionId == HideUIActionId)
    {
        ToggleUIHidden();
    }
    else if (ActionId == CaptureActionId)
    {
        FPSPhotoCapture Photo;
        Capture(FString(), Photo);
    }
}

void UPSPhotoModeSubsystem::HandleActionCompleted(FName ActionId)
{
    HeldActions.Remove(ActionId);
}
