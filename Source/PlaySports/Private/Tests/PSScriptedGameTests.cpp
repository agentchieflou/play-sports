// PSScriptedGameTests.cpp -- Epic 24 (headless play resolution) and milestone M7
//
// UPSPlaySimulation is the one authority on a play's outcome (rule 6), and its quick-sim mode
// plays a whole game with no world: kickoffs, drives, punts, field goals, scores and four
// quarters. These tests script games on it and assert their outcomes. It rolls on the
// process-global random stream (Specs/Determinism_Audit.md, finding A1), so the harness seeds
// that stream before each run: same seed, same game. When the simulation gets its own seeded
// stream (remediation R1), seed that instead.
//
// Tests covered:
//   1. A scripted full game (M7): the opening kickoff, plays in all four quarters, the game
//      ending after the fourth, and the final score: the far stronger home team wins.
//   2. The determinism harness: the same seed replays the same game play for play, compared as
//      replay recordings by UPSDeterminism. Another seed diverges and the report says where. A
//      recording survives the replay format's JSON round trip unchanged.
//   3. A scripted scenario: the same throws, rolled identically, gain fewer yards and no
//      touchdowns against a faster cornerback.
//   4. UPSQuickSimRunner, the franchise's quick sim, gives the same score for the same seed.
//   5. The starters take the quick sim's snaps: the first quarterback, receiver and defensive
//      back in roster order (the depth chart's), never a backup listed after them.
//   6. No flag after the whistle: offensive holding is called only while the ball is live, so a
//      finished play's result stands however long the whistle's wait.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "PSDeterminism.h"
#include "PSPlaySimulation.h"
#include "PSQuickSimRunner.h"
#include "PSReplayFormat.h"
#include "PSTelemetryBus.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSScriptedGameTests
{
    /** Seconds per AdvancePlay, as UPSQuickSimRunner advances. */
    constexpr float TickSeconds = 6.f;
    constexpr int32 MaxTicks = 4000;

    /** A side of eleven with every rating at Rating: the roles the quick-sim resolver reads. */
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

    /** Gives every defensive back in Roster this Speed. */
    void SetCornerbackSpeed(TArray<FPlayerAttributes>& Roster, float Speed)
    {
        for (FPlayerAttributes& Player : Roster)
        {
            if (Player.Role == EPlayerRole::DefensiveBack)
            {
                Player.Speed = Speed;
            }
        }
    }

    /** Hands the global stream back to chance once a test is done with its seeds. */
    void UnseedRandom()
    {
        FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()));
    }

    FString ResultName(EPlayResultType Result)
    {
        return StaticEnum<EPlayResultType>()->GetNameStringByValue(static_cast<int64>(Result));
    }

    /** One play in the recording: the situation it was run in and what came of it. */
    FString PlayPayload(const FPlayState& Situation, const FPlayResult& Result)
    {
        return FString::Printf(TEXT("{\"Quarter\":%d,\"Clock\":%.1f,\"HomeBall\":%s,\"Down\":%d,\"YardLine\":%d,\"Yards\":%d,\"Home\":%d,\"Away\":%d}"),
            Situation.Quarter, Situation.GameClockSeconds, Situation.bHomeHasPossession ? TEXT("true") : TEXT("false"),
            Situation.Down, Situation.YardLine, Result.YardsGained, Situation.HomeScore, Situation.AwayScore);
    }

    struct FScriptedGame
    {
        FPSReplayRecording Recording;
        FPlayState Final;
        int32 Ticks = 0;
        int32 Kickoffs = 0;
        int32 PlaysByQuarter[4] = { 0, 0, 0, 0 };
    };

    /**
     * Plays a whole game in quick-sim mode from the opening kickoff (Home kicks to Away), seeded,
     * the way UPSQuickSimRunner drives it, and records every play as a replay event: its tick,
     * its result, and the situation it was run in.
     */
    FScriptedGame PlayGame(int32 Seed, const TArray<FPlayerAttributes>& Home, const TArray<FPlayerAttributes>& Away)
    {
        FScriptedGame Game;
        FMath::RandInit(Seed);

        UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
        Sim->bQuickSimMode = true;
        Sim->InitializePlay(Home, Away);
        Sim->SetPlayPhase(EPlayPhase::Kickoff);

        Game.Recording = UPSReplayFormat::MakeRecording(Sim->GetPlayState(), Home, Away);
        Game.Recording.Header.RandomSeed = Seed;
        Game.Recording.Header.FixedDeltaSeconds = TickSeconds;

        EPlayPhase LastPhase = Sim->GetPlayState().Phase;
        while (Game.Ticks < MaxTicks && Sim->GetPlayState().Quarter <= 4)
        {
            if (Sim->GetPlayState().Phase == EPlayPhase::PreSnap)
            {
                Sim->TriggerSnap();
            }
            Sim->AdvancePlay(TickSeconds);
            ++Game.Ticks;

            // A play's result stands while the simulation is in Scoring, before it moves on.
            const FPlayState State = Sim->GetPlayState();
            if (State.Phase == EPlayPhase::Scoring && LastPhase != EPlayPhase::Scoring)
            {
                const FPlayResult Result = Sim->GetPlayResult();
                FPSReplayEventRecord& Event = Game.Recording.Events.AddDefaulted_GetRef();
                Event.TickIndex = Game.Ticks;
                Event.TimestampSeconds = State.GameTimeSeconds;
                Event.EventType = ResultName(Result.ResultType);
                Event.PayloadJson = PlayPayload(State, Result);
                if (Result.ResultType == EPlayResultType::KickoffResult)
                {
                    ++Game.Kickoffs;
                }
                if (State.Quarter >= 1 && State.Quarter <= 4)
                {
                    ++Game.PlaysByQuarter[State.Quarter - 1];
                }
            }
            LastPhase = State.Phase;
        }
        Game.Final = Sim->GetPlayState();
        return Game;
    }

    /** One snap from 1st and 10, played until its result stands. */
    FPlayResult PlayOneSnap(int32 Seed, const TArray<FPlayerAttributes>& Offense, const TArray<FPlayerAttributes>& Defense)
    {
        FMath::RandInit(Seed);
        UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
        Sim->bQuickSimMode = true;
        Sim->InitializePlay(Offense, Defense);
        Sim->TriggerSnap();
        for (int32 Tick = 0; Tick < 10 && Sim->GetPlayState().Phase != EPlayPhase::Scoring; ++Tick)
        {
            Sim->AdvancePlay(TickSeconds);
        }
        return Sim->GetPlayResult();
    }
}

