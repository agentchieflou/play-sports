#include "PSReplaySubsystem.h"
#include "PSBall.h"
#include "PSBroadcastCamera.h"
#include "PSDataIngestion.h"
#include "PSGameStateEvents.h"
#include "PSPlayerPawn.h"
#include "PSTelemetrySamplingSubsystem.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace PSReplaySubsystemPrivate
{
    /** Slack on frame times, so a playhead set onto a frame counts as on it. */
    constexpr float TimeTolerance = 1.0e-4f;

    /** The index of the last of Frames at or before Time; INDEX_NONE before the first. Frames
     *  are in time order. */
    int32 FindFrameAtOrBefore(const TArray<FPSSnapshotFrame>& Frames, float Time)
    {
        int32 Low = 0;
        int32 High = Frames.Num();
        while (Low < High)
        {
            const int32 Middle = Low + (High - Low) / 2;
            if (Frames[Middle].Time <= Time)
            {
                Low = Middle + 1;
            }
            else
            {
                High = Middle;
            }
        }
        return Low - 1;
    }

    FString EventTypeName(EPSTelemetryEventType EventType)
    {
        return StaticEnum<EPSTelemetryEventType>()->GetNameStringByValue(static_cast<int64>(EventType));
    }

    /** The event announces the whistle: a PhaseChange to Scoring, or a GameState in it. */
    bool IsWhistle(const FPSTelemetryEvent& Event)
    {
        if (Event.EventType == EPSTelemetryEventType::PhaseChange)
        {
            FPSTelemetryPhaseChangeEvent Phase;
            return FJsonObjectConverter::JsonObjectStringToUStruct(Event.PayloadJson, &Phase, 0, 0) && Phase.NewPhase == TEXT("Scoring");
        }
        if (Event.EventType == EPSTelemetryEventType::GameState)
        {
            FPSTelemetryGameStateEvent GameState;
            return FJsonObjectConverter::JsonObjectStringToUStruct(Event.PayloadJson, &GameState, 0, 0) && GameState.Phase == TEXT("Scoring");
        }
        return false;
    }
}

void UPSReplaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ApplyPlatformTier(PSPlatformTiers::GetActiveTier());
}

void UPSReplaySubsystem::Deinitialize()
{
    StopReplay();
    Super::Deinitialize();
}

bool UPSReplaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSReplaySubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    AdvanceReplay(DeltaTime);
}

TStatId UPSReplaySubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSReplaySubsystem, STATGROUP_Tickables);
}

// --- Tuning ------------------------------------------------------------------------------

FString UPSReplaySubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/replay.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSReplayTuning& UPSReplaySubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSReplaySubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSReplayTuning Loaded;
    if (!Ingestion->LoadReplayTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSReplaySubsystem: Could not load replay tuning from %s; keeping the current tuning."), *JsonFilePath);
        return false;
    }
    return SetTuning(Loaded);
}

bool UPSReplaySubsystem::SetTuning(const FPSReplayTuning& NewTuning)
{
    const TArray<FString> Problems = ValidateTuning(NewTuning);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSReplaySubsystem: Replay tuning refused: %s"), *Problem);
    }
    if (Problems.Num() > 0)
    {
        return false;
    }
    Tuning = NewTuning;
    bTuningLoaded = true;
    RateIndex = 0;
    return true;
}

TArray<FString> UPSReplaySubsystem::ValidateTuning(const FPSReplayTuning& InTuning)
{
    TArray<FString> Problems;
    if (InTuning.PreRollSeconds < 0.f || InTuning.PostRollSeconds < 0.f)
    {
        Problems.Add(TEXT("PreRollSeconds and PostRollSeconds must be 0 or more"));
    }
    if (!(InTuning.SaveFrameRateHz > 0.f))
    {
        Problems.Add(TEXT("SaveFrameRateHz must be above 0"));
    }
    if (InTuning.PlaybackRates.Num() == 0)
    {
        Problems.Add(TEXT("PlaybackRates needs at least one speed"));
        return Problems;
    }
    for (int32 Index = 0; Index < InTuning.PlaybackRates.Num(); ++Index)
    {
        const float Rate = InTuning.PlaybackRates[Index];
        if (!(Rate > 0.f) || Rate > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("PlaybackRates[%d] (%g) must be above 0 and at most 1"), Index, Rate));
        }
        else if (InTuning.PlaybackRates.IndexOfByKey(Rate) != Index)
        {
            Problems.Add(FString::Printf(TEXT("PlaybackRates[%d] (%g) is listed twice"), Index, Rate));
        }
    }
    if (InTuning.PlaybackRates[0] != 1.f)
    {
        Problems.Add(TEXT("PlaybackRates[0] must be 1, the speed a replay starts at"));
    }
    return Problems;
}

