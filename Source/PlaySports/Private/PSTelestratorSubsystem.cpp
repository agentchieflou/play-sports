#include "PSTelestratorSubsystem.h"
#include "PSBroadcastCamera.h"
#include "PSCameraAll22Component.h"
#include "PSCameraFraming.h"
#include "PSDataIngestion.h"
#include "PSOverlayEmphasisSubsystem.h"
#include "PSPlayerPawn.h"
#include "PSReplaySubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

UPSTelestratorSubsystem::UPSTelestratorSubsystem()
{
    EmphasisSource = TEXT("Telestrator");
}

bool UPSTelestratorSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSTelestratorSubsystem::Deinitialize()
{
    EndAnalysis();
    Super::Deinitialize();
}

// --- Tuning ------------------------------------------------------------------------------

FString UPSTelestratorSubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/telestrator.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSTelestratorTuning& UPSTelestratorSubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSTelestratorSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSTelestratorTuning Loaded;
    if (!Ingestion->LoadTelestratorTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSTelestratorSubsystem: Could not load telestrator tuning from %s; keeping the current tuning."), *JsonFilePath);
        return false;
    }
    return SetTuning(Loaded);
}

bool UPSTelestratorSubsystem::SetTuning(const FPSTelestratorTuning& NewTuning)
{
    const TArray<FString> Problems = ValidateTuning(NewTuning);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSTelestratorSubsystem: Telestrator tuning refused: %s"), *Problem);
    }
    if (Problems.Num() > 0)
    {
        return false;
    }
    Tuning = NewTuning;
    bTuningLoaded = true;
    return true;
}

TArray<FString> UPSTelestratorSubsystem::ValidateTuning(const FPSTelestratorTuning& InTuning)
{
    TArray<FString> Problems;
    if (InTuning.MinPointSpacing < 0.f)
    {
        Problems.Add(TEXT("MinPointSpacing must be 0 or more"));
    }
    if (!(InTuning.PlayerPickRadius > 0.f))
    {
        Problems.Add(TEXT("PlayerPickRadius must be above 0"));
    }
    if (InTuning.MaxStrokePoints < 2)
    {
        Problems.Add(TEXT("MaxStrokePoints must be 2 or more"));
    }
    if (InTuning.MaxMarks < 1)
    {
        Problems.Add(TEXT("MaxMarks must be 1 or more"));
    }
    return Problems;
}

// --- Analysis mode -----------------------------------------------------------------------

APSBroadcastCamera* UPSTelestratorSubsystem::FindBroadcastCamera() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }
    TActorIterator<APSBroadcastCamera> It(World);
    return It ? *It : nullptr;
}

APSPlayerPawn* UPSTelestratorSubsystem::FindPawn(FName PlayerId) const
{
    UWorld* World = GetWorld();
    if (!World || PlayerId.IsNone())
    {
        return nullptr;
    }
    for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
    {
        if (It->GetAttributes().PlayerId == PlayerId)
        {
            return *It;
        }
    }
    return nullptr;
}

bool UPSTelestratorSubsystem::BeginAnalysis()
{
    EndAnalysis();
    APSBroadcastCamera* Camera = FindBroadcastCamera();
    UPSCameraAll22Component* All22 = Camera ? Camera->GetAll22Component() : nullptr;
    UWorld* World = GetWorld();
    UPSReplaySubsystem* Replay = World ? World->GetSubsystem<UPSReplaySubsystem>() : nullptr;
    const bool bReplay = Replay && Replay->IsReplaying();
    if (!All22 || (!bReplay && !All22->IsFilmViewActive()))
    {
        return false;
    }

    // Drawing happens on a held frame.
    bResumeReplay = bReplay && Replay->GetState() == EPSReplayState::Playing;
    if (bResumeReplay)
    {
        Replay->SetPaused(true);
    }
    Telestration = FPSTelestration();
    Telestration.Frame = All22->CaptureFrame();
    bAnalysisActive = true;
    bStroking = false;
    StrokePoints.Reset();
    return true;
}

