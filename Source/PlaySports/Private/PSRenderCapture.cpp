#include "PSRenderCapture.h"
#include "PSAIFieldSnapshot.h"
#include "PSBall.h"
#include "PSDataIngestion.h"
#include "PSFieldDimensions.h"
#include "PSPlayerPawn.h"
#include "PSPlaySimulation.h"
#include "PSTelemetryBus.h"
#include "Algo/AllOf.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "ContentStreaming.h"
#include "CoreGlobals.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

namespace PSRenderCapturePrivate
{
    const TCHAR* const SwitchName = TEXT("PSRenderCapture");
    const TCHAR* const OutSwitch = TEXT("PSRenderCaptureOut=");
    const TCHAR* const ViewsSwitch = TEXT("PSRenderCaptureViews=");
    const TCHAR* const OnlySwitch = TEXT("PSRenderCaptureOnly=");
    const TCHAR* const CommitSwitch = TEXT("PSRenderCaptureCommit=");

    /** Seconds between progress lines while the capture waits. */
    const double LogIntervalSeconds = 10.0;

    /** A Field view's frame: yard line X, Y yards from the middle, Z yards up. */
    FVector FieldYardsToWorld(const FVector& Yards)
    {
        FVector Point = PSField::YardLineToWorld(static_cast<float>(Yards.X), static_cast<float>(Yards.Y));
        Point.Z += PSField::YardsToCentimetres(static_cast<float>(Yards.Z));
        return Point;
    }

    /** A Player view's frame: yards from the ground point under the player, on the field's axes. */
    FVector PlayerYardsToWorld(const FVector& AnchorGround, const FVector& Yards)
    {
        return AnchorGround + FVector(
            PSField::YardsToCentimetres(static_cast<float>(Yards.X)),
            PSField::YardsToCentimetres(static_cast<float>(Yards.Y)),
            PSField::YardsToCentimetres(static_cast<float>(Yards.Z)));
    }

    TArray<TSharedPtr<FJsonValue>> ToJsonStrings(const TArray<FString>& Lines)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        for (const FString& Line : Lines)
        {
            Values.Add(MakeShared<FJsonValueString>(Line));
        }
        return Values;
    }

    TArray<TSharedPtr<FJsonValue>> ToJsonNumbers(double A, double B, double C)
    {
        return { MakeShared<FJsonValueNumber>(A), MakeShared<FJsonValueNumber>(B), MakeShared<FJsonValueNumber>(C) };
    }

    /** Shaders and assets the editor binary is still compiling (always 0 in a cooked game). */
    int32 GetPendingCompilation()
    {
        int32 Pending = 0;
#if WITH_EDITOR
        if (GShaderCompilingManager)
        {
            Pending += GShaderCompilingManager->GetNumRemainingJobs();
        }
        Pending += FAssetCompilingManager::Get().GetNumRemainingAssets();
#endif
        return Pending;
    }

    /** The GPU's time for the last frame it finished, in ms (0 when the RHI doesn't report one). */
    float GetGpuFrameMs()
    {
        return static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
    }

    float MeanOf(const TArray<float>& Samples)
    {
        if (Samples.Num() == 0)
        {
            return 0.f;
        }
        double Sum = 0.0;
        for (const float Sample : Samples)
        {
            Sum += Sample;
        }
        return static_cast<float>(Sum / Samples.Num());
    }

    float LargestOf(const TArray<float>& Samples)
    {
        float Largest = 0.f;
        for (const float Sample : Samples)
        {
            Largest = FMath::Max(Largest, Sample);
        }
        return Largest;
    }
}

FString UPSRenderCapture::GetDefaultViewsPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/render_views.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

FString UPSRenderCapture::GetDefaultOutputDir()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Renders"));
}

bool UPSRenderCapture::ParseCommandLine(const TCHAR* CommandLine, FPSRenderCaptureOptions& OutOptions)
{
    using namespace PSRenderCapturePrivate;

    OutOptions = FPSRenderCaptureOptions();
    OutOptions.OutputDir = GetDefaultOutputDir();
    OutOptions.ViewsPath = GetDefaultViewsPath();
    if (!CommandLine || !FParse::Param(CommandLine, SwitchName))
    {
        return false;
    }

    FString Value;
    if (FParse::Value(CommandLine, OutSwitch, Value) && !Value.IsEmpty())
    {
        OutOptions.OutputDir = FPaths::ConvertRelativePathToFull(Value);
    }
    if (FParse::Value(CommandLine, ViewsSwitch, Value) && !Value.IsEmpty())
    {
        OutOptions.ViewsPath = FPaths::ConvertRelativePathToFull(Value);
    }
    if (FParse::Value(CommandLine, OnlySwitch, Value) && !Value.IsEmpty())
    {
        TArray<FString> Names;
        Value.ParseIntoArray(Names, TEXT("+"), true);
        for (const FString& Name : Names)
        {
            OutOptions.OnlyViews.AddUnique(FName(*Name.TrimStartAndEnd()));
        }
    }
    FParse::Value(CommandLine, CommitSwitch, OutOptions.Commit);
    return true;
}

