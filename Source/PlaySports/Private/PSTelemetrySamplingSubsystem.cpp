#include "PSTelemetrySamplingSubsystem.h"
#include "PSPerfBudget.h"
#include "PSBall.h"
#include "PSDataIngestion.h"
#include "PSPlayerPawn.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("PS Telemetry Sampling"), STATGROUP_PSTelemetrySampling, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Capture frame"), STAT_PSTelemetryCaptureFrame, STATGROUP_PSTelemetrySampling);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Pawns per frame"), STAT_PSTelemetryPawnsPerFrame, STATGROUP_PSTelemetrySampling);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Degrade level"), STAT_PSTelemetryDegradeLevel, STATGROUP_PSTelemetrySampling);

namespace PSTelemetrySamplingPrivate
{
    /** Due-time slack, so float steps that should land exactly on the interval do. */
    constexpr double IntervalTolerance = 1.0e-4;

    /** Weight of the newest cost in the moving average. */
    constexpr float CostAverageWeight = 0.1f;

    /** Above this, ValidateTuning refuses the ring size (memory). */
    constexpr int32 MaxRingFrames = 10000;

    constexpr int32 MaxAllowedDegradeLevel = 8;

    /** The snapshot of the same pawn in Frame: by pawn, else by PlayerId. */
    const FPSPawnSnapshot* FindSameSubject(const FPSSnapshotFrame& Frame, const FPSPawnSnapshot& Subject)
    {
        if (!Subject.Pawn.IsExplicitlyNull())
        {
            if (const FPSPawnSnapshot* ByPawn = Frame.Pawns.FindByPredicate([&Subject](const FPSPawnSnapshot& Candidate) { return Candidate.Pawn == Subject.Pawn; }))
            {
                return ByPawn;
            }
        }
        return Subject.PlayerId.IsNone() ? nullptr : Frame.FindPawn(Subject.PlayerId);
    }

    /** The ball's velocity as it moves: its carrier's while carried, its projectile movement's
     *  in flight. The actor's own velocity only catches up on the ball's next tick, so at a
     *  throw's keyframe it would still read the carry (Epic 32's arcs start from this). */
    FVector BallVelocity(const APSBall& Ball)
    {
        if (const AActor* Carrier = Ball.GetAttachParentActor())
        {
            return Carrier->GetVelocity();
        }
        const UProjectileMovementComponent* Flight = Ball.GetProjectileMovement();
        return Flight && Flight->IsActive() ? Flight->Velocity : FVector::ZeroVector;
    }

    /** Offense before defense, then by PlayerId. */
    bool SortsBefore(const APSPlayerPawn& A, FName AId, const APSPlayerPawn& B, FName BId)
    {
        if (A.TeamSide != B.TeamSide)
        {
            return A.TeamSide == EPSTeamSide::Offense;
        }
        return AId.Compare(BId) < 0;
    }
}

void UPSTelemetrySamplingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    LoadTuningFromJson(GetDefaultTuningPath());
    ApplyPlatformTier(PSPlatformTiers::GetActiveTier());

    if (UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>())
    {
        Bus->OnEventRecordedMC.AddUObject(this, &UPSTelemetrySamplingSubsystem::HandleEventRecorded);
        BoundBus = Bus;
    }

    if (UWorld* World = GetWorld())
    {
        ActorSpawnedHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UPSTelemetrySamplingSubsystem::HandleActorSpawned));
    }
}

void UPSTelemetrySamplingSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnEventRecordedMC.RemoveAll(this);
    }
    BoundBus.Reset();

    if (UWorld* World = GetWorld())
    {
        World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
    }
    ActorSpawnedHandle.Reset();

    Super::Deinitialize();
}

bool UPSTelemetrySamplingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSTelemetrySamplingSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    AdvanceTime(DeltaTime);
}

TStatId UPSTelemetrySamplingSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSTelemetrySamplingSubsystem, STATGROUP_Tickables);
}

FString UPSTelemetrySamplingSubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/telemetry_sampling.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSTelemetrySamplingSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSTelemetrySamplingTuning Loaded = Tuning;
    if (!Ingestion->LoadTelemetrySamplingTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSTelemetrySamplingSubsystem: Could not load sampling tuning from %s; keeping the current tuning."), *JsonFilePath);
        return false;
    }

    const TArray<FString> Problems = ValidateTuning(Loaded);
    if (Problems.Num() > 0)
    {
        for (const FString& Problem : Problems)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSTelemetrySamplingSubsystem: %s: %s"), *JsonFilePath, *Problem);
        }
        return false;
    }

    SetTuning(Loaded);
    return true;
}

void UPSTelemetrySamplingSubsystem::ApplyPlatformTier(const FPSPlatformTier& Tier)
{
    FPSTelemetrySamplingTuning NewTuning = Tuning;
    NewTuning.SampleRateHz = Tier.TelemetrySampleRateHz;
    NewTuning.SampleBudgetMs = Tier.TelemetrySampleBudgetMs;
    const TArray<FString> Problems = ValidateTuning(NewTuning);
    if (Problems.Num() > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSTelemetrySamplingSubsystem: tier '%s' gives an unusable sampling rate or budget (%s); keeping %.1f Hz."),
            *Tier.TierId.ToString(), *Problems[0], Tuning.SampleRateHz);
        return;
    }
    SetTuning(NewTuning);
}

void UPSTelemetrySamplingSubsystem::SetTuning(const FPSTelemetrySamplingTuning& NewTuning)
{
    Tuning = NewTuning;
    Tuning.SampleRateHz = FMath::Max(Tuning.SampleRateHz, KINDA_SMALL_NUMBER);
    Tuning.MaxDegradeLevel = FMath::Clamp(Tuning.MaxDegradeLevel, 0, PSTelemetrySamplingPrivate::MaxAllowedDegradeLevel);
    DegradeLevel = FMath::Min(DegradeLevel, Tuning.MaxDegradeLevel);
    OverBudgetStreak = 0;
    UnderBudgetStreak = 0;
    SinceLastSample = 0.0;
    ResizeRing(GetRingCapacity());
}

TArray<FString> UPSTelemetrySamplingSubsystem::ValidateTuning(const FPSTelemetrySamplingTuning& InTuning)
{
    TArray<FString> Problems;
    if (!(InTuning.SampleRateHz > 0.f))
    {
        Problems.Add(FString::Printf(TEXT("SampleRateHz (%g) must be above 0"), InTuning.SampleRateHz));
    }
    if (!(InTuning.HistorySeconds > 0.f))
    {
        Problems.Add(FString::Printf(TEXT("HistorySeconds (%g) must be above 0"), InTuning.HistorySeconds));
    }
    if (InTuning.SampleRateHz > 0.f && InTuning.HistorySeconds > 0.f
        && InTuning.SampleRateHz * InTuning.HistorySeconds > PSTelemetrySamplingPrivate::MaxRingFrames)
    {
        Problems.Add(FString::Printf(TEXT("SampleRateHz x HistorySeconds (%g) must be at most %d frames"),
            InTuning.SampleRateHz * InTuning.HistorySeconds, PSTelemetrySamplingPrivate::MaxRingFrames));
    }
    if (!(InTuning.SampleBudgetMs > 0.f))
    {
        Problems.Add(FString::Printf(TEXT("SampleBudgetMs (%g) must be above 0"), InTuning.SampleBudgetMs));
    }
    if (InTuning.DegradeAfterSamples < 1)
    {
        Problems.Add(FString::Printf(TEXT("DegradeAfterSamples (%d) must be 1 or more"), InTuning.DegradeAfterSamples));
    }
    if (InTuning.RecoverAfterSamples < 1)
    {
        Problems.Add(FString::Printf(TEXT("RecoverAfterSamples (%d) must be 1 or more"), InTuning.RecoverAfterSamples));
    }
    if (!(InTuning.RecoverBelowFraction > 0.f && InTuning.RecoverBelowFraction <= 1.f))
    {
        Problems.Add(FString::Printf(TEXT("RecoverBelowFraction (%g) must be above 0 and at most 1"), InTuning.RecoverBelowFraction));
    }
    if (InTuning.MaxDegradeLevel < 0 || InTuning.MaxDegradeLevel > PSTelemetrySamplingPrivate::MaxAllowedDegradeLevel)
    {
        Problems.Add(FString::Printf(TEXT("MaxDegradeLevel (%d) must be 0 to %d"), InTuning.MaxDegradeLevel, PSTelemetrySamplingPrivate::MaxAllowedDegradeLevel));
    }
    TSet<EPSTelemetryEventType> Seen;
    for (const EPSTelemetryEventType EventType : InTuning.KeyframeEvents)
    {
        bool bAlreadySeen = false;
        Seen.Add(EventType, &bAlreadySeen);
        if (bAlreadySeen)
        {
            Problems.Add(FString::Printf(TEXT("KeyframeEvents lists %s twice"), *UEnum::GetValueAsString(EventType)));
        }
    }
    return Problems;
}