// ---------------------------------------------------------------------------
// 1. A scripted full game (M7)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSScriptedFullGameTest,
    "PlaySports.Gym.ScriptedFullGame",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSScriptedFullGameTest::RunTest(const FString& Parameters)
{
    using namespace PSScriptedGameTests;

    const TArray<FPlayerAttributes> Strong = MakeRoster(TEXT("STR"), 95.f);
    const TArray<FPlayerAttributes> Weak = MakeRoster(TEXT("WEK"), 45.f);

    for (const int32 Seed : { 11, 22, 33 })
    {
        const FScriptedGame Game = PlayGame(Seed, Strong, Weak);
        const FString Label = FString::Printf(TEXT("Seed %d"), Seed);
        AddInfo(FString::Printf(TEXT("%s: final %d-%d after %d plays, %d ticks"),
            *Label, Game.Final.HomeScore, Game.Final.AwayScore, Game.Recording.Events.Num(), Game.Ticks));

        TestTrue(*(Label + TEXT(": the game opens with a kickoff")),
            Game.Recording.Events.Num() > 0 && Game.Recording.Events[0].EventType == TEXT("KickoffResult"));
        for (int32 Quarter = 1; Quarter <= 4; ++Quarter)
        {
            TestTrue(*FString::Printf(TEXT("%s: plays are run in quarter %d"), *Label, Quarter), Game.PlaysByQuarter[Quarter - 1] > 0);
        }
        TestTrue(*(Label + TEXT(": the game ends after the fourth quarter")), Game.Final.Quarter > 4);
        TestTrue(*(Label + TEXT(": well inside the tick cap")), Game.Ticks < MaxTicks);
        TestTrue(*(Label + TEXT(": a score is followed by a kickoff")), Game.Kickoffs >= 2);
        TestTrue(*(Label + TEXT(": the far stronger home team wins")), Game.Final.HomeScore > Game.Final.AwayScore);
        TestTrue(*(Label + TEXT(": by at least three touchdowns' worth")), Game.Final.HomeScore >= 21);
    }

    UnseedRandom();
    return true;
}

