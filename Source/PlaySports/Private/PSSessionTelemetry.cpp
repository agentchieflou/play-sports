#include "PSSessionTelemetry.h"
#include "PSCrashContext.h"
#include "PSDataIngestion.h"
#include "PSPlatformTiers.h"
#include "PSSaveSubsystem.h"
#include "PSSessionTelemetrySave.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GeneralProjectSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/Paths.h"

namespace PSSessionTelemetryPrivate
{
    const TCHAR* const UnspecifiedMode = TEXT("Unspecified");
}

// ---------------------------------------------------------------------------
// FPSFrameTimeHistogram
// ---------------------------------------------------------------------------

void FPSFrameTimeHistogram::Reset(float InBucketMs, int32 InBucketCount)
{
    BucketMs = FMath::Max(InBucketMs, KINDA_SMALL_NUMBER);
    Buckets.Reset();
    // One bucket more than asked: the last collects every frame slower than the rest.
    Buckets.SetNumZeroed(FMath::Max(InBucketCount, 1) + 1);
    FrameCount = 0;
    MaxMs = 0.f;
}

void FPSFrameTimeHistogram::AddFrame(float DeltaSeconds)
{
    if (Buckets.Num() == 0)
    {
        return;
    }
    const float Ms = FMath::Max(DeltaSeconds, 0.f) * 1000.f;
    const int32 Overflow = Buckets.Num() - 1;
    const float Slot = Ms / BucketMs;
    const int32 Index = Slot >= static_cast<float>(Overflow) ? Overflow : FMath::FloorToInt(Slot);
    ++Buckets[Index];
    ++FrameCount;
    MaxMs = FMath::Max(MaxMs, Ms);
}

float FPSFrameTimeHistogram::GetPercentileMs(float Percentile) const
{
    if (FrameCount == 0)
    {
        return 0.f;
    }

    // Rank of the frame the percentile names, in exact integer math (per mille), so p99 of
    // 100 frames is the 99th frame and not the 100th.
    const int64 PerMille = FMath::Clamp<int64>(FMath::RoundToInt(Percentile * 10.f), 0, 1000);
    const int64 Rank = FMath::Clamp<int64>((PerMille * FrameCount + 999) / 1000, 1, FrameCount);

    int64 Seen = 0;
    for (int32 Index = 0; Index < Buckets.Num(); ++Index)
    {
        Seen += Buckets[Index];
        if (Seen >= Rank)
        {
            const bool bOverflow = Index == Buckets.Num() - 1;
            return bOverflow ? MaxMs : FMath::Min((Index + 1) * BucketMs, MaxMs);
        }
    }
    return MaxMs;
}

// ---------------------------------------------------------------------------
// UPSSessionTelemetrySubsystem
// ---------------------------------------------------------------------------

void UPSSessionTelemetrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    BindToBus(Collection.InitializeDependency<UPSTelemetryBus>());
    FWorldDelegates::OnWorldCleanup.AddUObject(this, &UPSSessionTelemetrySubsystem::HandleWorldCleanup);
}

void UPSSessionTelemetrySubsystem::Deinitialize()
{
    FWorldDelegates::OnWorldCleanup.RemoveAll(this);

    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnEventRecordedMC.RemoveAll(this);
    }
    BoundBus.Reset();

    // The world's cleanup has already ended the session cleanly. This can run during garbage
    // collection, so it never saves; it only stops the crash context describing a dead world.
    if (bSessionOpen)
    {
        bSessionOpen = false;
        FPSCrashContext::Clear();
    }
    Saves = nullptr;

    Super::Deinitialize();
}

bool UPSSessionTelemetrySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPSSessionTelemetrySubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSSessionTelemetrySubsystem, STATGROUP_Tickables);
}

void UPSSessionTelemetrySubsystem::BindToBus(UPSTelemetryBus* Bus)
{
    if (!Bus)
    {
        return;
    }
    BoundBus = Bus;

    // A snap counts a play; every event, as the bus records it, refreshes the breadcrumbs.
    Bus->OnSnapMC.AddUObject(this, &UPSSessionTelemetrySubsystem::HandleSnap);
    Bus->OnEventRecordedMC.AddUObject(this, &UPSSessionTelemetrySubsystem::HandleEventRecorded);
}

void UPSSessionTelemetrySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    UGameInstance* GameInstance = InWorld.GetGameInstance();
    StartSession(ModeFromURL(InWorld.URL), GameInstance ? GameInstance->GetSubsystem<UPSSaveSubsystem>() : nullptr);
}

void UPSSessionTelemetrySubsystem::Tick(float DeltaTime)
{
    // The engine's frame time, not the world's: time dilation must not hide a slow frame.
    RecordFrame(static_cast<float>(FApp::GetDeltaTime()));
}

void UPSSessionTelemetrySubsystem::StartSession(const FString& InMode, UPSSaveSubsystem* InSaves, const FString& InSlotName)
{
    if (bSessionOpen)
    {
        EndSession();
    }

    const FPSSessionTelemetryTuning& Active = GetTuning();
    Saves = InSaves;
    StoreSlot = InSlotName.IsEmpty() ? UPSSessionTelemetrySave::GetDefaultSlotName() : InSlotName;
    SessionId = FGuid::NewGuid();
    Mode = InMode.IsEmpty() ? FString(PSSessionTelemetryPrivate::UnspecifiedMode) : InMode;
    PlatformTier = PSPlatformTiers::GetActiveTier().TierId.ToString();
    PlayCount = 0;
    DurationSeconds = 0.f;
    FrameTimes.Reset(Active.FrameTimeBucketMs, Active.FrameTimeBucketCount);
    bSessionOpen = true;

    RefreshCrashContext();
    // Saved as open now: if the game never reaches the world's cleanup, it stays that way.
    Persist(false);
}

void UPSSessionTelemetrySubsystem::EndSession()
{
    if (!bSessionOpen)
    {
        return;
    }
    Persist(true);
    bSessionOpen = false;
    FPSCrashContext::Clear();
}

void UPSSessionTelemetrySubsystem::RecordFrame(float DeltaSeconds)
{
    if (!bSessionOpen || DeltaSeconds <= 0.f)
    {
        return;
    }
    FrameTimes.AddFrame(DeltaSeconds);
    DurationSeconds += DeltaSeconds;
}

FPSSessionSummary UPSSessionTelemetrySubsystem::BuildSummary(bool bEndedCleanly) const
{
    FPSSessionSummary Summary;
    Summary.SessionId = SessionId;
    Summary.Mode = Mode;
    Summary.BuildVersion = GetDefault<UGeneralProjectSettings>()->ProjectVersion;
    Summary.Platform = UGameplayStatics::GetPlatformName();
    Summary.PlatformTier = PlatformTier;
    Summary.Date = FDateTime::UtcNow().ToString(TEXT("%Y-%m-%d"));
    Summary.DurationSeconds = DurationSeconds;
    Summary.PlayCount = PlayCount;
    Summary.FrameCount = FrameTimes.Num();
    Summary.MaxFrameMs = FrameTimes.GetMaxMs();
    for (const float Percentile : Tuning.Percentiles)
    {
        FPSFrameTimePercentile& Entry = Summary.FrameTimePercentiles.AddDefaulted_GetRef();
        Entry.Percentile = Percentile;
        Entry.Milliseconds = FrameTimes.GetPercentileMs(Percentile);
    }
    Summary.bEndedCleanly = bEndedCleanly;
    return Summary;
}

void UPSSessionTelemetrySubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    if (!bSessionOpen)
    {
        return;
    }
    ++PlayCount;
    RefreshCrashContext();

    const int32 CheckpointEvery = GetTuning().CheckpointEveryPlays;
    if (CheckpointEvery > 0 && PlayCount % CheckpointEvery == 0)
    {
        Persist(false);
    }
}

void UPSSessionTelemetrySubsystem::HandleEventRecorded(const FPSTelemetryEvent& Event)
{
    RefreshCrashContext();
}

void UPSSessionTelemetrySubsystem::HandleWorldCleanup(UWorld* CleanedWorld, bool bSessionEnded, bool bCleanupResources)
{
    if (CleanedWorld == GetWorld())
    {
        EndSession();
    }
}

