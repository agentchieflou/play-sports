// PSSessionTelemetryTests.cpp -- Epic 117 (crash reporting and session telemetry)
//
// Tests covered:
//   1. The frame-time histogram reports percentiles to bucket resolution, and the slowest
//      frame for the overflow bucket.
//   2. The tuning in Data/session_telemetry.json loads and equals the struct defaults;
//      ValidateTuning rejects nonsense.
//   3. A session counts the plays snapped on the world's bus, reads its mode from a travel URL,
//      and keeps the crash context's PS.* keys current until the world is cleaned up.
//   4. Consent gates the store: nothing is written before opting in; an opted-in session is
//      saved open, checkpointed, and ended cleanly by the world's cleanup; a short session is
//      dropped; opting out erases the slot and its backup.
//   5. The report counts sessions that never ended cleanly (crashes) and usage per mode; the
//      store keeps only the newest sessions.
//   6. Crash breadcrumbs are the bus history's last events, oldest first.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "PSCrashContext.h"
#include "PSSaveSubsystem.h"
#include "PSSessionTelemetry.h"
#include "PSSessionTelemetrySave.h"
#include "PSTelemetryBus.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSSessionTelemetryTests
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

    static void PublishSnaps(UPSTelemetryBus* Bus, int32 Count)
    {
        for (int32 Index = 0; Index < Count; ++Index)
        {
            FPSTelemetrySnapEvent Snap;
            Snap.YardLine = 20 + Index;
            Snap.Down = 1;
            Snap.Distance = 10;
            Bus->PublishSnap(Snap);
        }
    }

    static const TCHAR* TestSlot = TEXT("Test_TelemetrySessions");
}

// ---------------------------------------------------------------------------
// 1. Histogram percentiles
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSSessionFrameHistogramTest,
    "PlaySports.SessionTelemetry.FrameHistogramPercentiles",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSessionFrameHistogramTest::RunTest(const FString& Parameters)
{
    FPSFrameTimeHistogram Histogram;
    Histogram.Reset(0.5f, 200);
    TestEqual(TEXT("No frames reads 0"), Histogram.GetPercentileMs(50.f), 0.f);

    // 90 frames at 16.2 ms, 9 at 33.3 ms, 1 at 120 ms (past the 100 ms of buckets).
    for (int32 Index = 0; Index < 90; ++Index)
    {
        Histogram.AddFrame(0.0162f);
    }
    for (int32 Index = 0; Index < 9; ++Index)
    {
        Histogram.AddFrame(0.0333f);
    }
    Histogram.AddFrame(0.120f);

    TestEqual(TEXT("100 frames counted"), Histogram.Num(), 100);
    TestEqual(TEXT("p50 is the 16.2 ms bucket, rounded up"), Histogram.GetPercentileMs(50.f), 16.5f);
    TestEqual(TEXT("p90 is still the 16.2 ms bucket"), Histogram.GetPercentileMs(90.f), 16.5f);
    TestEqual(TEXT("p95 is the 33.3 ms bucket"), Histogram.GetPercentileMs(95.f), 33.5f);
    TestEqual(TEXT("p99 is the 99th frame, not the slowest"), Histogram.GetPercentileMs(99.f), 33.5f);
    TestEqual(TEXT("p100 is the overflow bucket: the slowest frame"), Histogram.GetPercentileMs(100.f), 120.f, 0.01f);
    TestEqual(TEXT("Max frame"), Histogram.GetMaxMs(), 120.f, 0.01f);

    Histogram.Reset(0.5f, 200);
    Histogram.AddFrame(0.010f);
    TestEqual(TEXT("A percentile never reads above the slowest frame"), Histogram.GetPercentileMs(99.f), 10.f, 0.01f);
    return true;
}

