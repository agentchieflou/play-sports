#include "PSReplaySubsystem.h"
#include "PSBall.h"
#include "PSBroadcastCamera.h"
#include "PSCameraAll22Component.h"
#include "PSCameraDirectorComponent.h"
#include "PSDataIngestion.h"
#include "PSGameStateEvents.h"
#include "PSMenuComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSTelemetrySamplingSubsystem.h"
#include "PSUIAccessibilitySubsystem.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Camera/CameraComponent.h"
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

const FName UPSReplaySubsystem::DirectorCamera(TEXT("Director"));
const FName UPSReplaySubsystem::SkycamCamera(TEXT("Skycam"));
const FName UPSReplaySubsystem::FreeCamera(TEXT("Free"));

UPSReplaySubsystem::UPSReplaySubsystem()
{
    ReplayContextId = TEXT("Replay");
    PlayPauseActionId = TEXT("ReplayPlayPause");
    SlowMotionActionId = TEXT("ReplaySlowMotion");
    StepBackActionId = TEXT("ReplayStepBack");
    StepForwardActionId = TEXT("ReplayStepForward");
    ScrubBackActionId = TEXT("ReplayScrubBack");
    ScrubForwardActionId = TEXT("ReplayScrubForward");
    CameraActionId = TEXT("ReplayCamera");
    ExitActionId = TEXT("ReplayExit");
}

void UPSReplaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ApplyPlatformTier(PSPlatformTiers::GetActiveTier());

    // The automatic replays watch the plays go by on the bus.
    if (UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>())
    {
        Bus->OnEventRecordedMC.AddUObject(this, &UPSReplaySubsystem::HandleEventRecorded);
        BoundBus = Bus;
    }
}

void UPSReplaySubsystem::Deinitialize()
{
    StopReplay();
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnEventRecordedMC.RemoveAll(this);
    }
    BoundBus.Reset();
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