void UPSSessionTelemetrySubsystem::RefreshCrashContext()
{
    if (!bSessionOpen)
    {
        return;
    }
    FPSCrashContext::SetSession(SessionId, Mode, PlatformTier, PlayCount, DurationSeconds);

    const UPSTelemetryBus* Bus = BoundBus.Get();
    FPSCrashContext::SetBreadcrumbs(Bus ? Bus->GetEventHistory() : TArray<FPSTelemetryEvent>(), GetTuning().CrashBreadcrumbCount);
}

void UPSSessionTelemetrySubsystem::Persist(bool bEndedCleanly)
{
    UPSSessionTelemetrySave* Store = UPSSessionTelemetrySave::LoadStore(Saves, StoreSlot);
    if (!Store || !Store->bOptedIn)
    {
        return;
    }

    const FPSSessionTelemetryTuning& Active = GetTuning();
    if (bEndedCleanly && DurationSeconds < Active.MinSessionSeconds)
    {
        Store->RemoveSession(SessionId);
    }
    else
    {
        Store->UpsertSession(BuildSummary(bEndedCleanly), Active.MaxStoredSessions);
    }

    if (!Saves->SaveToSlot(Store, StoreSlot))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSSessionTelemetrySubsystem: could not save the session to %s."), *StoreSlot);
    }
}

// ---------------------------------------------------------------------------
// Tuning and helpers
// ---------------------------------------------------------------------------

FString UPSSessionTelemetrySubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data") / TEXT("session_telemetry.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

TArray<FString> UPSSessionTelemetrySubsystem::ValidateTuning(const FPSSessionTelemetryTuning& Candidate)
{
    TArray<FString> Problems;
    if (Candidate.FrameTimeBucketMs <= 0.f)
    {
        Problems.Add(TEXT("FrameTimeBucketMs must be above 0"));
    }
    if (Candidate.FrameTimeBucketCount < 1)
    {
        Problems.Add(TEXT("FrameTimeBucketCount must be 1 or more"));
    }
    if (Candidate.Percentiles.Num() == 0)
    {
        Problems.Add(TEXT("Percentiles must name at least one percentile"));
    }
    for (const float Percentile : Candidate.Percentiles)
    {
        if (Percentile <= 0.f || Percentile > 100.f)
        {
            Problems.Add(FString::Printf(TEXT("Percentile %.1f is outside (0, 100]"), Percentile));
        }
    }
    if (Candidate.MinSessionSeconds < 0.f)
    {
        Problems.Add(TEXT("MinSessionSeconds must not be negative"));
    }
    if (Candidate.MaxStoredSessions < 1)
    {
        Problems.Add(TEXT("MaxStoredSessions must be 1 or more"));
    }
    if (Candidate.CheckpointEveryPlays < 0)
    {
        Problems.Add(TEXT("CheckpointEveryPlays must not be negative"));
    }
    if (Candidate.CrashBreadcrumbCount < 0)
    {
        Problems.Add(TEXT("CrashBreadcrumbCount must not be negative"));
    }
    return Problems;
}

bool UPSSessionTelemetrySubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;

    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSSessionTelemetryTuning Loaded;
    if (!Ingestion->LoadSessionTelemetryTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSSessionTelemetrySubsystem: could not load %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    const TArray<FString> Problems = ValidateTuning(Loaded);
    if (Problems.Num() > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSSessionTelemetrySubsystem: %s is invalid (%s); keeping defaults."), *JsonFilePath, *FString::Join(Problems, TEXT("; ")));
        return false;
    }
    Tuning = Loaded;
    return true;
}

const FPSSessionTelemetryTuning& UPSSessionTelemetrySubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

FString UPSSessionTelemetrySubsystem::ModeFromURL(const FURL& URL)
{
    const TCHAR* TravelMode = URL.GetOption(TEXT("mode="), nullptr);
    if (TravelMode && *TravelMode)
    {
        return TravelMode;
    }
    const TCHAR* GameOption = URL.GetOption(TEXT("game="), nullptr);
    if (GameOption && *GameOption)
    {
        return GameOption;
    }
    return PSSessionTelemetryPrivate::UnspecifiedMode;
}
