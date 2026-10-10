// PSReplaySystemTests.cpp -- Epic 41 (replay system)
//
// A scripted play in a headless world: a receiver runs a straight line, a corner trails him,
// the ball goes from the quarterback to the receiver, and the play's events are published on
// the telemetry bus while the telemetry sampler records everyone. UPSReplaySubsystem cuts that
// play into a clip and plays it back on the field.
//
// Tests covered:
//   1. Data/replay.json loads and validates, bad tunings are refused, the platform tiers carry
//      a pose rate, and a game-state event reads back as the play state it announced.
//   2. A clip joins the bus's history and the sampler's snapshots: the frames from the pre-roll
//      before the snap to the post-roll after the whistle, the events in that span in order,
//      each timed and ticked on the frames, the opening situation from the last game state.
//      Finding the last play, and clips that can't be cut.
//   3. State playback poses the field: the pawns and the ball at the blended frame at the
//      playhead, collision off, the sampler showing the replay and recording none of it, the
//      end of the clip, and everything put back afterwards.
//   4. Transport: pause, slow motion, frame steps, scrubbing, the playhead's limits, playing
//      from the end, and the platform tier's pose rate.
//   5. Persistence: a clip saved, listed and loaded is the same play, with its frames thinned
//      to the save rate (keyframes kept), and plays back by PlayerId.
//   6. The controls' catalog: a Replay context over every depth context, each button with a key,
//      a pad button and their glyphs, and a touch twin that does what its pad button does. The
//      buttons, Pause and the Move stick trigger while the game is paused under the replay.
//   7. The buttons drive the replay through the player controller, and leave with it.
//   8. Every camera in replay: the director, both all-22 rigs, the skycam and the free camera
//      on the Move stick, and the broadcast camera given back as it was.
//   9. Automatic replays: after a score or a turnover (not a plain play or a kick), at the
//      rule's speed and angle, giving the game back after the end unless the viewer took the
//      controls; the next snap cancels one; reduced motion opens on the still rig.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "Misc/Paths.h"
#include "Engine/GameInstance.h"
#include "PSBall.h"
#include "PSBroadcastCamera.h"
#include "PSCameraAll22Component.h"
#include "PSCameraDirectorComponent.h"
#include "PSDataIngestion.h"
#include "PSInputConfig.h"
#include "PSPlayerController.h"
#include "PSSettingsSubsystem.h"
#include "PSTouchControls.h"
#include "PSUIAccessibilitySubsystem.h"
#include "PSDeterminism.h"
#include "PSGameStateEvents.h"
#include "PSPlatformTiers.h"
#include "PSPlayerPawn.h"
#include "PSReplaySubsystem.h"
#include "PSTelemetryBus.h"
#include "PSTelemetrySamplingSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSReplaySystemTests
{
    constexpr float StepSeconds = 0.1f;
    constexpr float ReceiverSpeed = 200.f;

    UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    void DestroyTestWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, EPSTeamSide Side)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
        if (Pawn)
        {
            FPlayerAttributes Attributes;
            Attributes.PlayerId = PlayerId;
            Attributes.DisplayName = PlayerId;
            Attributes.Role = Role;
            Pawn->InitializePlayer(Attributes);
            Pawn->TeamSide = Side;
        }
        return Pawn;
    }

    void Place(APSPlayerPawn* Pawn, const FVector& Location, const FVector& Velocity)
    {
        Pawn->SetActorLocationAndRotation(Location, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
        if (UFloatingPawnMovement* Movement = Pawn->GetFloatingMovementComponent())
        {
            Movement->Velocity = Velocity;
        }
    }

    FVector QuarterbackAt(float Time)
    {
        return FVector(-500.f, 0.f, 90.f);
    }

    /** The receiver runs +X from the origin's sideline mark at ReceiverSpeed. */
    FVector ReceiverAt(float Time)
    {
        return FVector(ReceiverSpeed * Time, 500.f, 90.f);
    }

    FVector CornerAt(float Time)
    {
        return FVector(ReceiverSpeed * Time - 300.f, 600.f, 90.f);
    }

    /** With the quarterback until the throw at 1.5 s, in the air until the catch at 2.0 s,
     *  then with the receiver. */
    FVector BallAt(float Time)
    {
        if (Time <= 1.5f)
        {
            return QuarterbackAt(Time);
        }
        if (Time <= 2.f)
        {
            return FMath::Lerp(QuarterbackAt(1.5f), ReceiverAt(2.f), (Time - 1.5f) / 0.5f);
        }
        return ReceiverAt(Time);
    }

    struct FScriptedPlay
    {
        APSPlayerPawn* Quarterback = nullptr;
        APSPlayerPawn* Receiver = nullptr;
        APSPlayerPawn* Corner = nullptr;
        APSBall* Ball = nullptr;
        int32 SnapSequence = 0;
        int32 CatchSequence = 0;
        int32 WhistleSequence = 0;
    };

    void PlaceAll(const FScriptedPlay& Play, float Time)
    {
        Place(Play.Quarterback, QuarterbackAt(Time), FVector::ZeroVector);
        Place(Play.Receiver, ReceiverAt(Time), FVector(ReceiverSpeed, 0.f, 0.f));
        Place(Play.Corner, CornerAt(Time), FVector(ReceiverSpeed, 0.f, 0.f));
        Play.Ball->SetActorLocation(BallAt(Time), false, nullptr, ETeleportType::TeleportPhysics);
    }

    FPSTelemetrySamplingTuning SteadyTuning()
    {
        FPSTelemetrySamplingTuning Tuning;
        Tuning.SampleRateHz = 1.f / StepSeconds;
        Tuning.HistorySeconds = 20.f;
        Tuning.SampleBudgetMs = 1000.f;
        Tuning.RecoverAfterSamples = 100000;
        return Tuning;
    }

    FPSReplayTuning TestReplayTuning()
    {
        FPSReplayTuning Tuning;
        Tuning.PreRollSeconds = 0.5f;
        Tuning.PostRollSeconds = 0.5f;
        Tuning.PlaybackRates = { 1.f, 0.5f, 0.25f };
        Tuning.SaveFrameRateHz = 5.f;
        return Tuning;
    }

    /**
     * Spawns the players and the ball, announces 3rd and 7 at the 45, then runs 3.5 s at
     * 10 frames a second: the snap at 1.0 s, the throw at 1.5, the catch at 2.0, the tackle at
     * 2.5 and the whistle (a PhaseChange to Scoring) at 2.6.
     */
    bool RunScriptedPlay(UWorld* World, FScriptedPlay& OutPlay)
    {
        UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
        UPSTelemetrySamplingSubsystem* Sampler = World->GetSubsystem<UPSTelemetrySamplingSubsystem>();
        if (!Bus || !Sampler)
        {
            return false;
        }

        OutPlay.Quarterback = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB_01"), EPSTeamSide::Offense);
        OutPlay.Receiver = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_01"), EPSTeamSide::Offense);
        OutPlay.Corner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_01"), EPSTeamSide::Defense);
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        OutPlay.Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), QuarterbackAt(0.f), FRotator::ZeroRotator, SpawnParams);
        if (!OutPlay.Quarterback || !OutPlay.Receiver || !OutPlay.Corner || !OutPlay.Ball)
        {
            return false;
        }

        Sampler->SetTuning(SteadyTuning());
        PlaceAll(OutPlay, 0.f);

        FPSTelemetryGameStateEvent Situation;
        Situation.Phase = TEXT("PreSnap");
        Situation.Quarter = 2;
        Situation.Down = 3;
        Situation.Distance = 7;
        Situation.YardLine = 45;
        Situation.YardLineToGain = 52;
        Situation.HomeScore = 14;
        Bus->PublishGameState(Situation);

        for (int32 Step = 1; Step <= 35; ++Step)
        {
            PlaceAll(OutPlay, Step * StepSeconds);
            Sampler->AdvanceTime(StepSeconds);
            if (Step == 10)
            {
                FPSTelemetrySnapEvent Snap;
                Snap.Down = 3;
                Snap.Distance = 7;
                Snap.YardLine = 45;
                Bus->PublishSnap(Snap);
                OutPlay.SnapSequence = Bus->GetLastEventSequence();
            }
            else if (Step == 15)
            {
                FPSTelemetryThrowEvent Throw;
                Throw.PasserName = TEXT("QB_01");
                Throw.TargetReceiverName = TEXT("WR_01");
                Bus->PublishThrow(Throw);
            }
            else if (Step == 20)
            {
                FPSTelemetryCatchEvent Catch;
                Catch.ReceiverName = TEXT("WR_01");
                Bus->PublishCatch(Catch);
                OutPlay.CatchSequence = Bus->GetLastEventSequence();
            }
            else if (Step == 25)
            {
                FPSTelemetryTackleEvent Tackle;
                Tackle.YardsGained = 9;
                Bus->PublishTackle(Tackle);
            }
            else if (Step == 26)
            {
                FPSTelemetryPhaseChangeEvent Whistle;
                Whistle.OldPhase = TEXT("BallCarrierMovement");
                Whistle.NewPhase = TEXT("Scoring");
                Bus->PublishPhaseChange(Whistle);
                OutPlay.WhistleSequence = Bus->GetLastEventSequence();
            }
        }
        return true;
    }

    /** A world with the scripted play run in it and the replay system tuned for tests. */
    struct FReplayFixture
    {
        UWorld* World = nullptr;
        UPSTelemetryBus* Bus = nullptr;
        UPSTelemetrySamplingSubsystem* Sampler = nullptr;
        UPSReplaySubsystem* Replay = nullptr;
        FScriptedPlay Play;

        bool Setup()
        {
            World = CreateTestWorld();
            if (!World)
            {
                return false;
            }
            Bus = World->GetSubsystem<UPSTelemetryBus>();
            Sampler = World->GetSubsystem<UPSTelemetrySamplingSubsystem>();
            Replay = World->GetSubsystem<UPSReplaySubsystem>();
            if (!Bus || !Sampler || !Replay || !Replay->SetTuning(TestReplayTuning()))
            {
                return false;
            }
            FPSPlatformTier EveryFrame;
            EveryFrame.ReplayPoseRateHz = 0.f;
            Replay->ApplyPlatformTier(EveryFrame);
            return RunScriptedPlay(World, Play);
        }

        void Teardown()
        {
            if (Replay)
            {
                Replay->StopReplay();
            }
            if (World)
            {
                DestroyTestWorld(World);
            }
        }
    };

    const FPSPawnSnapshot* FindSnapshot(const FPSSnapshotFrame& Frame, const TCHAR* PlayerId)
    {
        return Frame.FindPawn(FName(PlayerId));
    }

    APSPlayerController* SpawnController(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    }

    APSBroadcastCamera* SpawnCamera(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<APSBroadcastCamera>(APSBroadcastCamera::StaticClass(), FVector(0.0, -2800.0, 600.0), FRotator(-10.0, 90.0, 0.0), SpawnParams);
    }

    /** StepCount more 0.1 s steps of the scripted motion from the sampler's clock on, with
     *  AtStep(Step) after each. */
    void RunSteps(FReplayFixture& Fixture, int32 StepCount, TFunctionRef<void(int32)> AtStep)
    {
        for (int32 Step = 1; Step <= StepCount; ++Step)
        {
            PlaceAll(Fixture.Play, Fixture.Sampler->GetClock() + StepSeconds);
            Fixture.Sampler->AdvanceTime(StepSeconds);
            AtStep(Step);
        }
    }

    void PublishSnap(UPSTelemetryBus* Bus)
    {
        FPSTelemetrySnapEvent Snap;
        Snap.Down = 1;
        Snap.Distance = 10;
        Snap.YardLine = 30;
        Bus->PublishSnap(Snap);
    }

    void PublishWhistle(UPSTelemetryBus* Bus)
    {
        FPSTelemetryPhaseChangeEvent Whistle;
        Whistle.OldPhase = TEXT("BallCarrierMovement");
        Whistle.NewPhase = TEXT("Scoring");
        Bus->PublishPhaseChange(Whistle);
    }

    void PublishGameState(UPSTelemetryBus* Bus, const TCHAR* Phase, int32 HomeScore, bool bHomeBall)
    {
        FPSTelemetryGameStateEvent GameState;
        GameState.Phase = Phase;
        GameState.Quarter = 2;
        GameState.HomeScore = HomeScore;
        GameState.bHomeHasPossession = bHomeBall;
        Bus->PublishGameState(GameState);
    }

    FPSTelemetryEvent MakeHistoryEvent(int32 Sequence, EPSTelemetryEventType Type, const FString& Payload)
    {
        FPSTelemetryEvent Event;
        Event.Sequence = Sequence;
        Event.EventType = Type;
        Event.PayloadJson = Payload;
        return Event;
    }
}

