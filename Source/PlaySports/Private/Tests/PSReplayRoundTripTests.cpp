// PSReplayRoundTripTests.cpp -- Epic 115 story 4: record, save, load and play back
//
// A recording is made the way a run is recorded: UPSReplayRecorder copies every event off the
// telemetry bus as it is published. UPSQuickSimRunner records a seeded quick-sim game that way,
// the recording goes through the replay format's JSON and back, and ReplayGame re-simulates the
// loaded recording from what it holds alone. UPSDeterminism compares the two runs.
//
// Tests covered:
//   1. The recorder drains the bus: every event while recording (more than the bus's history
//      holds), stamped with the driver's tick and named by type, nothing after it stops, and
//      only the chosen types when filtered.
//   2. The round trip: a whole game recorded, saved to JSON, loaded and played back is the same
//      game event for event, with the same final score, the same score the franchise's
//      unrecorded quick sim gives for that seed.
//   3. Diagnosed divergence: a recording edited after the fact, or played back with a changed
//      roster, is reported at the first event that differs, by index, tick and field; a
//      recording that can't be re-simulated (no seed, no fixed step, a mid-game start, a live
//      world) is refused with the reason.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "JsonObjectConverter.h"
#include "PSDeterminism.h"
#include "PSQuickSimRunner.h"
#include "PSReplayFormat.h"
#include "PSReplayRecorder.h"
#include "PSTelemetryBus.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSReplayRoundTripTests
{
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

    /** A side of eleven with every rating at Rating. */
    TArray<FPlayerAttributes> MakeRoster(const TCHAR* Prefix, float Rating)
    {
        static const TArray<EPlayerRole> Roles = {
            EPlayerRole::Quarterback, EPlayerRole::RunningBack, EPlayerRole::WideReceiver, EPlayerRole::WideReceiver,
            EPlayerRole::TightEnd, EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman, EPlayerRole::DefensiveLineman,
            EPlayerRole::Linebacker, EPlayerRole::DefensiveBack, EPlayerRole::DefensiveBack };

        TArray<FPlayerAttributes> Roster;
        for (int32 Index = 0; Index < Roles.Num(); ++Index)
        {
            FPlayerAttributes& Player = Roster.AddDefaulted_GetRef();
            Player.PlayerId = FName(*FString::Printf(TEXT("%s_%02d"), Prefix, Index));
            Player.DisplayName = Player.PlayerId.ToString();
            Player.Role = Roles[Index];
            Player.WeightKg = 100.f;
            Player.HeightCm = 188.f;
            Player.Speed = Rating;
            Player.Agility = Rating;
            Player.Strength = Rating;
            Player.Acceleration = Rating;
            Player.Awareness = Rating;
            Player.Stamina = Rating;
        }
        return Roster;
    }

    /** Through the format's JSON and back, as a saved recording is loaded. */
    bool SaveAndLoad(const FPSReplayRecording& Recording, FPSReplayRecording& OutLoaded)
    {
        const FString Json = UPSReplayFormat::SerializeToJson(Recording);
        return !Json.IsEmpty() && UPSReplayFormat::DeserializeFromJson(Json, OutLoaded);
    }

    FPSTelemetrySnapEvent MakeSnap(int32 YardLine)
    {
        FPSTelemetrySnapEvent Snap;
        Snap.YardLine = YardLine;
        Snap.Down = 1;
        Snap.Distance = 10;
        return Snap;
    }
}