// ---------------------------------------------------------------------------
// 2. The determinism harness
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSScriptedGameDeterminismTest,
    "PlaySports.Gym.SameSeedSameGame",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSScriptedGameDeterminismTest::RunTest(const FString& Parameters)
{
    using namespace PSScriptedGameTests;

    const TArray<FPlayerAttributes> Home = MakeRoster(TEXT("HOM"), 72.f);
    const TArray<FPlayerAttributes> Away = MakeRoster(TEXT("AWY"), 70.f);

    const FScriptedGame First = PlayGame(1234, Home, Away);
    const FScriptedGame Again = PlayGame(1234, Home, Away);
    const FPSReplayDivergence Same = UPSDeterminism::FindFirstDivergence(First.Recording, Again.Recording);
    TestFalse(*FString::Printf(TEXT("The same seed replays the same game (%s)"), *UPSDeterminism::DescribeDivergence(Same)), Same.bDiverged);
    TestEqual(TEXT("Same final home score"), Again.Final.HomeScore, First.Final.HomeScore);
    TestEqual(TEXT("Same final away score"), Again.Final.AwayScore, First.Final.AwayScore);
    TestTrue(TEXT("A real game was recorded"), First.Recording.Events.Num() > 50);

    // Another seed: the header says so first; with the seed set aside, the plays part ways.
    FScriptedGame Other = PlayGame(4321, Home, Away);
    const FPSReplayDivergence Seeds = UPSDeterminism::FindFirstDivergence(First.Recording, Other.Recording);
    TestTrue(TEXT("A different seed is a different header"), Seeds.bDiverged && Seeds.Field == TEXT("RandomSeed") && Seeds.EventIndex == INDEX_NONE);
    Other.Recording.Header.RandomSeed = First.Recording.Header.RandomSeed;
    const FPSReplayDivergence Plays = UPSDeterminism::FindFirstDivergence(First.Recording, Other.Recording);
    AddInfo(UPSDeterminism::DescribeDivergence(Plays));
    TestTrue(TEXT("A different seed plays a different game"), Plays.bDiverged);
    TestTrue(TEXT("The report points at an event"), Plays.EventIndex >= 0);
    TestTrue(TEXT("The report names the tick"), Plays.TickIndex > 0);
    TestTrue(TEXT("The description says where"), UPSDeterminism::DescribeDivergence(Plays).StartsWith(TEXT("diverged at event")));

    // The comparison itself: one changed play is found where it is, and a run cut short.
    if (!TestTrue(TEXT("Enough plays to edit one"), First.Recording.Events.Num() > 5))
    {
        UnseedRandom();
        return false;
    }
    FPSReplayRecording Edited = First.Recording;
    Edited.Events[5].PayloadJson += TEXT(" ");
    const FPSReplayDivergence Changed = UPSDeterminism::FindFirstDivergence(First.Recording, Edited);
    TestEqual(TEXT("A changed play is found at its index"), Changed.EventIndex, 5);
    TestEqual(TEXT("... as a payload difference"), Changed.Field, FString(TEXT("Payload")));
    TestEqual(TEXT("... at its tick"), Changed.TickIndex, First.Recording.Events[5].TickIndex);
    FPSReplayRecording Short = First.Recording;
    Short.Events.RemoveAt(Short.Events.Num() - 1);
    const FPSReplayDivergence Cut = UPSDeterminism::FindFirstDivergence(First.Recording, Short);
    TestEqual(TEXT("A run that stops early differs in its event count"), Cut.Field, FString(TEXT("EventCount")));
    TestEqual(TEXT("... from the first missing event"), Cut.EventIndex, Short.Events.Num());

    // A recording written to JSON and read back is the same run.
    FPSReplayRecording Loaded;
    TestTrue(TEXT("The recording reads back"), UPSReplayFormat::DeserializeFromJson(UPSReplayFormat::SerializeToJson(First.Recording), Loaded));
    const FPSReplayDivergence RoundTrip = UPSDeterminism::FindFirstDivergence(First.Recording, Loaded);
    TestFalse(*FString::Printf(TEXT("The JSON round trip changes nothing (%s)"), *UPSDeterminism::DescribeDivergence(RoundTrip)), RoundTrip.bDiverged);

    UnseedRandom();
    return true;
}