void UPSTelestratorSubsystem::EndAnalysis()
{
    if (!bAnalysisActive)
    {
        return;
    }
    ClearMarks();
    bAnalysisActive = false;
    bStroking = false;
    StrokePoints.Reset();
    PendingRequests.Reset();

    UWorld* World = GetWorld();
    UPSReplaySubsystem* Replay = World ? World->GetSubsystem<UPSReplaySubsystem>() : nullptr;
    if (bResumeReplay && Replay && Replay->IsReplaying())
    {
        Replay->SetPaused(false);
    }
    bResumeReplay = false;
}

// --- Drawing -----------------------------------------------------------------------------

void UPSTelestratorSubsystem::BeginStroke(FVector2D Screen)
{
    if (!bAnalysisActive)
    {
        return;
    }
    bStroking = true;
    StrokePoints.Reset();
    StrokePoints.Add(Screen);
}

void UPSTelestratorSubsystem::ExtendStroke(FVector2D Screen)
{
    if (!bStroking || Tool != EPSTelestratorTool::Freehand)
    {
        return;
    }
    // A freehand line keeps its points apart, up to its limit.
    const FPSTelestratorTuning& DrawTuning = GetTuning();
    if (StrokePoints.Num() < DrawTuning.MaxStrokePoints && FVector2D::Distance(StrokePoints.Last(), Screen) >= DrawTuning.MinPointSpacing)
    {
        StrokePoints.Add(Screen);
    }
}

int32 UPSTelestratorSubsystem::EndStroke(FVector2D Screen)
{
    if (!bStroking || !bAnalysisActive)
    {
        return INDEX_NONE;
    }
    bStroking = false;
    const FPSTelestratorTuning& DrawTuning = GetTuning();

    FPSTelestratorMark Mark;
    Mark.Tool = Tool;
    switch (Tool)
    {
    case EPSTelestratorTool::Freehand:
        if (StrokePoints.Num() < DrawTuning.MaxStrokePoints && FVector2D::Distance(StrokePoints.Last(), Screen) >= DrawTuning.MinPointSpacing)
        {
            StrokePoints.Add(Screen);
        }
        Mark.ScreenPoints = StrokePoints;
        break;
    case EPSTelestratorTool::Arrow:
    case EPSTelestratorTool::Circle:
        Mark.ScreenPoints = { StrokePoints[0], Screen };
        break;
    case EPSTelestratorTool::Player:
    {
        Mark.PlayerId = PickPlayer(Telestration.Frame, Screen, DrawTuning.PlayerPickRadius);
        if (Mark.PlayerId.IsNone())
        {
            return INDEX_NONE;
        }
        const FName Picked = Mark.PlayerId;
        const FPSFilmFramePlayer* Player = Telestration.Frame.Players.FindByPredicate([Picked](const FPSFilmFramePlayer& Candidate) { return Candidate.PlayerId == Picked; });
        Mark.ScreenPoints = { Player ? Player->ScreenPosition : Screen };
        break;
    }
    default:
        break;
    }
    StrokePoints.Reset();
    PinToField(Mark);
    return AddMark(Mark);
}

FName UPSTelestratorSubsystem::PickPlayer(const FPSFilmFrame& Frame, const FVector2D& Screen, float Radius)
{
    FName Best;
    double BestDistance = Radius;
    for (const FPSFilmFramePlayer& Player : Frame.Players)
    {
        const double Distance = FVector2D::Distance(Player.ScreenPosition, Screen);
        if (Player.bInFrame && Distance <= BestDistance)
        {
            Best = Player.PlayerId;
            BestDistance = Distance;
        }
    }
    return Best;
}