TArray<FString> UPSReplaySubsystem::ValidateTuning(const FPSReplayTuning& InTuning, const FPSAll22CameraTuning* All22)
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
    if (!(InTuning.ScrubSecondsPerSecond > 0.f))
    {
        Problems.Add(TEXT("ScrubSecondsPerSecond must be above 0"));
    }

    if (InTuning.PlaybackRates.Num() == 0)
    {
        Problems.Add(TEXT("PlaybackRates needs at least one speed"));
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
    if (InTuning.PlaybackRates.Num() > 0 && InTuning.PlaybackRates[0] != 1.f)
    {
        Problems.Add(TEXT("PlaybackRates[0] must be 1, the speed a replay starts at"));
    }

    // Cameras: the three named ones, or all-22 rigs.
    auto IsRig = [All22](FName Name)
    {
        return Name != DirectorCamera && Name != SkycamCamera && Name != FreeCamera
            && (!All22 || All22->All22Rigs.ContainsByPredicate([Name](const FPSAll22RigDef& Rig) { return Rig.RigId == Name; }));
    };
    if (InTuning.Cameras.Num() == 0)
    {
        Problems.Add(TEXT("Cameras needs at least one camera"));
    }
    for (int32 Index = 0; Index < InTuning.Cameras.Num(); ++Index)
    {
        const FName Name = InTuning.Cameras[Index];
        if (Name.IsNone())
        {
            Problems.Add(FString::Printf(TEXT("Cameras[%d] is empty"), Index));
        }
        else if (InTuning.Cameras.IndexOfByKey(Name) != Index)
        {
            Problems.Add(FString::Printf(TEXT("Cameras[%d] (%s) is listed twice"), Index, *Name.ToString()));
        }
        else if (Name != DirectorCamera && Name != SkycamCamera && Name != FreeCamera && !IsRig(Name))
        {
            Problems.Add(FString::Printf(TEXT("Cameras[%d] (%s) is neither Director, Skycam, Free nor an all-22 rig"), Index, *Name.ToString()));
        }
    }
    if (!InTuning.Cameras.Contains(InTuning.ReducedMotionCamera) || !IsRig(InTuning.ReducedMotionCamera))
    {
        Problems.Add(TEXT("ReducedMotionCamera must be a still all-22 rig among the Cameras"));
    }

    if (!(InTuning.FreeCamMinDistanceCm > 0.f) || InTuning.FreeCamDistanceCm < InTuning.FreeCamMinDistanceCm
        || InTuning.FreeCamDistanceCm > InTuning.FreeCamMaxDistanceCm)
    {
        Problems.Add(TEXT("The free camera's distances must be 0 < FreeCamMinDistanceCm <= FreeCamDistanceCm <= FreeCamMaxDistanceCm"));
    }
    if (!(InTuning.FreeCamPitchDegrees > 0.f) || !(InTuning.FreeCamPitchDegrees < 90.f))
    {
        Problems.Add(TEXT("FreeCamPitchDegrees must be above 0 and below 90"));
    }
    if (!(InTuning.FreeCamOrbitDegreesPerSecond > 0.f) || !(InTuning.FreeCamZoomCmPerSecond > 0.f))
    {
        Problems.Add(TEXT("FreeCamOrbitDegreesPerSecond and FreeCamZoomCmPerSecond must be above 0"));
    }

    if (InTuning.AutoReplayDelaySeconds < 0.f || InTuning.AutoReplayHoldSeconds < 0.f)
    {
        Problems.Add(TEXT("AutoReplayDelaySeconds and AutoReplayHoldSeconds must be 0 or more"));
    }
    for (int32 Index = 0; Index < InTuning.AutoReplays.Num(); ++Index)
    {
        const FPSAutoReplayRule& Rule = InTuning.AutoReplays[Index];
        const EPSReplayTrigger RuleTrigger = Rule.Trigger;
        if (InTuning.AutoReplays.IndexOfByPredicate([RuleTrigger](const FPSAutoReplayRule& Other) { return Other.Trigger == RuleTrigger; }) != Index)
        {
            Problems.Add(FString::Printf(TEXT("AutoReplays[%d]: its trigger already has a rule"), Index));
        }
        if (Rule.Shot == EPSDirectorShot::None)
        {
            Problems.Add(FString::Printf(TEXT("AutoReplays[%d]: needs a Shot"), Index));
        }
        if (!(Rule.PlaybackRate > 0.f) || Rule.PlaybackRate > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("AutoReplays[%d]: PlaybackRate must be above 0 and at most 1"), Index));
        }
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

    bAutoReplay = false;
    EndHoldRemaining = -1.f;
    ScrubDirection = 0;

    TakeField();
    HoldGame();
    BindControllers();
    ReplayState = EPSReplayState::Playing;
    ShowPlayhead();

    // With Reduced motion on, every replay opens on the still rig (Epic 103.5).
    const FName OpeningCamera = IsReducedMotion() ? PlayTuning.ReducedMotionCamera
        : (PlayTuning.Cameras.Num() > 0 ? PlayTuning.Cameras[0] : DirectorCamera);
    SetReplayCamera(OpeningCamera);
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
    // The game before the controllers: a pause menu still open keeps it paused.
    ReleaseGame();
    UnbindControllers();
    ReplayState = EPSReplayState::Idle;
    bAutoReplay = false;
    EndHoldRemaining = -1.f;
    ScrubDirection = 0;
    ActiveCamera = NAME_None;
    OnReplayEnded.Broadcast();
}

