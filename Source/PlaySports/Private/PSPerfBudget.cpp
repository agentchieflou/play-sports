#include "PSPerfBudget.h"
#include "PSSessionTelemetryTypes.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_STAT(STAT_PSPerfSimulation);
DEFINE_STAT(STAT_PSPerfAI);
DEFINE_STAT(STAT_PSPerfTelemetry);
DEFINE_STAT(STAT_PSPerfOverlays);
DEFINE_STAT(STAT_PSPerfUI);
DEFINE_STAT(STAT_PSPerfBusEvents);
DEFINE_STAT(STAT_PSPerfAIDecisions);
DEFINE_STAT(STAT_PSPerfFieldScans);

namespace PSPerfPrivate
{
    constexpr int32 SystemCount = static_cast<int32>(EPSPerfSystem::Audio) + 1;
    constexpr int32 CounterCount = static_cast<int32>(EPSPerfCounter::FieldScans) + 1;

    /** One capture. Game thread only, like the systems it times. */
    struct FCaptureState
    {
        bool bCapturing = false;
        int32 Frames = 0;

        /** The open scopes, innermost last; only the innermost is accumulating. */
        TArray<EPSPerfSystem, TInlineAllocator<16>> OpenScopes;
        uint64 SegmentStartCycles = 0;

        double FrameMs[SystemCount] = {};
        double TotalMs[SystemCount] = {};
        bool bEverRan[SystemCount] = {};
        FPSFrameTimeHistogram Histograms[SystemCount];
        int64 Counts[CounterCount] = {};
    };

    FCaptureState& State()
    {
        static FCaptureState Capture;
        return Capture;
    }

    int32 SystemIndex(EPSPerfSystem System)
    {
        return FMath::Clamp(static_cast<int32>(System), 0, SystemCount - 1);
    }

    /** Adds the time since the segment started to the innermost open scope's system. */
    void CloseSegment(FCaptureState& Capture, uint64 NowCycles)
    {
        if (Capture.OpenScopes.Num() > 0)
        {
            Capture.FrameMs[SystemIndex(Capture.OpenScopes.Last())] += FPlatformTime::ToMilliseconds64(NowCycles - Capture.SegmentStartCycles);
        }
        Capture.SegmentStartCycles = NowCycles;
    }
}

void PSPerf::BeginCapture(float InBucketMs, int32 InBucketCount)
{
    PSPerfPrivate::FCaptureState& Capture = PSPerfPrivate::State();
    Capture = PSPerfPrivate::FCaptureState();
    for (FPSFrameTimeHistogram& Histogram : Capture.Histograms)
    {
        Histogram.Reset(InBucketMs, InBucketCount);
    }
    Capture.bCapturing = true;
}

void PSPerf::EndCapture()
{
    PSPerfPrivate::FCaptureState& Capture = PSPerfPrivate::State();
    Capture.bCapturing = false;
    Capture.OpenScopes.Reset();
}

bool PSPerf::IsCapturing()
{
    return PSPerfPrivate::State().bCapturing;
}

void PSPerf::EnterScope(EPSPerfSystem System)
{
    PSPerfPrivate::FCaptureState& Capture = PSPerfPrivate::State();
    PSPerfPrivate::CloseSegment(Capture, FPlatformTime::Cycles64());
    Capture.OpenScopes.Add(System);
    Capture.bEverRan[PSPerfPrivate::SystemIndex(System)] = true;
}

void PSPerf::ExitScope()
{
    PSPerfPrivate::FCaptureState& Capture = PSPerfPrivate::State();
    if (Capture.OpenScopes.Num() == 0)
    {
        return;
    }
    PSPerfPrivate::CloseSegment(Capture, FPlatformTime::Cycles64());
    Capture.OpenScopes.Pop();
}

void PSPerf::EndFrame()
{
    PSPerfPrivate::FCaptureState& Capture = PSPerfPrivate::State();
    if (!Capture.bCapturing)
    {
        return;
    }
    // A scope still open at the frame's end (a capture started inside one) counts up to now.
    PSPerfPrivate::CloseSegment(Capture, FPlatformTime::Cycles64());
    for (int32 Index = 0; Index < PSPerfPrivate::SystemCount; ++Index)
    {
        Capture.Histograms[Index].AddFrame(static_cast<float>(Capture.FrameMs[Index] / 1000.0));
        Capture.TotalMs[Index] += Capture.FrameMs[Index];
        Capture.FrameMs[Index] = 0.0;
    }
    ++Capture.Frames;
}

int32 PSPerf::GetCapturedFrames()
{
    return PSPerfPrivate::State().Frames;
}