void UPSTelestratorSubsystem::PinToField(FPSTelestratorMark& Mark) const
{
    Mark.FieldPoints.Reset();
    for (const FVector2D& Point : Mark.ScreenPoints)
    {
        FVector OnField;
        if (UPSCameraFraming::DeprojectToField(Telestration.Frame.Shot, Point, Tuning.FieldHeightCm, OnField))
        {
            Mark.FieldPoints.Add(OnField);
        }
    }
    if (Mark.Tool == EPSTelestratorTool::Circle && Mark.FieldPoints.Num() == 2)
    {
        Mark.FieldRadiusCm = static_cast<float>(FVector::Dist(Mark.FieldPoints[0], Mark.FieldPoints[1]));
    }
}

int32 UPSTelestratorSubsystem::AddMark(FPSTelestratorMark Mark)
{
    if (Telestration.Marks.Num() >= GetTuning().MaxMarks)
    {
        return INDEX_NONE;
    }
    if (Mark.Tool == EPSTelestratorTool::Player)
    {
        UWorld* World = GetWorld();
        UPSOverlayEmphasisSubsystem* Emphasis = World ? World->GetSubsystem<UPSOverlayEmphasisSubsystem>() : nullptr;
        APSPlayerPawn* Pawn = FindPawn(Mark.PlayerId);
        if (Emphasis && Pawn)
        {
            Mark.EmphasisHandle = Emphasis->Emphasize(Pawn, EPSEmphasisKind::Highlight, EmphasisSource, 0.f);
        }
    }
    return Telestration.Marks.Add(Mark);
}

void UPSTelestratorSubsystem::RemoveMark(int32 Index)
{
    if (!Telestration.Marks.IsValidIndex(Index))
    {
        return;
    }
    const int32 Handle = Telestration.Marks[Index].EmphasisHandle;
    UWorld* World = GetWorld();
    if (UPSOverlayEmphasisSubsystem* Emphasis = (Handle != INDEX_NONE && World) ? World->GetSubsystem<UPSOverlayEmphasisSubsystem>() : nullptr)
    {
        Emphasis->ClearEmphasis(Handle);
    }
    Telestration.Marks.RemoveAt(Index);
}

bool UPSTelestratorSubsystem::Undo()
{
    if (Telestration.Marks.Num() == 0)
    {
        return false;
    }
    RemoveMark(Telestration.Marks.Num() - 1);
    return true;
}

void UPSTelestratorSubsystem::ClearMarks()
{
    while (Telestration.Marks.Num() > 0)
    {
        RemoveMark(Telestration.Marks.Num() - 1);
    }
    Telestration.Notes.Reset();
}

// --- Stills ------------------------------------------------------------------------------

FString UPSTelestratorSubsystem::GetDefaultExportDirectory()
{
    return FPaths::ProjectSavedDir() / TEXT("Telestrator");
}

