// PSTelemetrySamplingTests.cpp -- Epic 26 (telemetry sampling layer)
//
// Tests covered:
//   1. The bus numbers its events, finds them by sequence and type, keeps counting across
//      ClearHistory, and announces each one before its typed delegates.
//   2. Data/telemetry_sampling.json loads through UPSDataIngestion and validates; the rate and
//      the budget come from the platform tier; bad tunings and tiers are caught.
//   3. Scripted movement produces the expected snapshot stream: positions, velocities,
//      accelerations and facing per pawn at the sampling rate, a ring that keeps the newest
//      frames, trails, and blended samples between frames.
//   4. Snapshot-vs-event correlation: keyframes at the instant of an event, blended frames for
//      other events, the frames between two events, the events inside a window, and keyframes
//      leaving with their events when the bus's history moves on.
//   5. The budget: frames over it halve the rate, frames well under it double it back, and
//      the rate controls (rate, on/off) govern how many frames are taken.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSPlatformTiers.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "PSTelemetrySamplingSubsystem.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSTelemetrySamplingTests
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

    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const FName PlayerId, EPSTeamSide Side, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }

        FPlayerAttributes Attributes;
        Attributes.PlayerId = PlayerId;
        Attributes.DisplayName = PlayerId.ToString();
        Attributes.Role = Role;
        Pawn->InitializePlayer(Attributes);
        Pawn->TeamSide = Side;
        return Pawn;
    }

    /** Puts Pawn where a script says it is at this moment. */
    static void PlacePawn(APSPlayerPawn* Pawn, const FVector& Location, const FVector& Velocity, float Yaw)
    {
        Pawn->SetActorLocationAndRotation(Location, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
        if (UFloatingPawnMovement* Movement = Pawn->GetFloatingMovementComponent())
        {
            Movement->Velocity = Velocity;
        }
    }

    /** A tuning that never degrades by itself, so a test controls the rate. */
    static FPSTelemetrySamplingTuning SteadyTuning(float RateHz, float HistorySeconds)
    {
        FPSTelemetrySamplingTuning Tuning;
        Tuning.SampleRateHz = RateHz;
        Tuning.HistorySeconds = HistorySeconds;
        Tuning.SampleBudgetMs = 1000.f;
        Tuning.RecoverAfterSamples = 100000;
        return Tuning;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Bus event sequence numbers and lookups
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTelemetryBusSequenceTest,
    "PlaySports.TelemetryBus.EventSequenceAndLookup",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelemetryBusSequenceTest::RunTest(const FString& Parameters)
{
    UPSTelemetryBus* Bus = NewObject<UPSTelemetryBus>();
    if (!TestNotNull(TEXT("Bus is instantiated"), Bus))
    {
        return false;
    }

    TestEqual(TEXT("No sequence before the first event"), Bus->GetLastEventSequence(), 0);
    TestEqual(TEXT("No oldest sequence while empty"), Bus->GetOldestEventSequence(), 0);

    // The recorded delegate fires before the typed one, with the event's sequence.
    TArray<FString> Order;
    int32 RecordedSequence = 0;
    Bus->OnEventRecordedMC.AddLambda([&Order, &RecordedSequence](const FPSTelemetryEvent& Event)
    {
        Order.Add(TEXT("Recorded"));
        RecordedSequence = Event.Sequence;
    });
    Bus->OnCatchMC.AddLambda([&Order](const FPSTelemetryCatchEvent&)
    {
        Order.Add(TEXT("Catch"));
    });

    FPSTelemetryThrowEvent Throw;
    Throw.PasserName = TEXT("QB");
    Bus->PublishThrow(Throw);
    FPSTelemetryCatchEvent Catch;
    Catch.ReceiverName = TEXT("WR");
    Bus->PublishCatch(Catch);
    FPSTelemetryThrowEvent SecondThrow;
    SecondThrow.PasserName = TEXT("QB2");
    Bus->PublishThrow(SecondThrow);

    TestEqual(TEXT("Three events numbered 1..3"), Bus->GetLastEventSequence(), 3);
    TestEqual(TEXT("Oldest is the first"), Bus->GetOldestEventSequence(), 1);
    if (TestEqual(TEXT("Catch announced twice: recorded, then typed"), Order.Num(), 4))
    {
        TestEqual(TEXT("Recorded fires first"), Order[1], FString(TEXT("Recorded")));
        TestEqual(TEXT("Typed fires second"), Order[2], FString(TEXT("Catch")));
    }
    TestEqual(TEXT("Recorded delegate carries the sequence"), RecordedSequence, 3);

    FPSTelemetryEvent Found;
    TestTrue(TEXT("Sequence 2 is found"), Bus->FindEventBySequence(2, Found));
    TestEqual(TEXT("Sequence 2 is the catch"), Found.EventType, EPSTelemetryEventType::Catch);
    TestEqual(TEXT("Found event keeps its sequence"), Found.Sequence, 2);
    TestFalse(TEXT("Sequence 4 doesn't exist yet"), Bus->FindEventBySequence(4, Found));

    TestTrue(TEXT("Latest throw is found"), Bus->FindLatestEventOfType(EPSTelemetryEventType::Throw, Found));
    TestEqual(TEXT("Latest throw is the second"), Found.Sequence, 3);
    TestTrue(TEXT("Latest throw's payload is the second's"), Found.PayloadJson.Contains(TEXT("QB2")));
    TestFalse(TEXT("No tackle published"), Bus->FindLatestEventOfType(EPSTelemetryEventType::Tackle, Found));

    // Past the ring's size the oldest drop off, and their sequences with them.
    for (int32 Index = 0; Index < Bus->GetMaxHistorySize(); ++Index)
    {
        Bus->PublishThrow(Throw);
    }
    const int32 Last = Bus->GetLastEventSequence();
    TestEqual(TEXT("Every event counted"), Last, 3 + Bus->GetMaxHistorySize());
    TestEqual(TEXT("Oldest kept is Last - size + 1"), Bus->GetOldestEventSequence(), Last - Bus->GetMaxHistorySize() + 1);
    TestFalse(TEXT("The dropped catch can't be found"), Bus->FindEventBySequence(2, Found));
    TestTrue(TEXT("The oldest kept can"), Bus->FindEventBySequence(Bus->GetOldestEventSequence(), Found));

    // Clearing keeps counting, so sequences are never reused.
    Bus->ClearHistory();
    TestEqual(TEXT("Cleared history has no oldest"), Bus->GetOldestEventSequence(), 0);
    Bus->PublishCatch(Catch);
    TestEqual(TEXT("Counting continues after a clear"), Bus->GetLastEventSequence(), Last + 1);
    TestEqual(TEXT("Oldest is the new event"), Bus->GetOldestEventSequence(), Last + 1);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Tuning loads from Data/ and validates
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTelemetrySamplingTuningTest,
    "PlaySports.TelemetrySampling.TuningLoadsAndValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelemetrySamplingTuningTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSTelemetrySamplingTuning Loaded;
    Loaded.KeyframeEvents.Reset();
    if (!TestTrue(TEXT("telemetry_sampling.json loads"), Ingestion->LoadTelemetrySamplingTuningFromJson(UPSTelemetrySamplingSubsystem::GetDefaultTuningPath(), Loaded)))
    {
        return false;
    }

    for (const FString& Problem : UPSTelemetrySamplingSubsystem::ValidateTuning(Loaded))
    {
        AddError(FString::Printf(TEXT("telemetry_sampling.json: %s"), *Problem));
    }

    // The struct's defaults equal the file, so a missing file changes nothing.
    const FPSTelemetrySamplingTuning Defaults;
    TestEqual(TEXT("HistorySeconds matches the default"), Loaded.HistorySeconds, Defaults.HistorySeconds);
    TestEqual(TEXT("DegradeAfterSamples matches the default"), Loaded.DegradeAfterSamples, Defaults.DegradeAfterSamples);
    TestEqual(TEXT("RecoverAfterSamples matches the default"), Loaded.RecoverAfterSamples, Defaults.RecoverAfterSamples);
    TestEqual(TEXT("RecoverBelowFraction matches the default"), Loaded.RecoverBelowFraction, Defaults.RecoverBelowFraction);
    TestEqual(TEXT("MaxDegradeLevel matches the default"), Loaded.MaxDegradeLevel, Defaults.MaxDegradeLevel);
    TestTrue(TEXT("KeyframeEvents match the default"), Loaded.KeyframeEvents == Defaults.KeyframeEvents);
    TestTrue(TEXT("The catch takes a keyframe"), Loaded.KeyframeEvents.Contains(EPSTelemetryEventType::Catch));

    // The rate and the budget are per tier; the defaults are the desktop tier's.
    FPSPlatformTierCatalog Tiers;
    if (!TestTrue(TEXT("platform_tiers.json loads"), Ingestion->LoadPlatformTiersFromJson(PSPlatformTiers::GetDefaultCatalogPath(), Tiers)))
    {
        return false;
    }
    const FPSPlatformTier* Desktop = PSPlatformTiers::FindTier(Tiers, TEXT("DesktopHigh"));
    const FPSPlatformTier* Phone = PSPlatformTiers::FindTier(Tiers, TEXT("MobileBaseline"));
    const FPSPlatformTier* LowPhone = PSPlatformTiers::FindTier(Tiers, TEXT("MobileLow"));
    if (!TestNotNull(TEXT("Desktop tier"), Desktop) || !TestNotNull(TEXT("Phone tier"), Phone) || !TestNotNull(TEXT("Low phone tier"), LowPhone))
    {
        return false;
    }
    TestEqual(TEXT("Default rate is the desktop tier's"), Defaults.SampleRateHz, Desktop->TelemetrySampleRateHz);
    TestEqual(TEXT("Default budget is the desktop tier's"), Defaults.SampleBudgetMs, Desktop->TelemetrySampleBudgetMs);
    TestTrue(TEXT("A phone samples less often than the desktop"), Phone->TelemetrySampleRateHz < Desktop->TelemetrySampleRateHz);
    TestTrue(TEXT("The low tier samples least"), LowPhone->TelemetrySampleRateHz < Phone->TelemetrySampleRateHz);
    TestTrue(TEXT("A phone's budget is tighter"), Phone->TelemetrySampleBudgetMs < Desktop->TelemetrySampleBudgetMs);

    // A world's sampler runs the file's policy at the run's tier.
    UWorld* World = PSTelemetrySamplingTests::CreateTestWorld();
    if (TestNotNull(TEXT("Test world"), World))
    {
        UPSTelemetrySamplingSubsystem* Sampler = World->GetSubsystem<UPSTelemetrySamplingSubsystem>();
        if (TestNotNull(TEXT("Sampler exists in a game world"), Sampler))
        {
            const FPSPlatformTier& Active = PSPlatformTiers::GetActiveTier();
            TestEqual(TEXT("Sampler runs the active tier's rate"), Sampler->GetEffectiveSampleRateHz(), Active.TelemetrySampleRateHz);
            TestEqual(TEXT("Sampler runs the active tier's budget"), Sampler->GetTuning().SampleBudgetMs, Active.TelemetrySampleBudgetMs);
            TestEqual(TEXT("Sampler runs the file's history"), Sampler->GetTuning().HistorySeconds, Loaded.HistorySeconds);

            Sampler->ApplyPlatformTier(*Phone);
            TestEqual(TEXT("On the phone tier: its rate"), Sampler->GetEffectiveSampleRateHz(), Phone->TelemetrySampleRateHz);
            TestEqual(TEXT("On the phone tier: its budget"), Sampler->GetStats().SampleBudgetMs, Phone->TelemetrySampleBudgetMs);
            TestTrue(TEXT("On the phone tier: the file's keyframes"), Sampler->GetTuning().KeyframeEvents == Loaded.KeyframeEvents);

            FPSPlatformTier Broken = *Phone;
            Broken.TelemetrySampleRateHz = 0.f;
            Sampler->ApplyPlatformTier(Broken);
            TestEqual(TEXT("An unusable tier rate is refused"), Sampler->GetEffectiveSampleRateHz(), Phone->TelemetrySampleRateHz);
        }
        PSTelemetrySamplingTests::DestroyTestWorld(World);
    }

    FPSPlatformTierCatalog BrokenTiers = Tiers;
    BrokenTiers.Tiers[0].TelemetrySampleBudgetMs = 0.f;
    TestEqual(TEXT("Tier validation catches a zero budget"), PSPlatformTiers::ValidateCatalog(BrokenTiers).Num(), 1);

    FPSTelemetrySamplingTuning Bad;
    Bad.SampleRateHz = 0.f;
    Bad.HistorySeconds = -1.f;
    Bad.SampleBudgetMs = 0.f;
    Bad.DegradeAfterSamples = 0;
    Bad.RecoverAfterSamples = 0;
    Bad.RecoverBelowFraction = 1.5f;
    Bad.MaxDegradeLevel = 20;
    Bad.KeyframeEvents = { EPSTelemetryEventType::Catch, EPSTelemetryEventType::Catch };
    TestEqual(TEXT("Every bad field is reported"), UPSTelemetrySamplingSubsystem::ValidateTuning(Bad).Num(), 8);

    FPSTelemetrySamplingTuning TooBig;
    TooBig.SampleRateHz = 1000.f;
    TooBig.HistorySeconds = 60.f;
    TestEqual(TEXT("A ring too big for memory is reported"), UPSTelemetrySamplingSubsystem::ValidateTuning(TooBig).Num(), 1);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Scripted movement produces the expected snapshot stream
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTelemetrySamplingStreamTest,
    "PlaySports.TelemetrySampling.ScriptedMovementStream",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelemetrySamplingStreamTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSTelemetrySamplingTests::CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    UPSTelemetrySamplingSubsystem* Sampler = World->GetSubsystem<UPSTelemetrySamplingSubsystem>();
    if (!TestNotNull(TEXT("Sampler"), Sampler))
    {
        PSTelemetrySamplingTests::DestroyTestWorld(World);
        return false;
    }

    // 10 Hz, half a second of history: a ring of 5 frames.
    Sampler->SetTuning(PSTelemetrySamplingTests::SteadyTuning(10.f, 0.5f));

    // The receiver accelerates from rest along +X; the defender runs at a steady pace along -Y,
    // facing where he runs. Defense spawns first: frames still list offense first.
    const FVector ReceiverAccel(400.f, 0.f, 0.f);
    const FVector ReceiverStart(0.f, 0.f, 100.f);
    const FVector DefenderVelocity(0.f, -300.f, 0.f);
    const FVector DefenderStart(0.f, 3000.f, 100.f);
    APSPlayerPawn* Defender = PSTelemetrySamplingTests::SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_1"), EPSTeamSide::Defense, DefenderStart);
    APSPlayerPawn* Receiver = PSTelemetrySamplingTests::SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_1"), EPSTeamSide::Offense, ReceiverStart);
    if (!TestNotNull(TEXT("Receiver"), Receiver) || !TestNotNull(TEXT("Defender"), Defender))
    {
        PSTelemetrySamplingTests::DestroyTestWorld(World);
        return false;
    }
    Receiver->GainPossession();

    auto ReceiverAt = [&ReceiverStart, &ReceiverAccel](float T) { return ReceiverStart + 0.5f * ReceiverAccel * T * T; };
    auto DefenderAt = [&DefenderStart, &DefenderVelocity](float T) { return DefenderStart + DefenderVelocity * T; };

    const float Step = 0.05f;
    for (int32 StepIndex = 1; StepIndex <= 20; ++StepIndex)
    {
        const float T = StepIndex * Step;
        PSTelemetrySamplingTests::PlacePawn(Receiver, ReceiverAt(T), ReceiverAccel * T, 0.f);
        PSTelemetrySamplingTests::PlacePawn(Defender, DefenderAt(T), DefenderVelocity, -90.f);
        Sampler->AdvanceTime(Step);
    }

    TestEqual(TEXT("One frame per 0.1 s over 1 s"), Sampler->GetStats().ScheduledFramesTaken, 10);
    TestEqual(TEXT("The ring keeps half a second"), Sampler->GetScheduledFrameCount(), 5);
    TestEqual(TEXT("No events, no keyframes"), Sampler->GetKeyframeCount(), 0);

    const TArray<FPSSnapshotFrame> Frames = Sampler->GetFramesBetween(0.f, 10.f);
    if (TestEqual(TEXT("Every kept frame is returned"), Frames.Num(), 5))
    {
        for (int32 Index = 0; Index < Frames.Num(); ++Index)
        {
            const FPSSnapshotFrame& Frame = Frames[Index];
            const float ExpectedTime = 0.6f + 0.1f * Index;
            const FString At = FString::Printf(TEXT("frame at %.1f s"), ExpectedTime);
            TestEqual(*FString::Printf(TEXT("%s: time"), *At), Frame.Time, ExpectedTime, 1.e-3f);
            TestEqual(*FString::Printf(TEXT("%s: capture order"), *At), Frame.FrameIndex, 5 + Index);
            TestFalse(*FString::Printf(TEXT("%s: scheduled, not a keyframe"), *At), Frame.bKeyframe);
            if (!TestEqual(*FString::Printf(TEXT("%s: both pawns"), *At), Frame.Pawns.Num(), 2))
            {
                continue;
            }

            const FPSPawnSnapshot& WR = Frame.Pawns[0];
            const FPSPawnSnapshot& DB = Frame.Pawns[1];
            TestEqual(*FString::Printf(TEXT("%s: offense listed first"), *At), WR.PlayerId, FName(TEXT("WR_1")));
            TestEqual(*FString::Printf(TEXT("%s: defense second"), *At), DB.PlayerId, FName(TEXT("DB_1")));
            TestEqual(*FString::Printf(TEXT("%s: receiver's role"), *At), WR.Role, EPlayerRole::WideReceiver);
            TestEqual(*FString::Printf(TEXT("%s: defender's side"), *At), DB.TeamSide, EPSTeamSide::Defense);

            TestTrue(*FString::Printf(TEXT("%s: receiver's location"), *At), WR.Location.Equals(ReceiverAt(ExpectedTime), 0.1f));
            TestTrue(*FString::Printf(TEXT("%s: receiver's velocity"), *At), WR.Velocity.Equals(ReceiverAccel * ExpectedTime, 0.1f));
            TestTrue(*FString::Printf(TEXT("%s: receiver's acceleration"), *At), WR.Acceleration.Equals(ReceiverAccel, 0.5f));
            TestEqual(*FString::Printf(TEXT("%s: receiver faces +X"), *At), WR.FacingYaw, 0.f, 0.01f);
            TestTrue(*FString::Printf(TEXT("%s: receiver has the ball"), *At), WR.bHasBall);

            TestTrue(*FString::Printf(TEXT("%s: defender's location"), *At), DB.Location.Equals(DefenderAt(ExpectedTime), 0.1f));
            TestTrue(*FString::Printf(TEXT("%s: defender's velocity"), *At), DB.Velocity.Equals(DefenderVelocity, 0.1f));
            TestTrue(*FString::Printf(TEXT("%s: defender doesn't accelerate"), *At), DB.Acceleration.IsNearlyZero(0.5f));
            TestEqual(*FString::Printf(TEXT("%s: defender faces -Y"), *At), DB.FacingYaw, -90.f, 0.01f);
            TestFalse(*FString::Printf(TEXT("%s: defender has no ball"), *At), DB.bHasBall);
            TestFalse(*FString::Printf(TEXT("%s: nobody is human-controlled"), *At), DB.bUserControlled);
        }
    }

    FPSSnapshotFrame Latest;
    TestTrue(TEXT("Latest frame exists"), Sampler->GetLatestFrame(Latest));
    TestEqual(TEXT("Latest frame is at 1 s"), Latest.Time, 1.f, 1.e-3f);

    // A trail is one pawn's snapshots over a window, oldest first.
    const TArray<FPSPawnSnapshot> Trail = Sampler->GetPawnTrail(TEXT("WR_1"), 0.65f, 1.05f);
    if (TestEqual(TEXT("Trail holds the 0.7..1.0 frames"), Trail.Num(), 4))
    {
        TestTrue(TEXT("Trail starts at 0.7 s"), Trail[0].Location.Equals(ReceiverAt(0.7f), 0.1f));
        TestTrue(TEXT("Trail ends at 1.0 s"), Trail[3].Location.Equals(ReceiverAt(1.f), 0.1f));
    }
    TestEqual(TEXT("No trail for an unknown player"), Sampler->GetPawnTrail(TEXT("Nobody"), 0.f, 10.f).Num(), 0);

    // Between frames, a sample is blended from the frames either side.
    FPSSnapshotFrame Blended;
    if (TestTrue(TEXT("A sample between frames"), Sampler->SampleAt(0.75f, Blended)))
    {
        TestTrue(TEXT("The sample is blended"), Blended.bInterpolated);
        TestEqual(TEXT("The sample is at the asked time"), Blended.Time, 0.75f, 1.e-4f);
        FPSPawnSnapshot WR;
        if (TestTrue(TEXT("Receiver is in the sample"), UPSTelemetrySamplingSubsystem::FindPawnSnapshot(Blended, TEXT("WR_1"), WR)))
        {
            const FVector Midway = (ReceiverAt(0.7f) + ReceiverAt(0.8f)) * 0.5f;
            TestTrue(TEXT("Receiver is halfway between the two frames"), WR.Location.Equals(Midway, 0.1f));
        }
    }
    FPSSnapshotFrame Exact;
    const float FrameTime = Frames.Num() == 5 ? Frames[2].Time : 0.8f;
    if (TestTrue(TEXT("A sample on a frame"), Sampler->SampleAt(FrameTime, Exact)))
    {
        TestFalse(TEXT("A sample on a frame is that frame"), Exact.bInterpolated);
    }
    TestFalse(TEXT("Nothing before the history"), Sampler->SampleAt(0.1f, Blended));
    TestTrue(TEXT("The newest frame holds for one interval"), Sampler->SampleAt(1.05f, Blended));
    TestFalse(TEXT("Nothing well after the newest frame"), Sampler->SampleAt(1.5f, Blended));

    PSTelemetrySamplingTests::DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Snapshot-vs-event correlation
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTelemetrySamplingCorrelationTest,
    "PlaySports.TelemetrySampling.EventCorrelation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelemetrySamplingCorrelationTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSTelemetrySamplingTests::CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    UPSTelemetrySamplingSubsystem* Sampler = World->GetSubsystem<UPSTelemetrySamplingSubsystem>();
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    if (!TestNotNull(TEXT("Sampler"), Sampler) || !TestNotNull(TEXT("Bus"), Bus))
    {
        PSTelemetrySamplingTests::DestroyTestWorld(World);
        return false;
    }
    Sampler->SetTuning(PSTelemetrySamplingTests::SteadyTuning(10.f, 5.f));

    // The receiver accelerates, so a blend between frames differs from where he really was.
    const FVector ReceiverAccel(1000.f, 0.f, 0.f);
    const FVector ReceiverStart(0.f, 0.f, 100.f);
    const FVector QuarterbackSpot(-700.f, 0.f, 100.f);
    const FVector DefenderStart(0.f, 3000.f, 100.f);
    const FVector DefenderVelocity(0.f, -200.f, 0.f);
    APSPlayerPawn* Quarterback = PSTelemetrySamplingTests::SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB_1"), EPSTeamSide::Offense, QuarterbackSpot);
    APSPlayerPawn* Receiver = PSTelemetrySamplingTests::SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_1"), EPSTeamSide::Offense, ReceiverStart);
    APSPlayerPawn* Defender = PSTelemetrySamplingTests::SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_1"), EPSTeamSide::Defense, DefenderStart);
    if (!TestNotNull(TEXT("Quarterback"), Quarterback) || !TestNotNull(TEXT("Receiver"), Receiver) || !TestNotNull(TEXT("Defender"), Defender))
    {
        PSTelemetrySamplingTests::DestroyTestWorld(World);
        return false;
    }

    auto ReceiverAt = [&ReceiverStart, &ReceiverAccel](float T) { return ReceiverStart + 0.5f * ReceiverAccel * T * T; };
    auto DefenderAt = [&DefenderStart, &DefenderVelocity](float T) { return DefenderStart + DefenderVelocity * T; };

    const float Step = 0.05f;
    int32 StepIndex = 0;
    auto StepTo = [&](int32 LastStep)
    {
        while (StepIndex < LastStep)
        {
            ++StepIndex;
            const float T = StepIndex * Step;
            PSTelemetrySamplingTests::PlacePawn(Quarterback, QuarterbackSpot, FVector::ZeroVector, 0.f);
            PSTelemetrySamplingTests::PlacePawn(Receiver, ReceiverAt(T), ReceiverAccel * T, 0.f);
            PSTelemetrySamplingTests::PlacePawn(Defender, DefenderAt(T), DefenderVelocity, -90.f);
            Sampler->AdvanceTime(Step);
        }
    };

    // 0.3 s: the throw (a keyframe).
    StepTo(6);
    FPSTelemetryThrowEvent Throw;
    Throw.PasserName = TEXT("QB_1");
    Throw.TargetReceiverName = TEXT("WR_1");
    Bus->PublishThrow(Throw);

    // 0.65 s, between scheduled frames: the catch (a keyframe).
    StepTo(13);
    FPSTelemetryCatchEvent Catch;
    Catch.ReceiverName = TEXT("WR_1");
    Catch.CatchLocation = ReceiverAt(0.65f);
    Bus->PublishCatch(Catch);

    // 0.75 s: an event that takes no keyframe.
    StepTo(15);
    FPSTelemetryDamageEvent Damage;
    Damage.TargetName = TEXT("WR_1");
    Damage.Amount = 5.f;
    Bus->PublishDamage(Damage);

    // 1.0 s, right after that tick's scheduled frame: the tackle (a keyframe).
    StepTo(20);
    FPSTelemetryTackleEvent Tackle;
    Tackle.TacklerName = TEXT("DB_1");
    Tackle.BallCarrierName = TEXT("WR_1");
    Bus->PublishTackle(Tackle);

    FPSTelemetryEvent ThrowEvent, CatchEvent, DamageEvent, TackleEvent;
    if (!TestTrue(TEXT("Throw is on the bus"), Bus->FindLatestEventOfType(EPSTelemetryEventType::Throw, ThrowEvent))
        || !TestTrue(TEXT("Catch is on the bus"), Bus->FindLatestEventOfType(EPSTelemetryEventType::Catch, CatchEvent))
        || !TestTrue(TEXT("Damage is on the bus"), Bus->FindLatestEventOfType(EPSTelemetryEventType::Damage, DamageEvent))
        || !TestTrue(TEXT("Tackle is on the bus"), Bus->FindLatestEventOfType(EPSTelemetryEventType::Tackle, TackleEvent)))
    {
        PSTelemetrySamplingTests::DestroyTestWorld(World);
        return false;
    }

    TestEqual(TEXT("Three keyframe events, three keyframes"), Sampler->GetKeyframeCount(), 3);

    float EventTime = 0.f;
    TestTrue(TEXT("The throw's time is known"), Sampler->GetEventTime(ThrowEvent.Sequence, EventTime));
    TestEqual(TEXT("The throw happened at 0.3 s"), EventTime, 0.3f, 1.e-3f);

    // Everyone at the moment of the catch: the keyframe, exactly where they were.
    FPSSnapshotFrame AtCatch;
    FPSTelemetryEvent LatestCatch;
    if (TestTrue(TEXT("Frame at the latest catch"), Sampler->GetFrameAtLatestEvent(EPSTelemetryEventType::Catch, AtCatch, LatestCatch)))
    {
        TestEqual(TEXT("It is the catch's event"), LatestCatch.Sequence, CatchEvent.Sequence);
        TestTrue(TEXT("It is a keyframe"), AtCatch.bKeyframe);
        TestFalse(TEXT("It is captured, not blended"), AtCatch.bInterpolated);
        TestEqual(TEXT("Keyframe of a catch"), AtCatch.KeyframeEventType, EPSTelemetryEventType::Catch);
        TestEqual(TEXT("Keyframe carries the catch's sequence"), AtCatch.EventSequence, CatchEvent.Sequence);
        TestEqual(TEXT("Keyframe at 0.65 s"), AtCatch.Time, 0.65f, 1.e-3f);
        TestEqual(TEXT("All three players are in it"), AtCatch.Pawns.Num(), 3);
        FPSPawnSnapshot WR;
        if (TestTrue(TEXT("Receiver is in the catch frame"), UPSTelemetrySamplingSubsystem::FindPawnSnapshot(AtCatch, TEXT("WR_1"), WR)))
        {
            TestTrue(TEXT("Receiver exactly where he caught it"), WR.Location.Equals(ReceiverAt(0.65f), 0.01f));
            const FVector Blend = (ReceiverAt(0.6f) + ReceiverAt(0.7f)) * 0.5f;
            TestFalse(TEXT("Not the blend of the frames either side"), WR.Location.Equals(Blend, 0.5f));
        }
        FPSPawnSnapshot DB;
        if (TestTrue(TEXT("Defender is in the catch frame"), UPSTelemetrySamplingSubsystem::FindPawnSnapshot(AtCatch, TEXT("DB_1"), DB)))
        {
            TestTrue(TEXT("Defender where he was at the catch"), DB.Location.Equals(DefenderAt(0.65f), 0.01f));
        }
    }

    // An event without a keyframe gets a frame blended at its time.
    FPSSnapshotFrame AtDamage;
    if (TestTrue(TEXT("Frame at the damage event"), Sampler->GetFrameAtEvent(DamageEvent.Sequence, AtDamage)))
    {
        TestTrue(TEXT("Blended"), AtDamage.bInterpolated);
        TestEqual(TEXT("Blended at 0.75 s"), AtDamage.Time, 0.75f, 1.e-3f);
        TestEqual(TEXT("Carries the event's sequence"), AtDamage.EventSequence, DamageEvent.Sequence);
        FPSPawnSnapshot WR;
        if (TestTrue(TEXT("Receiver is in the blended frame"), UPSTelemetrySamplingSubsystem::FindPawnSnapshot(AtDamage, TEXT("WR_1"), WR)))
        {
            const FVector Blend = (ReceiverAt(0.7f) + ReceiverAt(0.8f)) * 0.5f;
            TestTrue(TEXT("Receiver halfway between the 0.7 and 0.8 frames"), WR.Location.Equals(Blend, 0.1f));
        }
    }

    // The replay window from the throw to the tackle.
    const TArray<FPSSnapshotFrame> Window = Sampler->GetFramesBetweenEvents(ThrowEvent.Sequence, TackleEvent.Sequence);
    // Throw keyframe; 0.4, 0.5, 0.6; catch keyframe; 0.7, 0.8, 0.9, 1.0; tackle keyframe.
    if (TestEqual(TEXT("Throw-to-tackle window holds 10 frames"), Window.Num(), 10))
    {
        TestTrue(TEXT("Starts with the throw's keyframe"), Window[0].bKeyframe && Window[0].KeyframeEventType == EPSTelemetryEventType::Throw);
        TestTrue(TEXT("Ends with the tackle's keyframe"), Window.Last().bKeyframe && Window.Last().KeyframeEventType == EPSTelemetryEventType::Tackle);
        TestTrue(TEXT("Catch keyframe in the middle"), Window[4].bKeyframe && Window[4].KeyframeEventType == EPSTelemetryEventType::Catch);
        for (int32 Index = 1; Index < Window.Num(); ++Index)
        {
            TestTrue(*FString::Printf(TEXT("Window frame %d follows the one before"), Index), Window[Index].FrameIndex > Window[Index - 1].FrameIndex);
            TestTrue(*FString::Printf(TEXT("Window frame %d is not earlier"), Index), Window[Index].Time >= Window[Index - 1].Time);
        }
        TestEqual(TEXT("The 1.0 s frame was taken before the tackle"), Window[8].EventSequence, DamageEvent.Sequence);
    }

    // And back: the events inside a time window come from the bus.
    const TArray<FPSTelemetryEvent> Events = Sampler->GetEventsBetween(0.25f, 0.7f);
    if (TestEqual(TEXT("Two events between 0.25 and 0.7 s"), Events.Num(), 2))
    {
        TestEqual(TEXT("First the throw"), Events[0].EventType, EPSTelemetryEventType::Throw);
        TestEqual(TEXT("Then the catch"), Events[1].EventType, EPSTelemetryEventType::Catch);
    }

    // Keyframes and event times leave with their events: push the catch out of the bus.
    for (int32 Index = 0; Index < Bus->GetMaxHistorySize(); ++Index)
    {
        Bus->PublishDamage(Damage);
    }
    FPSTelemetryEvent Gone;
    TestFalse(TEXT("The bus has dropped the catch"), Bus->FindEventBySequence(CatchEvent.Sequence, Gone));
    TestEqual(TEXT("Its keyframes went with it"), Sampler->GetKeyframeCount(), 0);
    TestFalse(TEXT("No frame at a dropped event"), Sampler->GetFrameAtEvent(CatchEvent.Sequence, AtCatch));
    TestFalse(TEXT("No time for a dropped event"), Sampler->GetEventTime(CatchEvent.Sequence, EventTime));
    TestEqual(TEXT("Scheduled frames are kept for their own window"), Sampler->GetScheduledFrameCount(), 10);

    PSTelemetrySamplingTests::DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The budget degrades and restores the rate; rate controls
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTelemetrySamplingBudgetTest,
    "PlaySports.TelemetrySampling.BudgetDegradesAndRecovers",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelemetrySamplingBudgetTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSTelemetrySamplingTests::CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    UPSTelemetrySamplingSubsystem* Sampler = World->GetSubsystem<UPSTelemetrySamplingSubsystem>();
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    if (!TestNotNull(TEXT("Sampler"), Sampler) || !TestNotNull(TEXT("Bus"), Bus))
    {
        PSTelemetrySamplingTests::DestroyTestWorld(World);
        return false;
    }

    FPSTelemetrySamplingTuning Tuning;
    Tuning.SampleRateHz = 10.f;
    Tuning.HistorySeconds = 10.f;
    Tuning.SampleBudgetMs = 1.f;
    Tuning.DegradeAfterSamples = 3;
    Tuning.RecoverAfterSamples = 4;
    Tuning.RecoverBelowFraction = 0.5f;
    Tuning.MaxDegradeLevel = 2;
    Sampler->SetTuning(Tuning);
    TestEqual(TEXT("Full rate to start"), Sampler->GetEffectiveSampleRateHz(), 10.f);

    // Three frames in a row over budget halve the rate.
    Sampler->RecordSampleCost(5.f);
    Sampler->RecordSampleCost(5.f);
    TestEqual(TEXT("Two over budget: still full rate"), Sampler->GetDegradeLevel(), 0);
    Sampler->RecordSampleCost(5.f);
    TestEqual(TEXT("Three over budget: one level down"), Sampler->GetDegradeLevel(), 1);
    TestEqual(TEXT("Half rate"), Sampler->GetEffectiveSampleRateHz(), 5.f);

    // A frame back within budget breaks the run.
    Sampler->RecordSampleCost(5.f);
    Sampler->RecordSampleCost(5.f);
    Sampler->RecordSampleCost(0.8f);
    Sampler->RecordSampleCost(5.f);
    TestEqual(TEXT("A broken run doesn't degrade"), Sampler->GetDegradeLevel(), 1);
    Sampler->RecordSampleCost(5.f);
    Sampler->RecordSampleCost(5.f);
    TestEqual(TEXT("A full run degrades again"), Sampler->GetDegradeLevel(), 2);
    TestEqual(TEXT("Quarter rate"), Sampler->GetEffectiveSampleRateHz(), 2.5f);
    for (int32 Index = 0; Index < 6; ++Index)
    {
        Sampler->RecordSampleCost(5.f);
    }
    TestEqual(TEXT("Never below MaxDegradeLevel"), Sampler->GetDegradeLevel(), 2);

    const FPSTelemetrySamplingStats Degraded = Sampler->GetStats();
    TestEqual(TEXT("Stats: last cost"), Degraded.LastSampleCostMs, 5.f);
    TestEqual(TEXT("Stats: budget"), Degraded.SampleBudgetMs, 1.f);
    TestEqual(TEXT("Stats: level"), Degraded.DegradeLevel, 2);
    TestEqual(TEXT("Stats: rate"), Degraded.EffectiveSampleRateHz, 2.5f);
    TestTrue(TEXT("Stats: average cost tracks the costs"), Degraded.AverageSampleCostMs > 1.f);

    // Frames well under budget restore it a level at a time; one merely within budget
    // restarts the count.
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Sampler->RecordSampleCost(0.1f);
    }
    TestEqual(TEXT("Four cheap frames: one level back"), Sampler->GetDegradeLevel(), 1);
    Sampler->RecordSampleCost(0.1f);
    Sampler->RecordSampleCost(0.1f);
    Sampler->RecordSampleCost(0.1f);
    Sampler->RecordSampleCost(0.8f);
    Sampler->RecordSampleCost(0.1f);
    Sampler->RecordSampleCost(0.1f);
    Sampler->RecordSampleCost(0.1f);
    TestEqual(TEXT("A frame near the budget restarts the count"), Sampler->GetDegradeLevel(), 1);
    Sampler->RecordSampleCost(0.1f);
    TestEqual(TEXT("Back to full rate"), Sampler->GetDegradeLevel(), 0);
    TestEqual(TEXT("Full rate again"), Sampler->GetEffectiveSampleRateHz(), 10.f);

    // The degraded rate governs real sampling: half the frames at level 1.
    for (int32 Index = 0; Index < 3; ++Index)
    {
        Sampler->RecordSampleCost(5.f);
    }
    Sampler->SetTuning(PSTelemetrySamplingTests::SteadyTuning(10.f, 10.f));
    TestEqual(TEXT("New tuning keeps the level"), Sampler->GetDegradeLevel(), 1);

    APSPlayerPawn* Runner = PSTelemetrySamplingTests::SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB_1"), EPSTeamSide::Offense, FVector(0.f, 0.f, 100.f));
    if (!TestNotNull(TEXT("Runner"), Runner))
    {
        PSTelemetrySamplingTests::DestroyTestWorld(World);
        return false;
    }

    auto CountFramesOver = [Sampler](int32 Steps)
    {
        const int32 Before = Sampler->GetStats().ScheduledFramesTaken;
        for (int32 Index = 0; Index < Steps; ++Index)
        {
            Sampler->AdvanceTime(0.05f);
        }
        return Sampler->GetStats().ScheduledFramesTaken - Before;
    };

    TestEqual(TEXT("Level 1 at 10 Hz: 5 frames a second"), CountFramesOver(20), 5);
    const FPSTelemetrySamplingStats Measured = Sampler->GetStats();
    TestTrue(TEXT("A real frame's cost is measured"), Measured.LastSampleCostMs >= 0.f && Measured.LastSampleCostMs < 1000.f);

    Sampler->SetSampleRateHz(20.f);
    TestEqual(TEXT("Level 1 at 20 Hz: 10 frames a second"), CountFramesOver(20), 10);

    // Off: no frames, no keyframes, but the clock runs on.
    Sampler->SetSamplingEnabled(false);
    const float ClockBefore = Sampler->GetClock();
    const int32 KeyframesBefore = Sampler->GetKeyframeCount();
    TestEqual(TEXT("Off: no frames"), CountFramesOver(20), 0);
    FPSTelemetryCatchEvent Catch;
    Bus->PublishCatch(Catch);
    TestEqual(TEXT("Off: no keyframes"), Sampler->GetKeyframeCount(), KeyframesBefore);
    TestEqual(TEXT("Off: the clock still runs"), Sampler->GetClock(), ClockBefore + 1.f, 1.e-3f);

    Sampler->SetSamplingEnabled(true);
    TestEqual(TEXT("On again: sampling resumes"), CountFramesOver(20), 10);
    Bus->PublishCatch(Catch);
    TestEqual(TEXT("On again: keyframes resume"), Sampler->GetKeyframeCount(), KeyframesBefore + 1);

    PSTelemetrySamplingTests::DestroyTestWorld(World);
    return true;
}

#endif