TArray<FString> UPSRenderCapture::ValidateSettings(const FPSRenderCaptureSettings& Candidate)
{
    TArray<FString> Problems;
    if (Candidate.ResolutionX < 64 || Candidate.ResolutionY < 64)
    {
        Problems.Add(TEXT("ResolutionX and ResolutionY must be 64 or more"));
    }
    if (Candidate.ExpectedPlayers < 0)
    {
        Problems.Add(TEXT("ExpectedPlayers must be 0 or more"));
    }
    if (Candidate.WarmupFrames < 0 || Candidate.SettleFrames < 1 || Candidate.ScreenshotTimeoutFrames < 1)
    {
        Problems.Add(TEXT("WarmupFrames must be 0 or more; SettleFrames and ScreenshotTimeoutFrames 1 or more"));
    }
    if (Candidate.MeasureFrames < 1 || Candidate.MeasureFrames > Candidate.SettleFrames)
    {
        Problems.Add(TEXT("MeasureFrames must be from 1 to SettleFrames"));
    }
    if (Candidate.WarmupSeconds < 0.f || Candidate.SettleSeconds < 0.f || Candidate.StreamingWaitSeconds < 0.f)
    {
        Problems.Add(TEXT("WarmupSeconds, SettleSeconds and StreamingWaitSeconds must be 0 or more"));
    }
    if (Candidate.MatchTimeoutSeconds <= 0.f || Candidate.CompileTimeoutSeconds <= 0.f || Candidate.TotalTimeoutSeconds <= 0.f)
    {
        Problems.Add(TEXT("MatchTimeoutSeconds, CompileTimeoutSeconds and TotalTimeoutSeconds must be above 0"));
    }
    if (Candidate.RenderViews.Num() == 0)
    {
        Problems.Add(TEXT("RenderViews must name at least one view"));
    }

    TSet<FName> Seen;
    for (int32 Index = 0; Index < Candidate.RenderViews.Num(); ++Index)
    {
        const FPSRenderView& View = Candidate.RenderViews[Index];
        const FString Where = FString::Printf(TEXT("RenderViews[%d] (%s)"), Index, *View.ViewId.ToString());
        const FString IdText = View.ViewId.ToString();
        const bool bFileSafe = !IdText.IsEmpty() && Algo::AllOf(IdText, [](const TCHAR Char) { return FChar::IsAlnum(Char) || Char == TEXT('_'); });
        if (View.ViewId.IsNone())
        {
            Problems.Add(FString::Printf(TEXT("%s: ViewId must not be empty"), *Where));
        }
        else if (!bFileSafe)
        {
            Problems.Add(FString::Printf(TEXT("%s: ViewId must be letters, digits and underscores (it names the PNG)"), *Where));
        }
        else if (Seen.Contains(View.ViewId))
        {
            Problems.Add(FString::Printf(TEXT("%s: ViewId is used twice"), *Where));
        }
        Seen.Add(View.ViewId);
        if (View.FieldOfViewDegrees < 5.f || View.FieldOfViewDegrees > 170.f)
        {
            Problems.Add(FString::Printf(TEXT("%s: FieldOfViewDegrees must be from 5 to 170"), *Where));
        }
        if (View.CameraYards.Equals(View.TargetYards, KINDA_SMALL_NUMBER))
        {
            Problems.Add(FString::Printf(TEXT("%s: CameraYards and TargetYards must differ"), *Where));
        }
        if (View.Anchor == EPSRenderViewAnchor::Player && View.PlayerIndex < 0)
        {
            Problems.Add(FString::Printf(TEXT("%s: PlayerIndex must be 0 or more"), *Where));
        }
    }
    return Problems;
}