void UPSTelemetrySamplingSubsystem::SetSampleRateHz(float RateHz)
{
    FPSTelemetrySamplingTuning NewTuning = Tuning;
    NewTuning.SampleRateHz = RateHz;
    SetTuning(NewTuning);
}

void UPSTelemetrySamplingSubsystem::SetSamplingEnabled(bool bEnabled)
{
    bSamplingEnabled = bEnabled;
    SinceLastSample = 0.0;
}

float UPSTelemetrySamplingSubsystem::GetEffectiveSampleRateHz() const
{
    return static_cast<float>(1.0 / GetEffectiveSampleInterval());
}

FPSTelemetrySamplingStats UPSTelemetrySamplingSubsystem::GetStats() const
{
    FPSTelemetrySamplingStats Out = Stats;
    Out.SampleBudgetMs = Tuning.SampleBudgetMs;
    Out.DegradeLevel = DegradeLevel;
    Out.EffectiveSampleRateHz = GetEffectiveSampleRateHz();
    return Out;
}

double UPSTelemetrySamplingSubsystem::GetClockSeconds() const
{
    double Now = Clock;
    if (const UWorld* World = GetWorld())
    {
        Now += FMath::Max(0.0, static_cast<double>(World->GetTimeSeconds()) - WorldTimeAtLastAdvance);
    }
    return Now;
}

double UPSTelemetrySamplingSubsystem::GetEffectiveSampleInterval() const
{
    const double BaseInterval = 1.0 / FMath::Max(static_cast<double>(Tuning.SampleRateHz), static_cast<double>(KINDA_SMALL_NUMBER));
    return BaseInterval * static_cast<double>(1 << DegradeLevel);
}

int32 UPSTelemetrySamplingSubsystem::GetRingCapacity() const
{
    const int32 Capacity = FMath::CeilToInt(Tuning.SampleRateHz * Tuning.HistorySeconds);
    return FMath::Clamp(Capacity, 2, PSTelemetrySamplingPrivate::MaxRingFrames);
}

void UPSTelemetrySamplingSubsystem::AdvanceTime(float DeltaSeconds)
{
    if (DeltaSeconds <= 0.f)
    {
        return;
    }

    Clock += DeltaSeconds;
    if (const UWorld* World = GetWorld())
    {
        WorldTimeAtLastAdvance = World->GetTimeSeconds();
    }

    if (!bSamplingEnabled)
    {
        return;
    }

    SinceLastSample += DeltaSeconds;
    const double Interval = GetEffectiveSampleInterval();
    if (SinceLastSample + PSTelemetrySamplingPrivate::IntervalTolerance < Interval)
    {
        return;
    }

    // One frame per step at most: after a hitch the schedule restarts rather than bursting.
    SinceLastSample -= Interval;
    if (SinceLastSample < 0.0 || SinceLastSample + PSTelemetrySamplingPrivate::IntervalTolerance >= Interval)
    {
        SinceLastSample = 0.0;
    }
    TakeScheduledFrame();
}

void UPSTelemetrySamplingSubsystem::TakeScheduledFrame()
{
    PS_PERF_SCOPE(Telemetry);
    const uint64 StartCycles = FPlatformTime::Cycles64();

    const UPSTelemetryBus* Bus = BoundBus.Get();
    FPSSnapshotFrame& Slot = PushScheduledSlot();
    CaptureFrame(Slot, GetClockSeconds(), Bus ? Bus->GetLastEventSequence() : 0);
    ++Stats.ScheduledFramesTaken;

    const float CostMs = static_cast<float>(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles));
    RecordSampleCost(CostMs);
}