void UPSReplaySubsystem::AdvanceReplay(float DeltaSeconds)
{
    if (bHeld)
    {
        return;
    }
    const float Step = FMath::Max(0.f, DeltaSeconds);
    if (!IsReplaying())
    {
        // A play worth it is replayed once its countdown runs out.
        if (AutoReplayCountdown >= 0.f)
        {
            AutoReplayCountdown -= Step;
            if (AutoReplayCountdown <= 0.f)
            {
                AutoReplayCountdown = -1.f;
                bAutoReplayDone = true;
                StartAutoReplay(PlayTrigger);
            }
        }
        return;
    }

    // The live game stays paused under the replay, whoever unpaused it (a pause menu that was
    // open when the replay started, say). The replay unpauses it once it ends.
    UWorld* World = GetWorld();
    if (World && !World->IsPaused() && UGameplayStatics::SetGamePaused(World, true))
    {
        bPausedGame = true;
    }

    // The pause menu over a replay holds it.
    if (IsMenuOpen())
    {
        return;
    }

    SincePose += Step;
    if (ActiveCamera == FreeCamera)
    {
        UpdateFreeCamera(Step);
    }
    if (ScrubDirection != 0)
    {
        Scrub(ScrubDirection * GetTuning().ScrubSecondsPerSecond * Step);
    }

    if (ReplayState != EPSReplayState::Playing)
    {
        // An automatic replay gives the game back a moment after its end.
        if (bAutoReplay && EndHoldRemaining >= 0.f)
        {
            EndHoldRemaining -= Step;
            if (EndHoldRemaining <= 0.f)
            {
                StopReplay();
            }
        }
        return;
    }

    Playhead += Step * PlaybackRate;
    if (Playhead >= GetDuration())
    {
        Playhead = GetDuration();
        ReplayState = EPSReplayState::Paused;
        ShowPlayhead();
        if (bAutoReplay)
        {
            EndHoldRemaining = GetTuning().AutoReplayHoldSeconds;
        }
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

    // The broadcast camera frames the replay through the pause; how it was is kept to give back.
    HeldCameras.Reset();
    for (TActorIterator<APSBroadcastCamera> It(World); It; ++It)
    {
        APSBroadcastCamera* Camera = *It;
        FHeldCamera& Held = HeldCameras.AddDefaulted_GetRef();
        Held.Camera = Camera;
        Held.bTickedWhenPaused = static_cast<bool>(Camera->PrimaryActorTick.bTickEvenWhenPaused);
        Held.bFreeCam = Camera->bIsFreeCam;
        Held.bFollowing = Camera->bIsFollowing;
        Held.SavedTransform = Camera->GetActorTransform();
        if (const UCameraComponent* View = Camera->GetCameraComponent())
        {
            Held.SavedFieldOfView = View->FieldOfView;
        }
        if (const UPSCameraAll22Component* All22 = Camera->GetAll22Component())
        {
            Held.FilmRig = All22->GetActiveRigId();
        }
        if (UPSCameraDirectorComponent* Director = Camera->GetDirectorComponent())
        {
            Held.bDirectorEnabled = Director->IsDirectorEnabled();
            Held.DirectorShot = Director->GetCurrentShot();
        }
        Camera->SetTickableWhenPaused(true);
    }
}

void UPSReplaySubsystem::ReleaseGame()
{
    for (const FHeldCamera& Held : HeldCameras)
    {
        APSBroadcastCamera* Camera = Held.Camera.Get();
        if (!Camera)
        {
            continue;
        }
        UPSCameraAll22Component* All22 = Camera->GetAll22Component();
        if (All22)
        {
            All22->SetFilmView(NAME_None);
        }
        Camera->SetActorTransform(Held.SavedTransform);
        if (UCameraComponent* View = Camera->GetCameraComponent())
        {
            View->SetFieldOfView(Held.SavedFieldOfView);
        }
        Camera->bIsFreeCam = Held.bFreeCam;
        Camera->bIsFollowing = Held.bFollowing;
        if (All22 && !Held.FilmRig.IsNone())
        {
            All22->SetFilmView(Held.FilmRig);
        }
        if (UPSCameraDirectorComponent* Director = Camera->GetDirectorComponent())
        {
            Director->SetDirectorEnabled(Held.bDirectorEnabled);
            if (Held.DirectorShot != EPSDirectorShot::None && Director->GetCurrentShot() != Held.DirectorShot)
            {
                Director->CutNow(Held.DirectorShot);
            }
        }
        Camera->SetTickableWhenPaused(Held.bTickedWhenPaused);
    }
    HeldCameras.Reset();

    // A pause menu still open keeps the game paused; its Resume unpauses it.
    UWorld* World = GetWorld();
    if (bPausedGame && World && !IsMenuOpen())
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

void UPSReplaySubsystem::SetHeld(bool bInHeld)
{
    bHeld = bInHeld;
    if (bHeld)
    {
        ScrubDirection = 0;
    }
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

// --- Cameras -----------------------------------------------------------------------------

APSBroadcastCamera* UPSReplaySubsystem::GetReplayBroadcastCamera() const
{
    for (const FHeldCamera& Held : HeldCameras)
    {
        if (APSBroadcastCamera* Camera = Held.Camera.Get())
        {
            return Camera;
        }
    }
    return nullptr;
}

bool UPSReplaySubsystem::SetReplayCamera(FName CameraName)
{
    const FPSReplayTuning& CameraTuning = GetTuning();
    const int32 Index = CameraTuning.Cameras.IndexOfByKey(CameraName);
    if (!IsReplaying() || Index == INDEX_NONE)
    {
        return false;
    }
    CameraIndex = Index;
    ActiveCamera = CameraName;

    APSBroadcastCamera* Camera = GetReplayBroadcastCamera();
    if (!Camera)
    {
        return false;
    }
    const bool bFree = CameraName == FreeCamera;
    const bool bRig = !bFree && CameraName != DirectorCamera && CameraName != SkycamCamera;

    // The broadcast camera's own modes: film view for a rig, the director for the director
    // and the skycam, and the free camera, which nothing drives but the replay.
    Camera->bIsFreeCam = bFree;
    if (UPSCameraAll22Component* All22 = Camera->GetAll22Component())
    {
        All22->SetFilmView(bRig ? CameraName : NAME_None);
    }
    UPSCameraDirectorComponent* Director = Camera->GetDirectorComponent();
    if (Director && !bRig && !bFree)
    {
        Director->SetDirectorEnabled(true);
        if (CameraName == SkycamCamera)
        {
            Director->CutNow(EPSDirectorShot::Skycam);
        }
    }
    if (bFree)
    {
        FreeCamYaw = static_cast<float>(Camera->GetActorRotation().Yaw);
        FreeCamDistance = CameraTuning.FreeCamDistanceCm;
        UpdateFreeCamera(0.f);
    }
    return true;
}

FName UPSReplaySubsystem::CycleReplayCamera()
{
    const TArray<FName>& Cameras = GetTuning().Cameras;
    if (IsReplaying() && Cameras.Num() > 0)
    {
        SetReplayCamera(Cameras[(CameraIndex + 1) % Cameras.Num()]);
    }
    return ActiveCamera;
}

void UPSReplaySubsystem::UpdateFreeCamera(float DeltaSeconds)
{
    APSBroadcastCamera* Camera = GetReplayBroadcastCamera();
    if (!Camera || !bHasShownFrame)
    {
        return;
    }

    FVector2D Move = FVector2D::ZeroVector;
    for (const TWeakObjectPtr<APSPlayerController>& Bound : BoundControllers)
    {
        if (const APSPlayerController* Controller = Bound.Get())
        {
            Move = Controller->GetMoveInput();
            break;
        }
    }

    // The stick circles the ball (left and right) and closes in or backs off (forward, back).
    const FPSReplayTuning& CameraTuning = GetTuning();
    const float Orbit = static_cast<float>(Move.X) * CameraTuning.FreeCamOrbitDegreesPerSecond * DeltaSeconds;
    const float Zoom = static_cast<float>(Move.Y) * CameraTuning.FreeCamZoomCmPerSecond * DeltaSeconds;
    FreeCamYaw = static_cast<float>(FRotator::NormalizeAxis(static_cast<double>(FreeCamYaw + Orbit)));
    FreeCamDistance = FMath::Clamp(FreeCamDistance - Zoom, CameraTuning.FreeCamMinDistanceCm, CameraTuning.FreeCamMaxDistanceCm);

    FVector Target = ShownFrame.BallLocation;
    if (!ShownFrame.bBallSampled && ShownFrame.Pawns.Num() > 0)
    {
        Target = FVector::ZeroVector;
        for (const FPSPawnSnapshot& Snapshot : ShownFrame.Pawns)
        {
            Target += Snapshot.Location;
        }
        Target /= ShownFrame.Pawns.Num();
    }
    const FRotator View(-CameraTuning.FreeCamPitchDegrees, FreeCamYaw, 0.f);
    Camera->SetActorLocationAndRotation(Target - View.Vector() * FreeCamDistance, View);
}

// --- Automatic replays -------------------------------------------------------------------

bool UPSReplaySubsystem::StartAutoReplay(EPSReplayTrigger Trigger)
{
    FPSReplayRecording AutoClip;
    if (!CaptureLastPlay(AutoClip) || !StartReplay(AutoClip))
    {
        return false;
    }
    bAutoReplay = true;

    const FPSReplayTuning& AutoTuning = GetTuning();
    const FPSAutoReplayRule* Rule = AutoTuning.AutoReplays.FindByPredicate([Trigger](const FPSAutoReplayRule& Candidate) { return Candidate.Trigger == Trigger; });
    if (Rule)
    {
        SetPlaybackRate(Rule->PlaybackRate);
    }
    if (Rule)
    {
        CutToDirectorShot(Rule->Shot);
    }
    return true;
}

bool UPSReplaySubsystem::CutToDirectorShot(EPSDirectorShot Shot)
{
    // With Reduced motion on it stays on the still rig StartReplay opened on.
    if (IsReducedMotion() || !SetReplayCamera(DirectorCamera))
    {
        return false;
    }
    APSBroadcastCamera* Camera = GetReplayBroadcastCamera();
    UPSCameraDirectorComponent* Director = Camera ? Camera->GetDirectorComponent() : nullptr;
    return Director && Director->CutNow(Shot);
}

void UPSReplaySubsystem::HandleEventRecorded(const FPSTelemetryEvent& Event)
{
    if (IsReplaying())
    {
        return;
    }

    switch (Event.EventType)
    {
    case EPSTelemetryEventType::Snap:
        bTrackingPlay = true;
        bWhistleBlown = false;
        bPlayHasTrigger = false;
        bAutoReplayDone = false;
        AutoReplayCountdown = -1.f;
        ScoreAtSnap = LastScoreTotal;
        bHomeBallAtSnap = bLastHomeBall;
        bKickPlay = bLastKickoff;
        break;
    case EPSTelemetryEventType::Score:
        NoteTrigger(EPSReplayTrigger::Score);
        break;
    case EPSTelemetryEventType::Catch:
    {
        FPSTelemetryCatchEvent Catch;
        if (FJsonObjectConverter::JsonObjectStringToUStruct(Event.PayloadJson, &Catch, 0, 0) && Catch.bIsInterception)
        {
            NoteTrigger(EPSReplayTrigger::Turnover);
        }
        break;
    }
    case EPSTelemetryEventType::Fumble:
    {
        FPSTelemetryFumbleEvent Fumble;
        if (FJsonObjectConverter::JsonObjectStringToUStruct(Event.PayloadJson, &Fumble, 0, 0) && Fumble.bIsTurnover)
        {
            NoteTrigger(EPSReplayTrigger::Turnover);
        }
        break;
    }
    case EPSTelemetryEventType::PhaseChange:
        if (bTrackingPlay && PSReplaySubsystemPrivate::IsWhistle(Event))
        {
            bWhistleBlown = true;
            ArmAutoReplay();
        }
        break;
    case EPSTelemetryEventType::GameState:
    {
        // The play simulation's game state is the authority on the score and the ball.
        FPSTelemetryGameStateEvent GameState;
        if (!FJsonObjectConverter::JsonObjectStringToUStruct(Event.PayloadJson, &GameState, 0, 0))
        {
            break;
        }
        const int32 ScoreTotal = GameState.HomeScore + GameState.AwayScore;
        if (bTrackingPlay)
        {
            if (GameState.Phase == TEXT("Kickoff") || GameState.Phase == TEXT("Punt") || GameState.Phase == TEXT("FieldGoal"))
            {
                bKickPlay = true;
            }
            if (GameState.Phase == TEXT("Scoring"))
            {
                bWhistleBlown = true;
            }
            if (ScoreTotal > ScoreAtSnap)
            {
                NoteTrigger(EPSReplayTrigger::Score);
            }
            else if (bWhistleBlown && !bKickPlay && GameState.bHomeHasPossession != bHomeBallAtSnap)
            {
                NoteTrigger(EPSReplayTrigger::Turnover);
            }
            ArmAutoReplay();
        }
        LastScoreTotal = ScoreTotal;
        bLastHomeBall = GameState.bHomeHasPossession;
        bLastKickoff = GameState.bKickoff;
        break;
    }
    default:
        break;
    }
}

void UPSReplaySubsystem::NoteTrigger(EPSReplayTrigger Trigger)
{
    if (!bTrackingPlay || bAutoReplayDone)
    {
        return;
    }
    if (!bPlayHasTrigger || Trigger == EPSReplayTrigger::Score)
    {
        PlayTrigger = Trigger;
        bPlayHasTrigger = true;
    }
    ArmAutoReplay();
}

void UPSReplaySubsystem::ArmAutoReplay()
{
    if (bPlayHasTrigger && bWhistleBlown && !bAutoReplayDone && AutoReplayCountdown < 0.f && GetTuning().bAutoReplay)
    {
        AutoReplayCountdown = GetTuning().AutoReplayDelaySeconds;
    }
}

bool UPSReplaySubsystem::IsReducedMotion() const
{
    const UWorld* World = GetWorld();
    UPSUIAccessibilitySubsystem* Accessibility = World ? World->GetSubsystem<UPSUIAccessibilitySubsystem>() : nullptr;
    return Accessibility && Accessibility->IsReducedMotion();
}

// --- Controls ----------------------------------------------------------------------------

void UPSReplaySubsystem::BindControllers()
{
    UnbindControllers();
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    for (TActorIterator<APSPlayerController> It(World); It; ++It)
    {
        APSPlayerController* Controller = *It;
        Controller->SetModeContextActive(ReplayContextId, true);
        Controller->OnCatalogActionStarted.AddUniqueDynamic(this, &UPSReplaySubsystem::HandleActionStarted);
        Controller->OnCatalogActionCompleted.AddUniqueDynamic(this, &UPSReplaySubsystem::HandleActionCompleted);
        BoundControllers.Add(Controller);
    }
}

void UPSReplaySubsystem::UnbindControllers()
{
    for (const TWeakObjectPtr<APSPlayerController>& Bound : BoundControllers)
    {
        if (APSPlayerController* Controller = Bound.Get())
        {
            Controller->OnCatalogActionStarted.RemoveDynamic(this, &UPSReplaySubsystem::HandleActionStarted);
            Controller->OnCatalogActionCompleted.RemoveDynamic(this, &UPSReplaySubsystem::HandleActionCompleted);
            Controller->SetModeContextActive(ReplayContextId, false);
        }
    }
    BoundControllers.Reset();
}

bool UPSReplaySubsystem::IsMenuOpen() const
{
    for (const TWeakObjectPtr<APSPlayerController>& Bound : BoundControllers)
    {
        const APSPlayerController* Controller = Bound.Get();
        const UPSMenuComponent* Menu = Controller ? Controller->GetMenuComponent() : nullptr;
        if (Menu && Menu->IsMenuOpen())
        {
            return true;
        }
    }
    return false;
}

void UPSReplaySubsystem::HandleActionStarted(FName ActionId)
{
    if (!IsReplaying() || bHeld)
    {
        return;
    }
    if (ActionId == ExitActionId)
    {
        StopReplay();
        return;
    }

    bool bHandled = true;
    if (ActionId == PlayPauseActionId)
    {
        TogglePause();
    }
    else if (ActionId == SlowMotionActionId)
    {
        CycleSlowMotion();
    }
    else if (ActionId == StepBackActionId)
    {
        StepFrames(-1);
    }
    else if (ActionId == StepForwardActionId)
    {
        StepFrames(1);
    }
    else if (ActionId == ScrubBackActionId)
    {
        ScrubDirection = -1;
    }
    else if (ActionId == ScrubForwardActionId)
    {
        ScrubDirection = 1;
    }
    else if (ActionId == CameraActionId)
    {
        CycleReplayCamera();
    }
    else
    {
        bHandled = false;
    }

    // The viewer has the controls: an automatic replay waits for him to leave it.
    if (bHandled)
    {
        bAutoReplay = false;
        EndHoldRemaining = -1.f;
    }
}

void UPSReplaySubsystem::HandleActionCompleted(FName ActionId)
{
    if ((ActionId == ScrubBackActionId && ScrubDirection < 0) || (ActionId == ScrubForwardActionId && ScrubDirection > 0))
    {
        ScrubDirection = 0;
    }
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