// ---------------------------------------------------------------------------
// 2. Tuning
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSSessionTuningTest,
    "PlaySports.SessionTelemetry.TuningMatchesData",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSessionTuningTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSSessionTelemetryTests::CreateTestWorld();
    UPSSessionTelemetrySubsystem* Telemetry = World ? World->GetSubsystem<UPSSessionTelemetrySubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Session subsystem"), Telemetry))
    {
        if (World)
        {
            PSSessionTelemetryTests::DestroyTestWorld(World);
        }
        return false;
    }
    TestTrue(TEXT("Data/session_telemetry.json loads"), Telemetry->LoadTuningFromJson(UPSSessionTelemetrySubsystem::GetDefaultTuningPath()));

    const FPSSessionTelemetryTuning Defaults;
    const FPSSessionTelemetryTuning& Loaded = Telemetry->GetTuning();
    TestEqual(TEXT("FrameTimeBucketMs"), Loaded.FrameTimeBucketMs, Defaults.FrameTimeBucketMs);
    TestEqual(TEXT("FrameTimeBucketCount"), Loaded.FrameTimeBucketCount, Defaults.FrameTimeBucketCount);
    TestEqual(TEXT("Percentile count"), Loaded.Percentiles.Num(), Defaults.Percentiles.Num());
    for (int32 Index = 0; Index < FMath::Min(Loaded.Percentiles.Num(), Defaults.Percentiles.Num()); ++Index)
    {
        TestEqual(FString::Printf(TEXT("Percentile %d"), Index), Loaded.Percentiles[Index], Defaults.Percentiles[Index]);
    }
    TestEqual(TEXT("MinSessionSeconds"), Loaded.MinSessionSeconds, Defaults.MinSessionSeconds);
    TestEqual(TEXT("MaxStoredSessions"), Loaded.MaxStoredSessions, Defaults.MaxStoredSessions);
    TestEqual(TEXT("CheckpointEveryPlays"), Loaded.CheckpointEveryPlays, Defaults.CheckpointEveryPlays);
    TestEqual(TEXT("CrashBreadcrumbCount"), Loaded.CrashBreadcrumbCount, Defaults.CrashBreadcrumbCount);
    TestEqual(TEXT("The defaults are sound"), UPSSessionTelemetrySubsystem::ValidateTuning(Defaults).Num(), 0);
    PSSessionTelemetryTests::DestroyTestWorld(World);

    FPSSessionTelemetryTuning Broken;
    Broken.FrameTimeBucketMs = 0.f;
    Broken.Percentiles = { 0.f, 101.f };
    Broken.MaxStoredSessions = 0;
    TestEqual(TEXT("Broken tuning reports each problem"), UPSSessionTelemetrySubsystem::ValidateTuning(Broken).Num(), 4);
    return true;
}