void UPSTelemetrySamplingSubsystem::RecordSampleCost(float CostMs)
{
    Stats.LastSampleCostMs = CostMs;
    if (bHasCostSample)
    {
        Stats.AverageSampleCostMs += PSTelemetrySamplingPrivate::CostAverageWeight * (CostMs - Stats.AverageSampleCostMs);
    }
    else
    {
        Stats.AverageSampleCostMs = CostMs;
        bHasCostSample = true;
    }

    const float Budget = Tuning.SampleBudgetMs;
    if (CostMs > Budget)
    {
        UnderBudgetStreak = 0;
        if (++OverBudgetStreak >= FMath::Max(1, Tuning.DegradeAfterSamples))
        {
            OverBudgetStreak = 0;
            if (DegradeLevel < Tuning.MaxDegradeLevel)
            {
                ++DegradeLevel;
                UE_LOG(LogTemp, Display, TEXT("UPSTelemetrySamplingSubsystem: sampling over its %.3f ms budget; rate down to %.1f Hz."), Budget, GetEffectiveSampleRateHz());
            }
        }
    }
    else
    {
        OverBudgetStreak = 0;
        if (CostMs < Budget * Tuning.RecoverBelowFraction)
        {
            if (++UnderBudgetStreak >= FMath::Max(1, Tuning.RecoverAfterSamples))
            {
                UnderBudgetStreak = 0;
                if (DegradeLevel > 0)
                {
                    --DegradeLevel;
                    UE_LOG(LogTemp, Display, TEXT("UPSTelemetrySamplingSubsystem: sampling back under budget; rate up to %.1f Hz."), GetEffectiveSampleRateHz());
                }
            }
        }
        else
        {
            UnderBudgetStreak = 0;
        }
    }
    SET_DWORD_STAT(STAT_PSTelemetryDegradeLevel, DegradeLevel);
}

void UPSTelemetrySamplingSubsystem::RefreshRoster()
{
    bRosterDirty = false;
    UWorld* World = GetWorld();
    if (!World)
    {
        Roster.Reset();
        Ball.Reset();
        return;
    }

    TArray<FSampledPawn> Fresh;
    for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
    {
        APSPlayerPawn* PlayerPawn = *It;
        if (!IsValid(PlayerPawn))
        {
            continue;
        }

        FSampledPawn Entry;
        // Keep the velocity history of a pawn already being sampled, so its acceleration
        // carries across the refresh.
        if (const FSampledPawn* Existing = Roster.FindByPredicate([PlayerPawn](const FSampledPawn& Candidate) { return Candidate.Pawn.Get() == PlayerPawn; }))
        {
            Entry = *Existing;
        }
        Entry.Pawn = PlayerPawn;
        // Identity is read here, not per frame: GetAttributes copies the whole row.
        const FPlayerAttributes Attributes = PlayerPawn->GetAttributes();
        Entry.PlayerId = Attributes.PlayerId;
        Entry.Role = Attributes.Role;
        Fresh.Add(Entry);
    }

    Fresh.Sort([](const FSampledPawn& A, const FSampledPawn& B)
    {
        return PSTelemetrySamplingPrivate::SortsBefore(*A.Pawn.Get(), A.PlayerId, *B.Pawn.Get(), B.PlayerId);
    });
    Roster = MoveTemp(Fresh);

    Ball.Reset();
    for (TActorIterator<APSBall> It(World); It; ++It)
    {
        if (IsValid(*It))
        {
            Ball = *It;
            break;
        }
    }
}

void UPSTelemetrySamplingSubsystem::HandleActorSpawned(AActor* Actor)
{
    if (Actor && (Actor->IsA<APSPlayerPawn>() || Actor->IsA<APSBall>()))
    {
        bRosterDirty = true;
    }
}