bool UPSRenderCapture::LoadSettings(const FString& JsonFilePath, FPSRenderCaptureSettings& OutSettings, TArray<FString>& OutProblems)
{
    OutProblems.Reset();
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSRenderCaptureSettings Loaded;
    if (!Ingestion->LoadRenderCaptureSettingsFromJson(JsonFilePath, Loaded))
    {
        OutProblems.Add(FString::Printf(TEXT("%s can't be read as render views"), *JsonFilePath));
        return false;
    }
    OutProblems = ValidateSettings(Loaded);
    if (OutProblems.Num() > 0)
    {
        return false;
    }
    OutSettings = Loaded;
    return true;
}

TArray<FPSRenderView> UPSRenderCapture::SelectViews(const FPSRenderCaptureSettings& InSettings, const TArray<FName>& Only)
{
    TArray<FPSRenderView> Selected;
    for (const FPSRenderView& View : InSettings.RenderViews)
    {
        if (Only.Num() == 0 || Only.Contains(View.ViewId))
        {
            Selected.Add(View);
        }
    }
    return Selected;
}

bool UPSRenderCapture::ComputeCameraPose(const FPSRenderView& View, const FVector& AnchorGround, FVector& OutLocation, FRotator& OutRotation)
{
    using namespace PSRenderCapturePrivate;

    FVector Target;
    if (View.Anchor == EPSRenderViewAnchor::Player)
    {
        OutLocation = PlayerYardsToWorld(AnchorGround, View.CameraYards);
        Target = PlayerYardsToWorld(AnchorGround, View.TargetYards);
    }
    else
    {
        OutLocation = FieldYardsToWorld(View.CameraYards);
        Target = FieldYardsToWorld(View.TargetYards);
    }

    const FVector Direction = Target - OutLocation;
    if (Direction.IsNearlyZero())
    {
        OutRotation = FRotator::ZeroRotator;
        return false;
    }
    OutRotation = Direction.Rotation();
    return true;
}

int32 UPSRenderCapture::PickAnchorPlayer(const TArray<TPair<EPlayerRole, FVector>>& Players, EPlayerRole Role, int32 Index)
{
    TArray<int32> Candidates;
    for (int32 PlayerSlot = 0; PlayerSlot < Players.Num(); ++PlayerSlot)
    {
        if (Players[PlayerSlot].Key == Role)
        {
            Candidates.Add(PlayerSlot);
        }
    }
    Candidates.Sort([&Players](const int32 A, const int32 B)
    {
        const FVector& LocationA = Players[A].Value;
        const FVector& LocationB = Players[B].Value;
        if (!FMath::IsNearlyEqual(LocationA.Y, LocationB.Y))
        {
            return LocationA.Y < LocationB.Y;
        }
        return LocationA.X < LocationB.X;
    });
    return Candidates.IsValidIndex(Index) ? Candidates[Index] : INDEX_NONE;
}

FString UPSRenderCapture::GetShotFileName(const FPSRenderView& View)
{
    return View.ViewId.ToString() + TEXT(".png");
}

bool UPSRenderCapture::IsLinedUpPhase(const FString& PhaseName)
{
    const UEnum* PhaseEnum = StaticEnum<EPlayPhase>();
    return PhaseEnum && PhaseName == PhaseEnum->GetNameStringByValue(static_cast<int64>(EPlayPhase::PreSnap));
}

void UPSRenderCapture::FinalizeReport(FPSRenderCaptureReport& InOutReport, int32 ExpectedShots)
{
    bool bAllWritten = InOutReport.Shots.Num() == ExpectedShots && ExpectedShots > 0;
    for (const FPSRenderShot& Shot : InOutReport.Shots)
    {
        bAllWritten &= Shot.bWritten;
    }
    InOutReport.bPassed = bAllWritten && InOutReport.Failures.Num() == 0;
}

int32 UPSRenderCapture::GetExitCode(const FPSRenderCaptureReport& InReport)
{
    return InReport.bPassed ? 0 : 1;
}