// ---------------------------------------------------------------------------
// 3. Plays, mode and the crash context
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSSessionCountsPlaysTest,
    "PlaySports.SessionTelemetry.CountsPlaysFromTheBus",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSessionCountsPlaysTest::RunTest(const FString& Parameters)
{
    using namespace PSSessionTelemetryTests;

    // The options UPSMenuComponent::BuildTravelOptions puts on the travel URL.
    FURL PlayNowURL;
    PlayNowURL.AddOption(TEXT("mode=PlayNow"));
    PlayNowURL.AddOption(TEXT("team=Hawks"));
    FURL FrontEndURL;
    FrontEndURL.AddOption(TEXT("game=Menu"));
    TestEqual(TEXT("Mode from the travel URL"), UPSSessionTelemetrySubsystem::ModeFromURL(PlayNowURL), FString(TEXT("PlayNow")));
    TestEqual(TEXT("The front end is its game option"), UPSSessionTelemetrySubsystem::ModeFromURL(FrontEndURL), FString(TEXT("Menu")));
    TestEqual(TEXT("No option is Unspecified"), UPSSessionTelemetrySubsystem::ModeFromURL(FURL()), FString(TEXT("Unspecified")));

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("World created"), World))
    {
        return false;
    }
    UPSSessionTelemetrySubsystem* Telemetry = World->GetSubsystem<UPSSessionTelemetrySubsystem>();
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    if (!TestNotNull(TEXT("Every game world has a session"), Telemetry) || !TestNotNull(TEXT("Bus"), Bus))
    {
        DestroyTestWorld(World);
        return false;
    }

    PublishSnaps(Bus, 1);
    TestFalse(TEXT("No session before BeginPlay"), Telemetry->IsSessionOpen());
    TestEqual(TEXT("A snap before the session counts nothing"), Telemetry->GetPlayCount(), 0);

    Telemetry->StartSession(TEXT("PlayNow"), nullptr);
    TestTrue(TEXT("Session open"), Telemetry->IsSessionOpen());
    TestEqual(TEXT("The crash context names the mode"), FPSCrashContext::GetValue(FPSCrashContext::ModeKey), FString(TEXT("PlayNow")));

    PublishSnaps(Bus, 3);
    FPSTelemetryTackleEvent Tackle;
    Tackle.TacklerName = TEXT("LB_TEST");
    Tackle.BallCarrierName = TEXT("RB_TEST");
    Bus->PublishTackle(Tackle);
    for (int32 Index = 0; Index < 60; ++Index)
    {
        Telemetry->RecordFrame(1.f / 60.f);
    }

    const FPSSessionSummary Summary = Telemetry->BuildSummary(false);
    TestEqual(TEXT("Three plays"), Summary.PlayCount, 3);
    TestEqual(TEXT("Mode"), Summary.Mode, FString(TEXT("PlayNow")));
    TestEqual(TEXT("Sixty frames"), Summary.FrameCount, 60);
    TestEqual(TEXT("One second of play"), Summary.DurationSeconds, 1.f, 0.001f);
    TestEqual(TEXT("One entry per tuned percentile"), Summary.FrameTimePercentiles.Num(), Telemetry->GetTuning().Percentiles.Num());
    if (Summary.FrameTimePercentiles.Num() > 0)
    {
        TestEqual(TEXT("p50 of steady 60 fps frames"), Summary.FrameTimePercentiles[0].Milliseconds, 1000.f / 60.f, 0.5f);
    }
    TestTrue(TEXT("A session id"), Summary.SessionId.IsValid());
    TestFalse(TEXT("Platform tier recorded"), Summary.PlatformTier.IsEmpty());

    TestEqual(TEXT("The crash context counts the plays"), FPSCrashContext::GetValue(FPSCrashContext::PlaysKey), FString(TEXT("3")));
    const FString Breadcrumbs = FPSCrashContext::GetValue(FPSCrashContext::RecentEventsKey);
    TestTrue(TEXT("Breadcrumbs carry the snaps"), Breadcrumbs.Contains(TEXT("Snap:")));
    TestTrue(TEXT("Breadcrumbs carry the tackle"), Breadcrumbs.Contains(TEXT("Tackle: Tackler=LB_TEST, Carrier=RB_TEST")));

    DestroyTestWorld(World);
    TestTrue(TEXT("The world's cleanup clears the crash context"), FPSCrashContext::GetValue(FPSCrashContext::ModeKey).IsEmpty());
    TestTrue(TEXT("... including the breadcrumbs"), FPSCrashContext::GetValue(FPSCrashContext::RecentEventsKey).IsEmpty());
    return true;
}

