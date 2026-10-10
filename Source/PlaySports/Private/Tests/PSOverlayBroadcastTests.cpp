// PSOverlayBroadcastTests.cpp -- Epic 33 (score bug and broadcast chyron framework)
//
// Tests covered:
//   1. The broadcast theme loads and validates; the shared game-state helpers (clock rule,
//      quarter and clock text) say what the simulation and the score bug both rely on.
//   2. The play simulation announces its state on the bus only when something other than the
//      running clocks changes, and the score bug follows it: teams, score, possession,
//      timeouts, quarter, clocks running on between events, down and distance, the red zone,
//      the two-minute state; a touchdown raises a score alert and a drive summary.
//   3. The chyron queue: priority order, first come first served within a priority, cut-ins
//      only after the minimum time up, the queue limit, the gap between chyrons, stat lines,
//      and no chyrons on a Minimal tier.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSGameStateEvents.h"
#include "PSOverlayBroadcastSubsystem.h"
#include "PSPlaySimulation.h"
#include "PSTelemetryBus.h"
#include "PSUITeamCatalog.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSOverlayBroadcastTests
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

    static int32 CountGameStateEvents(const UPSTelemetryBus& Bus)
    {
        int32 Count = 0;
        for (const FPSTelemetryEvent& Event : Bus.GetEventHistory())
        {
            Count += Event.EventType == EPSTelemetryEventType::GameState ? 1 : 0;
        }
        return Count;
    }

    /** A theme with round numbers for the chyron rules. */
    static FPSBroadcastOverlayTheme RulesTheme()
    {
        FPSBroadcastOverlayTheme Theme;
        Theme.ChyronMaxQueued = 3;
        Theme.ChyronMinShowSeconds = 2.f;
        Theme.ChyronGapSeconds = 0.5f;
        auto AddKind = [&Theme](EPSChyronKind Kind, int32 Priority, float Seconds)
        {
            FPSChyronKindStyle Style;
            Style.Kind = Kind;
            Style.Priority = Priority;
            Style.Seconds = Seconds;
            Theme.ChyronKinds.Add(Style);
        };
        AddKind(EPSChyronKind::ScoreAlert, 3, 5.f);
        AddKind(EPSChyronKind::DriveSummary, 2, 6.f);
        AddKind(EPSChyronKind::PlayStat, 1, 4.f);
        AddKind(EPSChyronKind::StatLine, 1, 4.f);
        AddKind(EPSChyronKind::Custom, 0, 3.f);
        return Theme;
    }

    static FString CurrentHeadline(const UPSOverlayBroadcastSubsystem& Broadcast)
    {
        FPSChyron Chyron;
        return Broadcast.GetCurrentChyron(Chyron) ? Chyron.Headline : FString(TEXT("<none>"));
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Theme and shared helpers
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSBroadcastThemeTest,
    "PlaySports.Overlay.BroadcastThemeValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSBroadcastThemeTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSBroadcastOverlayTheme Theme;
    if (TestTrue(TEXT("broadcast_overlay.json loads"), Ingestion->LoadBroadcastOverlayThemeFromJson(UPSOverlayBroadcastSubsystem::GetDefaultThemePath(), Theme)))
    {
        for (const FString& Problem : UPSOverlayBroadcastSubsystem::ValidateTheme(Theme))
        {
            AddError(FString::Printf(TEXT("broadcast_overlay.json: %s"), *Problem));
        }
        const FPSChyronKindStyle* Score = Theme.FindChyronKind(EPSChyronKind::ScoreAlert);
        const FPSChyronKindStyle* Play = Theme.FindChyronKind(EPSChyronKind::PlayStat);
        if (TestNotNull(TEXT("Score alerts are styled"), Score) && TestNotNull(TEXT("Play lines are styled"), Play))
        {
            TestTrue(TEXT("Points outrank a play line"), Score->Priority > Play->Priority);
        }
        TestEqual(TEXT("The red zone starts at the opponent's 20"), Theme.RedZoneYardLine, 80);
    }

    FPSBroadcastOverlayTheme Bad = Theme;
    Bad.BarColor = TEXT("black");
    Bad.ChyronMaxQueued = 0;
    Bad.ChyronKinds.Reset();
    // A bad color, a bad queue size, and five kinds with no entry.
    TestEqual(TEXT("A bad theme is caught"), UPSOverlayBroadcastSubsystem::ValidateTheme(Bad).Num(), 7);

    // One clock rule for the simulation and the score bug.
    FPlayState State;
    State.Phase = EPlayPhase::PreSnap;
    State.bIsClockRunning = false;
    TestFalse(TEXT("Pre-snap with the clock stopped: stopped"), PSGameStateEvents::IsGameClockRunning(State));
    TestTrue(TEXT("Pre-snap: the play clock runs"), PSGameStateEvents::IsPlayClockRunning(State));
    State.bIsClockRunning = true;
    TestTrue(TEXT("Pre-snap with the clock left running: runs"), PSGameStateEvents::IsGameClockRunning(State));
    State.Phase = EPlayPhase::PassRush;
    TestTrue(TEXT("During the play: runs"), PSGameStateEvents::IsGameClockRunning(State));
    TestFalse(TEXT("During the play: no play clock"), PSGameStateEvents::IsPlayClockRunning(State));
    State.Phase = EPlayPhase::Scoring;
    TestFalse(TEXT("While a play is scored: stopped"), PSGameStateEvents::IsGameClockRunning(State));

    TestEqual(TEXT("Quarter 1"), PSGameStateEvents::QuarterLabel(1), FString(TEXT("1st")));
    TestEqual(TEXT("Quarter 4"), PSGameStateEvents::QuarterLabel(4), FString(TEXT("4th")));
    TestEqual(TEXT("Overtime"), PSGameStateEvents::QuarterLabel(5), FString(TEXT("OT")));
    TestEqual(TEXT("Clock 15:00"), PSGameStateEvents::ClockText(900.f), FString(TEXT("15:00")));
    TestEqual(TEXT("Clock 0:42"), PSGameStateEvents::ClockText(41.3f), FString(TEXT("0:42")));
    TestEqual(TEXT("Clock never negative"), PSGameStateEvents::ClockText(-3.f), FString(TEXT("0:00")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The score bug follows the play simulation over the bus
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSScoreBugTest,
    "PlaySports.Overlay.ScoreBugFollowsTheSimulation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSScoreBugTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSOverlayBroadcastTests::CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    UPSOverlayBroadcastSubsystem* Broadcast = World->GetSubsystem<UPSOverlayBroadcastSubsystem>();
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Broadcast subsystem"), Broadcast))
    {
        PSOverlayBroadcastTests::DestroyTestWorld(World);
        return false;
    }
    Broadcast->SetOverlayDetail(EPSOverlayDetail::Full);
    TestFalse(TEXT("Nothing to show before the game says anything"), Broadcast->GetScoreBug().bValid);

    // The simulation announces its opening state.
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    Sim->InitializePlay(TArray<FPlayerAttributes>(), TArray<FPlayerAttributes>());
    Sim->InitializeWithWorld(World);
    TestEqual(TEXT("One announcement of the opening state"), PSOverlayBroadcastTests::CountGameStateEvents(*Bus), 1);

    FPSScoreBugState Bug = Broadcast->GetScoreBug();
    TestTrue(TEXT("The score bug has a game"), Bug.bValid);
    TestEqual(TEXT("1st quarter"), Bug.QuarterText, FString(TEXT("1st")));
    TestEqual(TEXT("15:00 on the clock"), Bug.GameClockText, FString(TEXT("15:00")));
    TestEqual(TEXT("1st & 10 at own 20"), Bug.SituationText, FString(TEXT("1st & 10 at own 20")));
    TestEqual(TEXT("0-0"), Bug.HomeScore + Bug.AwayScore, 0);
    TestTrue(TEXT("Home has the ball"), Bug.bHomeHasPossession);
    TestEqual(TEXT("Three timeouts each"), Bug.HomeTimeouts + Bug.AwayTimeouts, 6);
    TestEqual(TEXT("...out of three"), Bug.MaxTimeouts, 3);
    TestEqual(TEXT("The theme's home label"), Bug.HomeLabel, Broadcast->GetTheme().HomeLabel);
    TestFalse(TEXT("Not in the red zone"), Bug.bRedZone);

    // Only the play clock runs before the snap: the sim says nothing new, the bug runs it on.
    Sim->AdvancePlay(1.f);
    TestEqual(TEXT("A clock tick is not announced"), PSOverlayBroadcastTests::CountGameStateEvents(*Bus), 1);
    Broadcast->AdvanceTime(1.f);
    Bug = Broadcast->GetScoreBug();
    TestEqual(TEXT("The play clock runs on"), Bug.PlayClockText, FString(TEXT("39")));
    TestEqual(TEXT("The game clock waits for the snap"), Bug.GameClockText, FString(TEXT("15:00")));

    // A timeout before the snap.
    TestTrue(TEXT("Home calls a timeout"), Sim->CallTimeout(true));
    TestEqual(TEXT("Home has two left"), Broadcast->GetScoreBug().HomeTimeouts, 2);

    // The snap: the game clock runs, the play clock goes away.
    Sim->TriggerSnap();
    Broadcast->AdvanceTime(5.f);
    Bug = Broadcast->GetScoreBug();
    TestTrue(TEXT("After the snap the game clock runs"), Bug.bGameClockRunning);
    TestEqual(TEXT("...five seconds later 14:55"), Bug.GameClockText, FString(TEXT("14:55")));
    TestTrue(TEXT("No play clock during the play"), Bug.PlayClockText.IsEmpty());

    // A 65-yard gain: first down inside the opponent's 20.
    Sim->RecordTackle(65);
    Sim->EndPlayAndPrepareNext();
    Bug = Broadcast->GetScoreBug();
    TestEqual(TEXT("1st & 10 at opp 15"), Bug.SituationText, FString(TEXT("1st & 10 at opp 15")));
    TestTrue(TEXT("Red zone"), Bug.bRedZone);
    TestEqual(TEXT("The bug's clock is the sim's after the play"), Bug.GameClockSeconds, Sim->GetPlayState().GameClockSeconds, 0.01f);

    // A touchdown: points, possession changes, a score alert and a drive summary.
    Sim->TriggerSnap();
    Sim->RecordTouchdown();
    Sim->EndPlayAndPrepareNext();
    const FPlayState After = Sim->GetPlayState();
    Bug = Broadcast->GetScoreBug();
    TestTrue(TEXT("Six or seven points"), After.HomeScore == 6 || After.HomeScore == 7);
    TestEqual(TEXT("The bug shows the sim's score"), Bug.HomeScore, After.HomeScore);
    TestTrue(TEXT("The scorers kick off: home has the ball until the kick (Epic 75)"), Bug.bHomeHasPossession);
    TestEqual(TEXT("The kickoff is announced"), Bug.SituationText, FString(TEXT("Kickoff")));
    FPSChyron Chyron;
    if (TestTrue(TEXT("A chyron is up"), Broadcast->GetCurrentChyron(Chyron)))
    {
        TestEqual(TEXT("The score alert first"), Chyron.Kind, EPSChyronKind::ScoreAlert);
        TestEqual(TEXT("...a touchdown"), Chyron.Headline, FString(TEXT("TOUCHDOWN")));
        TestTrue(TEXT("...with the score"), Chyron.Detail.Contains(FString::Printf(TEXT("%s %d"), *Bug.HomeLabel, After.HomeScore)));
    }
    TestEqual(TEXT("The drive summary waits"), Broadcast->GetQueuedChyronCount(), 1);
    Broadcast->AdvanceTime(Chyron.Seconds + Broadcast->GetTheme().ChyronGapSeconds + 0.1f);
    if (TestTrue(TEXT("Then the drive summary"), Broadcast->GetCurrentChyron(Chyron)))
    {
        TestEqual(TEXT("A drive summary"), Chyron.Kind, EPSChyronKind::DriveSummary);
        TestEqual(TEXT("Credited to home"), Chyron.Headline, FString::Printf(TEXT("%s DRIVE"), *Bug.HomeLabel));
        TestTrue(TEXT("...ending in a touchdown"), Chyron.Detail.Contains(TEXT("Touchdown")));
    }

    // A sack becomes a play line.
    FPSTelemetryTackleEvent Sack;
    Sack.TacklerName = TEXT("DE_1");
    Sack.BallCarrierName = TEXT("QB_1");
    Sack.YardsGained = -7;
    Sack.bIsSack = true;
    Bus->PublishTackle(Sack);
    TestTrue(TEXT("The sack waits its turn"), Broadcast->GetQueuedChyronCount() >= 1);

    // The two-minute state, straight from an announcement.
    FPSTelemetryGameStateEvent Late = PSGameStateEvents::MakeEvent(Sim->GetPlayState(), FDriveSummary(), 1, 3);
    Late.Quarter = 4;
    Late.GameClockSeconds = 121.f;
    Late.bGameClockRunning = true;
    Bus->PublishGameState(Late);
    TestFalse(TEXT("2:01 left: not yet"), Broadcast->GetScoreBug().bTwoMinute);
    Broadcast->AdvanceTime(2.f);
    TestTrue(TEXT("The running clock crosses 2:00"), Broadcast->GetScoreBug().bTwoMinute);
    Late.Quarter = 3;
    Late.GameClockSeconds = 90.f;
    Bus->PublishGameState(Late);
    TestFalse(TEXT("Never in the 3rd quarter"), Broadcast->GetScoreBug().bTwoMinute);

    // Teams by ID: their abbreviations and colors.
    TArray<FPSTeamSummary> Teams;
    TArray<FString> Errors;
    UPSUITeamCatalog::BuildSummaries(UPSUITeamCatalog::GetDefaultTeamsPath(), Teams, Errors);
    if (TestTrue(TEXT("At least two teams"), Teams.Num() >= 2))
    {
        Broadcast->SetTeams(Teams[0].TeamId, Teams[1].TeamId);
        Bug = Broadcast->GetScoreBug();
        TestEqual(TEXT("Home team's abbreviation"), Bug.HomeLabel, Teams[0].Abbreviation);
        TestEqual(TEXT("Away team's abbreviation"), Bug.AwayLabel, Teams[1].Abbreviation);
        TestTrue(TEXT("Home team's color"), Bug.HomeColor.Equals(Teams[0].PrimaryColor));
        Broadcast->SetTeams(NAME_None, NAME_None);
        TestEqual(TEXT("Back to the theme's label"), Broadcast->GetScoreBug().HomeLabel, Broadcast->GetTheme().HomeLabel);
    }

    PSOverlayBroadcastTests::DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Chyron queue rules
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSChyronQueueTest,
    "PlaySports.Overlay.ChyronQueueRules",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSChyronQueueTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSOverlayBroadcastTests::CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    UPSOverlayBroadcastSubsystem* Broadcast = World->GetSubsystem<UPSOverlayBroadcastSubsystem>();
    if (!TestNotNull(TEXT("Broadcast subsystem"), Broadcast))
    {
        PSOverlayBroadcastTests::DestroyTestWorld(World);
        return false;
    }
    Broadcast->SetTheme(PSOverlayBroadcastTests::RulesTheme());
    Broadcast->SetOverlayDetail(EPSOverlayDetail::Full);

    // The first one goes straight up.
    TestTrue(TEXT("A play line is pushed"), Broadcast->PushChyron(EPSChyronKind::PlayStat, TEXT("A"), TEXT("first")));
    TestEqual(TEXT("...and shown at once"), PSOverlayBroadcastTests::CurrentHeadline(*Broadcast), FString(TEXT("A")));

    // A higher priority one waits until A has been up the minimum, then cuts in.
    Broadcast->AdvanceTime(1.f);
    Broadcast->PushChyron(EPSChyronKind::DriveSummary, TEXT("D"), TEXT("drive"));
    Broadcast->AdvanceTime(0.5f);
    TestEqual(TEXT("1.5 s up: A stays"), PSOverlayBroadcastTests::CurrentHeadline(*Broadcast), FString(TEXT("A")));
    Broadcast->AdvanceTime(0.6f);
    TestEqual(TEXT("2.1 s up: D cuts in"), PSOverlayBroadcastTests::CurrentHeadline(*Broadcast), FString(TEXT("D")));

    // The queue: best first, first come first served, three at most (the lowest priority,
    // oldest of them, goes).
    Broadcast->PushChyron(EPSChyronKind::PlayStat, TEXT("B"), TEXT("second"));
    Broadcast->PushStatLine(TEXT("C"), TEXT("5 catches, 72 yards"));
    Broadcast->PushChyron(EPSChyronKind::Custom, TEXT("X"), TEXT("filler"));
    TestEqual(TEXT("Three waiting"), Broadcast->GetQueuedChyronCount(), 3);
    Broadcast->PushChyron(EPSChyronKind::PlayStat, TEXT("E"), TEXT("third"));
    TestEqual(TEXT("Still three waiting"), Broadcast->GetQueuedChyronCount(), 3);

    // D runs its 6 s (it has 0 so far), a gap, then B, C, E in order; X was dropped.
    Broadcast->AdvanceTime(6.f);
    TestEqual(TEXT("D's time is up: the gap"), PSOverlayBroadcastTests::CurrentHeadline(*Broadcast), FString(TEXT("<none>")));
    Broadcast->AdvanceTime(0.5f);
    TestEqual(TEXT("After the gap: B"), PSOverlayBroadcastTests::CurrentHeadline(*Broadcast), FString(TEXT("B")));

    // An equal priority one never cuts in.
    Broadcast->AdvanceTime(2.5f);
    Broadcast->PushChyron(EPSChyronKind::PlayStat, TEXT("F"), TEXT("fourth"));
    Broadcast->AdvanceTime(0.1f);
    TestEqual(TEXT("B stays on against an equal"), PSOverlayBroadcastTests::CurrentHeadline(*Broadcast), FString(TEXT("B")));
    Broadcast->AdvanceTime(1.5f);
    Broadcast->AdvanceTime(0.5f);
    FPSChyron Chyron;
    if (TestTrue(TEXT("Then C"), Broadcast->GetCurrentChyron(Chyron)))
    {
        TestEqual(TEXT("C is the stat line"), Chyron.Headline, FString(TEXT("C")));
        TestEqual(TEXT("...of kind StatLine"), Chyron.Kind, EPSChyronKind::StatLine);
    }
    Broadcast->AdvanceTime(4.f);
    Broadcast->AdvanceTime(0.5f);
    TestEqual(TEXT("Then E"), PSOverlayBroadcastTests::CurrentHeadline(*Broadcast), FString(TEXT("E")));
    Broadcast->AdvanceTime(4.f);
    Broadcast->AdvanceTime(0.5f);
    TestEqual(TEXT("Then F; X never shows"), PSOverlayBroadcastTests::CurrentHeadline(*Broadcast), FString(TEXT("F")));

    // A Minimal tier draws the score bug only.
    Broadcast->SetOverlayDetail(EPSOverlayDetail::Minimal);
    TestEqual(TEXT("Minimal: nothing on screen"), PSOverlayBroadcastTests::CurrentHeadline(*Broadcast), FString(TEXT("<none>")));
    TestFalse(TEXT("Minimal: nothing is queued"), Broadcast->PushChyron(EPSChyronKind::ScoreAlert, TEXT("TOUCHDOWN"), TEXT("7-0")));
    TestEqual(TEXT("Minimal: the queue is empty"), Broadcast->GetQueuedChyronCount(), 0);

    PSOverlayBroadcastTests::DestroyTestWorld(World);
    return true;
}

#endif