FString UPSRenderCapture::BuildIndexJson(const FPSRenderCaptureReport& InReport)
{
    using namespace PSRenderCapturePrivate;

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("Passed"), InReport.bPassed);
    Root->SetStringField(TEXT("Commit"), InReport.Commit);
    Root->SetStringField(TEXT("Map"), InReport.MapName);
    Root->SetStringField(TEXT("Rhi"), InReport.RHIName);
    Root->SetStringField(TEXT("Adapter"), InReport.AdapterName);
    Root->SetNumberField(TEXT("PlayersOnField"), InReport.PlayersOnField);
    Root->SetNumberField(TEXT("MatchReadySeconds"), InReport.MatchReadySeconds);
    Root->SetNumberField(TEXT("CompileDoneSeconds"), InReport.CompileDoneSeconds);

    TArray<TSharedPtr<FJsonValue>> ShotValues;
    for (const FPSRenderShot& Shot : InReport.Shots)
    {
        TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("View"), Shot.ViewId.ToString());
        Entry->SetStringField(TEXT("Description"), Shot.Description);
        Entry->SetStringField(TEXT("File"), Shot.FileName);
        Entry->SetBoolField(TEXT("Written"), Shot.bWritten);
        Entry->SetNumberField(TEXT("ResolutionX"), Shot.ResolutionX);
        Entry->SetNumberField(TEXT("ResolutionY"), Shot.ResolutionY);
        Entry->SetNumberField(TEXT("GpuMs"), FMath::RoundToDouble(Shot.GpuMs * 100.0) / 100.0);
        Entry->SetNumberField(TEXT("GpuMsMax"), FMath::RoundToDouble(Shot.GpuMsMax * 100.0) / 100.0);
        Entry->SetNumberField(TEXT("FrameMs"), FMath::RoundToDouble(Shot.FrameMs * 100.0) / 100.0);
        Entry->SetArrayField(TEXT("CameraLocation"), ToJsonNumbers(Shot.CameraLocation.X, Shot.CameraLocation.Y, Shot.CameraLocation.Z));
        Entry->SetArrayField(TEXT("CameraRotation"), ToJsonNumbers(Shot.CameraRotation.Pitch, Shot.CameraRotation.Yaw, Shot.CameraRotation.Roll));
        Entry->SetNumberField(TEXT("Restarts"), Shot.Restarts);
        Entry->SetStringField(TEXT("Error"), Shot.Error);
        ShotValues.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Root->SetArrayField(TEXT("Views"), ShotValues);
    Root->SetArrayField(TEXT("Failures"), ToJsonStrings(InReport.Failures));

    FString Json;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(Root, Writer);
    return Json;
}

void UPSRenderCapture::RegisterCommandLineHook()
{
    if (FParse::Param(FCommandLine::Get(), PSRenderCapturePrivate::SwitchName))
    {
        FCoreDelegates::OnPostEngineInit.AddStatic(&UPSRenderCapture::StartFromCommandLine);
    }
}

void UPSRenderCapture::StartFromCommandLine()
{
    FPSRenderCaptureOptions ParsedOptions;
    if (!ParseCommandLine(FCommandLine::Get(), ParsedOptions))
    {
        return;
    }
    if (GIsEditor || IsRunningCommandlet())
    {
        UE_LOG(LogTemp, Warning, TEXT("PSRenderCapture: -PSRenderCapture runs in a game (packaged, or the editor binary with -game), not in the editor or a commandlet; ignored."));
        return;
    }

    FPSRenderCaptureSettings Loaded;
    TArray<FString> Problems;
    UPSRenderCapture* Capture = NewObject<UPSRenderCapture>(GetTransientPackage());
    Capture->AddToRoot();
    if (!LoadSettings(ParsedOptions.ViewsPath, Loaded, Problems))
    {
        // Nothing to render: report why and exit at once.
        Capture->Options = ParsedOptions;
        Capture->Report.Failures = Problems;
        Capture->Finish();
        return;
    }
    Capture->Begin(ParsedOptions, Loaded);
}

void UPSRenderCapture::Begin(const FPSRenderCaptureOptions& InOptions, const FPSRenderCaptureSettings& InSettings)
{
    Options = InOptions;
    Settings = InSettings;
    Views = SelectViews(Settings, Options.OnlyViews);
    Report = FPSRenderCaptureReport();
    Report.Commit = Options.Commit;
    for (const FName& Wanted : Options.OnlyViews)
    {
        if (!Views.ContainsByPredicate([&Wanted](const FPSRenderView& View) { return View.ViewId == Wanted; }))
        {
            Report.Failures.Add(FString::Printf(TEXT("-PSRenderCaptureOnly names %s, which isn't in %s"), *Wanted.ToString(), *Options.ViewsPath));
        }
    }

    IFileManager::Get().MakeDirectory(*Options.OutputDir, true);
    StartSeconds = FPlatformTime::Seconds();
    PhaseStartSeconds = StartSeconds;
    LastLogSeconds = StartSeconds;
    Phase = EPhase::WaitForMatch;
    UE_LOG(LogTemp, Display, TEXT("PSRenderCapture: %d view(s) from %s to %s at %dx%d"),
        Views.Num(), *Options.ViewsPath, *Options.OutputDir, Settings.ResolutionX, Settings.ResolutionY);

    if (Views.Num() == 0)
    {
        Report.Failures.Add(TEXT("no view to render"));
        Finish();
        return;
    }
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UPSRenderCapture::TickCapture));
}