void UPSReplaySubsystem::ApplyPlatformTier(const FPSPlatformTier& Tier)
{
    PoseRateHz = FMath::Max(0.f, Tier.ReplayPoseRateHz);
}

// --- Recording ---------------------------------------------------------------------------

UPSTelemetryBus* UPSReplaySubsystem::GetBus() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
}

UPSTelemetrySamplingSubsystem* UPSReplaySubsystem::GetSampler() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSTelemetrySamplingSubsystem>() : nullptr;
}

bool UPSReplaySubsystem::CaptureClip(int32 FromSequence, int32 ToSequence, FPSReplayRecording& OutClip)
{
    using namespace PSReplaySubsystemPrivate;

    const UPSTelemetryBus* Bus = GetBus();
    const UPSTelemetrySamplingSubsystem* Sampler = GetSampler();
    float FromTime = 0.f;
    float ToTime = 0.f;
    if (!Bus || !Sampler || !Sampler->GetEventTime(FromSequence, FromTime) || !Sampler->GetEventTime(ToSequence, ToTime) || ToTime < FromTime)
    {
        return false;
    }

    const FPSReplayTuning& ClipTuning = GetTuning();
    const float StartTime = FromTime - ClipTuning.PreRollSeconds;
    const float EndTime = ToTime + ClipTuning.PostRollSeconds;
    TArray<FPSSnapshotFrame> Frames = Sampler->GetFramesBetween(StartTime, EndTime);
    if (Frames.Num() == 0)
    {
        return false;
    }

    // The situation the clip opens on: the last game state announced by its first event.
    FPlayState Opening;
    for (const FPSTelemetryEvent& Event : Bus->GetEventHistory())
    {
        if (Event.Sequence > FromSequence)
        {
            break;
        }
        FPSTelemetryGameStateEvent GameState;
        if (Event.EventType == EPSTelemetryEventType::GameState
            && FJsonObjectConverter::JsonObjectStringToUStruct(Event.PayloadJson, &GameState, 0, 0))
        {
            Opening = PSGameStateEvents::ToPlayState(GameState);
        }
    }

    OutClip = UPSReplayFormat::MakeRecording(Opening, TArray<FPlayerAttributes>(), TArray<FPlayerAttributes>());
    OutClip.Header.GameBuildVersion = FApp::GetBuildVersion();
    OutClip.Frames = MoveTemp(Frames);
    for (const FPSTelemetryEvent& Event : Sampler->GetEventsBetween(StartTime, EndTime))
    {
        FPSReplayEventRecord& Record = OutClip.Events.AddDefaulted_GetRef();
        Record.EventType = EventTypeName(Event.EventType);
        Record.PayloadJson = Event.PayloadJson;
        Sampler->GetEventTime(Event.Sequence, Record.TimestampSeconds);
    }
    AssignEventTicks(OutClip);
    return true;
}

bool UPSReplaySubsystem::CaptureLastPlay(FPSReplayRecording& OutClip)
{
    const UPSTelemetryBus* Bus = GetBus();
    int32 FromSequence = 0;
    int32 ToSequence = 0;
    return Bus && FindLastPlay(Bus->GetEventHistory(), FromSequence, ToSequence) && CaptureClip(FromSequence, ToSequence, OutClip);
}