// ---------------------------------------------------------------------------
// 4. Consent and the store
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSSessionConsentTest,
    "PlaySports.SessionTelemetry.ConsentGatesTheStore",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSessionConsentTest::RunTest(const FString& Parameters)
{
    using namespace PSSessionTelemetryTests;

    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    const FString Slot = TestSlot;
    TestTrue(TEXT("Start from no store"), Saves->DeleteSlot(Slot));
    TestEqual(TEXT("Never asked"), UPSSessionTelemetrySave::GetConsentInSlot(Saves, Slot), EPSTelemetryConsent::NotAsked);

    // Not asked: a whole session writes nothing.
    {
        UWorld* World = CreateTestWorld();
        UPSSessionTelemetrySubsystem* Telemetry = World ? World->GetSubsystem<UPSSessionTelemetrySubsystem>() : nullptr;
        if (!TestNotNull(TEXT("Session subsystem"), Telemetry))
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
            return false;
        }
        Telemetry->StartSession(TEXT("PlayNow"), Saves, Slot);
        PublishSnaps(World->GetSubsystem<UPSTelemetryBus>(), 6);
        for (int32 Index = 0; Index < 10; ++Index)
        {
            Telemetry->RecordFrame(1.f);
        }
        DestroyTestWorld(World);
        TestFalse(TEXT("No store without consent"), Saves->DoesSlotExist(Slot));
    }

    TestTrue(TEXT("Opting in saves"), UPSSessionTelemetrySave::SetConsentInSlot(Saves, Slot, true));
    TestEqual(TEXT("Opted in"), UPSSessionTelemetrySave::GetConsentInSlot(Saves, Slot), EPSTelemetryConsent::OptedIn);

    // Opted in: saved open, checkpointed, ended cleanly by the cleanup.
    FGuid KeptSession;
    int32 ExpectedPlays = 0;
    {
        UWorld* World = CreateTestWorld();
        UPSSessionTelemetrySubsystem* Telemetry = World ? World->GetSubsystem<UPSSessionTelemetrySubsystem>() : nullptr;
        if (!TestNotNull(TEXT("Session subsystem"), Telemetry))
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
            return false;
        }
        Telemetry->StartSession(TEXT("Practice"), Saves, Slot);
        KeptSession = Telemetry->BuildSummary(false).SessionId;

        const UPSSessionTelemetrySave* Open = UPSSessionTelemetrySave::LoadStore(Saves, Slot);
        TestTrue(TEXT("Saved as open at the start"), Open && Open->Sessions.Num() == 1 && !Open->Sessions[0].bEndedCleanly);

        const int32 CheckpointEvery = Telemetry->GetTuning().CheckpointEveryPlays;
        PublishSnaps(World->GetSubsystem<UPSTelemetryBus>(), CheckpointEvery);
        const UPSSessionTelemetrySave* Checkpoint = UPSSessionTelemetrySave::LoadStore(Saves, Slot);
        TestTrue(TEXT("Checkpointed with its plays, still open"),
            Checkpoint && Checkpoint->Sessions.Num() == 1 && Checkpoint->Sessions[0].PlayCount == CheckpointEvery && !Checkpoint->Sessions[0].bEndedCleanly);

        PublishSnaps(World->GetSubsystem<UPSTelemetryBus>(), 1);
        ExpectedPlays = CheckpointEvery + 1;
        for (int32 Index = 0; Index < 10; ++Index)
        {
            Telemetry->RecordFrame(1.f);
        }
        DestroyTestWorld(World);
    }
    {
        const UPSSessionTelemetrySave* Store = UPSSessionTelemetrySave::LoadStore(Saves, Slot);
        if (TestNotNull(TEXT("Store after the session"), Store) && TestEqual(TEXT("One session"), Store->Sessions.Num(), 1))
        {
            const FPSSessionSummary& Kept = Store->Sessions[0];
            TestTrue(TEXT("The same session"), Kept.SessionId == KeptSession);
            TestTrue(TEXT("The world's cleanup ended it cleanly"), Kept.bEndedCleanly);
            TestEqual(TEXT("Its plays"), Kept.PlayCount, ExpectedPlays);
            TestEqual(TEXT("Its mode"), Kept.Mode, FString(TEXT("Practice")));
            TestEqual(TEXT("Its play time"), Kept.DurationSeconds, 10.f, 0.001f);
            TestFalse(TEXT("Day only"), Kept.Date.Contains(TEXT(":")));
        }
    }

    // A session shorter than MinSessionSeconds is dropped when it ends cleanly.
    {
        UWorld* World = CreateTestWorld();
        UPSSessionTelemetrySubsystem* Telemetry = World ? World->GetSubsystem<UPSSessionTelemetrySubsystem>() : nullptr;
        if (!TestNotNull(TEXT("Session subsystem"), Telemetry))
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
            return false;
        }
        Telemetry->StartSession(TEXT("Menu"), Saves, Slot);
        Telemetry->RecordFrame(0.5f);
        DestroyTestWorld(World);
        const UPSSessionTelemetrySave* Store = UPSSessionTelemetrySave::LoadStore(Saves, Slot);
        TestTrue(TEXT("Only the long session remains"), Store && Store->Sessions.Num() == 1 && Store->Sessions[0].SessionId == KeptSession);
    }

    // Opting out erases everything, backup included, and remembers the answer.
    TestTrue(TEXT("Opting out saves"), UPSSessionTelemetrySave::SetConsentInSlot(Saves, Slot, false));
    TestEqual(TEXT("Opted out"), UPSSessionTelemetrySave::GetConsentInSlot(Saves, Slot), EPSTelemetryConsent::OptedOut);
    const UPSSessionTelemetrySave* Erased = UPSSessionTelemetrySave::LoadStore(Saves, Slot);
    TestTrue(TEXT("No sessions after opting out"), Erased && Erased->Sessions.Num() == 0);
    TestFalse(TEXT("No backup holds the old sessions"), FPaths::FileExists(UPSSaveSubsystem::GetSlotPath(Slot) + TEXT(".bak")));

    TestTrue(TEXT("Clean up"), Saves->DeleteSlot(Slot));
    return true;
}

