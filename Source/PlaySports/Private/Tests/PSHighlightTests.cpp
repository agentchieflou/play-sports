// PSHighlightTests.cpp -- Epic 42 (auto-highlight generation)
//
// Plays are published on the telemetry bus in a headless world while the telemetry sampler
// records a receiver's run, the way the live game announces them. UPSHighlightSubsystem follows
// them, scores them, cuts the best into clips (Epic 41's replay system) and plays the reel.
//
// Tests covered:
//   1. Data/highlights.json loads and validates; bad tunings are refused; the win-probability
//      model behaves (even at the start, the leader favoured more the later it is, the ball near
//      the goal worth more than near its own).
//   2. Importance: each part by its weight, the kinds, and the order of plays it gives.
//   3. Plays from the bus: a short run isn't a highlight; a long catch with broken tackles, a
//      touchdown and an interception are, each of its kind, with a clip, an angle and a beat at
//      its key moment; a punt isn't a turnover; the reel keeps only its best, in game order.
//   4. The package: the reel plays clip after clip through the replay system, slowing for each
//      beat; the viewer leaving ends it; it plays by itself at the end of the game.
//   5. The franchise: a game's reel archived into a season, clips saved, the season keeping its
//      best (dropped clips deleted), and the season's highlights surviving a save.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "PSBall.h"
#include "PSDataIngestion.h"
#include "PSFranchiseSaveGame.h"
#include "PSHighlightSubsystem.h"
#include "PSPlayerPawn.h"
#include "PSReplaySubsystem.h"
#include "PSSaveSubsystem.h"
#include "PSTelemetryBus.h"
#include "PSTelemetrySamplingSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSHighlightTests
{
    constexpr float StepSeconds = 0.1f;

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

    /** A world following a game: the bus, the sampler at 10 frames a second, the replay system
     *  and the highlights, with a receiver running and a defender trailing him. */
    struct FGameFixture
    {
        UWorld* World = nullptr;
        UPSTelemetryBus* Bus = nullptr;
        UPSTelemetrySamplingSubsystem* Sampler = nullptr;
        UPSReplaySubsystem* Replay = nullptr;
        UPSHighlightSubsystem* Highlights = nullptr;
        APSPlayerPawn* Receiver = nullptr;
        APSPlayerPawn* Defender = nullptr;
        APSBall* Ball = nullptr;

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
            Highlights = World->GetSubsystem<UPSHighlightSubsystem>();
            Receiver = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_01"), EPSTeamSide::Offense);
            Defender = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_01"), EPSTeamSide::Defense);
            FActorSpawnParameters SpawnParams;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
            if (!Bus || !Sampler || !Replay || !Highlights || !Receiver || !Defender || !Ball)
            {
                return false;
            }

            FPSTelemetrySamplingTuning Steady;
            Steady.SampleRateHz = 1.f / StepSeconds;
            Steady.HistorySeconds = 60.f;
            Steady.SampleBudgetMs = 1000.f;
            Steady.RecoverAfterSamples = 100000;
            Sampler->SetTuning(Steady);

            // No automatic replays in the way: these tests drive the replay themselves.
            FPSReplayTuning ReplayTuning;
            ReplayTuning.bAutoReplay = false;
            ReplayTuning.PreRollSeconds = 0.5f;
            ReplayTuning.PostRollSeconds = 0.5f;
            FPSPlatformTier EveryFrame;
            EveryFrame.ReplayPoseRateHz = 0.f;
            Replay->ApplyPlatformTier(EveryFrame);
            return Replay->SetTuning(ReplayTuning);
        }

        void Teardown()
        {
            if (Highlights)
            {
                Highlights->StopReel();
            }
            if (Replay)
            {
                Replay->StopReplay();
            }
            if (World)
            {
                DestroyTestWorld(World);
            }
        }

        /** One 0.1 s step: the receiver runs on, the defender trails, the ball goes with the
         *  receiver, and the sampler records. */
        void Step()
        {
            const float Time = Sampler->GetClock() + StepSeconds;
            const FVector Run(250.f * Time, 400.f, 90.f);
            Receiver->SetActorLocation(Run, false, nullptr, ETeleportType::TeleportPhysics);
            Defender->SetActorLocation(Run - FVector(250.f, -100.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
            Ball->SetActorLocation(Run, false, nullptr, ETeleportType::TeleportPhysics);
            Sampler->AdvanceTime(StepSeconds);
        }

        void Steps(int32 Count)
        {
            for (int32 Index = 0; Index < Count; ++Index)
            {
                Step();
            }
        }

        void GameState(const TCHAR* Phase, int32 Quarter, float Clock, int32 YardLine, bool bHomeBall, int32 HomeScore, int32 AwayScore)
        {
            FPSTelemetryGameStateEvent State;
            State.Phase = Phase;
            State.Quarter = Quarter;
            State.GameClockSeconds = Clock;
            State.YardLine = YardLine;
            State.bHomeHasPossession = bHomeBall;
            State.HomeScore = HomeScore;
            State.AwayScore = AwayScore;
            Bus->PublishGameState(State);
        }

        void Snap()
        {
            Bus->PublishSnap(FPSTelemetrySnapEvent());
        }

        void Whistle()
        {
            FPSTelemetryPhaseChangeEvent Phase;
            Phase.OldPhase = TEXT("BallCarrierMovement");
            Phase.NewPhase = TEXT("Scoring");
            Bus->PublishPhaseChange(Phase);
        }

        void Catch(int32 Yards, bool bInterception)
        {
            FPSTelemetryCatchEvent Event;
            Event.ReceiverName = bInterception ? TEXT("DB_01") : TEXT("WR_01");
            Event.YardsGained = Yards;
            Event.bIsInterception = bInterception;
            Bus->PublishCatch(Event);
        }

        void Tackle()
        {
            FPSTelemetryTackleEvent Event;
            Event.BallCarrierName = TEXT("WR_01");
            Bus->PublishTackle(Event);
        }

        /** The simulation's result for the play: its yards, from the line of scrimmage. */
        void AnnounceResult(int32 Yards)
        {
            FPSTelemetryPlayResultEvent Event;
            Event.Result = TEXT("Tackle");
            Event.YardsGained = Yards;
            Bus->PublishPlayResult(Event);
        }

        void BrokenTackle()
        {
            FPSTelemetryDamageEvent Event;
            Event.TargetName = TEXT("WR_01");
            Event.Amount = 20.f;
            Event.RemainingHitPoints = 40.f;
            Bus->PublishDamage(Event);
        }

        /** The game's plays: a 4-yard run, a 45-yard catch with two broken tackles, a touchdown
         *  pass, an interception (settled by time) and a punt. Home has the ball, 7-7 in the
         *  third quarter. */
        void PlayGame()
        {
            GameState(TEXT("PreSnap"), 3, 600.f, 30, true, 7, 7);
            Steps(5);

            // 1. A 4-yard run.
            Snap();
            Steps(10);
            Tackle();
            Steps(2);
            Whistle();
            Steps(3);
            AnnounceResult(4);
            GameState(TEXT("PreSnap"), 3, 570.f, 34, true, 7, 7);
            Steps(10);

            // 2. A 45-yard catch and run through two tackles.
            Snap();
            Steps(8);
            Catch(12, false);
            Steps(5);
            BrokenTackle();
            Steps(4);
            BrokenTackle();
            Steps(8);
            Tackle();
            Steps(2);
            Whistle();
            Steps(3);
            AnnounceResult(45);
            GameState(TEXT("PreSnap"), 3, 520.f, 79, true, 7, 7);
            Steps(10);

            // 3. A touchdown pass: the score goes up when the play settles.
            Snap();
            Steps(10);
            Catch(21, false);
            Steps(4);
            Whistle();
            GameState(TEXT("Scoring"), 3, 500.f, 79, true, 7, 7);
            Steps(3);
            GameState(TEXT("PreSnap"), 3, 500.f, 35, true, 14, 7);
            Steps(10);

            // 4. An interception; nothing settles it but time.
            GameState(TEXT("PreSnap"), 3, 480.f, 40, false, 14, 7);
            Snap();
            Steps(9);
            Catch(0, true);
            Steps(5);
            Whistle();
            Steps(3);
            Highlights->AdvanceTime(GetTuningSettle());
            Steps(5);

            // 5. A punt: the ball changes hands, but it's a kick.
            GameState(TEXT("PreSnap"), 3, 450.f, 25, true, 14, 7);
            Snap();
            GameState(TEXT("Punt"), 3, 450.f, 25, true, 14, 7);
            Steps(10);
            Whistle();
            Steps(3);
            GameState(TEXT("PreSnap"), 3, 440.f, 35, false, 14, 7);
            Steps(5);
        }

        float GetTuningSettle()
        {
            return Highlights->GetTuning().SettleAfterWhistleSeconds + 0.1f;
        }
    };

    const FPSPlayHighlight* FindPlay(const TArray<FPSPlayHighlight>& Reel, int32 PlayNumber)
    {
        return Reel.FindByPredicate([PlayNumber](const FPSPlayHighlight& Highlight) { return Highlight.PlayNumber == PlayNumber; });
    }
}

// ---------------------------------------------------------------------------
// 1. Tuning and the win-probability model
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSHighlightTuningTest,
    "PlaySports.Highlights.TuningAndWinProbability",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSHighlightTuningTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSHighlightTuning Loaded;
    if (!TestTrue(TEXT("Data/highlights.json loads"), Ingestion->LoadHighlightTuningFromJson(UPSHighlightSubsystem::GetDefaultTuningPath(), Loaded)))
    {
        return false;
    }
    TestEqual(TEXT("The shipped tuning is sound"), UPSHighlightSubsystem::ValidateTuning(Loaded).Num(), 0);
    const FPSHighlightTuning Defaults;
    TestEqual(TEXT("Defaults equal the file: the turnover weight"), Loaded.TurnoverWeight, Defaults.TurnoverWeight);
    TestEqual(TEXT("... the reel size"), Loaded.ReelSize, Defaults.ReelSize);
    TestEqual(TEXT("... the beat's speed"), Loaded.BeatPlaybackRate, Defaults.BeatPlaybackRate);
    TestEqual(TEXT("... the margin scale"), Loaded.WinProbability.MarginScale, Defaults.WinProbability.MarginScale);
    TestEqual(TEXT("... a shot for each kind"), Loaded.KindShots.Num(), 3);

    FPSHighlightTuning Bad;
    Bad.YardWeight = -1.f;
    Bad.ReelSize = 0;
    Bad.BeatPlaybackRate = 2.f;
    Bad.KindShots.RemoveAt(0);
    Bad.WinProbability.TimeFloor = 0.f;
    const TArray<FString> Problems = UPSHighlightSubsystem::ValidateTuning(Bad);
    for (const FString& Problem : Problems)
    {
        AddInfo(Problem);
    }
    TestEqual(TEXT("Each problem is named: a weight, the reel, the beat, a kind's shot, the model"), Problems.Num(), 5);

    // The model.
    const FPSWinProbabilityTuning& Model = Defaults.WinProbability;
    auto Chance = [&Model](int32 Quarter, float Clock, int32 Home, int32 Away, bool bHomeBall, int32 YardLine)
    {
        FPlayState State;
        State.Quarter = Quarter;
        State.GameClockSeconds = Clock;
        State.HomeScore = Home;
        State.AwayScore = Away;
        State.bHomeHasPossession = bHomeBall;
        State.YardLine = YardLine;
        return UPSHighlightSubsystem::HomeWinProbability(State, Model);
    };
    const float Opening = Chance(1, 900.f, 0, 0, true, 20);
    TestTrue(*FString::Printf(TEXT("Even at the start (%.3f)"), Opening), FMath::IsNearlyEqual(Opening, 0.5f, 0.05f));
    const float EarlyLead = Chance(1, 900.f, 7, 0, false, 20);
    const float LateLead = Chance(4, 120.f, 7, 0, false, 20);
    TestTrue(*FString::Printf(TEXT("A lead favours the leader (%.3f)"), EarlyLead), EarlyLead > 0.55f);
    TestTrue(*FString::Printf(TEXT("... more the later it is (%.3f)"), LateLead), LateLead > EarlyLead && LateLead > 0.9f);
    TestTrue(TEXT("Trailing is the mirror image"), FMath::IsNearlyEqual(Chance(4, 120.f, 0, 7, true, 20), 1.f - LateLead, 0.001f));
    TestTrue(TEXT("The ball near the goal is worth more than near its own"), Chance(2, 600.f, 0, 0, true, 95) > Chance(2, 600.f, 0, 0, true, 5));
    TestEqual(TEXT("A game won is won"), Chance(5, 0.f, 21, 20, false, 50), 1.f);
    TestEqual(TEXT("... and a tie is a tie"), Chance(5, 0.f, 20, 20, true, 50), 0.5f);
    return true;
}