// ---------------------------------------------------------------------------
// 1. The recorder drains the bus
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplayRecorderDrainsBusTest,
    "PlaySports.Replay.RecorderDrainsTheBus",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplayRecorderDrainsBusTest::RunTest(const FString& Parameters)
{
    using namespace PSReplayRoundTripTests;

    UPSTelemetryBus* Bus = NewObject<UPSTelemetryBus>();
    UPSReplayRecorder* Recorder = NewObject<UPSReplayRecorder>();

    // Published before the recording starts: not in it.
    Bus->PublishSnap(MakeSnap(1));

    FPSReplayRecording Start = UPSReplayFormat::MakeRecording(FPlayState(), MakeRoster(TEXT("OFF"), 70.f), MakeRoster(TEXT("DEF"), 70.f));
    Start.Header.RandomSeed = 99;
    Start.Header.FixedDeltaSeconds = 0.5f;
    FPSReplayEventRecord& Stale = Start.Events.AddDefaulted_GetRef();
    Stale.EventType = TEXT("Stale");
    Recorder->BeginRecording(Bus, Start);
    TestTrue(TEXT("Recording"), Recorder->IsRecording());
    TestEqual(TEXT("The tick starts at 0"), Recorder->GetTick(), 0);

    const int32 Published = Bus->GetMaxHistorySize() + 50;
    for (int32 Tick = 1; Tick <= Published; ++Tick)
    {
        Recorder->SetTick(Tick);
        Bus->PublishSnap(MakeSnap(Tick));
    }
    FPSTelemetryTackleEvent Tackle;
    Tackle.YardsGained = 7;
    Bus->PublishTackle(Tackle);

    const FPSReplayRecording Recording = Recorder->EndRecording();
    TestFalse(TEXT("Stopped"), Recorder->IsRecording());
    Bus->PublishSnap(MakeSnap(999));

    TestEqual(TEXT("The bus kept only its history's worth"), Bus->GetEventHistory().Num(), Bus->GetMaxHistorySize());
    TestEqual(TEXT("The recording kept every event while it recorded, and none before or after"), Recording.Events.Num(), Published + 1);
    TestEqual(TEXT("The header is the start's"), Recording.Header.RandomSeed, 99);
    TestEqual(TEXT("... step and all"), Recording.Header.FixedDeltaSeconds, 0.5f);
    TestEqual(TEXT("The initial state is the start's"), Recording.InitialState.OffenseRoster.Num(), 11);
    if (Recording.Events.Num() == Published + 1)
    {
        TestEqual(TEXT("The start's own events were dropped: the first is the first published"), Recording.Events[0].EventType, FString(TEXT("Snap")));
        TestEqual(TEXT("Each event carries the tick it was published in"), Recording.Events[0].TickIndex, 1);
        TestEqual(TEXT("... to the last"), Recording.Events[Published - 1].TickIndex, Published);
        TestEqual(TEXT("Types are written by name"), Recording.Events[Published].EventType, FString(TEXT("Tackle")));
        TestEqual(TEXT("An event published after a step keeps that step's tick"), Recording.Events[Published].TickIndex, Published);

        FPSTelemetryEvent Latest;
        TestTrue(TEXT("The bus still has the tackle"), Bus->FindLatestEventOfType(EPSTelemetryEventType::Tackle, Latest));
        TestEqual(TEXT("The payload is the bus's"), Recording.Events[Published].PayloadJson, Latest.PayloadJson);

        FPSTelemetrySnapEvent First;
        TestTrue(TEXT("A recorded payload reads back as its event"),
            FJsonObjectConverter::JsonObjectStringToUStruct(Recording.Events[0].PayloadJson, &First, 0, 0));
        TestEqual(TEXT("... the first snap"), First.YardLine, 1);
    }

    // Filtered to tackles: snaps go by.
    Recorder->EventTypes = { EPSTelemetryEventType::Tackle };
    Recorder->BeginRecording(Bus, Start);
    Bus->PublishSnap(MakeSnap(5));
    Bus->PublishTackle(Tackle);
    const FPSReplayRecording Tackles = Recorder->EndRecording();
    TestEqual(TEXT("Filtered to one type, only it is recorded"), Tackles.Events.Num(), 1);

    // No bus: nothing to record from.
    Recorder->BeginRecording(nullptr, Start);
    TestFalse(TEXT("Without a bus it isn't recording"), Recorder->IsRecording());
    return true;
}