// ---------------------------------------------------------------------------
// 5. Report
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSSessionReportTest,
    "PlaySports.SessionTelemetry.ReportCountsCrashesAndModes",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSessionReportTest::RunTest(const FString& Parameters)
{
    UPSSessionTelemetrySave* Store = NewObject<UPSSessionTelemetrySave>();
    TestEqual(TEXT("A telemetry store saves in the Telemetry category"), Store->Category, EPSSaveCategory::Telemetry);

    auto MakeSession = [](const TCHAR* InMode, int32 Plays, float Seconds, bool bClean)
    {
        FPSSessionSummary Session;
        Session.SessionId = FGuid::NewGuid();
        Session.Mode = InMode;
        Session.PlayCount = Plays;
        Session.DurationSeconds = Seconds;
        Session.bEndedCleanly = bClean;
        return Session;
    };

    const FPSSessionSummary First = MakeSession(TEXT("PlayNow"), 10, 300.f, true);
    FPSSessionSummary Crashed = MakeSession(TEXT("Practice"), 2, 40.f, false);
    Store->UpsertSession(First, 3);
    Store->UpsertSession(Crashed, 3);
    Store->UpsertSession(MakeSession(TEXT("PlayNow"), 20, 600.f, true), 3);

    Crashed.PlayCount = 4;
    Store->UpsertSession(Crashed, 3);
    TestEqual(TEXT("Upserting a stored session replaces it"), Store->Sessions.Num(), 3);

    FPSSessionTelemetryReport Report = Store->BuildReport();
    TestEqual(TEXT("Three sessions"), Report.Sessions, 3);
    TestEqual(TEXT("One never ended cleanly"), Report.UncleanSessions, 1);
    const FPSModeUsage* PlayNow = Report.Modes.FindByPredicate([](const FPSModeUsage& Usage) { return Usage.Mode == TEXT("PlayNow"); });
    const FPSModeUsage* Practice = Report.Modes.FindByPredicate([](const FPSModeUsage& Usage) { return Usage.Mode == TEXT("Practice"); });
    if (TestNotNull(TEXT("Play Now usage"), PlayNow))
    {
        TestEqual(TEXT("Play Now sessions"), PlayNow->Sessions, 2);
        TestEqual(TEXT("Play Now plays"), PlayNow->Plays, 30);
        TestEqual(TEXT("Play Now seconds"), PlayNow->Seconds, 900.f);
    }
    if (TestNotNull(TEXT("Practice usage"), Practice))
    {
        TestEqual(TEXT("Practice plays, from the replaced record"), Practice->Plays, 4);
    }
    TestTrue(TEXT("The report serializes"), UPSSessionTelemetrySave::ReportToJson(Report).Contains(TEXT("uncleanSessions"), ESearchCase::IgnoreCase));

    Store->UpsertSession(MakeSession(TEXT("Franchise"), 1, 60.f, true), 3);
    TestEqual(TEXT("The store keeps the newest MaxSessions"), Store->Sessions.Num(), 3);
    TestFalse(TEXT("The oldest went first"), Store->Sessions.ContainsByPredicate([&First](const FPSSessionSummary& Session) { return Session.SessionId == First.SessionId; }));

    Store->RemoveSession(Crashed.SessionId);
    TestEqual(TEXT("RemoveSession"), Store->BuildReport().UncleanSessions, 0);
    return true;
}