UWorld* UPSRenderCapture::FindGameWorld() const
{
    if (!GEngine)
    {
        return nullptr;
    }
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.WorldType == EWorldType::Game && Context.World())
        {
            return Context.World();
        }
    }
    return nullptr;
}

int32 UPSRenderCapture::CountPlayersOnField(UWorld* World) const
{
    return World ? UPSAIFieldSnapshot::GetFieldPawns(World).Num() : 0;
}

bool UPSRenderCapture::IsMatchReady(UWorld* World) const
{
    if (!World || !World->HasBegunPlay() || !World->GetFirstPlayerController())
    {
        return false;
    }
    if (CountPlayersOnField(World) < Settings.ExpectedPlayers)
    {
        return false;
    }
    TActorIterator<APSBall> Ball(World);
    return Settings.ExpectedPlayers == 0 || static_cast<bool>(Ball);
}

void UPSRenderCapture::BindToBus(UWorld* World)
{
    // Kickoff is pre-snap; from here on the bus says when a play starts and ends.
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (Bus && !bBoundToBus)
    {
        Bus->OnPhaseChangeMC.AddUObject(this, &UPSRenderCapture::HandlePhaseChange);
        bBoundToBus = true;
        bLinedUp = true;
    }
}

void UPSRenderCapture::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    bLinedUp = IsLinedUpPhase(Event.NewPhase);
    ++PhaseChanges;
}

void UPSRenderCapture::PrepareScene(UWorld* World)
{
    Report.MapName = World->GetMapName();
    Report.RHIName = FApp::GetGraphicsRHI();
    Report.AdapterName = GRHIAdapterName;
    Report.PlayersOnField = CountPlayersOnField(World);

    // The render, not the game's overlays: no HUD and no on-screen debug messages.
    if (APlayerController* Controller = World->GetFirstPlayerController())
    {
        if (AHUD* Hud = Controller->GetHUD())
        {
            Hud->bShowHUD = false;
        }
    }
    if (GEngine)
    {
        GEngine->Exec(World, TEXT("DisableAllScreenMessages"));
        GEngine->Exec(World, *FString::Printf(TEXT("r.SetRes %dx%dw"), Settings.ResolutionX, Settings.ResolutionY));
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, SpawnParams);
    if (Camera && Camera->GetCameraComponent())
    {
        Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
    }
    bSceneReady = Camera != nullptr;
    if (!bSceneReady)
    {
        Report.Failures.Add(TEXT("the capture camera could not be spawned"));
    }
}

bool UPSRenderCapture::PlaceCamera(UWorld* World, const FPSRenderView& View, FVector& OutLocation, FRotator& OutRotation, FString& OutError)
{
    FVector AnchorGround = FVector::ZeroVector;
    if (View.Anchor == EPSRenderViewAnchor::Player)
    {
        TArray<TPair<EPlayerRole, FVector>> Players;
        for (const APSPlayerPawn* Pawn : UPSAIFieldSnapshot::GetFieldPawns(World))
        {
            if (Pawn)
            {
                Players.Emplace(Pawn->GetAttributes().Role, Pawn->GetActorLocation());
            }
        }
        const int32 Picked = PickAnchorPlayer(Players, View.PlayerRole, View.PlayerIndex);
        if (Picked == INDEX_NONE)
        {
            OutError = FString::Printf(TEXT("no %s #%d on the field"), *StaticEnum<EPlayerRole>()->GetNameStringByValue(static_cast<int64>(View.PlayerRole)), View.PlayerIndex);
            return false;
        }
        AnchorGround = FVector(Players[Picked].Value.X, Players[Picked].Value.Y, 0.f);
    }
    if (!ComputeCameraPose(View, AnchorGround, OutLocation, OutRotation))
    {
        OutError = TEXT("the camera and its target coincide");
        return false;
    }

    Camera->SetActorLocationAndRotation(OutLocation, OutRotation);
    Camera->GetCameraComponent()->SetFieldOfView(View.FieldOfViewDegrees);
    HoldViewTarget(World);
    if (Settings.StreamingWaitSeconds > 0.f)
    {
        // The textures and meshes this view sees, at the detail the pool allows.
        IStreamingManager::Get().StreamAllResources(Settings.StreamingWaitSeconds);
    }
    return true;
}