void UPSTelemetrySamplingSubsystem::CaptureFrame(FPSSnapshotFrame& OutFrame, double Now, int32 EventSequence)
{
    SCOPE_CYCLE_COUNTER(STAT_PSTelemetryCaptureFrame);

    if (bRosterDirty)
    {
        RefreshRoster();
    }

    OutFrame.FrameIndex = NextFrameIndex++;
    OutFrame.Time = static_cast<float>(Now);
    OutFrame.EventSequence = EventSequence;
    OutFrame.bKeyframe = false;
    OutFrame.KeyframeEventType = EPSTelemetryEventType::Snap;
    OutFrame.bInterpolated = false;
    OutFrame.Pawns.Reset(Roster.Num());

    for (FSampledPawn& Entry : Roster)
    {
        const APSPlayerPawn* PlayerPawn = Entry.Pawn.Get();
        if (!IsValid(PlayerPawn))
        {
            // Destroyed since the last refresh: re-read the roster before the next frame.
            bRosterDirty = true;
            continue;
        }

        FPSPawnSnapshot& Snapshot = OutFrame.Pawns.AddDefaulted_GetRef();
        Snapshot.Pawn = Entry.Pawn;
        Snapshot.PlayerId = Entry.PlayerId;
        Snapshot.Role = Entry.Role;
        Snapshot.TeamSide = PlayerPawn->TeamSide;
        Snapshot.Location = PlayerPawn->GetActorLocation();
        const UFloatingPawnMovement* Movement = PlayerPawn->GetFloatingMovementComponent();
        Snapshot.Velocity = Movement ? Movement->Velocity : PlayerPawn->GetVelocity();
        Snapshot.FacingYaw = PlayerPawn->GetActorRotation().Yaw;
        Snapshot.bHasBall = PlayerPawn->HasPossession();
        Snapshot.bUserControlled = PlayerPawn->IsUserControlled();

        // Acceleration is the change in velocity over real elapsed time. Two frames at the
        // same instant (an event inside a tick) keep the last measured value.
        const double Elapsed = Now - Entry.LastTime;
        if (Entry.bHasLast && Elapsed > PSTelemetrySamplingPrivate::IntervalTolerance)
        {
            Entry.LastAcceleration = (Snapshot.Velocity - Entry.LastVelocity) / Elapsed;
            Entry.LastVelocity = Snapshot.Velocity;
            Entry.LastTime = Now;
        }
        else if (!Entry.bHasLast)
        {
            Entry.LastVelocity = Snapshot.Velocity;
            Entry.LastTime = Now;
            Entry.bHasLast = true;
        }
        Snapshot.Acceleration = Entry.LastAcceleration;
    }

    const APSBall* SampledBall = Ball.Get();
    OutFrame.bBallSampled = IsValid(SampledBall);
    OutFrame.BallLocation = OutFrame.bBallSampled ? SampledBall->GetActorLocation() : FVector::ZeroVector;
    OutFrame.BallVelocity = OutFrame.bBallSampled ? PSTelemetrySamplingPrivate::BallVelocity(*SampledBall) : FVector::ZeroVector;

    SET_DWORD_STAT(STAT_PSTelemetryPawnsPerFrame, OutFrame.Pawns.Num());
}

FPSSnapshotFrame& UPSTelemetrySamplingSubsystem::PushScheduledSlot()
{
    if (Ring.Num() < GetRingCapacity())
    {
        return Ring.AddDefaulted_GetRef();
    }
    // Full: overwrite the oldest, reusing its pawn array's allocation.
    FPSSnapshotFrame& Slot = Ring[RingStart];
    RingStart = (RingStart + 1) % Ring.Num();
    return Slot;
}

const FPSSnapshotFrame& UPSTelemetrySamplingSubsystem::GetScheduledFrame(int32 Index) const
{
    return Ring[(RingStart + Index) % Ring.Num()];
}

void UPSTelemetrySamplingSubsystem::ResizeRing(int32 NewCapacity)
{
    if (Ring.Num() == 0)
    {
        RingStart = 0;
        return;
    }

    // Oldest first, keeping the newest NewCapacity frames.
    const int32 Keep = FMath::Min(Ring.Num(), NewCapacity);
    TArray<FPSSnapshotFrame> Ordered;
    Ordered.Reserve(Keep);
    for (int32 Index = Ring.Num() - Keep; Index < Ring.Num(); ++Index)
    {
        Ordered.Add(GetScheduledFrame(Index));
    }
    Ring = MoveTemp(Ordered);
    RingStart = 0;
}

