// PSPerfBudgetTests.cpp -- Epic 114 (performance budget and profiling harness)
//
// Tests covered:
//   1. Every platform tier budgets every system once, within its frame; perf_harness.json loads
//      through UPSDataIngestion, matches the struct's defaults and validates; unsound budgets
//      and tuning are reported.
//   2. The profiler: a scope's time is its own (a nested system's time is not counted twice),
//      frames close into histograms, counters count, systems that never ran are "not
//      measured", and a report holds the capture to a tier's budgets and survives its JSON.
//   3. The standard play under full load, headless: every captured frame steps the game's
//      systems, the AI scans the field at most once a frame, the bus stays within its events
//      per play, and the report is written to Saved/Profiling for tools/perf_budget.py.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "PSDataIngestion.h"
#include "PSPerfBudget.h"
#include "PSPerfHarness.h"
#include "PSPlatformTiers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPerfBudgetTests
{
    static UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    static void DestroyTestWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    /** Burns at least Milliseconds of wall time. */
    static void Spin(double Milliseconds)
    {
        const double Until = FPlatformTime::Seconds() + Milliseconds / 1000.0;
        while (FPlatformTime::Seconds() < Until)
        {
        }
    }

    static FPSSystemBudget MakeBudget(EPSPerfSystem System, float Ms)
    {
        FPSSystemBudget Budget;
        Budget.System = System;
        Budget.BudgetMs = Ms;
        return Budget;
    }

    static bool LoadTiers(FPSPlatformTierCatalog& OutCatalog)
    {
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        return Ingestion->LoadPlatformTiersFromJson(PSPlatformTiers::GetDefaultCatalogPath(), OutCatalog);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Budgets and harness tuning validate
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPerfBudgetDataTest,
    "PlaySports.Perf.BudgetsAndHarnessDataValidate",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPerfBudgetDataTest::RunTest(const FString& Parameters)
{
    FPSPlatformTierCatalog Catalog;
    if (!TestTrue(TEXT("platform_tiers.json loads"), PSPerfBudgetTests::LoadTiers(Catalog)))
    {
        return false;
    }
    for (const FString& Problem : PSPlatformTiers::ValidateCatalog(Catalog))
    {
        AddError(FString::Printf(TEXT("platform_tiers.json: %s"), *Problem));
    }
    const UEnum* Systems = StaticEnum<EPSPerfSystem>();
    for (const FPSPlatformTier& Tier : Catalog.Tiers)
    {
        float Total = 0.f;
        for (int32 Index = 0; Systems && Index < Systems->NumEnums() - 1; ++Index)
        {
            const float Budget = PSPlatformTiers::FindSystemBudget(Tier, static_cast<EPSPerfSystem>(Systems->GetValueByIndex(Index)));
            TestTrue(*FString::Printf(TEXT("%s budgets %s"), *Tier.TierId.ToString(), *Systems->GetNameStringByIndex(Index)), Budget >= 0.f);
            Total += FMath::Max(Budget, 0.f);
        }
        TestTrue(*FString::Printf(TEXT("%s's budgets fit its frame"), *Tier.TierId.ToString()), Total <= PSPlatformTiers::GetFrameBudgetMs(Tier));
    }
    const FPSPlatformTier* Phone = PSPlatformTiers::FindTier(Catalog, TEXT("MobileBaseline"));
    if (TestNotNull(TEXT("The phone tier"), Phone))
    {
        // Specs/Platform_Audit.md section 6: the whole AI under 2 ms on the phone.
        TestTrue(TEXT("The phone's AI budget is at most 2 ms"), PSPlatformTiers::FindSystemBudget(*Phone, EPSPerfSystem::AI) <= 2.f);
    }

    FPSPlatformTierCatalog Broken = Catalog;
    if (Broken.Tiers.Num() > 0)
    {
        FPSPlatformTier& First = Broken.Tiers[0];
        First.SystemBudgets.RemoveAll([](const FPSSystemBudget& Budget) { return Budget.System == EPSPerfSystem::Crowd; });
        First.SystemBudgets.Add(PSPerfBudgetTests::MakeBudget(EPSPerfSystem::AI, 1.f));
        TestTrue(TEXT("A missing and a doubled budget are reported"), PSPlatformTiers::ValidateSystemBudgets(First, 0).Num() >= 2);
        FPSPlatformTier Overfull = Catalog.Tiers[0];
        Overfull.TargetFrameRate = 240.f;
        TestTrue(TEXT("Budgets over the frame are reported"), PSPlatformTiers::ValidateSystemBudgets(Overfull, 0).ContainsByPredicate(
            [](const FString& Problem) { return Problem.Contains(TEXT("add up to")); }));
    }

    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSPerfHarnessTuning FromFile;
    if (TestTrue(TEXT("perf_harness.json loads through UPSDataIngestion"), Ingestion->LoadPerfHarnessTuningFromJson(UPSPerfHarness::GetDefaultTuningPath(), FromFile)))
    {
        for (const FString& Problem : UPSPerfHarness::ValidateTuning(FromFile))
        {
            AddError(FString::Printf(TEXT("perf_harness.json: %s"), *Problem));
        }
        const FPSPerfHarnessTuning Defaults;
        TestEqual(TEXT("Frame seconds"), FromFile.FrameSeconds, Defaults.FrameSeconds);
        TestEqual(TEXT("Warmup frames"), FromFile.WarmupFrames, Defaults.WarmupFrames);
        TestEqual(TEXT("Pass frames"), FromFile.PassFrames, Defaults.PassFrames);
        TestEqual(TEXT("Pursuit frames"), FromFile.PursuitFrames, Defaults.PursuitFrames);
        TestEqual(TEXT("Pre-snap frames"), FromFile.PreSnapFrames, Defaults.PreSnapFrames);
        TestEqual(TEXT("Bucket width"), FromFile.HistogramBucketMs, Defaults.HistogramBucketMs);
        TestEqual(TEXT("Bucket count"), FromFile.HistogramBucketCount, Defaults.HistogramBucketCount);
        TestEqual(TEXT("Bus events per play"), FromFile.MaxBusEventsPerPlay, Defaults.MaxBusEventsPerPlay);
        TestEqual(TEXT("Hard fail multiplier"), FromFile.HardFailMultiplier, Defaults.HardFailMultiplier);
        TestEqual(TEXT("Regression tolerance"), FromFile.RegressionTolerance, Defaults.RegressionTolerance);
        TestEqual(TEXT("Minimum regression"), FromFile.MinRegressionMs, Defaults.MinRegressionMs);
        TestEqual(TEXT("Trend window"), FromFile.TrendWindow, Defaults.TrendWindow);
    }
    FPSPerfHarnessTuning Bad;
    Bad.FrameSeconds = 0.f;
    Bad.PassFrames = 0;
    Bad.TrendWindow = 0;
    Bad.HardFailMultiplier = 0.5f;
    Bad.RegressionTolerance = -1.f;
    TestEqual(TEXT("Unsound tuning is reported"), UPSPerfHarness::ValidateTuning(Bad).Num(), 5);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The profiler's scopes, frames, counters and report
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPerfScopeTest,
    "PlaySports.Perf.ScopesTimeExclusively",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPerfScopeTest::RunTest(const FString& Parameters)
{
    // Outside a capture a scope records nothing.
    PSPerf::EndCapture();
    {
        FPSPerfScope Idle(EPSPerfSystem::AI);
    }

    PSPerf::BeginCapture(0.01f, 5000);
    TestTrue(TEXT("Capturing"), PSPerf::IsCapturing());
    {
        // 1 ms of AI, 20 ms of telemetry inside it, 1 ms more of AI.
        FPSPerfScope Outer(EPSPerfSystem::AI);
        PSPerfBudgetTests::Spin(1.0);
        {
            FPSPerfScope Inner(EPSPerfSystem::Telemetry);
            PSPerfBudgetTests::Spin(20.0);
        }
        PSPerfBudgetTests::Spin(1.0);
    }
    const double AIMs = PSPerf::GetOpenFrameMs(EPSPerfSystem::AI);
    const double TelemetryMs = PSPerf::GetOpenFrameMs(EPSPerfSystem::Telemetry);
    TestTrue(TEXT("The nested system has its own time"), TelemetryMs >= 20.0);
    TestTrue(TEXT("The outer system has at least its own time"), AIMs >= 2.0);
    TestTrue(TEXT("...and not the nested system's (exclusive)"), AIMs < 15.0);
    PSPerf::AddCount(EPSPerfCounter::BusEvents, 3);
    PSPerf::AddCount(EPSPerfCounter::FieldScans);
    PSPerf::EndFrame();
    TestEqual(TEXT("A frame end starts the next frame empty"), PSPerf::GetOpenFrameMs(EPSPerfSystem::AI), 0.0);

    // A second frame with no AI and a little simulation.
    {
        FPSPerfScope SimulationScope(EPSPerfSystem::Simulation);
        PSPerfBudgetTests::Spin(0.5);
    }
    PSPerf::EndFrame();
    PSPerf::EndCapture();
    TestFalse(TEXT("Stopped"), PSPerf::IsCapturing());
    TestEqual(TEXT("Two frames"), PSPerf::GetCapturedFrames(), 2);
    TestEqual(TEXT("Bus events counted"), PSPerf::GetCount(EPSPerfCounter::BusEvents), static_cast<int64>(3));
    TestEqual(TEXT("Field scans counted"), PSPerf::GetCount(EPSPerfCounter::FieldScans), static_cast<int64>(1));

    // Held to a tier where AI may take 1 ms: over budget; Simulation within its 10 ms.
    FPSPlatformTier Tier;
    Tier.TierId = TEXT("TestTier");
    Tier.TargetFrameRate = 30.f;
    Tier.SystemBudgets.Add(PSPerfBudgetTests::MakeBudget(EPSPerfSystem::AI, 1.f));
    Tier.SystemBudgets.Add(PSPerfBudgetTests::MakeBudget(EPSPerfSystem::Telemetry, 50.f));
    Tier.SystemBudgets.Add(PSPerfBudgetTests::MakeBudget(EPSPerfSystem::Simulation, 10.f));
    const FPSPerfReport Report = PSPerf::BuildReport(Tier, TEXT("Unit"));
    TestEqual(TEXT("The report has every system"), Report.Systems.Num(), static_cast<int32>(EPSPerfSystem::Audio) + 1);
    const FPSPerfSystemResult* AI = Report.FindSystem(EPSPerfSystem::AI);
    const FPSPerfSystemResult* Simulation = Report.FindSystem(EPSPerfSystem::Simulation);
    const FPSPerfSystemResult* Crowd = Report.FindSystem(EPSPerfSystem::Crowd);
    if (TestNotNull(TEXT("AI result"), AI) && TestNotNull(TEXT("Simulation result"), Simulation) && TestNotNull(TEXT("Crowd result"), Crowd))
    {
        TestTrue(TEXT("AI was measured"), AI->bMeasured);
        TestTrue(TEXT("Its slowest frame is the first"), AI->MaxMs >= 2.f && AI->MaxMs < 15.f);
        TestTrue(TEXT("...and over its 1 ms budget at the 95th percentile"), AI->bOverBudget);
        TestTrue(TEXT("Simulation is within budget"), Simulation->bMeasured && !Simulation->bOverBudget);
        TestFalse(TEXT("A system that never ran is not measured"), Crowd->bMeasured);
    }
    TestEqual(TEXT("One system over budget"), Report.SystemsOverBudget, 1);
    TestEqual(TEXT("A 30 fps frame"), Report.FrameBudgetMs, 1000.f / 30.f);
    TestTrue(TEXT("Bus throughput is worked out"), Report.BusEventsPerMs > 0.f);

    const FString Path = FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("PerfUnitReport.json");
    FPSPerfReport ReadBack;
    TestTrue(TEXT("The report is written"), PSPerf::WriteReport(Report, Path));
    TestTrue(TEXT("...and read back"), PSPerf::ReadReport(Path, ReadBack));
    TestEqual(TEXT("...the same tier"), ReadBack.TierId, Report.TierId);
    TestEqual(TEXT("...the same systems"), ReadBack.Systems.Num(), Report.Systems.Num());
    const FPSPerfSystemResult* ReadAI = ReadBack.FindSystem(EPSPerfSystem::AI);
    TestTrue(TEXT("...the same AI time"), ReadAI && AI && FMath::IsNearlyEqual(ReadAI->P95Ms, AI->P95Ms, 0.001f));
    TestEqual(TEXT("A report is named by scenario and tier"), FPaths::GetCleanFilename(PSPerf::GetReportPath(TEXT("StandardPlay"), TEXT("MobileLow"))), FString(TEXT("StandardPlay_MobileLow.json")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The standard play under full load
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPerfStandardPlayTest,
    "PlaySports.Perf.StandardPlayProfile",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPerfStandardPlayTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSPerfBudgetTests::CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    UPSPerfHarness* Harness = NewObject<UPSPerfHarness>();
    const FPSPerfHarnessTuning& Tuning = Harness->GetTuning();
    const FPSPlatformTier& Tier = PSPlatformTiers::GetActiveTier();

    const FPSPerfReport Report = Harness->RunStandardPlay(World, Tier);
    UPSPerfHarness::LogReport(Report);

    TestEqual(TEXT("The scenario"), Report.Scenario, FString(UPSPerfHarness::StandardPlayScenario));
    TestEqual(TEXT("Held to the running tier"), Report.TierId, Tier.TierId);
    TestEqual(TEXT("Every captured frame closed"), Report.Frames, UPSPerfHarness::GetCapturedFrameCount(Tuning));
    for (const EPSPerfSystem Built : { EPSPerfSystem::Simulation, EPSPerfSystem::AI, EPSPerfSystem::Telemetry, EPSPerfSystem::Overlays,
        EPSPerfSystem::Crowd, EPSPerfSystem::Audio })
    {
        const FPSPerfSystemResult* Result = Report.FindSystem(Built);
        TestTrue(*FString::Printf(TEXT("%s is measured"), *UEnum::GetValueAsString(Built)), Result && Result->bMeasured);
        TestTrue(*FString::Printf(TEXT("%s has its tier's budget"), *UEnum::GetValueAsString(Built)),
            Result && FMath::IsNearlyEqual(Result->BudgetMs, FMath::Max(PSPlatformTiers::FindSystemBudget(Tier, Built), 0.f)));
    }
    // The crowd's excitement and the audio (Epic 23) are built; the rendered crowd and animation
    // are not.
    for (const EPSPerfSystem Unbuilt : { EPSPerfSystem::Animation })
    {
        const FPSPerfSystemResult* Result = Report.FindSystem(Unbuilt);
        TestTrue(*FString::Printf(TEXT("%s isn't built, so isn't measured"), *UEnum::GetValueAsString(Unbuilt)), Result && !Result->bMeasured);
    }

    // The work, counted exactly: these hold on any machine.
    TestTrue(TEXT("The AI decided"), Report.AIDecisions > 0);
    TestTrue(TEXT("The field was scanned"), Report.FieldScans > 0);
    TestTrue(TEXT("...at most once a frame (Epic 17.5)"), Report.FieldScans <= Report.Frames);
    TestTrue(TEXT("The bus recorded the play"), Report.BusEvents > 0);
    TestTrue(*FString::Printf(TEXT("...within %d events (%d)"), Tuning.MaxBusEventsPerPlay, Report.BusEvents), Report.BusEvents <= Tuning.MaxBusEventsPerPlay);

    // For tools/perf_budget.py (CI's budget check and trend) and the artifact.
    const FString Path = PSPerf::GetReportPath(UPSPerfHarness::StandardPlayScenario, Tier.TierId);
    TestTrue(*FString::Printf(TEXT("The report is written to %s"), *Path), PSPerf::WriteReport(Report, Path));

    PSPerfBudgetTests::DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