void UPSRenderCapture::BeginView(UWorld* World)
{
    const FPSRenderView& View = Views[ViewIndex];
    CurrentShot = FPSRenderShot();
    CurrentShot.ViewId = View.ViewId;
    CurrentShot.Description = View.Description;
    CurrentShot.FileName = GetShotFileName(View);
    GpuSamples.Reset();
    FrameSamples.Reset();
    ViewPhaseChanges = PhaseChanges;

    FVector Location;
    FRotator Rotation;
    FString Error;
    if (!PlaceCamera(World, View, Location, Rotation, Error))
    {
        UE_LOG(LogTemp, Error, TEXT("PSRenderCapture: view %s skipped: %s"), *View.ViewId.ToString(), *Error);
        FinishShot(false, Error);
        return;
    }
    CurrentShot.CameraLocation = Location;
    CurrentShot.CameraRotation = Rotation;

    Phase = EPhase::SettleView;
    PhaseFrames = 0;
    PhaseStartSeconds = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Display, TEXT("PSRenderCapture: view %d/%d %s at %s looking %s, %.0f degrees"),
        ViewIndex + 1, Views.Num(), *View.ViewId.ToString(), *Location.ToCompactString(), *Rotation.ToCompactString(), View.FieldOfViewDegrees);
}

void UPSRenderCapture::RestartView(UWorld* World)
{
    const int32 Restarts = CurrentShot.Restarts + 1;
    UE_LOG(LogTemp, Display, TEXT("PSRenderCapture: view %s starts again at pre-snap (restart %d)"), *CurrentShot.ViewId.ToString(), Restarts);
    BeginView(World);
    if (Phase == EPhase::SettleView)
    {
        CurrentShot.Restarts = Restarts;
    }
}

void UPSRenderCapture::HoldViewTarget(UWorld* World)
{
    // The game may hand the view to a pawn or a broadcast camera at any time; the capture's
    // camera keeps it.
    APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
    ACameraActor* CaptureCamera = Camera.Get();
    if (Controller && CaptureCamera && Controller->GetViewTarget() != CaptureCamera)
    {
        Controller->SetViewTarget(CaptureCamera);
    }
}

void UPSRenderCapture::RequestShot(UWorld* World)
{
    using namespace PSRenderCapturePrivate;

    CurrentShot.GpuMs = MeanOf(GpuSamples);
    CurrentShot.GpuMsMax = LargestOf(GpuSamples);
    CurrentShot.FrameMs = MeanOf(FrameSamples);
    if (UGameViewportClient* Viewport = World->GetGameViewport())
    {
        if (Viewport->Viewport)
        {
            const FIntPoint Size = Viewport->Viewport->GetSizeXY();
            CurrentShot.ResolutionX = Size.X;
            CurrentShot.ResolutionY = Size.Y;
        }
    }

    const FString Path = Options.OutputDir / CurrentShot.FileName;
    IFileManager::Get().Delete(*Path, false, true, true);
    FScreenshotRequest::RequestScreenshot(Path, false, false);
    Phase = EPhase::WaitForShot;
    PhaseFrames = 0;
}

void UPSRenderCapture::FinishShot(bool bWritten, const FString& Error)
{
    CurrentShot.bWritten = bWritten;
    CurrentShot.Error = Error;
    if (bWritten)
    {
        UE_LOG(LogTemp, Display, TEXT("PSRenderCapture: wrote %s (%dx%d) GPU %.2f ms (max %.2f), frame %.2f ms"),
            *CurrentShot.FileName, CurrentShot.ResolutionX, CurrentShot.ResolutionY, CurrentShot.GpuMs, CurrentShot.GpuMsMax, CurrentShot.FrameMs);
    }
    // A PNG taken while something was still wrong (compile jobs pending) still fails the run.
    if (!CurrentShot.Error.IsEmpty())
    {
        Report.Failures.Add(FString::Printf(TEXT("view %s: %s"), *CurrentShot.ViewId.ToString(), *CurrentShot.Error));
    }
    Report.Shots.Add(CurrentShot);
    ++ViewIndex;
    if (ViewIndex >= Views.Num())
    {
        Phase = EPhase::Done;
    }
    else if (UWorld* World = FindGameWorld())
    {
        BeginView(World);
    }
    else
    {
        Report.Failures.Add(TEXT("the game world went away during the capture"));
        Phase = EPhase::Done;
    }
}