void PSPerf::AddCount(EPSPerfCounter Counter, int32 Amount)
{
    switch (Counter)
    {
    case EPSPerfCounter::BusEvents:
        INC_DWORD_STAT_BY(STAT_PSPerfBusEvents, Amount);
        break;
    case EPSPerfCounter::AIDecisions:
        INC_DWORD_STAT_BY(STAT_PSPerfAIDecisions, Amount);
        break;
    case EPSPerfCounter::FieldScans:
        INC_DWORD_STAT_BY(STAT_PSPerfFieldScans, Amount);
        break;
    default:
        break;
    }
    PSPerfPrivate::FCaptureState& Capture = PSPerfPrivate::State();
    if (Capture.bCapturing)
    {
        Capture.Counts[FMath::Clamp(static_cast<int32>(Counter), 0, PSPerfPrivate::CounterCount - 1)] += Amount;
    }
}

int64 PSPerf::GetCount(EPSPerfCounter Counter)
{
    return PSPerfPrivate::State().Counts[FMath::Clamp(static_cast<int32>(Counter), 0, PSPerfPrivate::CounterCount - 1)];
}

double PSPerf::GetOpenFrameMs(EPSPerfSystem System)
{
    return PSPerfPrivate::State().FrameMs[PSPerfPrivate::SystemIndex(System)];
}

FPSPerfReport PSPerf::BuildReport(const FPSPlatformTier& Tier, const FString& Scenario)
{
    const PSPerfPrivate::FCaptureState& Capture = PSPerfPrivate::State();

    FPSPerfReport Report;
    Report.Scenario = Scenario;
    Report.TierId = Tier.TierId;
    Report.Platform = UGameplayStatics::GetPlatformName();
    Report.Date = FDateTime::UtcNow().ToIso8601();
    Report.TargetFrameRate = Tier.TargetFrameRate;
    Report.FrameBudgetMs = PSPlatformTiers::GetFrameBudgetMs(Tier);
    Report.Frames = Capture.Frames;
    Report.BusEvents = static_cast<int32>(Capture.Counts[static_cast<int32>(EPSPerfCounter::BusEvents)]);
    Report.AIDecisions = static_cast<int32>(Capture.Counts[static_cast<int32>(EPSPerfCounter::AIDecisions)]);
    Report.FieldScans = static_cast<int32>(Capture.Counts[static_cast<int32>(EPSPerfCounter::FieldScans)]);

    for (int32 Index = 0; Index < PSPerfPrivate::SystemCount; ++Index)
    {
        FPSPerfSystemResult& Result = Report.Systems.AddDefaulted_GetRef();
        Result.System = static_cast<EPSPerfSystem>(Index);
        Result.bMeasured = Capture.bEverRan[Index] && Capture.Frames > 0;
        Result.BudgetMs = FMath::Max(PSPlatformTiers::FindSystemBudget(Tier, Result.System), 0.f);
        if (!Result.bMeasured)
        {
            continue;
        }
        const FPSFrameTimeHistogram& Histogram = Capture.Histograms[Index];
        Result.MeanMs = static_cast<float>(Capture.TotalMs[Index] / Capture.Frames);
        Result.P50Ms = Histogram.GetPercentileMs(50.f);
        Result.P95Ms = Histogram.GetPercentileMs(95.f);
        Result.MaxMs = Histogram.GetMaxMs();
        Result.bOverBudget = PSPlatformTiers::FindSystemBudget(Tier, Result.System) >= 0.f && Result.P95Ms > Result.BudgetMs;
        Report.SystemsP95Ms += Result.P95Ms;
        Report.SystemsOverBudget += Result.bOverBudget ? 1 : 0;
    }

    const double TelemetryMs = Capture.TotalMs[PSPerfPrivate::SystemIndex(EPSPerfSystem::Telemetry)];
    Report.BusEventsPerMs = TelemetryMs > 0.0 ? static_cast<float>(Report.BusEvents / TelemetryMs) : 0.f;
    return Report;
}

FString PSPerf::GetReportDir()
{
    return FPaths::ProjectSavedDir() / TEXT("Profiling");
}

FString PSPerf::GetReportPath(const FString& Scenario, FName TierId)
{
    return GetReportDir() / FString::Printf(TEXT("%s_%s.json"), *Scenario, *TierId.ToString());
}

bool PSPerf::WriteReport(const FPSPerfReport& Report, const FString& Path)
{
    FString Json;
    if (!FJsonObjectConverter::UStructToJsonObjectString(FPSPerfReport::StaticStruct(), &Report, Json, 0, 0))
    {
        return false;
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    return FFileHelper::SaveStringToFile(Json, *Path);
}

bool PSPerf::ReadReport(const FString& Path, FPSPerfReport& OutReport)
{
    FString Json;
    return FFileHelper::LoadFileToString(Json, *Path) && FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutReport, 0, 0);
}