bool UPSReplaySubsystem::FindLastPlay(const TArray<FPSTelemetryEvent>& History, int32& OutFromSequence, int32& OutToSequence)
{
    int32 SnapIndex = INDEX_NONE;
    for (int32 Index = History.Num() - 1; Index >= 0; --Index)
    {
        if (History[Index].EventType == EPSTelemetryEventType::Snap)
        {
            SnapIndex = Index;
            break;
        }
    }
    if (SnapIndex == INDEX_NONE)
    {
        return false;
    }

    OutFromSequence = History[SnapIndex].Sequence;
    OutToSequence = History.Last().Sequence;
    for (int32 Index = SnapIndex + 1; Index < History.Num(); ++Index)
    {
        if (PSReplaySubsystemPrivate::IsWhistle(History[Index]))
        {
            OutToSequence = History[Index].Sequence;
            break;
        }
    }
    return true;
}

// --- Playback ----------------------------------------------------------------------------

bool UPSReplaySubsystem::StartReplay(const FPSReplayRecording& InClip)
{
    if (InClip.Frames.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSReplaySubsystem: A clip without frames can't be played back."));
        return false;
    }
    StopReplay();

    Clip = InClip;
    Clip.Frames.StableSort([](const FPSSnapshotFrame& A, const FPSSnapshotFrame& B) { return A.Time < B.Time; });

    const FPSReplayTuning& PlayTuning = GetTuning();
    RateIndex = 0;
    PlaybackRate = PlayTuning.PlaybackRates.Num() > 0 ? PlayTuning.PlaybackRates[0] : 1.f;
    Playhead = 0.f;
    SincePose = 0.f;
    PoseCount = 0;
    bHasShownFrame = false;

    TakeField();
    HoldGame();
    ReplayState = EPSReplayState::Playing;
    ShowPlayhead();
    OnReplayStarted.Broadcast();
    return true;
}

void UPSReplaySubsystem::StopReplay()
{
    if (!IsReplaying())
    {
        return;
    }
    ReleaseField();
    if (UPSTelemetrySamplingSubsystem* Sampler = GetSampler())
    {
        Sampler->ClearReplayFrame();
    }
    ReleaseGame();
    ReplayState = EPSReplayState::Idle;
    OnReplayEnded.Broadcast();
}

void UPSReplaySubsystem::AdvanceReplay(float DeltaSeconds)
{
    if (!IsReplaying())
    {
        return;
    }

    // The live game stays paused under the replay, whoever unpaused it (a pause menu that was
    // open when the replay started, say). The replay unpauses it once it ends.
    UWorld* World = GetWorld();
    if (World && !World->IsPaused() && UGameplayStatics::SetGamePaused(World, true))
    {
        bPausedGame = true;
    }

    const float Step = FMath::Max(0.f, DeltaSeconds);
    SincePose += Step;
    if (ReplayState != EPSReplayState::Playing)
    {
        return;
    }

    Playhead += Step * PlaybackRate;
    if (Playhead >= GetDuration())
    {
        Playhead = GetDuration();
        ReplayState = EPSReplayState::Paused;
        ShowPlayhead();
        OnReplayReachedEnd.Broadcast();
        return;
    }

    if (PoseRateHz <= 0.f || SincePose + PSReplaySubsystemPrivate::TimeTolerance >= 1.f / PoseRateHz)
    {
        ShowPlayhead();
    }
}

float UPSReplaySubsystem::GetDuration() const
{
    return Clip.Frames.Num() > 0 ? Clip.Frames.Last().Time - Clip.Frames[0].Time : 0.f;
}

bool UPSReplaySubsystem::IsAtEnd() const
{
    return IsReplaying() && Playhead >= GetDuration() - PSReplaySubsystemPrivate::TimeTolerance;
}

bool UPSReplaySubsystem::GetShownFrame(FPSSnapshotFrame& OutFrame) const
{
    if (!bHasShownFrame)
    {
        return false;
    }
    OutFrame = ShownFrame;
    return true;
}

bool UPSReplaySubsystem::SampleClip(const FPSReplayRecording& InClip, float Time, FPSSnapshotFrame& OutFrame)
{
    const TArray<FPSSnapshotFrame>& Frames = InClip.Frames;
    if (Frames.Num() == 0)
    {
        return false;
    }
    const int32 Before = PSReplaySubsystemPrivate::FindFrameAtOrBefore(Frames, Time);
    if (Before == INDEX_NONE)
    {
        OutFrame = Frames[0];
        return true;
    }
    if (Before == Frames.Num() - 1 || Frames[Before].Time >= Time)
    {
        OutFrame = Frames[Before];
        return true;
    }

    const FPSSnapshotFrame& From = Frames[Before];
    const FPSSnapshotFrame& To = Frames[Before + 1];
    UPSTelemetrySamplingSubsystem::BlendFrames(From, To, (Time - From.Time) / (To.Time - From.Time), OutFrame);
    OutFrame.Time = Time;
    return true;
}

