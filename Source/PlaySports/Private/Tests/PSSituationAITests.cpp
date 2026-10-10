// PSSituationAITests.cpp -- Epic 76 (situational football intelligence), the coaching side
//
// Tests covered:
//   1. Data/situational_tuning.json loads through UPSDataIngestion and equals the defaults.
//   2. Tempo: the CPU's tempo follows the situation and the clock, a spike or kneel takes its
//      own, each tempo has its play-clock mark, and the human cycle wraps.
//   3. Play weights: the two-minute drill sinks the run and favours a sideline throw over one
//      in the middle, four-minute offense puts the run first, the defense plays prevent against
//      a two-minute drill, and a spike or kneel is never ranked or called when the clock
//      doesn't call for it -- each with the reasons the play-call screen shows.
//   4. The end-of-half decision harness: scripted clock/score/field scenarios, each asserting
//      the situation, the clock play, both sides' timeout calls, the tempo, the carrier's
//      sideline intent, and that the play caller picks the kneel or spike exactly when due.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSCoachingAI.h"
#include "PSDataIngestion.h"
#include "PSSituationAI.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSSituationAITests
{
    static FPSSituationContext MakeContext(int32 Quarter, float Clock, int32 ScoreDifferential, int32 Down, int32 Distance, int32 YardLine,
        int32 Timeouts, int32 OpponentTimeouts, bool bClockRunning)
    {
        FPSSituationContext Context;
        Context.Quarter = Quarter;
        Context.GameClockSeconds = Clock;
        Context.ScoreDifferential = ScoreDifferential;
        Context.Down = Down;
        Context.Distance = Distance;
        Context.YardLine = YardLine;
        Context.TimeoutsRemaining = Timeouts;
        Context.OpponentTimeoutsRemaining = OpponentTimeouts;
        Context.bClockRunning = bClockRunning;
        return Context;
    }

    static FPSPlayDefinition MakePlay(const TCHAR* PlayId, const TCHAR* Category, bool bOffense, const TCHAR* RouteId = nullptr)
    {
        FPSPlayDefinition Play;
        Play.PlayId = FName(PlayId);
        Play.DisplayName = PlayId;
        Play.PlayCategory = Category;
        Play.bIsOffensivePlay = bOffense;
        if (RouteId)
        {
            FPSPlayAssignment Route;
            Route.Role = EPlayerRole::WideReceiver;
            Route.Kind = EPSAssignmentKind::Route;
            Route.RouteId = FName(RouteId);
            Play.Assignments.Add(Route);
        }
        return Play;
    }

    /** Spike, kneel, a run and a pass: the play caller's menu in every harness scenario. */
    static TArray<FPSPlayDefinition> MakeOffenseMenu()
    {
        return {
            MakePlay(TEXT("Pass"), TEXT("ShortPass"), true, TEXT("Out")),
            MakePlay(TEXT("Run"), TEXT("Run"), true),
            MakePlay(TEXT("Spike"), TEXT("Spike"), true),
            MakePlay(TEXT("Kneel"), TEXT("Kneel"), true) };
    }

    static const FPSPlaySuggestion* FindSuggestion(const TArray<FPSPlaySuggestion>& Ranked, const TCHAR* PlayId)
    {
        return Ranked.FindByPredicate([PlayId](const FPSPlaySuggestion& Suggestion) { return Suggestion.PlayId == FName(PlayId); });
    }

    static bool AnyContains(const TArray<FString>& Lines, const TCHAR* Fragment)
    {
        return Lines.ContainsByPredicate([Fragment](const FString& Line) { return Line.Contains(Fragment); });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The tuning file equals the defaults
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSituationTuningFileTest,
    "PlaySports.Situation.TuningFileMatchesDefaults",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSituationTuningFileTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSSituationalTuning FromFile;
    FromFile.Tempos.Reset();
    FromFile.CategoryWeights.Reset();
    if (!TestTrue(TEXT("situational_tuning.json loads"), Ingestion->LoadSituationalTuningFromJson(UPSSituationAI::GetDefaultTuningPath(), FromFile)))
    {
        return false;
    }

    const FPSSituationalTuning Defaults;
    TestEqual(TEXT("Tempos"), FromFile.Tempos.Num(), Defaults.Tempos.Num());
    for (int32 Index = 0; Index < FMath::Min(FromFile.Tempos.Num(), Defaults.Tempos.Num()); ++Index)
    {
        TestEqual(*FString::Printf(TEXT("Tempo %d"), Index), FromFile.Tempos[Index].Tempo, Defaults.Tempos[Index].Tempo);
        TestEqual(*FString::Printf(TEXT("Tempo %d label"), Index), FromFile.Tempos[Index].Label, Defaults.Tempos[Index].Label);
        TestEqual(*FString::Printf(TEXT("Tempo %d snap mark"), Index), FromFile.Tempos[Index].SnapAtPlayClockSeconds, Defaults.Tempos[Index].SnapAtPlayClockSeconds);
        TestTrue(*FString::Printf(TEXT("Tempo %d rerun"), Index), FromFile.Tempos[Index].bRerunLastCall == Defaults.Tempos[Index].bRerunLastCall);
    }
    TestEqual(TEXT("Situation tempos"), FromFile.SituationTempos.Num(), Defaults.SituationTempos.Num());
    for (int32 Index = 0; Index < FMath::Min(FromFile.SituationTempos.Num(), Defaults.SituationTempos.Num()); ++Index)
    {
        TestEqual(*FString::Printf(TEXT("Situation tempo %d running"), Index), FromFile.SituationTempos[Index].ClockRunningTempo, Defaults.SituationTempos[Index].ClockRunningTempo);
        TestEqual(*FString::Printf(TEXT("Situation tempo %d stopped"), Index), FromFile.SituationTempos[Index].ClockStoppedTempo, Defaults.SituationTempos[Index].ClockStoppedTempo);
    }
    TestTrue(TEXT("Human tempo cycle"), FromFile.HumanTempoCycle == Defaults.HumanTempoCycle);
    TestEqual(TEXT("SpikeTempo"), FromFile.SpikeTempo, Defaults.SpikeTempo);
    TestEqual(TEXT("KneelTempo"), FromFile.KneelTempo, Defaults.KneelTempo);
    TestEqual(TEXT("TwoMinuteWindowSeconds"), FromFile.TwoMinuteWindowSeconds, Defaults.TwoMinuteWindowSeconds);
    TestEqual(TEXT("TwoScoreWindowSeconds"), FromFile.TwoScoreWindowSeconds, Defaults.TwoScoreWindowSeconds);
    TestEqual(TEXT("OneScorePoints"), FromFile.OneScorePoints, Defaults.OneScorePoints);
    TestEqual(TEXT("ClockUrgencySeconds"), FromFile.ClockUrgencySeconds, Defaults.ClockUrgencySeconds);
    TestEqual(TEXT("MaxSpikeDown"), FromFile.MaxSpikeDown, Defaults.MaxSpikeDown);
    TestEqual(TEXT("SpikeMinSeconds"), FromFile.SpikeMinSeconds, Defaults.SpikeMinSeconds);
    TestEqual(TEXT("FourMinuteWindowSeconds"), FromFile.FourMinuteWindowSeconds, Defaults.FourMinuteWindowSeconds);
    TestEqual(TEXT("DefenseTimeoutWindowSeconds"), FromFile.DefenseTimeoutWindowSeconds, Defaults.DefenseTimeoutWindowSeconds);
    TestEqual(TEXT("MaxDeficitToChase"), FromFile.MaxDeficitToChase, Defaults.MaxDeficitToChase);
    TestEqual(TEXT("KneelPlaySeconds"), FromFile.KneelPlaySeconds, Defaults.KneelPlaySeconds);
    TestEqual(TEXT("KneelPreSnapSeconds"), FromFile.KneelPreSnapSeconds, Defaults.KneelPreSnapSeconds);
    TestEqual(TEXT("EndOfHalfKneelSeconds"), FromFile.EndOfHalfKneelSeconds, Defaults.EndOfHalfKneelSeconds);
    TestEqual(TEXT("EndOfHalfKneelMaxYardLine"), FromFile.EndOfHalfKneelMaxYardLine, Defaults.EndOfHalfKneelMaxYardLine);
    TestEqual(TEXT("ClockPlayWeight"), FromFile.ClockPlayWeight, Defaults.ClockPlayWeight);
    TestTrue(TEXT("SidelineRouteIds"), FromFile.SidelineRouteIds == Defaults.SidelineRouteIds);
    TestTrue(TEXT("MiddleRouteIds"), FromFile.MiddleRouteIds == Defaults.MiddleRouteIds);
    TestEqual(TEXT("SidelinePlayDelta"), FromFile.SidelinePlayDelta, Defaults.SidelinePlayDelta);
    TestEqual(TEXT("MiddlePlayDelta"), FromFile.MiddlePlayDelta, Defaults.MiddlePlayDelta);
    TestEqual(TEXT("Category weights"), FromFile.CategoryWeights.Num(), Defaults.CategoryWeights.Num());
    for (int32 Index = 0; Index < FMath::Min(FromFile.CategoryWeights.Num(), Defaults.CategoryWeights.Num()); ++Index)
    {
        const FPSSituationCategoryWeight& Loaded = FromFile.CategoryWeights[Index];
        const FPSSituationCategoryWeight& Expected = Defaults.CategoryWeights[Index];
        TestTrue(*FString::Printf(TEXT("Category weight %d"), Index), Loaded.Situation == Expected.Situation && Loaded.bOffense == Expected.bOffense
            && Loaded.Category == Expected.Category && FMath::IsNearlyEqual(Loaded.Delta, Expected.Delta) && Loaded.Reason == Expected.Reason);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Tempo follows the situation and the clock
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSituationTempoTest,
    "PlaySports.Situation.TempoFollowsTheClock",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSituationTempoTest::RunTest(const FString& Parameters)
{
    using namespace PSSituationAITests;

    UPSSituationAI* Situation = NewObject<UPSSituationAI>();
    TestEqual(TEXT("Normal football huddles"), Situation->ChooseTempo(MakeContext(1, 600.f, 0, 1, 10, 25, 3, 3, true)), EPSTempo::Huddle);
    TestEqual(TEXT("A two-minute drill on a running clock hurries"), Situation->ChooseTempo(MakeContext(4, 90.f, -4, 1, 10, 40, 3, 3, true)), EPSTempo::HurryUp);
    TestEqual(TEXT("...and goes no-huddle once the clock is stopped"), Situation->ChooseTempo(MakeContext(4, 90.f, -4, 1, 10, 40, 3, 3, false)), EPSTempo::NoHuddle);
    TestEqual(TEXT("A four-minute offense milks a running clock"), Situation->ChooseTempo(MakeContext(4, 200.f, 7, 1, 10, 40, 3, 3, true)), EPSTempo::MilkClock);

    const FPSPlayDefinition Spike = MakePlay(TEXT("Spike"), TEXT("Spike"), true);
    const FPSPlayDefinition Kneel = MakePlay(TEXT("Kneel"), TEXT("Kneel"), true);
    const FPSPlayDefinition Pass = MakePlay(TEXT("Pass"), TEXT("ShortPass"), true);
    TestEqual(TEXT("A spike snaps at hurry-up whatever the tempo"), Situation->GetTempoForPlay(Spike, EPSTempo::Huddle), EPSTempo::HurryUp);
    TestEqual(TEXT("A kneel milks the clock whatever the tempo"), Situation->GetTempoForPlay(Kneel, EPSTempo::HurryUp), EPSTempo::MilkClock);
    TestEqual(TEXT("Any other play keeps its tempo"), Situation->GetTempoForPlay(Pass, EPSTempo::NoHuddle), EPSTempo::NoHuddle);

    const FPSSituationalTuning& Tuning = Situation->GetTuning();
    TestTrue(TEXT("Hurry-up snaps with more play clock left than a huddle"), Situation->GetSnapPlayClock(EPSTempo::HurryUp) > Situation->GetSnapPlayClock(EPSTempo::Huddle));
    TestTrue(TEXT("...and milking the clock leaves the least"), Situation->GetSnapPlayClock(EPSTempo::MilkClock) < Situation->GetSnapPlayClock(EPSTempo::Huddle));
    TestTrue(TEXT("Hurry-up reruns the last call"), Situation->FindTempo(EPSTempo::HurryUp) && Situation->FindTempo(EPSTempo::HurryUp)->bRerunLastCall);
    TestFalse(TEXT("A huddle doesn't"), Situation->FindTempo(EPSTempo::Huddle) && Situation->FindTempo(EPSTempo::Huddle)->bRerunLastCall);

    // The human cycle wraps, and a tempo outside it starts the cycle over.
    if (TestTrue(TEXT("The human cycle has tempos"), Tuning.HumanTempoCycle.Num() > 1))
    {
        EPSTempo Tempo = Tuning.HumanTempoCycle[0];
        for (int32 Step = 0; Step < Tuning.HumanTempoCycle.Num(); ++Step)
        {
            Tempo = Situation->GetNextHumanTempo(Tempo);
        }
        TestEqual(TEXT("A full cycle comes back round"), Tempo, Tuning.HumanTempoCycle[0]);
        TestEqual(TEXT("Huddle steps to no-huddle"), Situation->GetNextHumanTempo(EPSTempo::Huddle), EPSTempo::NoHuddle);
        TestEqual(TEXT("Milking the clock isn't in the cycle: back to the start"), Situation->GetNextHumanTempo(EPSTempo::MilkClock), Tuning.HumanTempoCycle[0]);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The situation moves play weights, with reasons
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSituationPlayWeightTest,
    "PlaySports.Situation.PlayWeightsWithReasons",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSituationPlayWeightTest::RunTest(const FString& Parameters)
{
    using namespace PSSituationAITests;

    UPSCoachingAI* CoachingAI = NewObject<UPSCoachingAI>();
    const FPSTendencyProfile Tendency;
    const TArray<FPSPlayDefinition> Offense = {
        MakePlay(TEXT("Run"), TEXT("Run"), true),
        MakePlay(TEXT("SidelineOut"), TEXT("ShortPass"), true, TEXT("Out")),
        MakePlay(TEXT("MiddleSlant"), TEXT("ShortPass"), true, TEXT("Slant")),
        MakePlay(TEXT("Deep"), TEXT("DeepPass"), true, TEXT("Go")),
        MakePlay(TEXT("Spike"), TEXT("Spike"), true),
        MakePlay(TEXT("Kneel"), TEXT("Kneel"), true) };

    // Down four with 1:30 left, timeouts in hand, the clock running.
    const TArray<FPSPlaySuggestion> TwoMinute = CoachingAI->RankPlays(MakeContext(4, 90.f, -4, 2, 6, 50, 3, 3, true), Tendency, Offense, true);
    const FPSPlaySuggestion* Sideline = FindSuggestion(TwoMinute, TEXT("SidelineOut"));
    const FPSPlaySuggestion* Middle = FindSuggestion(TwoMinute, TEXT("MiddleSlant"));
    const FPSPlaySuggestion* Run = FindSuggestion(TwoMinute, TEXT("Run"));
    if (TestNotNull(TEXT("Sideline throw ranked"), Sideline) && TestNotNull(TEXT("Middle throw ranked"), Middle) && TestNotNull(TEXT("Run ranked"), Run))
    {
        TestTrue(TEXT("Two-minute drill: the sideline throw beats the same throw in the middle"), Sideline->Weight > Middle->Weight);
        TestTrue(TEXT("...and the run sinks below both"), Run->Weight < Middle->Weight);
        TestTrue(TEXT("...saying why"), AnyContains(Sideline->Reasons, TEXT("sideline")) && AnyContains(Run->Reasons, TEXT("Two-minute drill")));
    }
    for (const TCHAR* ClockPlay : { TEXT("Spike"), TEXT("Kneel") })
    {
        const FPSPlaySuggestion* Ranked = FindSuggestion(TwoMinute, ClockPlay);
        TestTrue(*FString::Printf(TEXT("%s is ranked with weight 0 when the clock doesn't call for it"), ClockPlay), Ranked && Ranked->Weight == 0.f);
    }
    TestTrue(TEXT("Every candidate is ranked"), TwoMinute.Num() == Offense.Num());

    // Up three with 3:00 left against three timeouts: run the clock.
    const TArray<FPSPlaySuggestion> FourMinute = CoachingAI->RankPlays(MakeContext(4, 180.f, 3, 2, 6, 50, 3, 3, true), Tendency, Offense, true);
    if (TestTrue(TEXT("Four-minute ranking"), FourMinute.Num() > 0))
    {
        TestEqual(TEXT("Four-minute offense ranks the run first"), FourMinute[0].PlayId, FName(TEXT("Run")));
        TestTrue(TEXT("...saying why"), AnyContains(FourMinute[0].Reasons, TEXT("Four-minute offense")));
        const FPSPlaySuggestion* Deep = FindSuggestion(FourMinute, TEXT("Deep"));
        TestTrue(TEXT("...and the deep shot last of the real plays"), Deep && Deep->Weight < FindSuggestion(FourMinute, TEXT("SidelineOut"))->Weight);
    }

    // The defense against a two-minute drill keeps everything in front.
    const TArray<FPSPlayDefinition> Defense = {
        MakePlay(TEXT("Base"), TEXT("Base"), false),
        MakePlay(TEXT("Blitz"), TEXT("Blitz"), false),
        MakePlay(TEXT("Prevent"), TEXT("Prevent"), false) };
    const TArray<FPSPlaySuggestion> Prevent = CoachingAI->RankPlays(MakeContext(4, 90.f, -4, 2, 6, 50, 3, 3, true), Tendency, Defense, false);
    if (TestTrue(TEXT("Defensive ranking"), Prevent.Num() == 3))
    {
        TestEqual(TEXT("Prevent first against the two-minute drill"), Prevent[0].PlayId, FName(TEXT("Prevent")));
        TestEqual(TEXT("...the blitz last"), Prevent[2].PlayId, FName(TEXT("Blitz")));
    }

    // A clock play the clock doesn't call for is never picked, however many rolls.
    CoachingAI->SeedDeterminism(76);
    const FPSSituationContext Normal = MakeContext(1, 600.f, 0, 1, 10, 25, 3, 3, true);
    bool bClockPlayPicked = false;
    for (int32 Roll = 0; Roll < 50; ++Roll)
    {
        const FName Picked = CoachingAI->SelectOffensivePlay(Normal, Tendency, Offense);
        bClockPlayPicked |= Picked == FName(TEXT("Spike")) || Picked == FName(TEXT("Kneel"));
    }
    TestFalse(TEXT("No spike or kneel in normal football"), bClockPlayPicked);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- End-of-half decision harness
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSEndOfHalfHarnessTest,
    "PlaySports.Situation.EndOfHalfDecisionHarness",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSEndOfHalfHarnessTest::RunTest(const FString& Parameters)
{
    using namespace PSSituationAITests;

    struct FScenario
    {
        const TCHAR* Name;
        FPSSituationContext Context;
        EPSGameSituation ExpectedSituation;
        EPSClockPlay ExpectedClockPlay;
        bool bOffenseTimeout;
        bool bDefenseTimeout;
        EPSTempo ExpectedTempo;
        EPSBoundaryIntent ExpectedIntent;
    };

    // Quarter, clock, score (offense's), down, distance, yard line, timeouts, defense's timeouts, clock running.
    const FScenario Scenarios[] = {
        { TEXT("Up 3, 1:20 left, defense out of timeouts: kneel it out"),
            MakeContext(4, 80.f, 3, 1, 10, 40, 2, 0, true),
            EPSGameSituation::VictoryFormation, EPSClockPlay::Kneel, false, false, EPSTempo::MilkClock, EPSBoundaryIntent::StayInbounds },
        { TEXT("Up 3, 1:40 left, defense has three timeouts: four-minute offense, the defense stops the clock"),
            MakeContext(4, 100.f, 3, 1, 10, 40, 3, 3, true),
            EPSGameSituation::FourMinuteOffense, EPSClockPlay::None, false, true, EPSTempo::MilkClock, EPSBoundaryIntent::StayInbounds },
        { TEXT("Down 4, 0:45, clock running, timeouts left: take a timeout"),
            MakeContext(4, 45.f, -4, 2, 6, 60, 2, 3, true),
            EPSGameSituation::TwoMinuteDrill, EPSClockPlay::None, true, false, EPSTempo::HurryUp, EPSBoundaryIntent::GetOutOfBounds },
        { TEXT("Down 4, 0:45, clock running, no timeouts: spike it"),
            MakeContext(4, 45.f, -4, 1, 10, 60, 0, 3, true),
            EPSGameSituation::TwoMinuteDrill, EPSClockPlay::Spike, false, false, EPSTempo::HurryUp, EPSBoundaryIntent::GetOutOfBounds },
        { TEXT("Down 4, 0:45, no timeouts, 3rd down: don't waste the down on a spike"),
            MakeContext(4, 45.f, -4, 3, 4, 60, 0, 3, true),
            EPSGameSituation::TwoMinuteDrill, EPSClockPlay::None, false, false, EPSTempo::HurryUp, EPSBoundaryIntent::GetOutOfBounds },
        { TEXT("Down 2, 0:02 left: no time to spike and run a play"),
            MakeContext(4, 2.f, -2, 1, 10, 75, 0, 3, true),
            EPSGameSituation::TwoMinuteDrill, EPSClockPlay::None, false, false, EPSTempo::HurryUp, EPSBoundaryIntent::GetOutOfBounds },
        { TEXT("Down 3, 0:40, clock stopped: no spike, no timeout, no-huddle"),
            MakeContext(4, 40.f, -3, 1, 10, 50, 1, 3, false),
            EPSGameSituation::TwoMinuteDrill, EPSClockPlay::None, false, false, EPSTempo::NoHuddle, EPSBoundaryIntent::GetOutOfBounds },
        { TEXT("End of the 1st half, 0:25, backed up at the own 20: kneel"),
            MakeContext(2, 25.f, 0, 1, 10, 20, 3, 3, false),
            EPSGameSituation::VictoryFormation, EPSClockPlay::Kneel, false, false, EPSTempo::MilkClock, EPSBoundaryIntent::StayInbounds },
        { TEXT("End of the 1st half, 0:25, at the opponent's 35: two-minute drill, take a timeout"),
            MakeContext(2, 25.f, 0, 1, 10, 65, 1, 3, true),
            EPSGameSituation::TwoMinuteDrill, EPSClockPlay::None, true, false, EPSTempo::HurryUp, EPSBoundaryIntent::GetOutOfBounds },
        { TEXT("Down 14 with 4:30 left: hurry early"),
            MakeContext(4, 270.f, -14, 1, 10, 30, 3, 3, true),
            EPSGameSituation::TwoMinuteDrill, EPSClockPlay::None, false, false, EPSTempo::HurryUp, EPSBoundaryIntent::GetOutOfBounds },
        { TEXT("Down 3 with 4:30 left: normal football"),
            MakeContext(4, 270.f, -3, 1, 10, 30, 3, 3, true),
            EPSGameSituation::Normal, EPSClockPlay::None, false, false, EPSTempo::Huddle, EPSBoundaryIntent::None },
        { TEXT("Up 20 with 2:30 left: the defense doesn't burn timeouts chasing"),
            MakeContext(4, 150.f, 20, 1, 10, 50, 3, 3, true),
            EPSGameSituation::FourMinuteOffense, EPSClockPlay::None, false, false, EPSTempo::MilkClock, EPSBoundaryIntent::StayInbounds },
        { TEXT("3rd quarter: nothing special"),
            MakeContext(3, 60.f, -7, 1, 10, 30, 3, 3, true),
            EPSGameSituation::Normal, EPSClockPlay::None, false, false, EPSTempo::Huddle, EPSBoundaryIntent::None },
        { TEXT("Tied, 1:30 left: two-minute drill, never a kneel"),
            MakeContext(4, 90.f, 0, 1, 10, 25, 3, 3, true),
            EPSGameSituation::TwoMinuteDrill, EPSClockPlay::None, false, false, EPSTempo::HurryUp, EPSBoundaryIntent::GetOutOfBounds },
        { TEXT("Up 1 on 4th down, 0:30, defense can stop the clock: no kneel"),
            MakeContext(4, 30.f, 1, 4, 8, 45, 0, 1, true),
            EPSGameSituation::FourMinuteOffense, EPSClockPlay::None, false, true, EPSTempo::MilkClock, EPSBoundaryIntent::StayInbounds },
        { TEXT("Up 1 on 4th down, 0:30, defense out of timeouts: kneel"),
            MakeContext(4, 30.f, 1, 4, 8, 45, 0, 0, true),
            EPSGameSituation::VictoryFormation, EPSClockPlay::Kneel, false, false, EPSTempo::MilkClock, EPSBoundaryIntent::StayInbounds },
    };

    UPSCoachingAI* CoachingAI = NewObject<UPSCoachingAI>();
    CoachingAI->SeedDeterminism(24);
    const UPSSituationAI* Situation = CoachingAI->GetSituationAI();
    if (!TestNotNull(TEXT("The coaching AI has a situational read"), Situation))
    {
        return false;
    }
    const FPSTendencyProfile Tendency;
    const TArray<FPSPlayDefinition> Menu = MakeOffenseMenu();

    for (const FScenario& Scenario : Scenarios)
    {
        const FString Name = Scenario.Name;
        const FPSSituationContext& Context = Scenario.Context;
        TestEqual(*(Name + TEXT(": situation")), Situation->ClassifySituation(Context), Scenario.ExpectedSituation);
        TestEqual(*(Name + TEXT(": clock play")), Situation->DecideClockPlay(Context), Scenario.ExpectedClockPlay);
        TestTrue(*(Name + TEXT(": offense timeout")), CoachingAI->ShouldCallTimeout(Context, true) == Scenario.bOffenseTimeout);
        TestTrue(*(Name + TEXT(": defense timeout")), CoachingAI->ShouldCallTimeout(Context, false) == Scenario.bDefenseTimeout);
        TestEqual(*(Name + TEXT(": tempo")), CoachingAI->ChooseTempo(Context), Scenario.ExpectedTempo);
        TestEqual(*(Name + TEXT(": sideline intent")), Situation->GetBoundaryIntent(Context), Scenario.ExpectedIntent);

        // The play caller: the kneel or spike exactly when it's due, ranked first too.
        const FName Expected = Scenario.ExpectedClockPlay == EPSClockPlay::Kneel ? FName(TEXT("Kneel"))
            : Scenario.ExpectedClockPlay == EPSClockPlay::Spike ? FName(TEXT("Spike")) : FName();
        bool bAsExpected = true;
        for (int32 Roll = 0; Roll < 10; ++Roll)
        {
            const FName Picked = CoachingAI->SelectOffensivePlay(Context, Tendency, Menu);
            const bool bClockPlay = Picked == FName(TEXT("Kneel")) || Picked == FName(TEXT("Spike"));
            bAsExpected &= Expected.IsNone() ? !bClockPlay : Picked == Expected;
        }
        TestTrue(*(Name + TEXT(": the play caller's pick")), bAsExpected);
        if (!Expected.IsNone())
        {
            const TArray<FPSPlaySuggestion> Ranked = CoachingAI->RankPlays(Context, Tendency, Menu, true);
            TestTrue(*(Name + TEXT(": suggested first")), Ranked.Num() > 0 && Ranked[0].PlayId == Expected && Ranked[0].Reasons.Num() > 0);
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