void UPSTelemetrySamplingSubsystem::HandleEventRecorded(const FPSTelemetryEvent& Event)
{
    UPSTelemetryBus* Bus = BoundBus.Get();
    if (!Bus)
    {
        return;
    }

    PruneToBus(*Bus);
    if (!bSamplingEnabled)
    {
        return;
    }

    const double Now = GetClockSeconds();
    FEventMark& Mark = EventMarks.AddDefaulted_GetRef();
    Mark.Sequence = Event.Sequence;
    Mark.Time = Now;

    if (Tuning.KeyframeEvents.Contains(Event.EventType))
    {
        FPSSnapshotFrame& Keyframe = Keyframes.AddDefaulted_GetRef();
        CaptureFrame(Keyframe, Now, Event.Sequence);
        Keyframe.bKeyframe = true;
        Keyframe.KeyframeEventType = Event.EventType;
        ++Stats.KeyframesTaken;
    }
}

void UPSTelemetrySamplingSubsystem::PruneToBus(const UPSTelemetryBus& Bus)
{
    const int32 Oldest = Bus.GetOldestEventSequence();

    int32 StaleMarks = 0;
    while (StaleMarks < EventMarks.Num() && EventMarks[StaleMarks].Sequence < Oldest)
    {
        ++StaleMarks;
    }
    EventMarks.RemoveAt(0, StaleMarks);

    int32 StaleKeyframes = 0;
    while (StaleKeyframes < Keyframes.Num() && Keyframes[StaleKeyframes].EventSequence < Oldest)
    {
        ++StaleKeyframes;
    }
    Keyframes.RemoveAt(0, StaleKeyframes);
}

void UPSTelemetrySamplingSubsystem::GatherFrames(TArray<const FPSSnapshotFrame*>& OutFrames, TFunctionRef<bool(const FPSSnapshotFrame&)> Filter) const
{
    OutFrames.Reset();
    for (int32 Index = 0; Index < Ring.Num(); ++Index)
    {
        const FPSSnapshotFrame& Frame = GetScheduledFrame(Index);
        if (Filter(Frame))
        {
            OutFrames.Add(&Frame);
        }
    }
    for (const FPSSnapshotFrame& Frame : Keyframes)
    {
        if (Filter(Frame))
        {
            OutFrames.Add(&Frame);
        }
    }
    // Capture order is time order.
    OutFrames.Sort([](const FPSSnapshotFrame& A, const FPSSnapshotFrame& B) { return A.FrameIndex < B.FrameIndex; });
}

const FPSSnapshotFrame* UPSTelemetrySamplingSubsystem::FindKeyframe(int32 EventSequence) const
{
    return Keyframes.FindByPredicate([EventSequence](const FPSSnapshotFrame& Frame) { return Frame.EventSequence == EventSequence; });
}

bool UPSTelemetrySamplingSubsystem::GetLatestFrame(FPSSnapshotFrame& OutFrame) const
{
    const FPSSnapshotFrame* Latest = nullptr;
    if (Ring.Num() > 0)
    {
        Latest = &GetScheduledFrame(Ring.Num() - 1);
    }
    if (Keyframes.Num() > 0 && (!Latest || Keyframes.Last().FrameIndex > Latest->FrameIndex))
    {
        Latest = &Keyframes.Last();
    }
    if (!Latest)
    {
        return false;
    }
    OutFrame = *Latest;
    return true;
}

TArray<FPSSnapshotFrame> UPSTelemetrySamplingSubsystem::GetFramesBetween(float FromTime, float ToTime) const
{
    TArray<const FPSSnapshotFrame*> Found;
    GatherFrames(Found, [FromTime, ToTime](const FPSSnapshotFrame& Frame) { return Frame.Time >= FromTime && Frame.Time <= ToTime; });

    TArray<FPSSnapshotFrame> Out;
    Out.Reserve(Found.Num());
    for (const FPSSnapshotFrame* Frame : Found)
    {
        Out.Add(*Frame);
    }
    return Out;
}