// ---------------------------------------------------------------------------
// 1. Tuning, tiers and the game state
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplayTuningTest,
    "PlaySports.Replay.System.TuningAndTiers",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplayTuningTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSReplayTuning Loaded;
    if (!TestTrue(TEXT("Data/replay.json loads"), Ingestion->LoadReplayTuningFromJson(UPSReplaySubsystem::GetDefaultTuningPath(), Loaded)))
    {
        return false;
    }
    TestEqual(TEXT("The shipped tuning is sound"), UPSReplaySubsystem::ValidateTuning(Loaded).Num(), 0);
    const FPSReplayTuning Defaults;
    TestEqual(TEXT("Defaults equal the file: pre-roll"), Loaded.PreRollSeconds, Defaults.PreRollSeconds);
    TestEqual(TEXT("... post-roll"), Loaded.PostRollSeconds, Defaults.PostRollSeconds);
    TestEqual(TEXT("... save rate"), Loaded.SaveFrameRateHz, Defaults.SaveFrameRateHz);
    TestEqual(TEXT("... playback rates"), Loaded.PlaybackRates.Num(), Defaults.PlaybackRates.Num());
    TestTrue(TEXT("A replay starts in real time"), Loaded.PlaybackRates.Num() > 0 && Loaded.PlaybackRates[0] == 1.f);

    FPSReplayTuning Bad;
    Bad.PreRollSeconds = -1.f;
    Bad.SaveFrameRateHz = 0.f;
    Bad.PlaybackRates = { 0.5f, 2.f, 0.5f };
    const TArray<FString> Problems = UPSReplaySubsystem::ValidateTuning(Bad);
    for (const FString& Problem : Problems)
    {
        AddInfo(Problem);
    }
    TestEqual(TEXT("Each problem is named: the roll, the save rate, a rate over 1, a repeat, a first rate other than 1"), Problems.Num(), 5);
    FPSReplayTuning NoRates;
    NoRates.PlaybackRates.Reset();
    TestEqual(TEXT("No playback rates is a problem"), UPSReplaySubsystem::ValidateTuning(NoRates).Num(), 1);

    UWorld* World = PSReplaySystemTests::CreateTestWorld();
    UPSReplaySubsystem* Replay = World ? World->GetSubsystem<UPSReplaySubsystem>() : nullptr;
    if (TestNotNull(TEXT("The replay system is a subsystem of a game world"), Replay))
    {
        const FPSReplayTuning Good = PSReplaySystemTests::TestReplayTuning();
        TestTrue(TEXT("A sound tuning is applied"), Replay->SetTuning(Good));
        TestFalse(TEXT("A bad one is refused"), Replay->SetTuning(Bad));
        TestEqual(TEXT("... and the current one kept"), Replay->GetTuning().PreRollSeconds, Good.PreRollSeconds);

        FPSPlatformTier Phone;
        Phone.ReplayPoseRateHz = 15.f;
        Replay->ApplyPlatformTier(Phone);
        TestEqual(TEXT("The pose rate is the tier's"), Replay->GetPoseRateHz(), 15.f);
    }
    if (World)
    {
        PSReplaySystemTests::DestroyTestWorld(World);
    }

    // The tiers: every one sound, the phones posing less often than every frame.
    FPSPlatformTierCatalog Catalog;
    if (TestTrue(TEXT("Data/platform_tiers.json loads"), Ingestion->LoadPlatformTiersFromJson(PSPlatformTiers::GetDefaultCatalogPath(), Catalog)))
    {
        TestEqual(TEXT("The tier catalog is sound"), PSPlatformTiers::ValidateCatalog(Catalog).Num(), 0);
        for (const FPSPlatformTier& Tier : Catalog.Tiers)
        {
            if (Tier.TierId != FName(TEXT("DesktopHigh")))
            {
                TestTrue(*FString::Printf(TEXT("%s re-poses a replay less often than every frame"), *Tier.TierId.ToString()), Tier.ReplayPoseRateHz > 0.f);
            }
        }
        FPSPlatformTierCatalog Broken = Catalog;
        if (Broken.Tiers.Num() > 0)
        {
            Broken.Tiers[0].ReplayPoseRateHz = -1.f;
            TestEqual(TEXT("A negative pose rate is a problem"), PSPlatformTiers::ValidateCatalog(Broken).Num(), 1);
        }
    }

    // A game-state event reads back as the play state it announced.
    FPlayState State;
    State.Phase = EPlayPhase::BallCarrierMovement;
    State.Quarter = 3;
    State.GameClockSeconds = 412.f;
    State.Down = 2;
    State.Distance = 4;
    State.YardLine = 61;
    State.YardLineToGain = 65;
    State.bHomeHasPossession = false;
    State.HomeScore = 10;
    State.AwayScore = 17;
    State.HomeTimeoutsRemaining = 1;
    State.bIsClockRunning = true;
    const FPlayState Back = PSGameStateEvents::ToPlayState(PSGameStateEvents::MakeEvent(State, FDriveSummary(), 4, 3));
    TestEqual(TEXT("Phase"), Back.Phase, State.Phase);
    TestEqual(TEXT("Quarter"), Back.Quarter, State.Quarter);
    TestEqual(TEXT("Clock"), Back.GameClockSeconds, State.GameClockSeconds);
    TestEqual(TEXT("Down"), Back.Down, State.Down);
    TestEqual(TEXT("Distance"), Back.Distance, State.Distance);
    TestEqual(TEXT("Yard line"), Back.YardLine, State.YardLine);
    TestEqual(TEXT("Possession"), Back.bHomeHasPossession, State.bHomeHasPossession);
    TestEqual(TEXT("Away score"), Back.AwayScore, State.AwayScore);
    TestEqual(TEXT("Timeouts"), Back.HomeTimeoutsRemaining, State.HomeTimeoutsRemaining);
    TestEqual(TEXT("The running clock"), Back.bIsClockRunning, true);
    FPSTelemetryGameStateEvent Unknown;
    Unknown.Phase = TEXT("Halftime");
    TestEqual(TEXT("An unknown phase reads as PreSnap"), PSGameStateEvents::ToPlayState(Unknown).Phase, EPlayPhase::PreSnap);
    return true;
}