// ---------------------------------------------------------------------------
// 2. Importance
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSHighlightImportanceTest,
    "PlaySports.Highlights.ImportanceScoring",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSHighlightImportanceTest::RunTest(const FString& Parameters)
{
    const FPSHighlightTuning Tuning;
    auto Make = [](int32 Yards, int32 Points, bool bTurnover, int32 Broken, float Swing)
    {
        FPSPlayImpact Impact;
        Impact.Yards = Yards;
        Impact.Points = Points;
        Impact.bTurnover = bTurnover;
        Impact.BrokenTackles = Broken;
        Impact.WinProbabilitySwing = Swing;
        return Impact;
    };

    const FPSPlayImpact Everything = Make(-10, 3, true, 2, 0.25f);
    const float Expected = Tuning.YardWeight * 10.f + Tuning.PointsWeight * 3.f + Tuning.TurnoverWeight + Tuning.BrokenTackleWeight * 2.f
        + Tuning.WinProbabilityWeight * 0.25f;
    TestTrue(TEXT("Each part by its weight; lost yards count too"), FMath::IsNearlyEqual(UPSHighlightSubsystem::ScoreImportance(Everything, Tuning), Expected, 0.001f));

    TestEqual(TEXT("Points make a Score"), UPSHighlightSubsystem::ClassifyPlay(Make(5, 7, true, 0, 0.f)), EPSHighlightKind::Score);
    TestEqual(TEXT("A turnover without points is a Turnover"), UPSHighlightSubsystem::ClassifyPlay(Make(0, 0, true, 0, 0.f)), EPSHighlightKind::Turnover);
    TestEqual(TEXT("Otherwise a BigPlay"), UPSHighlightSubsystem::ClassifyPlay(Make(40, 0, false, 1, 0.f)), EPSHighlightKind::BigPlay);

    const float Run = UPSHighlightSubsystem::ScoreImportance(Make(4, 0, false, 0, 0.01f), Tuning);
    const float Catch = UPSHighlightSubsystem::ScoreImportance(Make(45, 0, false, 2, 0.05f), Tuning);
    const float Pick = UPSHighlightSubsystem::ScoreImportance(Make(0, 0, true, 0, 0.15f), Tuning);
    const float Touchdown = UPSHighlightSubsystem::ScoreImportance(Make(21, 7, false, 0, 0.2f), Tuning);
    TestTrue(TEXT("A short run is below the bar"), Run < Tuning.MinImportance);
    TestTrue(TEXT("A long catch through tackles is above it"), Catch >= Tuning.MinImportance);
    TestTrue(TEXT("A touchdown outranks an interception, which outranks the catch"), Touchdown > Pick && Pick > Catch);
    return true;
}