bool UPSTelemetrySamplingSubsystem::SampleAt(float Time, FPSSnapshotFrame& OutFrame) const
{
    // The latest frame at or before Time, and the earliest at or after it.
    const FPSSnapshotFrame* Before = nullptr;
    const FPSSnapshotFrame* After = nullptr;
    auto Consider = [Time, &Before, &After](const FPSSnapshotFrame& Frame)
    {
        if (Frame.Time <= Time && (!Before || Frame.FrameIndex > Before->FrameIndex))
        {
            Before = &Frame;
        }
        if (Frame.Time >= Time && (!After || Frame.FrameIndex < After->FrameIndex))
        {
            After = &Frame;
        }
    };
    for (int32 Index = 0; Index < Ring.Num(); ++Index)
    {
        Consider(GetScheduledFrame(Index));
    }
    for (const FPSSnapshotFrame& Frame : Keyframes)
    {
        Consider(Frame);
    }

    if (!Before)
    {
        return false;
    }
    if (!After)
    {
        // After the newest frame: hold it, for up to one sampling interval.
        if (Time - Before->Time > GetEffectiveSampleInterval() + PSTelemetrySamplingPrivate::IntervalTolerance)
        {
            return false;
        }
        OutFrame = *Before;
        OutFrame.FrameIndex = -1;
        OutFrame.Time = Time;
        OutFrame.bKeyframe = false;
        OutFrame.bInterpolated = true;
        return true;
    }
    if (Before == After || After->Time <= Before->Time)
    {
        OutFrame = *Before;
        return true;
    }

    const float Alpha = (Time - Before->Time) / (After->Time - Before->Time);
    BlendFrames(*Before, *After, Alpha, OutFrame);
    OutFrame.Time = Time;
    return true;
}

TArray<FPSPawnSnapshot> UPSTelemetrySamplingSubsystem::GetPawnTrail(FName PlayerId, float FromTime, float ToTime) const
{
    TArray<const FPSSnapshotFrame*> Found;
    GatherFrames(Found, [FromTime, ToTime](const FPSSnapshotFrame& Frame) { return Frame.Time >= FromTime && Frame.Time <= ToTime; });

    TArray<FPSPawnSnapshot> Trail;
    Trail.Reserve(Found.Num());
    for (const FPSSnapshotFrame* Frame : Found)
    {
        if (const FPSPawnSnapshot* Snapshot = Frame->FindPawn(PlayerId))
        {
            Trail.Add(*Snapshot);
        }
    }
    return Trail;
}

bool UPSTelemetrySamplingSubsystem::FindPawnSnapshot(const FPSSnapshotFrame& Frame, FName PlayerId, FPSPawnSnapshot& OutSnapshot)
{
    if (const FPSPawnSnapshot* Snapshot = Frame.FindPawn(PlayerId))
    {
        OutSnapshot = *Snapshot;
        return true;
    }
    return false;
}

bool UPSTelemetrySamplingSubsystem::GetEventTime(int32 EventSequence, float& OutTime) const
{
    if (const FEventMark* Mark = EventMarks.FindByPredicate([EventSequence](const FEventMark& Candidate) { return Candidate.Sequence == EventSequence; }))
    {
        OutTime = static_cast<float>(Mark->Time);
        return true;
    }
    return false;
}

bool UPSTelemetrySamplingSubsystem::GetFrameAtEvent(int32 EventSequence, FPSSnapshotFrame& OutFrame) const
{
    if (const FPSSnapshotFrame* Keyframe = FindKeyframe(EventSequence))
    {
        OutFrame = *Keyframe;
        return true;
    }

    float EventTime = 0.f;
    if (!GetEventTime(EventSequence, EventTime) || !SampleAt(EventTime, OutFrame))
    {
        return false;
    }
    OutFrame.EventSequence = EventSequence;
    return true;
}