// ---------------------------------------------------------------------------
// 2. A clip joins the bus and the snapshots
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplayCaptureTest,
    "PlaySports.Replay.System.CaptureJoinsBusAndSnapshots",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplayCaptureTest::RunTest(const FString& Parameters)
{
    using namespace PSReplaySystemTests;

    // Finding the last play in a history.
    const FString Scoring = TEXT("{\"oldPhase\":\"BallCarrierMovement\",\"newPhase\":\"Scoring\"}");
    const FString ToPreSnap = TEXT("{\"oldPhase\":\"Scoring\",\"newPhase\":\"PreSnap\"}");
    TArray<FPSTelemetryEvent> History;
    History.Add(MakeHistoryEvent(1, EPSTelemetryEventType::Snap, TEXT("{}")));
    History.Add(MakeHistoryEvent(2, EPSTelemetryEventType::PhaseChange, Scoring));
    History.Add(MakeHistoryEvent(3, EPSTelemetryEventType::Snap, TEXT("{}")));
    History.Add(MakeHistoryEvent(4, EPSTelemetryEventType::PhaseChange, ToPreSnap));
    History.Add(MakeHistoryEvent(5, EPSTelemetryEventType::Throw, TEXT("{}")));
    int32 From = 0;
    int32 To = 0;
    TestTrue(TEXT("A live play is found"), UPSReplaySubsystem::FindLastPlay(History, From, To));
    TestEqual(TEXT("... from the latest snap"), From, 3);
    TestEqual(TEXT("... to the newest event while it is live"), To, 5);
    History.Add(MakeHistoryEvent(6, EPSTelemetryEventType::PhaseChange, Scoring));
    History.Add(MakeHistoryEvent(7, EPSTelemetryEventType::GameState, TEXT("{\"phase\":\"Scoring\"}")));
    TestTrue(TEXT("A finished play is found"), UPSReplaySubsystem::FindLastPlay(History, From, To));
    TestEqual(TEXT("... to its whistle"), To, 6);
    TArray<FPSTelemetryEvent> GameStateWhistle;
    GameStateWhistle.Add(MakeHistoryEvent(10, EPSTelemetryEventType::Snap, TEXT("{}")));
    GameStateWhistle.Add(MakeHistoryEvent(11, EPSTelemetryEventType::GameState, TEXT("{\"phase\":\"Scoring\"}")));
    GameStateWhistle.Add(MakeHistoryEvent(12, EPSTelemetryEventType::GameState, TEXT("{\"phase\":\"PreSnap\"}")));
    TestTrue(TEXT("A game state in Scoring is a whistle too"), UPSReplaySubsystem::FindLastPlay(GameStateWhistle, From, To) && To == 11);
    TArray<FPSTelemetryEvent> NoSnap;
    NoSnap.Add(MakeHistoryEvent(1, EPSTelemetryEventType::Throw, TEXT("{}")));
    TestFalse(TEXT("No snap, no play"), UPSReplaySubsystem::FindLastPlay(NoSnap, From, To));

    FReplayFixture Fixture;
    if (!TestTrue(TEXT("The scripted play runs"), Fixture.Setup()))
    {
        Fixture.Teardown();
        return false;
    }

    const int32 FramesBefore = Fixture.Sampler->GetScheduledFrameCount();
    FPSReplayRecording Clip;
    if (!TestTrue(TEXT("The last play is cut"), Fixture.Replay->CaptureLastPlay(Clip)))
    {
        Fixture.Teardown();
        return false;
    }
    TestEqual(TEXT("Cutting a clip records nothing"), Fixture.Sampler->GetScheduledFrameCount(), FramesBefore);

    // The frames: from the pre-roll before the snap (1.0 s) to the post-roll after the whistle
    // (2.6 s), half a second each.
    TestTrue(TEXT("Frames were taken"), Clip.Frames.Num() > 20);
    TestTrue(*FString::Printf(TEXT("The clip opens half a second before the snap (%.3f)"), Clip.Frames[0].Time),
        FMath::IsNearlyEqual(Clip.Frames[0].Time, 0.5f, StepSeconds + 0.01f));
    TestTrue(*FString::Printf(TEXT("... and closes half a second after the whistle (%.3f)"), Clip.Frames.Last().Time),
        FMath::IsNearlyEqual(Clip.Frames.Last().Time, 3.1f, StepSeconds + 0.01f));
    bool bInOrder = true;
    for (int32 Index = 1; Index < Clip.Frames.Num(); ++Index)
    {
        bInOrder &= Clip.Frames[Index].Time >= Clip.Frames[Index - 1].Time;
    }
    TestTrue(TEXT("Frames are in time order"), bInOrder);
    TestTrue(TEXT("The catch's keyframe is in it"), Clip.Frames.ContainsByPredicate([](const FPSSnapshotFrame& Frame)
    {
        return Frame.bKeyframe && Frame.KeyframeEventType == EPSTelemetryEventType::Catch;
    }));
    const FPSPawnSnapshot* Receiver = FindSnapshot(Clip.Frames[0], TEXT("WR_01"));
    TestTrue(TEXT("Every player is in the frames"), Receiver && FindSnapshot(Clip.Frames[0], TEXT("DB_01")) && FindSnapshot(Clip.Frames[0], TEXT("QB_01")));
    if (Receiver)
    {
        TestTrue(TEXT("... where he was"), Receiver->Location.Equals(ReceiverAt(Clip.Frames[0].Time), 1.f));
    }
    TestTrue(TEXT("The ball is in the frames"), Clip.Frames[0].bBallSampled);

    // The events: the snap to the whistle, in order, by name, timed on the frames. (Whatever
    // else in the world answers them on the bus, a CPU play call at the snap, is in the clip
    // too.)
    static const TArray<FString> PlayTypes = { TEXT("Snap"), TEXT("Throw"), TEXT("Catch"), TEXT("Tackle"), TEXT("PhaseChange") };
    TArray<FString> Types;
    const FPSReplayEventRecord* CatchRecord = nullptr;
    for (const FPSReplayEventRecord& Event : Clip.Events)
    {
        if (PlayTypes.Contains(Event.EventType))
        {
            Types.Add(Event.EventType);
        }
        if (Event.EventType == TEXT("Catch"))
        {
            CatchRecord = &Event;
        }
        TestTrue(*FString::Printf(TEXT("%s happened inside the clip"), *Event.EventType),
            Event.TimestampSeconds >= Clip.Frames[0].Time - 0.001f && Event.TimestampSeconds <= 3.1f + 0.01f);
    }
    TestEqual(TEXT("The play's events, in order"), FString::Join(Types, TEXT(",")), FString(TEXT("Snap,Throw,Catch,Tackle,PhaseChange")));
    TestFalse(TEXT("The game state before the pre-roll is not an event of the clip"), Clip.Events.ContainsByPredicate(
        [](const FPSReplayEventRecord& Event) { return Event.EventType == TEXT("GameState"); }));
    for (const FPSReplayEventRecord& Event : Clip.Events)
    {
        const bool bTickValid = Clip.Frames.IsValidIndex(Event.TickIndex);
        TestTrue(*FString::Printf(TEXT("%s is on a frame"), *Event.EventType), bTickValid);
        if (bTickValid)
        {
            const bool bAtOrBefore = Clip.Frames[Event.TickIndex].Time <= Event.TimestampSeconds + 0.001f;
            const bool bLast = !Clip.Frames.IsValidIndex(Event.TickIndex + 1) || Clip.Frames[Event.TickIndex + 1].Time > Event.TimestampSeconds + 0.001f;
            TestTrue(*FString::Printf(TEXT("%s's tick is the last frame at or before it"), *Event.EventType), bAtOrBefore && bLast);
        }
    }
    if (TestNotNull(TEXT("The catch is in the clip"), CatchRecord))
    {
        TestTrue(TEXT("The catch is timed at 2.0 s"), FMath::IsNearlyEqual(CatchRecord->TimestampSeconds, 2.f, 0.01f));
        FPSTelemetryEvent CatchOnBus;
        TestTrue(TEXT("The bus still has the catch"), Fixture.Bus->FindEventBySequence(Fixture.Play.CatchSequence, CatchOnBus));
        TestEqual(TEXT("... and the clip's payload is the bus's"), CatchRecord->PayloadJson, CatchOnBus.PayloadJson);
    }

    // The opening situation and the header.
    TestEqual(TEXT("It opens on the last game state: the down"), Clip.InitialState.PlayState.Down, 3);
    TestEqual(TEXT("... the yard line"), Clip.InitialState.PlayState.YardLine, 45);
    TestEqual(TEXT("... the quarter"), Clip.InitialState.PlayState.Quarter, 2);
    TestEqual(TEXT("... the score"), Clip.InitialState.PlayState.HomeScore, 14);
    TestEqual(TEXT("The format version is stamped"), Clip.Header.FormatVersion, UPSReplayFormat::CurrentFormatVersion);
    TestEqual(TEXT("No seed: a clip plays back, it doesn't re-simulate"), Clip.Header.RandomSeed, 0);

    // A span of the caller's choosing: the catch to the whistle.
    FPSReplayRecording AfterCatch;
    TestTrue(TEXT("A clip between any two events"), Fixture.Replay->CaptureClip(Fixture.Play.CatchSequence, Fixture.Play.WhistleSequence, AfterCatch));
    TestTrue(TEXT("... starts its pre-roll before the first"), AfterCatch.Frames.Num() > 0 && FMath::IsNearlyEqual(AfterCatch.Frames[0].Time, 1.5f, StepSeconds + 0.01f));
    TestFalse(TEXT("Backwards is no clip"), Fixture.Replay->CaptureClip(Fixture.Play.WhistleSequence, Fixture.Play.CatchSequence, AfterCatch));
    TestFalse(TEXT("An event the bus never published is no clip"), Fixture.Replay->CaptureClip(Fixture.Play.SnapSequence, Fixture.Play.WhistleSequence + 1000, AfterCatch));

    // Once the play's events leave the bus's history, it can't be cut any more.
    for (int32 Index = 0; Index < Fixture.Bus->GetMaxHistorySize(); ++Index)
    {
        Fixture.Bus->PublishTackle(FPSTelemetryTackleEvent());
    }
    TestFalse(TEXT("A play gone from the history can't be cut"), Fixture.Replay->CaptureClip(Fixture.Play.SnapSequence, Fixture.Play.WhistleSequence, AfterCatch));
    TestFalse(TEXT("... and there is no last play without a snap"), Fixture.Replay->CaptureLastPlay(AfterCatch));

    Fixture.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// 3. State playback poses the field
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplayPlaybackTest,
    "PlaySports.Replay.System.StatePlaybackPosesTheField",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplayPlaybackTest::RunTest(const FString& Parameters)
{
    using namespace PSReplaySystemTests;

    FReplayFixture Fixture;
    FPSReplayRecording Clip;
    if (!TestTrue(TEXT("The scripted play runs"), Fixture.Setup()) || !TestTrue(TEXT("... and is cut"), Fixture.Replay->CaptureLastPlay(Clip)))
    {
        Fixture.Teardown();
        return false;
    }
    TestFalse(TEXT("A clip without frames doesn't play"), Fixture.Replay->StartReplay(FPSReplayRecording()));
    TestFalse(TEXT("... so nothing is replaying"), Fixture.Replay->IsReplaying());

    // The live field after the play: everyone elsewhere, the receiver jogging back.
    const FScriptedPlay& Play = Fixture.Play;
    Place(Play.Receiver, FVector(-2000.f, 100.f, 90.f), FVector(-150.f, 0.f, 0.f));
    Place(Play.Corner, FVector(-2200.f, -100.f, 90.f), FVector::ZeroVector);
    Play.Ball->SetActorLocation(FVector(-2000.f, 0.f, 20.f));
    const FVector LiveReceiver = Play.Receiver->GetActorLocation();
    const FVector LiveCorner = Play.Corner->GetActorLocation();
    const FVector LiveBall = Play.Ball->GetActorLocation();
    const int32 FramesBefore = Fixture.Sampler->GetScheduledFrameCount();
    const int32 KeyframesBefore = Fixture.Sampler->GetKeyframeCount();

    if (!TestTrue(TEXT("The clip plays"), Fixture.Replay->StartReplay(Clip)))
    {
        Fixture.Teardown();
        return false;
    }
    TestEqual(TEXT("Playing"), Fixture.Replay->GetState(), EPSReplayState::Playing);
    TestEqual(TEXT("From the start"), Fixture.Replay->GetPlayhead(), 0.f);
    TestEqual(TEXT("At real time"), Fixture.Replay->GetPlaybackRate(), 1.f);
    const float Start = Clip.Frames[0].Time;
    TestTrue(TEXT("The receiver is posed at the first frame"), Play.Receiver->GetActorLocation().Equals(ReceiverAt(Start), 1.f));
    TestTrue(TEXT("... the corner too"), Play.Corner->GetActorLocation().Equals(CornerAt(Start), 1.f));
    TestTrue(TEXT("... and the ball"), Play.Ball->GetActorLocation().Equals(BallAt(Start), 1.f));
    TestFalse(TEXT("A posed pawn can't touch anything"), Play.Receiver->GetActorEnableCollision());
    TestFalse(TEXT("... nor the ball"), Play.Ball->GetActorEnableCollision());

    // The sampler shows the replay as the present, so its readers follow it.
    FPSSnapshotFrame Latest;
    TestTrue(TEXT("The sampler shows the replay"), Fixture.Sampler->IsShowingReplay() && Fixture.Sampler->GetLatestFrame(Latest));
    TestTrue(TEXT("... its first frame"), FMath::IsNearlyEqual(Latest.Time, Start, 0.001f));

    // Between frames, the blend: the receiver's straight run, exactly.
    Fixture.Replay->AdvanceReplay(0.25f);
    TestTrue(TEXT("The playhead moves in real time"), FMath::IsNearlyEqual(Fixture.Replay->GetPlayhead(), 0.25f, 0.001f));
    TestTrue(*FString::Printf(TEXT("The receiver is where he was then (%s)"), *Play.Receiver->GetActorLocation().ToString()),
        Play.Receiver->GetActorLocation().Equals(ReceiverAt(Start + 0.25f), 1.f));
    UFloatingPawnMovement* Movement = Play.Receiver->GetFloatingMovementComponent();
    TestTrue(TEXT("... running as he was"), Movement && Movement->Velocity.Equals(FVector(ReceiverSpeed, 0.f, 0.f), 1.f));
    TestTrue(TEXT("The sampler shows the same moment"), Fixture.Sampler->GetLatestFrame(Latest) && FMath::IsNearlyEqual(Latest.Time, Start + 0.25f, 0.001f));

    // The replay records nothing, whatever happens on the bus meanwhile.
    Fixture.Sampler->AdvanceTime(StepSeconds);
    Fixture.Sampler->AdvanceTime(StepSeconds);
    Fixture.Bus->PublishCatch(FPSTelemetryCatchEvent());
    TestEqual(TEXT("No scheduled frames during a replay"), Fixture.Sampler->GetScheduledFrameCount(), FramesBefore);
    TestEqual(TEXT("No keyframes either"), Fixture.Sampler->GetKeyframeCount(), KeyframesBefore);

    // Through the catch: the ball arrives with the receiver.
    Fixture.Replay->SetPlayhead(2.f - Start);
    TestTrue(TEXT("At the catch the ball is the receiver's"), Play.Ball->GetActorLocation().Equals(ReceiverAt(2.f), 1.f));

    // To the end: it holds the last frame.
    Fixture.Replay->AdvanceReplay(100.f);
    TestTrue(TEXT("It reaches the end"), Fixture.Replay->IsAtEnd());
    TestEqual(TEXT("... and holds there"), Fixture.Replay->GetState(), EPSReplayState::Paused);
    TestTrue(TEXT("... on the last frame"), FMath::IsNearlyEqual(Fixture.Replay->GetPlayhead(), Fixture.Replay->GetDuration(), 0.001f));
    TestTrue(TEXT("The receiver is at the last frame"), Play.Receiver->GetActorLocation().Equals(ReceiverAt(Clip.Frames.Last().Time), 1.f));

    // Stopping gives the field back as it was.
    Fixture.Replay->StopReplay();
    TestFalse(TEXT("Stopped"), Fixture.Replay->IsReplaying());
    TestTrue(TEXT("The receiver is back"), Play.Receiver->GetActorLocation().Equals(LiveReceiver, 0.1f));
    TestTrue(TEXT("... jogging back as he was"), Movement && Movement->Velocity.Equals(FVector(-150.f, 0.f, 0.f), 0.1f));
    TestTrue(TEXT("The corner is back"), Play.Corner->GetActorLocation().Equals(LiveCorner, 0.1f));
    TestTrue(TEXT("The ball is back"), Play.Ball->GetActorLocation().Equals(LiveBall, 0.1f));
    TestTrue(TEXT("Collision is back"), Play.Receiver->GetActorEnableCollision() && Play.Ball->GetActorEnableCollision());
    TestFalse(TEXT("The sampler is live again"), Fixture.Sampler->IsShowingReplay());
    Fixture.Sampler->AdvanceTime(StepSeconds);
    TestEqual(TEXT("... and records again"), Fixture.Sampler->GetScheduledFrameCount(), FramesBefore + 1);
    TestTrue(TEXT("... the live field"), Fixture.Sampler->GetLatestFrame(Latest) && FindSnapshot(Latest, TEXT("WR_01"))
        && FindSnapshot(Latest, TEXT("WR_01"))->Location.Equals(LiveReceiver, 0.1f));

    Fixture.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// 4. Transport
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplayTransportTest,
    "PlaySports.Replay.System.TransportControls",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplayTransportTest::RunTest(const FString& Parameters)
{
    using namespace PSReplaySystemTests;

    FReplayFixture Fixture;
    FPSReplayRecording Clip;
    if (!TestTrue(TEXT("The scripted play runs"), Fixture.Setup()) || !TestTrue(TEXT("... and is cut"), Fixture.Replay->CaptureLastPlay(Clip)))
    {
        Fixture.Teardown();
        return false;
    }
    UPSReplaySubsystem* Replay = Fixture.Replay;

    // Nothing happens with no replay.
    Replay->TogglePause();
    Replay->StepFrames(1);
    Replay->Scrub(1.f);
    TestFalse(TEXT("The transport does nothing without a replay"), Replay->IsReplaying());

    Replay->StartReplay(Clip);
    const float Start = Clip.Frames[0].Time;

    // Pause and play.
    Replay->TogglePause();
    TestEqual(TEXT("Paused"), Replay->GetState(), EPSReplayState::Paused);
    Replay->AdvanceReplay(1.f);
    TestEqual(TEXT("A paused replay holds"), Replay->GetPlayhead(), 0.f);
    Replay->TogglePause();
    TestEqual(TEXT("Playing again"), Replay->GetState(), EPSReplayState::Playing);

    // Slow motion through the tuning's rates, then back to real time.
    TestEqual(TEXT("Half speed"), Replay->CycleSlowMotion(), 0.5f);
    Replay->AdvanceReplay(0.2f);
    TestTrue(TEXT("... moves half as far"), FMath::IsNearlyEqual(Replay->GetPlayhead(), 0.1f, 0.001f));
    TestTrue(TEXT("... and the field follows"), Fixture.Play.Receiver->GetActorLocation().Equals(ReceiverAt(Start + 0.1f), 1.f));
    TestEqual(TEXT("Quarter speed"), Replay->CycleSlowMotion(), 0.25f);
    TestEqual(TEXT("Back to real time"), Replay->CycleSlowMotion(), 1.f);

    // Frame steps land on captured frames.
    Replay->SetPlayhead(0.15f);
    Replay->StepFrames(1);
    TestEqual(TEXT("A step holds the replay"), Replay->GetState(), EPSReplayState::Paused);
    TestTrue(*FString::Printf(TEXT("A step forward lands on the next frame (%.3f)"), Replay->GetPlayhead()),
        FMath::IsNearlyEqual(Replay->GetPlayhead(), 0.2f, 0.01f));
    Replay->StepFrames(-1);
    TestTrue(*FString::Printf(TEXT("A step back lands on the one before (%.3f)"), Replay->GetPlayhead()),
        FMath::IsNearlyEqual(Replay->GetPlayhead(), 0.1f, 0.01f));
    Replay->StepFrames(3);
    TestTrue(*FString::Printf(TEXT("Three steps, three frames (%.3f)"), Replay->GetPlayhead()),
        FMath::IsNearlyEqual(Replay->GetPlayhead(), 0.4f, 0.01f));
    TestTrue(TEXT("... the field shows it"), Fixture.Play.Receiver->GetActorLocation().Equals(ReceiverAt(Start + Replay->GetPlayhead()), 1.f));
    Replay->StepFrames(-1000);
    TestEqual(TEXT("Steps stop at the first frame"), Replay->GetPlayhead(), 0.f);

    // Scrubbing.
    Replay->Restart();
    Replay->Scrub(0.35f);
    TestEqual(TEXT("A scrub holds the replay"), Replay->GetState(), EPSReplayState::Paused);
    TestTrue(TEXT("... and moves the playhead"), FMath::IsNearlyEqual(Replay->GetPlayhead(), 0.35f, 0.001f));
    TestTrue(TEXT("... showing the field at once"), Fixture.Play.Receiver->GetActorLocation().Equals(ReceiverAt(Start + 0.35f), 1.f));
    Replay->Scrub(-100.f);
    TestEqual(TEXT("Not before the start"), Replay->GetPlayhead(), 0.f);
    Replay->SetPlayhead(1000.f);
    TestTrue(TEXT("Not after the end"), FMath::IsNearlyEqual(Replay->GetPlayhead(), Replay->GetDuration(), 0.001f));
    Replay->SetPaused(false);
    TestEqual(TEXT("Playing from the end starts over"), Replay->GetPlayhead(), 0.f);
    TestEqual(TEXT("... playing"), Replay->GetState(), EPSReplayState::Playing);

    // The tier's pose rate: ten poses a second at most while playing; transport shows at once.
    FPSPlatformTier Phone;
    Phone.ReplayPoseRateHz = 10.f;
    Replay->ApplyPlatformTier(Phone);
    Replay->Restart();
    const int32 Posed = Replay->GetPoseCount();
    for (int32 Step = 0; Step < 4; ++Step)
    {
        Replay->AdvanceReplay(0.02f);
    }
    TestEqual(TEXT("No pose sooner than the tier allows"), Replay->GetPoseCount(), Posed);
    Replay->AdvanceReplay(0.02f);
    TestEqual(TEXT("One once its interval is up"), Replay->GetPoseCount(), Posed + 1);
    Replay->Scrub(0.01f);
    TestEqual(TEXT("A scrub poses at once"), Replay->GetPoseCount(), Posed + 2);
    FPSPlatformTier Desktop;
    Desktop.ReplayPoseRateHz = 0.f;
    Replay->ApplyPlatformTier(Desktop);
    Replay->SetPaused(false);
    Replay->AdvanceReplay(0.01f);
    Replay->AdvanceReplay(0.01f);
    TestEqual(TEXT("Every frame on a tier with no limit"), Replay->GetPoseCount(), Posed + 4);

    Fixture.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// 5. Persistence
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplaySaveLoadTest,
    "PlaySports.Replay.System.SaveAndLoad",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplaySaveLoadTest::RunTest(const FString& Parameters)
{
    using namespace PSReplaySystemTests;

    FReplayFixture Fixture;
    FPSReplayRecording Clip;
    if (!TestTrue(TEXT("The scripted play runs"), Fixture.Setup()) || !TestTrue(TEXT("... and is cut"), Fixture.Replay->CaptureLastPlay(Clip)))
    {
        Fixture.Teardown();
        return false;
    }

    const FString Directory = FPaths::AutomationTransientDir() / TEXT("ReplayTests");
    IFileManager::Get().DeleteDirectory(*Directory, false, true);

    FString SavedPath;
    TestFalse(TEXT("A clip without frames isn't saved"), Fixture.Replay->SaveClip(FPSReplayRecording(), TEXT("Empty"), Directory, SavedPath));
    if (!TestTrue(TEXT("The clip is saved"), Fixture.Replay->SaveClip(Clip, TEXT("Catch and run: Q2"), Directory, SavedPath)))
    {
        Fixture.Teardown();
        return false;
    }
    AddInfo(SavedPath);
    TestTrue(TEXT("... to a file"), IFileManager::Get().FileExists(*SavedPath));
    TestTrue(TEXT("... in the directory asked for"), FPaths::IsSamePath(FPaths::GetPath(SavedPath), Directory));
    const TArray<FString> Listed = UPSReplaySubsystem::ListSavedClips(Directory);
    TestTrue(TEXT("It is listed"), Listed.Num() == 1 && FPaths::IsSamePath(Listed[0], SavedPath));

    FPSReplayRecording Loaded;
    if (!TestTrue(TEXT("It loads"), UPSReplaySubsystem::LoadClip(SavedPath, Loaded)))
    {
        Fixture.Teardown();
        return false;
    }
    FPSReplayRecording Missing;
    TestFalse(TEXT("A missing file doesn't"), UPSReplaySubsystem::LoadClip(Directory / TEXT("Missing.json"), Missing));

    // The same play: what was saved is the clip thinned to the save rate.
    const FPSReplayRecording Thinned = UPSReplaySubsystem::ThinFrames(Clip, Fixture.Replay->GetTuning().SaveFrameRateHz);
    const FPSReplayDivergence Divergence = UPSDeterminism::FindFirstDivergence(Thinned, Loaded);
    TestFalse(*FString::Printf(TEXT("The loaded clip has the play's events (%s)"), *UPSDeterminism::DescribeDivergence(Divergence)), Divergence.bDiverged);
    TestEqual(TEXT("... and the thinned frames"), Loaded.Frames.Num(), Thinned.Frames.Num());
    TestTrue(TEXT("Thinning dropped frames"), Loaded.Frames.Num() < Clip.Frames.Num());
    TestEqual(TEXT("The opening situation survives"), Loaded.InitialState.PlayState.YardLine, 45);

    int32 ClipKeyframes = 0;
    for (const FPSSnapshotFrame& Frame : Clip.Frames)
    {
        ClipKeyframes += Frame.bKeyframe ? 1 : 0;
    }
    int32 LoadedKeyframes = 0;
    bool bGapsKept = true;
    for (int32 Index = 0; Index < Loaded.Frames.Num(); ++Index)
    {
        const FPSSnapshotFrame& Frame = Loaded.Frames[Index];
        LoadedKeyframes += Frame.bKeyframe ? 1 : 0;
        const bool bEnds = Index == 0 || Index == Loaded.Frames.Num() - 1;
        if (Index > 0 && !Frame.bKeyframe && !bEnds)
        {
            bGapsKept &= Frame.Time - Loaded.Frames[Index - 1].Time >= 1.f / 5.f - 0.001f;
        }
    }
    TestEqual(TEXT("Every keyframe is kept"), LoadedKeyframes, ClipKeyframes);
    TestTrue(TEXT("Scheduled frames are at most the save rate apart"), bGapsKept);
    TestTrue(TEXT("The first and last frames are kept"), FMath::IsNearlyEqual(Loaded.Frames[0].Time, Clip.Frames[0].Time)
        && FMath::IsNearlyEqual(Loaded.Frames.Last().Time, Clip.Frames.Last().Time));
    const FPSPawnSnapshot* Saved = FindSnapshot(Loaded.Frames[0], TEXT("WR_01"));
    if (TestNotNull(TEXT("The receiver is in the saved frames"), Saved))
    {
        TestTrue(TEXT("... where he was"), Saved->Location.Equals(ReceiverAt(Loaded.Frames[0].Time), 1.f));
        TestFalse(TEXT("... by PlayerId: the live pawn isn't saved"), Saved->Pawn.IsValid());
        TestEqual(TEXT("... nor is his side lost"), Saved->TeamSide, EPSTeamSide::Offense);
    }

    // A loaded clip plays back on the field by PlayerId.
    if (TestTrue(TEXT("The loaded clip plays"), Fixture.Replay->StartReplay(Loaded)))
    {
        Fixture.Replay->SetPlayhead(1.f);
        TestTrue(TEXT("The receiver is posed from it"),
            Fixture.Play.Receiver->GetActorLocation().Equals(ReceiverAt(Loaded.Frames[0].Time + 1.f), 1.f));
        Fixture.Replay->StopReplay();
    }

    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    Fixture.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// 6. The controls' catalog and touch twins
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplayControlsCatalogTest,
    "PlaySports.Replay.Controls.CatalogAndTouch",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplayControlsCatalogTest::RunTest(const FString& Parameters)
{
    UPSInputConfig* Input = NewObject<UPSInputConfig>();
    if (!TestTrue(TEXT("The input catalog loads"), Input->LoadDefaults()))
    {
        return false;
    }
    const UPSReplaySubsystem* Probe = GetDefault<UPSReplaySubsystem>();
    const FName Replay = Probe->ReplayContextId;
    TestNotNull(TEXT("The Replay context is in the catalog"), Input->FindContext(Replay));
    for (const TCHAR* Depth : { TEXT("OnField"), TEXT("PreSnap"), TEXT("DefensePreSnap"), TEXT("Passing"), TEXT("BallCarrier"), TEXT("Defense"), TEXT("Kicking") })
    {
        TestTrue(*FString::Printf(TEXT("Replay outranks %s"), Depth), Input->GetContextPriority(Replay) > Input->GetContextPriority(FName(Depth)));
    }

    FPSTouchLayout Layout;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    TestTrue(TEXT("The touch layout loads"), Ingestion->LoadTouchLayoutFromJson(PSTouchControls::GetDefaultLayoutPath(), Layout));

    // Each on-screen button and the pad button it stands for.
    TMap<FName, FKey> PadTwins;
    PadTwins.Add(TEXT("ButtonBottom"), EKeys::Gamepad_FaceButton_Bottom);
    PadTwins.Add(TEXT("ButtonRight"), EKeys::Gamepad_FaceButton_Right);
    PadTwins.Add(TEXT("ButtonLeft"), EKeys::Gamepad_FaceButton_Left);
    PadTwins.Add(TEXT("ButtonTop"), EKeys::Gamepad_FaceButton_Top);
    PadTwins.Add(TEXT("ButtonUpperLeft"), EKeys::Gamepad_LeftShoulder);
    PadTwins.Add(TEXT("ButtonUpperRight"), EKeys::Gamepad_RightShoulder);
    PadTwins.Add(TEXT("DPadLeft"), EKeys::Gamepad_DPad_Left);
    PadTwins.Add(TEXT("DPadRight"), EKeys::Gamepad_DPad_Right);
    const TArray<FName> Stack = { FName(TEXT("OnField")), Replay };
    const TArray<FName> Actions = { Probe->PlayPauseActionId, Probe->SlowMotionActionId, Probe->StepBackActionId, Probe->StepForwardActionId,
        Probe->ScrubBackActionId, Probe->ScrubForwardActionId, Probe->CameraActionId, Probe->ExitActionId };
    for (const FName& ActionId : Actions)
    {
        const FString Name = ActionId.ToString();
        const FPSInputActionDef* Def = Input->Catalog.Actions.FindByPredicate([ActionId](const FPSInputActionDef& Candidate) { return Candidate.ActionId == ActionId; });
        TestTrue(*FString::Printf(TEXT("%s is a Boolean action of the Replay context"), *Name),
            Def && Def->ValueType == EInputActionValueType::Boolean && Def->Contexts.Contains(Replay));
        FPSInputGlyph Glyph;
        TestTrue(*FString::Printf(TEXT("%s: the pad glyph"), *Name), Input->GetGlyphForAction(ActionId, Replay, EPSInputDevice::Gamepad, Glyph));
        TestTrue(*FString::Printf(TEXT("%s: the key glyph"), *Name), Input->GetGlyphForAction(ActionId, Replay, EPSInputDevice::KeyboardMouse, Glyph));
        TestTrue(*FString::Printf(TEXT("%s: the touch glyph"), *Name), Input->GetGlyphForAction(ActionId, Replay, EPSInputDevice::Touch, Glyph));

        // Its touch twin: the button bound to it in Replay does what that button's pad twin does.
        bool bHasTwin = false;
        for (const TPair<FName, FKey>& Twin : PadTwins)
        {
            FName TouchAction;
            FName TouchContext;
            if (PSTouchControls::ResolveControl(Layout, Input->Catalog, Twin.Key, Stack, TouchAction, TouchContext) && TouchAction == ActionId)
            {
                bHasTwin = true;
                TestEqual(*FString::Printf(TEXT("%s: the %s button is its pad button's twin"), *Name, *Twin.Key.ToString()),
                    Input->FindActionForKey(Twin.Value, Replay), ActionId);
            }
        }
        TestTrue(*FString::Printf(TEXT("%s has a touch button"), *Name), bHasTwin);

        // The replay pauses the game, and Enhanced Input drops an action that can't trigger then.
        const UInputAction* Action = Input->FindAction(ActionId);
        TestTrue(*FString::Printf(TEXT("%s triggers while the game is paused"), *Name), Action && Action->bTriggerWhenPaused);
    }
    for (const TCHAR* FieldActionId : { TEXT("Move"), TEXT("Pause") })
    {
        const UInputAction* Action = Input->FindAction(FName(FieldActionId));
        TestTrue(*FString::Printf(TEXT("%s triggers over a replay too (the free camera's stick, the pause menu)"), FieldActionId), Action && Action->bTriggerWhenPaused);
    }
    const UInputAction* Juke = Input->FindAction(TEXT("Juke"));
    TestTrue(TEXT("A move on the field doesn't trigger while paused"), Juke && !Juke->bTriggerWhenPaused);
    for (const FString& Problem : PSTouchControls::ValidateLayout(Layout, &Input->Catalog, nullptr))
    {
        AddError(FString::Printf(TEXT("touch_controls.json: %s"), *Problem));
    }

    // The pause button and the Move stick stay the field's.
    TestTrue(TEXT("Start isn't a replay button: it still pauses"), Input->FindActionForKey(EKeys::Gamepad_Special_Right, Replay).IsNone());
    TestTrue(TEXT("The left stick isn't either: it steers the free camera"), Input->FindActionForKey(EKeys::Gamepad_Left2D, Replay).IsNone());
    return true;
}

// ---------------------------------------------------------------------------
// 7. The buttons drive the replay
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplayButtonsTest,
    "PlaySports.Replay.Controls.ButtonsDriveTheReplay",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplayButtonsTest::RunTest(const FString& Parameters)
{
    using namespace PSReplaySystemTests;

    FReplayFixture Fixture;
    FPSReplayRecording Clip;
    if (!TestTrue(TEXT("The scripted play runs"), Fixture.Setup()) || !TestTrue(TEXT("... and is cut"), Fixture.Replay->CaptureLastPlay(Clip)))
    {
        Fixture.Teardown();
        return false;
    }
    APSPlayerController* Controller = SpawnController(Fixture.World);
    if (!TestNotNull(TEXT("A player controller"), Controller))
    {
        Fixture.Teardown();
        return false;
    }
    UPSReplaySubsystem* Replay = Fixture.Replay;
    const FName Context = Replay->ReplayContextId;
    auto Press = [Controller](FName ActionId) { Controller->OnCatalogActionStarted.Broadcast(ActionId); };
    auto Release = [Controller](FName ActionId) { Controller->OnCatalogActionCompleted.Broadcast(ActionId); };

    TestFalse(TEXT("No Replay context before a replay"), Controller->IsInputContextActive(Context));
    Replay->StartReplay(Clip);
    TestTrue(TEXT("The replay puts its context on the controller"), Controller->IsInputContextActive(Context));

    Press(Replay->PlayPauseActionId);
    TestEqual(TEXT("A pauses"), Replay->GetState(), EPSReplayState::Paused);
    Press(Replay->PlayPauseActionId);
    TestEqual(TEXT("... and plays"), Replay->GetState(), EPSReplayState::Playing);
    Press(Replay->SlowMotionActionId);
    TestEqual(TEXT("X slows it"), Replay->GetPlaybackRate(), 0.5f);

    Replay->SetPlayhead(0.15f);
    Press(Replay->StepForwardActionId);
    TestTrue(TEXT("D-pad right steps a frame on"), FMath::IsNearlyEqual(Replay->GetPlayhead(), 0.2f, 0.01f));
    Press(Replay->StepBackActionId);
    TestTrue(TEXT("D-pad left steps a frame back"), FMath::IsNearlyEqual(Replay->GetPlayhead(), 0.1f, 0.01f));

    // A held scrub moves the playhead each step until it is let go.
    Press(Replay->ScrubForwardActionId);
    Replay->AdvanceReplay(0.5f);
    const float Scrubbed = Replay->GetPlayhead();
    TestTrue(*FString::Printf(TEXT("RB held scrubs forward (%.3f)"), Scrubbed),
        FMath::IsNearlyEqual(Scrubbed, 0.1f + 0.5f * Replay->GetTuning().ScrubSecondsPerSecond, 0.01f));
    TestEqual(TEXT("... holding the replay"), Replay->GetState(), EPSReplayState::Paused);
    Release(Replay->ScrubForwardActionId);
    Replay->AdvanceReplay(0.5f);
    TestEqual(TEXT("Let go, it stops"), Replay->GetPlayhead(), Scrubbed);
    Press(Replay->ScrubBackActionId);
    Replay->AdvanceReplay(0.25f);
    TestTrue(TEXT("LB held scrubs back"), Replay->GetPlayhead() < Scrubbed);
    Release(Replay->ScrubForwardActionId);
    Replay->AdvanceReplay(0.1f);
    TestTrue(TEXT("Letting go of the other button doesn't stop it"), Replay->GetPlayhead() < Scrubbed - 0.3f);
    Release(Replay->ScrubBackActionId);

    // B leaves, and the buttons are the field's again.
    Press(Replay->ExitActionId);
    TestFalse(TEXT("B leaves the replay"), Replay->IsReplaying());
    TestFalse(TEXT("... and takes its context off"), Controller->IsInputContextActive(Context));
    Press(Replay->PlayPauseActionId);
    TestFalse(TEXT("A replay button after it does nothing"), Replay->IsReplaying());

    Fixture.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// 8. Every camera in replay
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplayCamerasTest,
    "PlaySports.Replay.Controls.EveryCameraInReplay",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplayCamerasTest::RunTest(const FString& Parameters)
{
    using namespace PSReplaySystemTests;

    FReplayFixture Fixture;
    FPSReplayRecording Clip;
    if (!TestTrue(TEXT("The scripted play runs"), Fixture.Setup()) || !TestTrue(TEXT("... and is cut"), Fixture.Replay->CaptureLastPlay(Clip)))
    {
        Fixture.Teardown();
        return false;
    }
    APSPlayerController* Controller = SpawnController(Fixture.World);
    APSBroadcastCamera* Camera = SpawnCamera(Fixture.World);
    if (!TestNotNull(TEXT("A player controller"), Controller) || !TestNotNull(TEXT("A broadcast camera"), Camera))
    {
        Fixture.Teardown();
        return false;
    }
    UPSReplaySubsystem* Replay = Fixture.Replay;
    UPSCameraAll22Component* All22 = Camera->GetAll22Component();
    UPSCameraDirectorComponent* Director = Camera->GetDirectorComponent();
    const FTransform LiveTransform = Camera->GetActorTransform();
    const bool bDirectorLive = Director->IsDirectorEnabled();

    Replay->StartReplay(Clip);
    TestTrue(TEXT("The camera ticks through the pause"), static_cast<bool>(Camera->PrimaryActorTick.bTickEvenWhenPaused));
    TestEqual(TEXT("A replay opens on the first camera, the director"), Replay->GetReplayCamera(), UPSReplaySubsystem::DirectorCamera);
    TestTrue(TEXT("... which drives"), Director->IsDirectorEnabled() && !All22->IsFilmViewActive() && !Camera->bIsFreeCam);

    TestEqual(TEXT("Y: the all-22 sideline rig"), Replay->CycleReplayCamera(), FName(TEXT("Sideline")));
    TestEqual(TEXT("... in film view"), All22->GetActiveRigId(), FName(TEXT("Sideline")));
    TestEqual(TEXT("Y: the end-zone rig"), Replay->CycleReplayCamera(), FName(TEXT("EndZone")));
    TestEqual(TEXT("... in film view"), All22->GetActiveRigId(), FName(TEXT("EndZone")));
    TestEqual(TEXT("Y: the skycam"), Replay->CycleReplayCamera(), UPSReplaySubsystem::SkycamCamera);
    TestTrue(TEXT("... cut to at once by the director"), !All22->IsFilmViewActive() && Director->GetCurrentShot() == EPSDirectorShot::Skycam);

    // The free camera circles the ball on the stick.
    Controller->OnCatalogActionStarted.Broadcast(Replay->CameraActionId);
    TestEqual(TEXT("Y on the controller: the free camera"), Replay->GetReplayCamera(), UPSReplaySubsystem::FreeCamera);
    TestTrue(TEXT("... which nothing else drives"), Camera->bIsFreeCam);
    FPSSnapshotFrame Shown;
    Replay->GetShownFrame(Shown);
    const FPSReplayTuning& Tuning = Replay->GetTuning();
    const float StartDistance = static_cast<float>(FVector::Dist(Camera->GetActorLocation(), Shown.BallLocation));
    TestTrue(*FString::Printf(TEXT("It stands its distance from the ball (%.1f)"), StartDistance), FMath::IsNearlyEqual(StartDistance, Tuning.FreeCamDistanceCm, 1.f));
    TestTrue(TEXT("... looking down at it"), FMath::IsNearlyEqual(static_cast<float>(Camera->GetActorRotation().Pitch), -Tuning.FreeCamPitchDegrees, 0.1f));
    const float YawBefore = static_cast<float>(Camera->GetActorRotation().Yaw);
    Replay->SetPaused(true);
    Controller->HandleMove(FInputActionValue(FVector2D(1.0, 0.0)));
    Replay->AdvanceReplay(0.5f);
    const float YawAfter = static_cast<float>(Camera->GetActorRotation().Yaw);
    TestTrue(*FString::Printf(TEXT("Stick right circles it (%.1f to %.1f)"), YawBefore, YawAfter),
        FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(YawBefore, YawAfter), 0.5f * Tuning.FreeCamOrbitDegreesPerSecond, 0.5f));
    Controller->HandleMove(FInputActionValue(FVector2D(0.0, 1.0)));
    Replay->AdvanceReplay(0.5f);
    const float Closer = static_cast<float>(FVector::Dist(Camera->GetActorLocation(), Shown.BallLocation));
    TestTrue(*FString::Printf(TEXT("Stick forward closes in (%.1f)"), Closer),
        FMath::IsNearlyEqual(Closer, FMath::Max(Tuning.FreeCamMinDistanceCm, Tuning.FreeCamDistanceCm - 0.5f * Tuning.FreeCamZoomCmPerSecond), 1.f));
    Replay->AdvanceReplay(10.f);
    const float Closest = static_cast<float>(FVector::Dist(Camera->GetActorLocation(), Shown.BallLocation));
    TestTrue(*FString::Printf(TEXT("... no nearer than its minimum (%.1f)"), Closest), FMath::IsNearlyEqual(Closest, Tuning.FreeCamMinDistanceCm, 1.f));
    Controller->HandleMove(FInputActionValue(FVector2D::ZeroVector));

    TestEqual(TEXT("Y after the last camera: the director again"), Replay->CycleReplayCamera(), UPSReplaySubsystem::DirectorCamera);
    TestFalse(TEXT("A camera that isn't in the list is refused"), Replay->SetReplayCamera(TEXT("Blimp")));

    // The end of the replay gives the camera back.
    Replay->SetReplayCamera(TEXT("EndZone"));
    Replay->StopReplay();
    TestFalse(TEXT("The camera rests with the pause again"), static_cast<bool>(Camera->PrimaryActorTick.bTickEvenWhenPaused));
    TestFalse(TEXT("No film view"), All22->IsFilmViewActive());
    TestFalse(TEXT("No free camera"), Camera->bIsFreeCam);
    TestEqual(TEXT("The director as it was"), Director->IsDirectorEnabled(), bDirectorLive);
    TestTrue(TEXT("Where it was"), Camera->GetActorTransform().Equals(LiveTransform, 0.1));

    Fixture.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// 9. Automatic replays
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplayAutoTest,
    "PlaySports.Replay.Controls.AutoReplayAfterScoresAndTurnovers",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplayAutoTest::RunTest(const FString& Parameters)
{
    using namespace PSReplaySystemTests;

    FReplayFixture Fixture;
    if (!TestTrue(TEXT("The scripted play runs"), Fixture.Setup()))
    {
        Fixture.Teardown();
        return false;
    }
    APSPlayerController* Controller = SpawnController(Fixture.World);
    APSBroadcastCamera* Camera = SpawnCamera(Fixture.World);
    if (!TestNotNull(TEXT("A player controller"), Controller) || !TestNotNull(TEXT("A broadcast camera"), Camera))
    {
        Fixture.Teardown();
        return false;
    }
    UPSReplaySubsystem* Replay = Fixture.Replay;
    UPSTelemetryBus* Bus = Fixture.Bus;
    UPSCameraDirectorComponent* Director = Camera->GetDirectorComponent();
    const FPSReplayTuning Tuning = Replay->GetTuning();
    TestFalse(TEXT("The scripted play, with no score or turnover, isn't replayed"), Replay->IsAutoReplayPending());

    // A touchdown: the game state's score goes up after the whistle.
    RunSteps(Fixture, 16, [Bus](int32 Step)
    {
        if (Step == 2)
        {
            PublishSnap(Bus);
        }
        else if (Step == 12)
        {
            PublishWhistle(Bus);
        }
        else if (Step == 14)
        {
            PublishGameState(Bus, TEXT("PreSnap"), 21, true);
        }
    });
    TestTrue(TEXT("A score is worth a replay"), Replay->IsAutoReplayPending());
    Replay->AdvanceReplay(Tuning.AutoReplayDelaySeconds - 0.1f);
    TestFalse(TEXT("... not before its delay"), Replay->IsReplaying());
    Replay->AdvanceReplay(0.2f);
    TestTrue(TEXT("... then it plays by itself"), Replay->IsReplaying() && Replay->IsAutoReplay());
    TestEqual(TEXT("... at the score rule's speed"), Replay->GetPlaybackRate(), 0.5f);
    TestEqual(TEXT("... from the director"), Replay->GetReplayCamera(), UPSReplaySubsystem::DirectorCamera);
    TestEqual(TEXT("... on the score rule's angle"), Director->GetCurrentShot(), EPSDirectorShot::EndZone);
    TestTrue(TEXT("The clip is the scoring play"), Replay->GetClip().Events.ContainsByPredicate(
        [](const FPSReplayEventRecord& Event) { return Event.EventType == TEXT("Snap"); }));
    Replay->AdvanceReplay(100.f);
    TestTrue(TEXT("At its end it holds"), Replay->IsReplaying() && Replay->IsAtEnd());
    Replay->AdvanceReplay(Tuning.AutoReplayHoldSeconds + 0.05f);
    TestFalse(TEXT("... then gives the game back"), Replay->IsReplaying());

    // An interception: replayed from the skycam; once the viewer takes the controls, it waits for him.
    RunSteps(Fixture, 14, [Bus](int32 Step)
    {
        if (Step == 2)
        {
            PublishSnap(Bus);
        }
        else if (Step == 8)
        {
            FPSTelemetryCatchEvent Pick;
            Pick.ReceiverName = TEXT("DB_01");
            Pick.bIsInterception = true;
            Bus->PublishCatch(Pick);
        }
        else if (Step == 12)
        {
            PublishWhistle(Bus);
        }
    });
    TestTrue(TEXT("An interception is worth a replay"), Replay->IsAutoReplayPending());
    Replay->AdvanceReplay(Tuning.AutoReplayDelaySeconds + 0.1f);
    TestTrue(TEXT("... which plays by itself"), Replay->IsAutoReplay());
    TestEqual(TEXT("... on the turnover rule's angle"), Director->GetCurrentShot(), EPSDirectorShot::Skycam);
    Controller->OnCatalogActionStarted.Broadcast(Replay->SlowMotionActionId);
    TestFalse(TEXT("A button press hands it to the viewer"), Replay->IsAutoReplay());
    Replay->AdvanceReplay(100.f);
    Replay->AdvanceReplay(Tuning.AutoReplayHoldSeconds + 5.f);
    TestTrue(TEXT("... so it waits at its end"), Replay->IsReplaying());
    Controller->OnCatalogActionStarted.Broadcast(Replay->ExitActionId);
    TestFalse(TEXT("... until he leaves"), Replay->IsReplaying());

    // A lost fumble, cancelled by the next snap before its replay.
    RunSteps(Fixture, 14, [Bus](int32 Step)
    {
        if (Step == 2)
        {
            PublishSnap(Bus);
        }
        else if (Step == 6)
        {
            FPSTelemetryFumbleEvent Fumble;
            Fumble.bIsTurnover = true;
            Bus->PublishFumble(Fumble);
        }
        else if (Step == 12)
        {
            PublishWhistle(Bus);
        }
    });
    TestTrue(TEXT("A lost fumble is worth a replay"), Replay->IsAutoReplayPending());
    PublishSnap(Bus);
    TestFalse(TEXT("The next snap cancels it"), Replay->IsAutoReplayPending());
    Replay->AdvanceReplay(Tuning.AutoReplayDelaySeconds + 1.f);
    TestFalse(TEXT("... and nothing plays"), Replay->IsReplaying());

    // A punt changes hands without a turnover; the ball changing hands on a down is one.
    RunSteps(Fixture, 16, [Bus](int32 Step)
    {
        if (Step == 2)
        {
            PublishSnap(Bus);
        }
        else if (Step == 4)
        {
            PublishGameState(Bus, TEXT("Punt"), 21, true);
        }
        else if (Step == 12)
        {
            PublishWhistle(Bus);
        }
        else if (Step == 14)
        {
            PublishGameState(Bus, TEXT("PreSnap"), 21, false);
        }
    });
    TestFalse(TEXT("A punt isn't a turnover"), Replay->IsAutoReplayPending());
    RunSteps(Fixture, 16, [Bus](int32 Step)
    {
        if (Step == 2)
        {
            PublishSnap(Bus);
        }
        else if (Step == 12)
        {
            PublishWhistle(Bus);
        }
        else if (Step == 14)
        {
            PublishGameState(Bus, TEXT("PreSnap"), 21, true);
        }
    });
    TestTrue(TEXT("The ball changing hands on a down is"), Replay->IsAutoReplayPending());
    PublishSnap(Bus);

    // Reduced motion: the still rig, never a flying angle.
    UPSUIAccessibilitySubsystem* Accessibility = Fixture.World->GetSubsystem<UPSUIAccessibilitySubsystem>();
    if (TestNotNull(TEXT("Accessibility"), Accessibility))
    {
        UPSSettingsSubsystem* Settings = NewObject<UPSSettingsSubsystem>(NewObject<UGameInstance>());
        Accessibility->SetSettings(Settings);
        Settings->SetValue(Accessibility->ReducedMotionSettingId, 1.f);
        RunSteps(Fixture, 6, [Bus](int32 Step)
        {
            if (Step == 5)
            {
                PublishWhistle(Bus);
            }
        });
        TestTrue(TEXT("With reduced motion a turnover still replays"), Replay->StartAutoReplay(EPSReplayTrigger::Turnover));
        TestEqual(TEXT("... from the still rig"), Replay->GetReplayCamera(), Tuning.ReducedMotionCamera);
        TestEqual(TEXT("... in film view"), Camera->GetAll22Component()->GetActiveRigId(), Tuning.ReducedMotionCamera);
        Replay->StopReplay();
        Accessibility->SetSettings(nullptr);
    }

    // Switched off in the tuning: nothing replays by itself.
    FPSReplayTuning Off = TestReplayTuning();
    Off.bAutoReplay = false;
    TestTrue(TEXT("Automatic replays can be switched off"), Replay->SetTuning(Off));
    RunSteps(Fixture, 16, [Bus](int32 Step)
    {
        if (Step == 2)
        {
            PublishSnap(Bus);
        }
        else if (Step == 12)
        {
            PublishWhistle(Bus);
        }
        else if (Step == 14)
        {
            PublishGameState(Bus, TEXT("PreSnap"), 28, true);
        }
    });
    TestFalse(TEXT("... and a score doesn't replay"), Replay->IsAutoReplayPending());

    Fixture.Teardown();
    return true;
}

#endif