bool UPSRenderCapture::TickCapture(float DeltaSeconds)
{
    using namespace PSRenderCapturePrivate;

    const double Now = FPlatformTime::Seconds();
    if (Now - StartSeconds > Settings.TotalTimeoutSeconds)
    {
        Report.Failures.Add(FString::Printf(TEXT("the capture did not finish within %.0f s"), Settings.TotalTimeoutSeconds));
        Phase = EPhase::Done;
    }

    UWorld* World = FindGameWorld();
    if (!World && (Phase == EPhase::Warmup || Phase == EPhase::SettleView || Phase == EPhase::WaitForShot))
    {
        Report.Failures.Add(TEXT("the game world went away during the capture"));
        Phase = EPhase::Done;
    }
    const bool bLog = Now - LastLogSeconds >= LogIntervalSeconds;
    if (bLog)
    {
        LastLogSeconds = Now;
    }
    ++PhaseFrames;

    switch (Phase)
    {
    case EPhase::WaitForMatch:
        if (IsMatchReady(World))
        {
            BindToBus(World);
            Report.MatchReadySeconds = static_cast<float>(Now - StartSeconds);
            UE_LOG(LogTemp, Display, TEXT("PSRenderCapture: the match is ready after %.1f s (%d players)"), Report.MatchReadySeconds, CountPlayersOnField(World));
            Phase = EPhase::WaitForCompilation;
            PhaseStartSeconds = Now;
            PhaseFrames = 0;
        }
        else if (Now - StartSeconds > Settings.MatchTimeoutSeconds && World && World->HasBegunPlay())
        {
            Report.Failures.Add(FString::Printf(TEXT("the match was not ready within %.0f s (%d of %d players on the field)"),
                Settings.MatchTimeoutSeconds, CountPlayersOnField(World), Settings.ExpectedPlayers));
            BindToBus(World);
            Phase = EPhase::WaitForCompilation;
            PhaseStartSeconds = Now;
            PhaseFrames = 0;
        }
        else if (bLog)
        {
            UE_LOG(LogTemp, Display, TEXT("PSRenderCapture: waiting for the match (%s, %d players)"),
                World ? *World->GetMapName() : TEXT("no game world"), CountPlayersOnField(World));
        }
        break;

    case EPhase::WaitForCompilation:
    {
        const int32 Pending = GetPendingCompilation();
        const bool bTimedOut = Now - PhaseStartSeconds > Settings.CompileTimeoutSeconds;
        if (Pending == 0 || bTimedOut)
        {
            if (Pending > 0)
            {
                Report.Failures.Add(FString::Printf(TEXT("%d shader or asset job(s) still compiling after %.0f s"), Pending, Settings.CompileTimeoutSeconds));
            }
            Report.CompileDoneSeconds = static_cast<float>(Now - StartSeconds);
            UE_LOG(LogTemp, Display, TEXT("PSRenderCapture: compilation done after %.1f s; warming up"), Report.CompileDoneSeconds);
            if (!World)
            {
                Report.Failures.Add(TEXT("no game world to render"));
                Phase = EPhase::Done;
                break;
            }
            PrepareScene(World);
            if (!bSceneReady)
            {
                Phase = EPhase::Done;
                break;
            }
            // Warm up looking where the first view looks (or across the field, if it can't be placed).
            FVector Location;
            FRotator Rotation;
            FString Error;
            if (!PlaceCamera(World, Views[0], Location, Rotation, Error))
            {
                Camera->SetActorLocationAndRotation(FVector(2000.f, -5000.f, 2000.f), FRotator(-20.f, 90.f, 0.f));
                HoldViewTarget(World);
            }
            Phase = EPhase::Warmup;
            PhaseStartSeconds = Now;
            PhaseFrames = 0;
        }
        else if (bLog)
        {
            UE_LOG(LogTemp, Display, TEXT("PSRenderCapture: %d shader or asset job(s) still compiling"), Pending);
        }
        break;
    }

    case EPhase::Warmup:
        HoldViewTarget(World);
        if (PhaseFrames >= Settings.WarmupFrames && Now - PhaseStartSeconds >= Settings.WarmupSeconds)
        {
            UE_LOG(LogTemp, Display, TEXT("PSRenderCapture: warmed up over %d frames in %.1f s"), PhaseFrames, Now - PhaseStartSeconds);
            BeginView(World);
        }
        break;

    case EPhase::SettleView:
    {
        HoldViewTarget(World);
        if (Settings.bCaptureOnlyPreSnap && (!bLinedUp || PhaseChanges != ViewPhaseChanges))
        {
            if (bLinedUp)
            {
                // Pre-snap again after a play: the players are back in their spots.
                RestartView(World);
            }
            else if (bLog)
            {
                UE_LOG(LogTemp, Display, TEXT("PSRenderCapture: view %s waits for the next pre-snap"), *CurrentShot.ViewId.ToString());
            }
            break;
        }
        const int32 FramesNeeded = Settings.SettleFrames;
        const double SecondsNeeded = Settings.SettleSeconds;
        if (PhaseFrames > FramesNeeded - Settings.MeasureFrames)
        {
            // The last MeasureFrames frames before the PNG.
            GpuSamples.Add(GetGpuFrameMs());
            FrameSamples.Add(DeltaSeconds * 1000.f);
            if (GpuSamples.Num() > Settings.MeasureFrames)
            {
                GpuSamples.RemoveAt(0);
                FrameSamples.RemoveAt(0);
            }
        }
        if (PhaseFrames >= FramesNeeded && Now - PhaseStartSeconds >= SecondsNeeded)
        {
            // A view that brought new materials into sight waits for them (within the limit).
            const int32 Pending = GetPendingCompilation();
            if (Pending > 0 && Now - StartSeconds < Settings.CompileTimeoutSeconds)
            {
                if (bLog)
                {
                    UE_LOG(LogTemp, Display, TEXT("PSRenderCapture: view %s waits for %d compile job(s)"), *CurrentShot.ViewId.ToString(), Pending);
                }
                break;
            }
            if (Pending > 0)
            {
                CurrentShot.Error = FString::Printf(TEXT("%d compile job(s) still pending when taken"), Pending);
            }
            RequestShot(World);
        }
        break;
    }

    case EPhase::WaitForShot:
    {
        HoldViewTarget(World);
        const FString Path = Options.OutputDir / CurrentShot.FileName;
        if (!FScreenshotRequest::IsScreenshotRequested() && IFileManager::Get().FileSize(*Path) > 0)
        {
            FinishShot(true, CurrentShot.Error);
        }
        else if (PhaseFrames > Settings.ScreenshotTimeoutFrames)
        {
            FinishShot(false, FString::Printf(TEXT("the PNG did not reach %s within %d frames"), *Path, Settings.ScreenshotTimeoutFrames));
        }
        break;
    }

    case EPhase::Done:
        break;
    }

    if (Phase == EPhase::Done)
    {
        Finish();
        return false;
    }
    return true;
}