// ---------------------------------------------------------------------------
// 3. A scripted scenario: a faster cornerback
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSFasterCornerbackScenarioTest,
    "PlaySports.Gym.Scenario.FasterCornerbackGivesUpFewerYards",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFasterCornerbackScenarioTest::RunTest(const FString& Parameters)
{
    using namespace PSScriptedGameTests;

    const TArray<FPlayerAttributes> Offense = MakeRoster(TEXT("OFF"), 80.f);
    TArray<FPlayerAttributes> SlowCoverage = MakeRoster(TEXT("SLO"), 70.f);
    TArray<FPlayerAttributes> FastCoverage = MakeRoster(TEXT("FST"), 70.f);
    SetCornerbackSpeed(SlowCoverage, 60.f);
    SetCornerbackSpeed(FastCoverage, 99.f);

    // Each seed is one throw, rolled the same against both secondaries: only the cornerback's
    // speed differs, so only what happens after the catch can.
    int32 Completions = 0;
    int32 SlowYards = 0;
    int32 FastYards = 0;
    int32 FastTouchdowns = 0;
    for (int32 Seed = 500; Seed < 530; ++Seed)
    {
        const FPlayResult AgainstSlow = PlayOneSnap(Seed, Offense, SlowCoverage);
        const FPlayResult AgainstFast = PlayOneSnap(Seed, Offense, FastCoverage);

        const bool bSlowComplete = AgainstSlow.ResultType != EPlayResultType::Incomplete;
        const bool bFastComplete = AgainstFast.ResultType != EPlayResultType::Incomplete;
        TestTrue(*FString::Printf(TEXT("Seed %d: the throw is caught against both or neither"), Seed), bFastComplete == bSlowComplete);
        if (bSlowComplete && bFastComplete)
        {
            ++Completions;
            SlowYards += AgainstSlow.YardsGained;
            FastYards += AgainstFast.YardsGained;
            TestTrue(*FString::Printf(TEXT("Seed %d: the faster cornerback gives up fewer yards (%d vs %d)"), Seed, AgainstFast.YardsGained, AgainstSlow.YardsGained),
                AgainstFast.YardsGained < AgainstSlow.YardsGained);
        }
        if (AgainstFast.ResultType == EPlayResultType::Touchdown)
        {
            ++FastTouchdowns;
        }
    }

    AddInfo(FString::Printf(TEXT("%d completions: %d yards against the slow cornerback, %d against the fast one"), Completions, SlowYards, FastYards));
    TestTrue(TEXT("Some throws are caught"), Completions > 5);
    TestTrue(TEXT("The faster cornerback gives up fewer yards in all"), FastYards < SlowYards);
    TestEqual(TEXT("... and no touchdowns"), FastTouchdowns, 0);

    UnseedRandom();
    return true;
}

// ---------------------------------------------------------------------------
// 4. The franchise quick sim is reproducible
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSQuickSimReproducibleTest,
    "PlaySports.Gym.QuickSimRunnerSameSeedSameScore",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSQuickSimReproducibleTest::RunTest(const FString& Parameters)
{
    using namespace PSScriptedGameTests;

    const TArray<FPlayerAttributes> Home = MakeRoster(TEXT("HOM"), 75.f);
    const TArray<FPlayerAttributes> Away = MakeRoster(TEXT("AWY"), 70.f);
    UPSQuickSimRunner* Runner = NewObject<UPSQuickSimRunner>();

    FMath::RandInit(77);
    const FPSQuickSimResult First = Runner->SimulateGame(Home, Away);
    FMath::RandInit(77);
    const FPSQuickSimResult Again = Runner->SimulateGame(Home, Away);
    TestEqual(TEXT("Same seed, same home score"), Again.HomeScore, First.HomeScore);
    TestEqual(TEXT("Same seed, same away score"), Again.AwayScore, First.AwayScore);
    TestTrue(TEXT("Somebody scored"), First.HomeScore + First.AwayScore > 0);

    UnseedRandom();
    return true;
}