// ---------------------------------------------------------------------------
// 3. Plays from the bus
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSHighlightPlaysTest,
    "PlaySports.Highlights.PlaysFromTheBus",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSHighlightPlaysTest::RunTest(const FString& Parameters)
{
    using namespace PSHighlightTests;

    FGameFixture Fixture;
    if (!TestTrue(TEXT("The world is set up"), Fixture.Setup()))
    {
        Fixture.Teardown();
        return false;
    }
    Fixture.PlayGame();
    UPSHighlightSubsystem* Highlights = Fixture.Highlights;
    const FPSHighlightTuning& Tuning = Highlights->GetTuning();

    const TArray<FPSPlayImpact> Impacts = Highlights->GetPlayImpacts();
    TestEqual(TEXT("Every play settled"), Impacts.Num(), 5);
    if (Impacts.Num() == 5)
    {
        TestEqual(TEXT("The run: its yards from the simulation's result"), Impacts[0].Yards, 4);
        TestEqual(TEXT("The catch and run: the play's yards from the result, not the catch's 12"), Impacts[1].Yards, 45);
        TestEqual(TEXT("... two broken tackles"), Impacts[1].BrokenTackles, 2);
        TestEqual(TEXT("The touchdown: seven points, from the game state"), Impacts[2].Points, 7);
        TestTrue(TEXT("... a swing in who wins"), Impacts[2].WinProbabilitySwing > 0.05f);
        TestTrue(TEXT("The interception is a turnover"), Impacts[3].bTurnover);
        TestFalse(TEXT("The punt isn't"), Impacts[4].bTurnover);
    }

    const TArray<FPSPlayHighlight> Reel = Highlights->GetReel();
    TestEqual(TEXT("Three plays make the reel"), Reel.Num(), 3);
    TestNull(TEXT("Not the run"), FindPlay(Reel, 1));
    TestNull(TEXT("Not the punt"), FindPlay(Reel, 5));
    TestTrue(TEXT("The reel is in game order"), Reel.Num() == 3 && Reel[0].PlayNumber == 2 && Reel[1].PlayNumber == 3 && Reel[2].PlayNumber == 4);

    struct FExpected
    {
        int32 PlayNumber;
        EPSHighlightKind Kind;
        EPSDirectorShot Shot;
        const TCHAR* Name;
    };
    for (const FExpected& Want : { FExpected{ 2, EPSHighlightKind::BigPlay, EPSDirectorShot::TightFollow, TEXT("The catch") },
        FExpected{ 3, EPSHighlightKind::Score, EPSDirectorShot::EndZone, TEXT("The touchdown") },
        FExpected{ 4, EPSHighlightKind::Turnover, EPSDirectorShot::Skycam, TEXT("The interception") } })
    {
        const FPSPlayHighlight* Highlight = FindPlay(Reel, Want.PlayNumber);
        if (!TestNotNull(Want.Name, Highlight))
        {
            continue;
        }
        TestEqual(*FString::Printf(TEXT("%s: its kind"), Want.Name), Highlight->Kind, Want.Kind);
        TestEqual(*FString::Printf(TEXT("%s: its angle"), Want.Name), Highlight->Shot, Want.Shot);
        TestTrue(*FString::Printf(TEXT("%s: worth it"), Want.Name), Highlight->Importance >= Tuning.MinImportance);
        TestTrue(*FString::Printf(TEXT("%s: has its clip"), Want.Name), Highlight->Clip.Frames.Num() > 10);
        TestTrue(*FString::Printf(TEXT("%s: the clip starts at its snap"), Want.Name), Highlight->Clip.Events.Num() > 0 && Highlight->Clip.Events.ContainsByPredicate(
            [](const FPSReplayEventRecord& Event) { return Event.EventType == TEXT("Snap"); }));
        TestTrue(*FString::Printf(TEXT("%s: a beat inside the clip"), Want.Name), Highlight->BeatEndSeconds > Highlight->BeatStartSeconds);
        TestEqual(*FString::Printf(TEXT("%s: in the third quarter"), Want.Name), Highlight->Situation.Quarter, 3);
    }

    // The beat is built around the key moment: for the catch, the catch (snap + 0.8 s).
    if (const FPSPlayHighlight* Catch = FindPlay(Reel, 2))
    {
        float SnapTime = 0.f;
        for (const FPSReplayEventRecord& Event : Catch->Clip.Events)
        {
            if (Event.EventType == TEXT("Snap"))
            {
                SnapTime = Event.TimestampSeconds;
            }
        }
        const float CatchOnClip = SnapTime + 0.8f - Catch->Clip.Frames[0].Time;
        TestTrue(*FString::Printf(TEXT("The catch's beat begins just before the catch (%.2f vs %.2f)"), Catch->BeatStartSeconds, CatchOnClip),
            FMath::IsNearlyEqual(Catch->BeatStartSeconds, CatchOnClip - Tuning.BeatLeadSeconds, 0.05f));
    }

    // A smaller reel keeps only the best: a new game with room for one.
    FPSHighlightTuning One = Tuning;
    One.ReelSize = 1;
    TestTrue(TEXT("A reel of one"), Highlights->SetTuning(One));
    Highlights->ResetGame();
    TestEqual(TEXT("A new game forgets the last"), Highlights->GetReel().Num(), 0);
    Fixture.PlayGame();
    const TArray<FPSPlayHighlight> Best = Highlights->GetReel();
    TestTrue(TEXT("... and keeps its touchdown alone"), Best.Num() == 1 && Best[0].Kind == EPSHighlightKind::Score);

    Fixture.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// 4. The package
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSHighlightReelTest,
    "PlaySports.Highlights.ReelPlaysWithBeats",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSHighlightReelTest::RunTest(const FString& Parameters)
{
    using namespace PSHighlightTests;

    FGameFixture Fixture;
    if (!TestTrue(TEXT("The world is set up"), Fixture.Setup()))
    {
        Fixture.Teardown();
        return false;
    }
    UPSHighlightSubsystem* Highlights = Fixture.Highlights;
    UPSReplaySubsystem* Replay = Fixture.Replay;
    TestFalse(TEXT("No reel before there are highlights"), Highlights->PlayReel());
    Fixture.PlayGame();
    const FPSHighlightTuning& Tuning = Highlights->GetTuning();
    const TArray<FPSPlayHighlight> Reel = Highlights->GetReel();
    if (!TestEqual(TEXT("Three highlights"), Reel.Num(), 3))
    {
        Fixture.Teardown();
        return false;
    }

    TestTrue(TEXT("The reel plays"), Highlights->PlayReel());
    TestTrue(TEXT("... through the replay system"), Replay->IsReplaying());
    TestEqual(TEXT("... from its first clip"), Highlights->GetReelIndex(), 0);
    TestEqual(TEXT("... the first play's clip"), Replay->GetDuration(), Reel[0].Clip.Frames.Last().Time - Reel[0].Clip.Frames[0].Time);

    // Real time, then the beat in slow motion, then real time again.
    Highlights->AdvanceTime(0.f);
    TestEqual(TEXT("Before the beat, real time"), Replay->GetPlaybackRate(), 1.f);
    Replay->SetPlayhead(Reel[0].BeatStartSeconds + 0.05f);
    Highlights->AdvanceTime(0.f);
    TestEqual(TEXT("In the beat, slow motion"), Replay->GetPlaybackRate(), Tuning.BeatPlaybackRate);
    Replay->SetPlayhead(Reel[0].BeatEndSeconds + 0.05f);
    Highlights->AdvanceTime(0.f);
    TestEqual(TEXT("After it, real time"), Replay->GetPlaybackRate(), 1.f);

    // The end of a clip, a moment, the next.
    Replay->AdvanceReplay(100.f);
    Highlights->AdvanceTime(Tuning.ClipGapSeconds * 0.5f);
    TestEqual(TEXT("A clip holds its end a moment"), Highlights->GetReelIndex(), 0);
    Highlights->AdvanceTime(Tuning.ClipGapSeconds);
    TestEqual(TEXT("... then the next plays"), Highlights->GetReelIndex(), 1);
    TestTrue(TEXT("... still in the replay"), Replay->IsReplaying() && Highlights->IsPlayingReel());

    // The viewer leaving the replay ends the reel.
    Replay->StopReplay();
    TestFalse(TEXT("Leaving the replay ends the reel"), Highlights->IsPlayingReel());

    // To the end on its own: every clip, then the game back.
    TestTrue(TEXT("The reel plays again"), Highlights->PlayReel());
    for (int32 Clip = 0; Clip < Reel.Num(); ++Clip)
    {
        Replay->AdvanceReplay(100.f);
        Highlights->AdvanceTime(Tuning.ClipGapSeconds + 0.05f);
    }
    TestFalse(TEXT("After its last clip it's over"), Highlights->IsPlayingReel());
    TestFalse(TEXT("... and the replay with it"), Replay->IsReplaying());

    // The end of the game plays it by itself.
    Fixture.GameState(TEXT("PreSnap"), 5, 0.f, 50, true, 21, 7);
    TestTrue(TEXT("The final whistle queues the reel"), Highlights->IsReelPending());
    Highlights->AdvanceTime(Tuning.GameEndReelDelaySeconds - 0.1f);
    TestFalse(TEXT("... not at once"), Highlights->IsPlayingReel());
    Highlights->AdvanceTime(0.2f);
    TestTrue(TEXT("... then it plays"), Highlights->IsPlayingReel());
    Highlights->StopReel();
    TestFalse(TEXT("Stopped"), Replay->IsReplaying());

    Fixture.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// 5. The franchise
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSHighlightSeasonTest,
    "PlaySports.Highlights.SeasonArchive",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSHighlightSeasonTest::RunTest(const FString& Parameters)
{
    using namespace PSHighlightTests;

    // Keeping a season's best, in the order they came.
    TArray<FPSSeasonHighlight> Season;
    for (const float Importance : { 5.f, 12.f, 3.f, 9.f })
    {
        FPSSeasonHighlight& Entry = Season.AddDefaulted_GetRef();
        Entry.Importance = Importance;
    }
    TArray<FPSSeasonHighlight> Dropped;
    UPSHighlightSubsystem::KeepSeasonBest(Season, 2, Dropped);
    TestTrue(TEXT("The two best stay, in order"), Season.Num() == 2 && Season[0].Importance == 12.f && Season[1].Importance == 9.f);
    TestEqual(TEXT("The rest are dropped"), Dropped.Num(), 2);

    FGameFixture Fixture;
    if (!TestTrue(TEXT("The world is set up"), Fixture.Setup()))
    {
        Fixture.Teardown();
        return false;
    }
    Fixture.PlayGame();
    UPSHighlightSubsystem* Highlights = Fixture.Highlights;

    const FString Directory = FPaths::AutomationTransientDir() / TEXT("HighlightTests");
    IFileManager::Get().DeleteDirectory(*Directory, false, true);

    // A season with one big highlight already, and room for three.
    FPSHighlightTuning Three = Highlights->GetTuning();
    Three.SeasonHighlightsKept = 3;
    Highlights->SetTuning(Three);
    TArray<FPSSeasonHighlight> Archive;
    FPSSeasonHighlight& Earlier = Archive.AddDefaulted_GetRef();
    Earlier.Week = 1;
    Earlier.Importance = 100.f;
    const int32 Kept = Highlights->ArchiveForSeason(Archive, 2, TEXT("HOME"), TEXT("AWAY"), Directory);
    TestEqual(TEXT("The season keeps three"), Archive.Num(), 3);
    TestEqual(TEXT("... two of them from this game"), Kept, 2);
    TestEqual(TEXT("... the earlier one first"), Archive[0].Week, 1);
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(Directory / TEXT("*.json")), true, false);
    TestEqual(TEXT("Only the kept clips stay on disk"), Files.Num(), 2);
    for (int32 Index = 1; Index < Archive.Num(); ++Index)
    {
        const FPSSeasonHighlight& Entry = Archive[Index];
        TestTrue(*FString::Printf(TEXT("Highlight %d is this game's"), Index), Entry.Week == 2 && Entry.HomeTeamId == FName(TEXT("HOME")) && Entry.AwayTeamId == FName(TEXT("AWAY")));
        TestTrue(TEXT("... with its clip saved"), IFileManager::Get().FileExists(*Entry.ClipFile));
        FPSReplayRecording Clip;
        TestTrue(TEXT("... which loads"), UPSReplaySubsystem::LoadClip(Entry.ClipFile, Clip) && Clip.Frames.Num() > 0);
    }
    TestFalse(TEXT("The least of the game, the interception, didn't make it"),
        Archive.ContainsByPredicate([](const FPSSeasonHighlight& Candidate) { return Candidate.Kind == EPSHighlightKind::Turnover; }));

    // The season's highlights survive a franchise save.
    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    Save->SeasonHighlights = Archive;
    const FString Slot = TEXT("Test_SeasonHighlights");
    TestTrue(TEXT("The franchise saves"), Saves->SaveToSlot(Save, Slot));
    const UPSFranchiseSaveGame* Loaded = Cast<UPSFranchiseSaveGame>(Saves->LoadFromSlot(Slot));
    if (TestNotNull(TEXT("... and loads"), Loaded))
    {
        TestEqual(TEXT("... with its highlights"), Loaded->SeasonHighlights.Num(), Archive.Num());
        if (Loaded->SeasonHighlights.Num() == Archive.Num())
        {
            TestEqual(TEXT("... their kinds"), Loaded->SeasonHighlights[1].Kind, Archive[1].Kind);
            TestEqual(TEXT("... their clips"), Loaded->SeasonHighlights[1].ClipFile, Archive[1].ClipFile);
            TestEqual(TEXT("... their teams"), Loaded->SeasonHighlights[1].HomeTeamId, FName(TEXT("HOME")));
        }
    }
    Saves->DeleteSlot(Slot);

    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    Fixture.Teardown();
    return true;
}

#endif