// ---------------------------------------------------------------------------
// 6. Crash breadcrumbs
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSCrashBreadcrumbsTest,
    "PlaySports.CrashContext.BreadcrumbsKeepTheLastEvents",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCrashBreadcrumbsTest::RunTest(const FString& Parameters)
{
    TArray<FPSTelemetryEvent> History;
    for (int32 Index = 1; Index <= 5; ++Index)
    {
        FPSTelemetryEvent& Event = History.AddDefaulted_GetRef();
        Event.Timestamp = static_cast<float>(Index);
        Event.Description = FString::Printf(TEXT("Event %d"), Index);
    }

    const FString Text = FPSCrashContext::FormatBreadcrumbs(History, 3);
    TArray<FString> Lines;
    Text.ParseIntoArrayLines(Lines);
    if (TestEqual(TEXT("The last three"), Lines.Num(), 3))
    {
        TestEqual(TEXT("Oldest first"), Lines[0], FString(TEXT("[3.0s] Event 3")));
        TestEqual(TEXT("Newest last"), Lines[2], FString(TEXT("[5.0s] Event 5")));
    }
    TestEqual(TEXT("More than there are gives all"), FPSCrashContext::FormatBreadcrumbs(History, 50).Len(), FPSCrashContext::FormatBreadcrumbs(History, 5).Len());
    TestTrue(TEXT("None asked: empty"), FPSCrashContext::FormatBreadcrumbs(History, 0).IsEmpty());

    FPSCrashContext::SetSession(FGuid::NewGuid(), TEXT("Franchise"), TEXT("DesktopHigh"), 7, 42.f);
    FPSCrashContext::SetBreadcrumbs(History, 2);
    TestEqual(TEXT("Mode key"), FPSCrashContext::GetValue(FPSCrashContext::ModeKey), FString(TEXT("Franchise")));
    TestEqual(TEXT("Plays key"), FPSCrashContext::GetValue(FPSCrashContext::PlaysKey), FString(TEXT("7")));
    TestEqual(TEXT("Seconds key"), FPSCrashContext::GetValue(FPSCrashContext::SessionSecondsKey), FString(TEXT("42.0")));
    TestTrue(TEXT("Breadcrumb key"), FPSCrashContext::GetValue(FPSCrashContext::RecentEventsKey).StartsWith(TEXT("[4.0s] Event 4")));

    FPSCrashContext::Clear();
    TestTrue(TEXT("Clear removes the keys"), FPSCrashContext::GetValue(FPSCrashContext::PlaysKey).IsEmpty());
    TestTrue(TEXT("... all of them"), FPSCrashContext::GetValue(FPSCrashContext::RecentEventsKey).IsEmpty());
    return true;
}

#endif