// ---------------------------------------------------------------------------
// 2. A recorded game plays back to the same game
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplayGameRoundTripTest,
    "PlaySports.Replay.RoundTrip.GamePlaysBackIdentically",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplayGameRoundTripTest::RunTest(const FString& Parameters)
{
    using namespace PSReplayRoundTripTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    TestNotNull(TEXT("Telemetry bus"), Bus);

    const TArray<FPlayerAttributes> Home = MakeRoster(TEXT("HOM"), 74.f);
    const TArray<FPlayerAttributes> Away = MakeRoster(TEXT("AWY"), 70.f);
    UPSQuickSimRunner* Runner = NewObject<UPSQuickSimRunner>();

    for (const int32 Seed : { 2024, 77 })
    {
        const FString Label = FString::Printf(TEXT("Seed %d"), Seed);

        FPSReplayRecording Recording;
        const FPSQuickSimResult Recorded = Runner->RecordGame(World, Home, Away, Seed, Recording);
        AddInfo(FString::Printf(TEXT("%s: recorded %d events, final %d-%d"), *Label, Recording.Events.Num(), Recorded.HomeScore, Recorded.AwayScore));
        TestEqual(*(Label + TEXT(": the header carries the seed")), Recording.Header.RandomSeed, Seed);
        TestEqual(*(Label + TEXT(": ... and the step")), Recording.Header.FixedDeltaSeconds, Runner->SecondsPerPlayAdvance);
        TestEqual(*(Label + TEXT(": the format version is stamped")), Recording.Header.FormatVersion, UPSReplayFormat::CurrentFormatVersion);
        TestTrue(*(Label + TEXT(": a whole game is more than the bus's history holds")), Bus && Recording.Events.Num() > Bus->GetMaxHistorySize());
        TestTrue(*(Label + TEXT(": the setup is step 0")), Recording.Events.Num() > 0 && Recording.Events[0].TickIndex == 0);
        // The quick sim publishes only its own events: the game state and, since Epic 92, each
        // play's result.
        TestTrue(*(Label + TEXT(": every event is the simulation's (game state or play result)")), !Recording.Events.ContainsByPredicate(
            [](const FPSReplayEventRecord& Event) { return Event.EventType != TEXT("GameState") && Event.EventType != TEXT("PlayResult"); }));
        TestTrue(*(Label + TEXT(": somebody scored")), Recorded.HomeScore + Recorded.AwayScore > 0);

        // Recording doesn't change the game: the franchise's quick sim, unrecorded, agrees.
        FMath::RandInit(Seed);
        const FPSQuickSimResult Unrecorded = Runner->SimulateGame(Home, Away);
        TestEqual(*(Label + TEXT(": the unrecorded game's home score")), Unrecorded.HomeScore, Recorded.HomeScore);
        TestEqual(*(Label + TEXT(": the unrecorded game's away score")), Unrecorded.AwayScore, Recorded.AwayScore);

        // Saved, loaded, played back from the loaded recording alone.
        FPSReplayRecording Loaded;
        if (!TestTrue(*(Label + TEXT(": the recording saves and loads")), SaveAndLoad(Recording, Loaded)))
        {
            continue;
        }
        FPSReplayRecording Replay;
        FPSQuickSimResult Replayed;
        FString Failure;
        const bool bPlayedBack = Runner->ReplayGame(World, Loaded, Replay, Replayed, Failure);
        TestTrue(*FString::Printf(TEXT("%s: the loaded recording plays back (%s)"), *Label, *Failure), bPlayedBack);

        const FPSReplayDivergence Divergence = UPSDeterminism::FindFirstDivergence(Recording, Replay);
        TestFalse(*FString::Printf(TEXT("%s: the playback is the recorded game (%s)"), *Label, *UPSDeterminism::DescribeDivergence(Divergence)),
            Divergence.bDiverged);
        TestEqual(*(Label + TEXT(": event for event")), Replay.Events.Num(), Recording.Events.Num());
        TestEqual(*(Label + TEXT(": the same final home score")), Replayed.HomeScore, Recorded.HomeScore);
        TestEqual(*(Label + TEXT(": the same final away score")), Replayed.AwayScore, Recorded.AwayScore);

        // The playback's own recording round-trips too: a replay of a replay is the game.
        FPSReplayRecording ReplayLoaded;
        TestTrue(*(Label + TEXT(": the playback's recording saves and loads")), SaveAndLoad(Replay, ReplayLoaded));
        TestFalse(*(Label + TEXT(": ... unchanged")), UPSDeterminism::FindFirstDivergence(Recording, ReplayLoaded).bDiverged);
    }

    FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()));
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// 3. Divergence is diagnosed; what can't be re-simulated is refused
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplayDivergenceDiagnosedTest,
    "PlaySports.Replay.RoundTrip.DivergenceIsDiagnosed",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplayDivergenceDiagnosedTest::RunTest(const FString& Parameters)
{
    using namespace PSReplayRoundTripTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }

    const TArray<FPlayerAttributes> Home = MakeRoster(TEXT("HOM"), 72.f);
    const TArray<FPlayerAttributes> Away = MakeRoster(TEXT("AWY"), 72.f);
    UPSQuickSimRunner* Runner = NewObject<UPSQuickSimRunner>();

    FPSReplayRecording Recording;
    Runner->RecordGame(World, Home, Away, 4321, Recording);
    FPSReplayRecording Loaded;
    if (!TestTrue(TEXT("The recording saves and loads"), SaveAndLoad(Recording, Loaded))
        || !TestTrue(TEXT("A real game was recorded"), Loaded.Events.Num() > 40))
    {
        DestroyTestWorld(World);
        return false;
    }

    // A recording edited after it was made (as one from a build whose rules played differently
    // would read): the playback is the true game, and the report names the edited event.
    const int32 Edited = 37;
    FPSReplayRecording Tampered = Loaded;
    Tampered.Events[Edited].PayloadJson = Loaded.Events[Edited + 1].PayloadJson;
    if (Tampered.Events[Edited].PayloadJson == Loaded.Events[Edited].PayloadJson)
    {
        Tampered.Events[Edited].PayloadJson += TEXT(" ");
    }
    FPSReplayRecording Replay;
    FPSQuickSimResult Result;
    FString Failure;
    TestTrue(TEXT("The edited recording plays back"), Runner->ReplayGame(World, Tampered, Replay, Result, Failure));
    const FPSReplayDivergence AtEdit = UPSDeterminism::FindFirstDivergence(Tampered, Replay);
    AddInfo(UPSDeterminism::DescribeDivergence(AtEdit));
    TestTrue(TEXT("The playback departs from the edited recording"), AtEdit.bDiverged);
    TestEqual(TEXT("... at the edited event"), AtEdit.EventIndex, Edited);
    TestEqual(TEXT("... at its tick"), AtEdit.TickIndex, Loaded.Events[Edited].TickIndex);
    TestEqual(TEXT("... in its payload"), AtEdit.Field, FString(TEXT("Payload")));
    TestEqual(TEXT("... where the playback is the recorded truth"), AtEdit.Actual, Loaded.Events[Edited].PayloadJson);
    TestTrue(TEXT("The report says where"), UPSDeterminism::DescribeDivergence(AtEdit).StartsWith(TEXT("diverged at event 37")));

    // The same seed and step with a changed roster: a different game, found from its first
    // differing event.
    FPSReplayRecording Stronger = Loaded;
    for (FPlayerAttributes& Player : Stronger.InitialState.DefenseRoster)
    {
        Player.Speed = 99.f;
        Player.Awareness = 99.f;
        Player.Strength = 99.f;
    }
    FPSReplayRecording StrongerReplay;
    TestTrue(TEXT("A recording with a changed roster plays back"), Runner->ReplayGame(World, Stronger, StrongerReplay, Result, Failure));
    const FPSReplayDivergence Changed = UPSDeterminism::FindFirstDivergence(Loaded, StrongerReplay);
    AddInfo(UPSDeterminism::DescribeDivergence(Changed));
    TestTrue(TEXT("A changed roster plays a different game"), Changed.bDiverged);
    TestTrue(TEXT("... reported at an event"), Changed.EventIndex > 0);
    TestTrue(TEXT("... in a step of the game, not the setup"), Changed.TickIndex > 0);
    TestTrue(TEXT("The setup, which no roster plays, is the same"),
        StrongerReplay.Events.Num() > 0 && StrongerReplay.Events[0].PayloadJson == Loaded.Events[0].PayloadJson);

    // Recordings that can't be re-simulated are refused, with the reason.
    FPSReplayRecording Unseeded = Loaded;
    Unseeded.Header.RandomSeed = 0;
    TestFalse(TEXT("No seed: refused"), Runner->ReplayGame(World, Unseeded, Replay, Result, Failure));
    TestTrue(*FString::Printf(TEXT("... because of the seed (%s)"), *Failure), Failure.Contains(TEXT("seed")));
    TestEqual(TEXT("... with nothing played back"), Replay.Events.Num(), 0);

    FPSReplayRecording VariableStep = Loaded;
    VariableStep.Header.FixedDeltaSeconds = 0.f;
    TestFalse(TEXT("No fixed step: refused"), Runner->ReplayGame(World, VariableStep, Replay, Result, Failure));
    TestTrue(*FString::Printf(TEXT("... because of the step (%s)"), *Failure), Failure.Contains(TEXT("step")));

    FPSReplayRecording MidGame = Loaded;
    MidGame.InitialState.PlayState.Quarter = 3;
    MidGame.InitialState.PlayState.YardLine = 45;
    TestFalse(TEXT("A mid-game start: refused"), Runner->ReplayGame(World, MidGame, Replay, Result, Failure));
    TestTrue(*FString::Printf(TEXT("... because of where it starts (%s)"), *Failure), Failure.Contains(TEXT("Q3")));

    TestFalse(TEXT("No world: refused"), Runner->ReplayGame(nullptr, Loaded, Replay, Result, Failure));
    TestFalse(TEXT("... with the reason"), Failure.IsEmpty());

    FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()));
    DestroyTestWorld(World);
    return true;
}

#endif