bool UPSTelemetrySamplingSubsystem::GetFrameAtLatestEvent(EPSTelemetryEventType EventType, FPSSnapshotFrame& OutFrame, FPSTelemetryEvent& OutEvent) const
{
    const UPSTelemetryBus* Bus = BoundBus.Get();
    return Bus && Bus->FindLatestEventOfType(EventType, OutEvent) && GetFrameAtEvent(OutEvent.Sequence, OutFrame);
}

TArray<FPSSnapshotFrame> UPSTelemetrySamplingSubsystem::GetFramesBetweenEvents(int32 FromSequence, int32 ToSequence) const
{
    TArray<FPSSnapshotFrame> Out;
    if (ToSequence < FromSequence)
    {
        return Out;
    }

    FPSSnapshotFrame Boundary;
    if (!FindKeyframe(FromSequence) && GetFrameAtEvent(FromSequence, Boundary))
    {
        Out.Add(Boundary);
    }

    // A frame's EventSequence is the last event published before it was captured, so the
    // frames after From and before To are exactly those in [From, To) -- plus To's keyframe.
    TArray<const FPSSnapshotFrame*> Found;
    GatherFrames(Found, [FromSequence, ToSequence](const FPSSnapshotFrame& Frame)
    {
        return Frame.EventSequence >= FromSequence
            && (Frame.EventSequence < ToSequence || (Frame.bKeyframe && Frame.EventSequence == ToSequence));
    });
    for (const FPSSnapshotFrame* Frame : Found)
    {
        Out.Add(*Frame);
    }

    if (ToSequence != FromSequence && !FindKeyframe(ToSequence) && GetFrameAtEvent(ToSequence, Boundary))
    {
        Out.Add(Boundary);
    }
    return Out;
}

TArray<FPSTelemetryEvent> UPSTelemetrySamplingSubsystem::GetEventsBetween(float FromTime, float ToTime) const
{
    TArray<FPSTelemetryEvent> Out;
    const UPSTelemetryBus* Bus = BoundBus.Get();
    if (!Bus)
    {
        return Out;
    }
    for (const FEventMark& Mark : EventMarks)
    {
        const float MarkTime = static_cast<float>(Mark.Time);
        FPSTelemetryEvent Event;
        if (MarkTime >= FromTime && MarkTime <= ToTime && Bus->FindEventBySequence(Mark.Sequence, Event))
        {
            Out.Add(Event);
        }
    }
    return Out;
}

void UPSTelemetrySamplingSubsystem::BlendFrames(const FPSSnapshotFrame& From, const FPSSnapshotFrame& To, float Alpha, FPSSnapshotFrame& OutFrame)
{
    OutFrame = From;
    OutFrame.FrameIndex = -1;
    OutFrame.bKeyframe = false;
    OutFrame.bInterpolated = true;
    OutFrame.Time = FMath::Lerp(From.Time, To.Time, Alpha);

    if (From.bBallSampled && To.bBallSampled)
    {
        OutFrame.BallLocation = FMath::Lerp(From.BallLocation, To.BallLocation, Alpha);
        OutFrame.BallVelocity = FMath::Lerp(From.BallVelocity, To.BallVelocity, Alpha);
    }

    for (FPSPawnSnapshot& Snapshot : OutFrame.Pawns)
    {
        const FPSPawnSnapshot* Next = PSTelemetrySamplingPrivate::FindSameSubject(To, Snapshot);
        if (!Next)
        {
            continue;
        }
        Snapshot.Location = FMath::Lerp(Snapshot.Location, Next->Location, Alpha);
        Snapshot.Velocity = FMath::Lerp(Snapshot.Velocity, Next->Velocity, Alpha);
        Snapshot.Acceleration = FMath::Lerp(Snapshot.Acceleration, Next->Acceleration, Alpha);
        Snapshot.FacingYaw = FRotator::NormalizeAxis(Snapshot.FacingYaw + FMath::FindDeltaAngleDegrees(Snapshot.FacingYaw, Next->FacingYaw) * Alpha);
        if (Alpha >= 0.5f)
        {
            Snapshot.bHasBall = Next->bHasBall;
            Snapshot.bUserControlled = Next->bUserControlled;
            Snapshot.TeamSide = Next->TeamSide;
        }
    }
}