bool UPSTelestratorSubsystem::ExportStill(const FString& Directory, FString& OutFilePath)
{
    OutFilePath.Reset();
    if (!bAnalysisActive)
    {
        return false;
    }
    const FString TargetDirectory = Directory.IsEmpty() ? GetDefaultExportDirectory() : Directory;
    const FString BaseName = FString::Printf(TEXT("still_%s_%04d"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")), Telestration.Frame.FrameIndex);

    // The pixels need a rendered viewport, and the drawing layer is UI, so the shot keeps it.
    FPSTelestration Still = Telestration;
    UWorld* World = GetWorld();
    if (World && World->GetGameViewport())
    {
        Still.Frame.ImageFile = TargetDirectory / (BaseName + TEXT(".png"));
        FScreenshotRequest::RequestScreenshot(Still.Frame.ImageFile, true, false);
    }

    FString Json;
    if (!FJsonObjectConverter::UStructToJsonObjectString(Still, Json, 0, CPF_Transient))
    {
        return false;
    }
    IFileManager::Get().MakeDirectory(*TargetDirectory, true);
    const FString FilePath = TargetDirectory / (BaseName + TEXT(".json"));
    if (!FFileHelper::SaveStringToFile(Json, *FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSTelestratorSubsystem: Could not write %s."), *FilePath);
        return false;
    }
    Telestration.Frame.ImageFile = Still.Frame.ImageFile;
    OutFilePath = FilePath;
    return true;
}

bool UPSTelestratorSubsystem::LoadStill(const FString& FilePath, FPSTelestration& OutTelestration)
{
    FString Json;
    return FFileHelper::LoadFileToString(Json, *FilePath)
        && FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutTelestration, 0, CPF_Transient);
}

// --- Auto-annotation ---------------------------------------------------------------------

bool UPSTelestratorSubsystem::IsAutoAnnotationAvailable() const
{
    return bBridgeOnline || OnAutoAnnotationRequestedMC.IsBound() || OnAutoAnnotationRequested.IsBound();
}

int32 UPSTelestratorSubsystem::RequestAutoAnnotation()
{
    if (!bAnalysisActive || !IsAutoAnnotationAvailable())
    {
        return INDEX_NONE;
    }

    FPSAnnotationRequest& Request = PendingRequests.AddDefaulted_GetRef();
    Request.RequestId = NextRequestId++;
    Request.Frame = Telestration.Frame;
    UWorld* World = GetWorld();
    const UPSReplaySubsystem* Replay = World ? World->GetSubsystem<UPSReplaySubsystem>() : nullptr;
    if (Replay && Replay->IsReplaying())
    {
        Request.Events = Replay->GetClip().Events;
        Request.Situation = Replay->GetClip().InitialState.PlayState;
    }

    // A copy: a listener may answer at once, which changes the pending list.
    const FPSAnnotationRequest Asked = Request;
    OnAutoAnnotationRequestedMC.Broadcast(Asked);
    OnAutoAnnotationRequested.Broadcast(Asked);
    return Asked.RequestId;
}

bool UPSTelestratorSubsystem::SubmitAutoAnnotation(int32 RequestId, const FString& Notes, const TArray<FPSAnnotationSuggestion>& Suggestions)
{
    const int32 Index = PendingRequests.IndexOfByPredicate([RequestId](const FPSAnnotationRequest& Request) { return Request.RequestId == RequestId; });
    if (Index == INDEX_NONE)
    {
        return false;
    }
    const int32 FrameIndex = PendingRequests[Index].Frame.FrameIndex;
    PendingRequests.RemoveAt(Index);
    if (!bAnalysisActive || FrameIndex != Telestration.Frame.FrameIndex)
    {
        return false;
    }

    if (!Notes.IsEmpty())
    {
        Telestration.Notes.Add(Notes);
    }
    for (const FPSAnnotationSuggestion& Suggestion : Suggestions)
    {
        FPSTelestratorMark Mark;
        Mark.Tool = Suggestion.Tool;
        Mark.bAuto = true;
        if (Suggestion.Tool == EPSTelestratorTool::Player)
        {
            const FName Wanted = Suggestion.PlayerId;
            const FPSFilmFramePlayer* Player = Telestration.Frame.Players.FindByPredicate([Wanted](const FPSFilmFramePlayer& Candidate) { return Candidate.PlayerId == Wanted; });
            if (!Player)
            {
                continue;
            }
            Mark.PlayerId = Wanted;
            Mark.ScreenPoints.Add(Player->ScreenPosition);
            Mark.FieldPoints.Add(Player->WorldLocation);
        }
        else
        {
            // An annotator speaks in field terms: put each point on the screen through the shot.
            for (const FVector& Point : Suggestion.FieldPoints)
            {
                FVector2D OnScreen;
                UPSCameraFraming::ProjectToShot(Telestration.Frame.Shot, Point, OnScreen);
                Mark.ScreenPoints.Add(OnScreen);
                Mark.FieldPoints.Add(Point);
            }
            if (Mark.FieldPoints.Num() == 0 || (Suggestion.Tool == EPSTelestratorTool::Arrow && Mark.FieldPoints.Num() != 2))
            {
                continue;
            }
            Mark.FieldRadiusCm = Suggestion.Tool == EPSTelestratorTool::Circle ? Suggestion.RadiusCm : 0.f;
        }
        AddMark(Mark);
    }
    OnAutoAnnotationReceived.Broadcast(RequestId);
    return true;
}