float UPSReplaySubsystem::GetClipStartTime() const
{
    return Clip.Frames.Num() > 0 ? Clip.Frames[0].Time : 0.f;
}

void UPSReplaySubsystem::ClampPlayhead()
{
    Playhead = FMath::Clamp(Playhead, 0.f, GetDuration());
}

void UPSReplaySubsystem::ShowPlayhead()
{
    FPSSnapshotFrame Frame;
    if (!SampleClip(Clip, GetClipStartTime() + Playhead, Frame))
    {
        return;
    }

    for (const FPSPawnSnapshot& Snapshot : Frame.Pawns)
    {
        const int32* Index = PosedPawnIndexById.Find(Snapshot.PlayerId);
        APSPlayerPawn* Pawn = Index ? Cast<APSPlayerPawn>(PosedPawns[*Index].Actor.Get()) : nullptr;
        if (!Pawn)
        {
            continue;
        }
        Pawn->SetActorLocationAndRotation(Snapshot.Location, FRotator(0.f, Snapshot.FacingYaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
        if (UFloatingPawnMovement* Movement = Pawn->GetFloatingMovementComponent())
        {
            Movement->Velocity = Snapshot.Velocity;
        }
    }
    AActor* Ball = bHasPosedBall ? PosedBall.Actor.Get() : nullptr;
    if (Ball && Frame.bBallSampled)
    {
        Ball->SetActorLocation(Frame.BallLocation, false, nullptr, ETeleportType::TeleportPhysics);
    }

    if (UPSTelemetrySamplingSubsystem* Sampler = GetSampler())
    {
        Sampler->SetReplayFrame(Frame);
    }
    ShownFrame = Frame;
    bHasShownFrame = true;
    SincePose = 0.f;
    ++PoseCount;
}

void UPSReplaySubsystem::TakeField()
{
    PosedPawns.Reset();
    PosedPawnIndexById.Reset();
    bHasPosedBall = false;

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
    {
        APSPlayerPawn* Pawn = *It;
        FPosedActor& Posed = PosedPawns.AddDefaulted_GetRef();
        Posed.Actor = Pawn;
        Posed.PlayerId = Pawn->GetAttributes().PlayerId;
        Posed.SavedTransform = Pawn->GetActorTransform();
        Posed.bSavedCollision = Pawn->GetActorEnableCollision();
        if (const UFloatingPawnMovement* Movement = Pawn->GetFloatingMovementComponent())
        {
            Posed.SavedVelocity = Movement->Velocity;
        }
        if (!Posed.PlayerId.IsNone())
        {
            PosedPawnIndexById.Add(Posed.PlayerId, PosedPawns.Num() - 1);
        }
        // A posed pawn must never tackle, catch or block anything.
        Pawn->SetActorEnableCollision(false);
    }
    for (TActorIterator<APSBall> It(World); It; ++It)
    {
        APSBall* Ball = *It;
        PosedBall = FPosedActor();
        PosedBall.Actor = Ball;
        PosedBall.SavedTransform = Ball->GetActorTransform();
        PosedBall.bSavedCollision = Ball->GetActorEnableCollision();
        if (const UProjectileMovementComponent* Flight = Ball->GetProjectileMovement())
        {
            PosedBall.SavedVelocity = Flight->Velocity;
        }
        Ball->SetActorEnableCollision(false);
        bHasPosedBall = true;
        break;
    }
}

void UPSReplaySubsystem::ReleaseField()
{
    for (const FPosedActor& Posed : PosedPawns)
    {
        APSPlayerPawn* Pawn = Cast<APSPlayerPawn>(Posed.Actor.Get());
        if (!Pawn)
        {
            continue;
        }
        Pawn->SetActorTransform(Posed.SavedTransform, false, nullptr, ETeleportType::TeleportPhysics);
        if (UFloatingPawnMovement* Movement = Pawn->GetFloatingMovementComponent())
        {
            Movement->Velocity = Posed.SavedVelocity;
        }
        Pawn->SetActorEnableCollision(Posed.bSavedCollision);
    }
    // After the pawns: a carried ball moved with its carrier and goes back on top of him.
    if (APSBall* Ball = bHasPosedBall ? Cast<APSBall>(PosedBall.Actor.Get()) : nullptr)
    {
        Ball->SetActorTransform(PosedBall.SavedTransform, false, nullptr, ETeleportType::TeleportPhysics);
        if (UProjectileMovementComponent* Flight = Ball->GetProjectileMovement())
        {
            Flight->Velocity = PosedBall.SavedVelocity;
        }
        Ball->SetActorEnableCollision(PosedBall.bSavedCollision);
    }
    PosedPawns.Reset();
    PosedPawnIndexById.Reset();
    bHasPosedBall = false;
}

void UPSReplaySubsystem::HoldGame()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    bPausedGame = !World->IsPaused() && UGameplayStatics::SetGamePaused(World, true);

    // The broadcast camera frames the replay through the pause.
    HeldCameras.Reset();
    for (TActorIterator<APSBroadcastCamera> It(World); It; ++It)
    {
        APSBroadcastCamera* Camera = *It;
        HeldCameras.Emplace(Camera, static_cast<bool>(Camera->PrimaryActorTick.bTickEvenWhenPaused));
        Camera->SetTickableWhenPaused(true);
    }
}

void UPSReplaySubsystem::ReleaseGame()
{
    for (const TPair<TWeakObjectPtr<AActor>, bool>& Held : HeldCameras)
    {
        if (AActor* Camera = Held.Key.Get())
        {
            Camera->SetTickableWhenPaused(Held.Value);
        }
    }
    HeldCameras.Reset();

    UWorld* World = GetWorld();
    if (bPausedGame && World)
    {
        UGameplayStatics::SetGamePaused(World, false);
    }
    bPausedGame = false;
}

// --- Transport ---------------------------------------------------------------------------

void UPSReplaySubsystem::SetPaused(bool bPaused)
{
    if (!IsReplaying())
    {
        return;
    }
    if (bPaused)
    {
        ReplayState = EPSReplayState::Paused;
        return;
    }
    if (IsAtEnd())
    {
        Playhead = 0.f;
        ShowPlayhead();
    }
    ReplayState = EPSReplayState::Playing;
}

void UPSReplaySubsystem::TogglePause()
{
    SetPaused(ReplayState == EPSReplayState::Playing);
}

void UPSReplaySubsystem::SetPlaybackRate(float Rate)
{
    if (Rate > 0.f)
    {
        PlaybackRate = Rate;
    }
}

float UPSReplaySubsystem::CycleSlowMotion()
{
    const TArray<float>& Rates = GetTuning().PlaybackRates;
    if (Rates.Num() > 0)
    {
        RateIndex = (RateIndex + 1) % Rates.Num();
        PlaybackRate = Rates[RateIndex];
    }
    return PlaybackRate;
}

void UPSReplaySubsystem::StepFrames(int32 Frames)
{
    if (!IsReplaying() || Clip.Frames.Num() == 0)
    {
        return;
    }
    ReplayState = EPSReplayState::Paused;

    using namespace PSReplaySubsystemPrivate;
    const float Start = GetClipStartTime();
    for (int32 Step = 0; Step < FMath::Abs(Frames); ++Step)
    {
        const float Now = Start + Playhead;
        int32 Target = INDEX_NONE;
        if (Frames > 0)
        {
            // The first frame after the playhead.
            const int32 AtOrBefore = FindFrameAtOrBefore(Clip.Frames, Now + TimeTolerance);
            Target = AtOrBefore + 1 < Clip.Frames.Num() ? AtOrBefore + 1 : INDEX_NONE;
        }
        else
        {
            // The last frame before the playhead.
            Target = FindFrameAtOrBefore(Clip.Frames, Now - TimeTolerance);
        }
        if (Target == INDEX_NONE)
        {
            break;
        }
        Playhead = Clip.Frames[Target].Time - Start;
    }
    ClampPlayhead();
    ShowPlayhead();
}

void UPSReplaySubsystem::Scrub(float Seconds)
{
    if (!IsReplaying())
    {
        return;
    }
    ReplayState = EPSReplayState::Paused;
    SetPlayhead(Playhead + Seconds);
}

void UPSReplaySubsystem::SetPlayhead(float Seconds)
{
    if (!IsReplaying())
    {
        return;
    }
    Playhead = Seconds;
    ClampPlayhead();
    ShowPlayhead();
}

void UPSReplaySubsystem::Restart()
{
    if (!IsReplaying())
    {
        return;
    }
    Playhead = 0.f;
    ReplayState = EPSReplayState::Playing;
    ShowPlayhead();
}

// --- Persistence -------------------------------------------------------------------------

FString UPSReplaySubsystem::GetDefaultSaveDirectory()
{
    return FPaths::ProjectSavedDir() / TEXT("Replays");
}

bool UPSReplaySubsystem::SaveClip(const FPSReplayRecording& InClip, const FString& Name, const FString& Directory, FString& OutFilePath)
{
    OutFilePath.Reset();
    if (InClip.Frames.Num() == 0)
    {
        return false;
    }

    const FString SaveDirectory = Directory.IsEmpty() ? GetDefaultSaveDirectory() : Directory;
    FString FileName = FPaths::MakeValidFileName(Name);
    if (FileName.IsEmpty())
    {
        FileName = FString::Printf(TEXT("Replay_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
    }

    const FString Json = UPSReplayFormat::SerializeToJson(ThinFrames(InClip, GetTuning().SaveFrameRateHz));
    if (Json.IsEmpty())
    {
        return false;
    }
    IFileManager::Get().MakeDirectory(*SaveDirectory, true);
    const FString FilePath = SaveDirectory / (FileName + TEXT(".json"));
    if (!FFileHelper::SaveStringToFile(Json, *FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSReplaySubsystem: Could not write %s."), *FilePath);
        return false;
    }
    OutFilePath = FilePath;
    return true;
}

bool UPSReplaySubsystem::LoadClip(const FString& FilePath, FPSReplayRecording& OutClip)
{
    FString Json;
    return FFileHelper::LoadFileToString(Json, *FilePath) && UPSReplayFormat::DeserializeFromJson(Json, OutClip);
}

TArray<FString> UPSReplaySubsystem::ListSavedClips(const FString& Directory)
{
    const FString SaveDirectory = Directory.IsEmpty() ? GetDefaultSaveDirectory() : Directory;
    TArray<FString> FileNames;
    IFileManager::Get().FindFiles(FileNames, *(SaveDirectory / TEXT("*.json")), true, false);
    FileNames.Sort();

    TArray<FString> Paths;
    for (const FString& FileName : FileNames)
    {
        Paths.Add(SaveDirectory / FileName);
    }
    return Paths;
}

FPSReplayRecording UPSReplaySubsystem::ThinFrames(const FPSReplayRecording& InClip, float MaxRateHz)
{
    FPSReplayRecording Thinned = InClip;
    const int32 Count = InClip.Frames.Num();
    if (!(MaxRateHz > 0.f) || Count <= 2)
    {
        return Thinned;
    }

    const float MinGap = 1.f / MaxRateHz;
    Thinned.Frames.Reset();
    float LastKeptTime = 0.f;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FPSSnapshotFrame& Frame = InClip.Frames[Index];
        const bool bAlwaysKept = Index == 0 || Index == Count - 1 || Frame.bKeyframe;
        if (bAlwaysKept || Frame.Time - LastKeptTime + PSReplaySubsystemPrivate::TimeTolerance >= MinGap)
        {
            Thinned.Frames.Add(Frame);
            LastKeptTime = Frame.Time;
        }
    }
    AssignEventTicks(Thinned);
    return Thinned;
}

void UPSReplaySubsystem::AssignEventTicks(FPSReplayRecording& InOutClip)
{
    for (FPSReplayEventRecord& Event : InOutClip.Events)
    {
        Event.TickIndex = FMath::Max(0, PSReplaySubsystemPrivate::FindFrameAtOrBefore(InOutClip.Frames, Event.TimestampSeconds + PSReplaySubsystemPrivate::TimeTolerance));
    }
}