// ---------------------------------------------------------------------------
// 5. The starters take the quick sim's snaps
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSQuickSimStartersTest,
    "PlaySports.Gym.QuickSimStartersTakeTheSnaps",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSQuickSimStartersTest::RunTest(const FString& Parameters)
{
    using namespace PSScriptedGameTests;

    // OFF_00 is the quarterback, OFF_02 and OFF_03 the receivers, OFF_04 the tight end; a
    // backup quarterback is listed last. DEF_08 is the linebacker, DEF_09 and DEF_10 the backs.
    TArray<FPlayerAttributes> Offense = MakeRoster(TEXT("OFF"), 80.f);
    FPlayerAttributes Backup = Offense[0];
    Backup.PlayerId = FName(TEXT("OFF_QB2"));
    Backup.DisplayName = TEXT("OFF_QB2");
    Offense.Add(Backup);
    const TArray<FPlayerAttributes> Defense = MakeRoster(TEXT("DEF"), 80.f);

    FMath::RandInit(5);
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    Sim->bQuickSimMode = true;
    Sim->InitializePlay(Offense, Defense);
    TArray<FPSTelemetryPlayResultEvent> Plays;
    Sim->OnPlayResolved.AddLambda([&Plays](const FPSTelemetryPlayResultEvent& Event) { Plays.Add(Event); });
    Sim->TriggerSnap();
    Sim->ActivePenalty = EPSPenaltyType::None;
    Sim->SetPlayPhase(EPlayPhase::BallCarrierMovement);
    Sim->AdvancePlay(3.5f);
    Sim->EndPlayAndPrepareNext();

    if (TestEqual(TEXT("One play"), Plays.Num(), 1))
    {
        TestEqual(TEXT("The starting quarterback throws, not the backup"), Plays[0].PasserId, FName(TEXT("OFF_00")));
        TestEqual(TEXT("...to the first receiver, not the tight end listed last"), Plays[0].ReceiverId, FName(TEXT("OFF_02")));
        TestTrue(TEXT("...and a tackle is the first defensive back's"), Plays[0].TacklerId.IsNone() || Plays[0].TacklerId == FName(TEXT("DEF_09")));
    }

    UnseedRandom();
    return true;
}

// ---------------------------------------------------------------------------
// 6. No flag after the whistle
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSNoFlagAfterWhistleTest,
    "PlaySports.Gym.NoHoldingAfterTheWhistle",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSNoFlagAfterWhistleTest::RunTest(const FString& Parameters)
{
    using namespace PSScriptedGameTests;

    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    Sim->InitializePlay(MakeRoster(TEXT("OFF"), 80.f), MakeRoster(TEXT("DEF"), 80.f));
    Sim->TriggerSnap();
    Sim->ActivePenalty = EPSPenaltyType::None;

    // Down at the 24, then a long wait in the whistle's phase. Holding is the snap's, once per play
    // (UPSPenaltyModel); were it rolled through the whistle's wait, a flag would be likely, and an
    // accepted one would take the 4 yards back and 10 more.
    Sim->RecordTackle(4);
    Sim->AdvancePlay(100.f);
    const FPlayState State = Sim->GetPlayState();
    TestEqual(TEXT("The play is over"), State.Phase, EPlayPhase::PreSnap);
    TestEqual(TEXT("No flag after the whistle: the 4 yards stand"), State.YardLine, 24);
    TestEqual(TEXT("...2nd and 6"), State.Distance, 6);
    TestEqual(TEXT("...and no flag is pending"), Sim->ActivePenalty, EPSPenaltyType::None);
    return true;
}

#endif