void UPSRenderCapture::Finish()
{
    FinalizeReport(Report, Views.Num());
    if (Options.OutputDir.IsEmpty())
    {
        Options.OutputDir = GetDefaultOutputDir();
    }
    IFileManager::Get().MakeDirectory(*Options.OutputDir, true);
    const FString IndexPath = Options.OutputDir / TEXT("index.json");
    const bool bIndexWritten = FFileHelper::SaveStringToFile(BuildIndexJson(Report), *IndexPath);
    if (!bIndexWritten)
    {
        UE_LOG(LogTemp, Error, TEXT("PSRenderCapture: could not write %s"), *IndexPath);
    }
    for (const FString& Failure : Report.Failures)
    {
        UE_LOG(LogTemp, Error, TEXT("PSRenderCapture: %s"), *Failure);
    }
    const int32 ExitCode = bIndexWritten ? GetExitCode(Report) : 1;
    UE_LOG(LogTemp, Display, TEXT("PSRenderCapture: %s, %d of %d view(s) written to %s, exit code %d"),
        Report.bPassed ? TEXT("PASS") : TEXT("FAIL"), Report.Shots.FilterByPredicate([](const FPSRenderShot& Shot) { return Shot.bWritten; }).Num(),
        Views.Num(), *Options.OutputDir, ExitCode);

    // The ticker stops when TickCapture returns false, and the exit below ends the process.
    TickerHandle.Reset();
    if (bBoundToBus)
    {
        if (UWorld* World = FindGameWorld())
        {
            if (UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>())
            {
                Bus->OnPhaseChangeMC.RemoveAll(this);
            }
        }
        bBoundToBus = false;
    }
    if (GLog)
    {
        GLog->Flush();
    }
    // A forced exit hands the code back to the caller on every platform (as -PSSmokeTest does).
    FPlatformMisc::RequestExitWithStatus(true, static_cast<uint8>(ExitCode));
}
