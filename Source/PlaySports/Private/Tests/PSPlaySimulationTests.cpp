// PSPlaySimulationTests.cpp -- Epic C2: Single Outcome Authority tests
//
// Tests covered:
//   1. Physical-event-driven outcome: publish a Catch event on the bus ->
//      phase must transition to BallCarrierMovement (not stay in PassRush).
//   2. Quick-sim flag: bQuickSimMode=true + AdvancePlay past BallCarrierMovement
//      timer -> ResolvePlayResult ran (result is not the default Incomplete).
//   3. No dual-write: OnBusScoreEvent keeps FPlayState.HomeScore/AwayScore in sync.
//   4. Timeout budget.
//   5. A live interception is a turnover: the defense gets the ball where the return ended (the
//      interceptor's tackle; a touchback in the end zone he defends), the box score counts the
//      turnover and the takeaway, and a defensive flag the offense accepts wipes it out.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSPlaySimulation.h"
#include "PSSpecialTeamsModel.h"
#include "PSStatsData.h"
#include "PSStatsEngine.h"
#include "PSTelemetryBus.h"
#include "PSPlayerAttributes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Catch bus event drives phase transition (no quick-sim mode)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSimC2BusCatchDrivesPhase,
    "PlaySports.C2.BusCatchDrivesPhase",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSimC2BusCatchDrivesPhase::RunTest(const FString& Parameters)
{
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    TestNotNull(TEXT("Sim created"), Sim);
    if (!Sim) { return false; }

    TArray<FPlayerAttributes> Offense, Defense;
    Sim->InitializePlay(Offense, Defense);
    Sim->TriggerSnap();                              // PreSnap -> Snap
    Sim->SetPlayPhase(EPlayPhase::PassRush);         // Advance to PassRush

    // Invoke the handler directly (public UFUNCTION)
    FPSTelemetryCatchEvent CatchEvt;
    CatchEvt.ReceiverName    = TEXT("TestReceiver");
    CatchEvt.CatchLocation   = FVector::ZeroVector;
    CatchEvt.YardsGained     = 0;
    CatchEvt.bIsInterception = false;
    Sim->OnBusCatchEvent(CatchEvt);

    TestEqual(TEXT("Catch bus event transitions to BallCarrierMovement"),
        Sim->GetPlayState().Phase, EPlayPhase::BallCarrierMovement);

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Quick-sim flag enables statistical resolver
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSimC2QuickSimResolves,
    "PlaySports.C2.QuickSimResolves",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSimC2QuickSimResolves::RunTest(const FString& Parameters)
{
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    TestNotNull(TEXT("Sim created"), Sim);
    if (!Sim) { return false; }

    FPlayerAttributes QB;
    QB.Role         = EPlayerRole::Quarterback;
    QB.Awareness    = 80.f;
    QB.Speed        = 80.f;
    QB.Agility      = 80.f;
    QB.Strength     = 80.f;
    QB.Acceleration = 80.f;

    FPlayerAttributes DB;
    DB.Role         = EPlayerRole::DefensiveBack;
    DB.Awareness    = 60.f;
    DB.Speed        = 70.f;
    DB.Agility      = 70.f;
    DB.Strength     = 70.f;
    DB.Acceleration = 70.f;

    TArray<FPlayerAttributes> Offense = { QB };
    TArray<FPlayerAttributes> Defense = { DB };
    Sim->InitializePlay(Offense, Defense);
    Sim->bQuickSimMode = true;

    Sim->TriggerSnap();                                     // PreSnap -> Snap
    Sim->SetPlayPhase(EPlayPhase::BallCarrierMovement);     // Skip to BCM

    // Advance past the 3.0s BCM timer -- statistical resolver should fire
    Sim->AdvancePlay(3.5f);

    // Phase must now be Scoring
    TestEqual(TEXT("Quick-sim advances to Scoring phase"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);

    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- No dual-write: bus score event keeps FPlayState in sync
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSimC2NoDualWrite,
    "PlaySports.C2.NoDualWrite",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSimC2NoDualWrite::RunTest(const FString& Parameters)
{
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    TestNotNull(TEXT("Sim created"), Sim);
    if (!Sim) { return false; }

    TArray<FPlayerAttributes> Offense, Defense;
    Sim->InitializePlay(Offense, Defense);

    TestEqual(TEXT("Initial HomeScore is 0"), Sim->GetPlayState().HomeScore, 0);
    TestEqual(TEXT("Initial AwayScore is 0"), Sim->GetPlayState().AwayScore, 0);

    FPSTelemetryScoreEvent ScoreEvt;
    ScoreEvt.ScoreType   = TEXT("Touchdown");
    ScoreEvt.bHomeScored = true;
    ScoreEvt.Points      = 7;
    ScoreEvt.HomeScore   = 7;
    ScoreEvt.AwayScore   = 0;
    Sim->OnBusScoreEvent(ScoreEvt);

    TestEqual(TEXT("FPlayState.HomeScore synced from bus"), Sim->GetPlayState().HomeScore, 7);
    TestEqual(TEXT("FPlayState.AwayScore stays 0"),         Sim->GetPlayState().AwayScore, 0);

    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Timeout budget tests
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSimTimeoutBudget,
    "PlaySports.Clock.TimeoutBudget",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSimTimeoutBudget::RunTest(const FString& Parameters)
{
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    TestNotNull(TEXT("Sim created"), Sim);
    if (!Sim) { return false; }

    TArray<FPlayerAttributes> Offense, Defense;
    Sim->InitializePlay(Offense, Defense);

    // Initial timeouts should be 3
    TestEqual(TEXT("Initial Home Timeouts is 3"), Sim->GetPlayState().HomeTimeoutsRemaining, 3);
    TestEqual(TEXT("Initial Away Timeouts is 3"), Sim->GetPlayState().AwayTimeoutsRemaining, 3);

    // Call home timeout pre-snap
    Sim->SetPlayPhase(EPlayPhase::PreSnap);
    bool bSuccess = Sim->CallTimeout(true);
    TestTrue(TEXT("Call timeout successfully"), bSuccess);
    TestEqual(TEXT("Home Timeouts decremented to 2"), Sim->GetPlayState().HomeTimeoutsRemaining, 2);
    TestFalse(TEXT("Clock is stopped"), Sim->GetPlayState().bIsClockRunning);

    // Call remaining home timeouts
    Sim->CallTimeout(true);
    Sim->CallTimeout(true);
    TestEqual(TEXT("Home Timeouts is 0"), Sim->GetPlayState().HomeTimeoutsRemaining, 0);

    // Call one more - should fail
    bool bFail = Sim->CallTimeout(true);
    TestFalse(TEXT("Cannot call timeout with 0 remaining"), bFail);

    // Test transition to Q3 resets timeouts
    Sim->SetPlayPhase(EPlayPhase::PassRush); // Set to phase that ticks game clock
    Sim->TriggerSnap(); // starts clock

    Sim->AdvancePlay(905.f); // Q1 -> Q2
    TestEqual(TEXT("Quarter is 2"), Sim->GetPlayState().Quarter, 2);
    Sim->AdvancePlay(905.f); // Q2 -> Q3
    TestEqual(TEXT("Quarter is 3"), Sim->GetPlayState().Quarter, 3);

    // After transitioning to Q3 (2nd half), timeouts should reset to 3
    TestEqual(TEXT("Home Timeouts reset to 3 in Q3"), Sim->GetPlayState().HomeTimeoutsRemaining, 3);

    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- A live interception is a turnover
// ---------------------------------------------------------------------------
namespace PSSimInterceptionTests
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

    static FPlayerAttributes MakePlayer(const TCHAR* PlayerId, const TCHAR* DisplayName, EPlayerRole Role)
    {
        FPlayerAttributes Player;
        Player.PlayerId = FName(PlayerId);
        Player.DisplayName = DisplayName;
        Player.Role = Role;
        return Player;
    }

    /** The home quarterback and receiver against the away corner and linebacker. */
    static const TArray<FPlayerAttributes>& HomeOffense()
    {
        static const TArray<FPlayerAttributes> Players = {
            MakePlayer(TEXT("HOME_QB"), TEXT("Home Passer"), EPlayerRole::Quarterback),
            MakePlayer(TEXT("HOME_WR"), TEXT("Home Receiver"), EPlayerRole::WideReceiver) };
        return Players;
    }

    static const TArray<FPlayerAttributes>& AwayDefense()
    {
        static const TArray<FPlayerAttributes> Players = {
            MakePlayer(TEXT("AWAY_CB"), TEXT("Away Corner"), EPlayerRole::DefensiveBack),
            MakePlayer(TEXT("AWAY_LB"), TEXT("Away Backer"), EPlayerRole::Linebacker) };
        return Players;
    }

    /** A simulation on World's bus, the home offense at its 20, snapped with no flag down (the
     *  snap's random offside is cleared so the test needs no luck). */
    static UPSPlaySimulation* SnapNewSimulation(UWorld* World)
    {
        UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
        Sim->InitializePlay(HomeOffense(), AwayDefense());
        Sim->InitializeWithWorld(World);
        Sim->TriggerSnap();
        Sim->ActivePenalty = EPSPenaltyType::None;
        return Sim;
    }

    /** The home passer throws for his receiver; the away corner catches it at CatchX (cm). */
    static void PublishPick(UPSTelemetryBus* Bus, float CatchX)
    {
        FPSTelemetryThrowEvent Throw;
        Throw.PasserName = HomeOffense()[0].DisplayName;
        Throw.TargetReceiverName = HomeOffense()[1].DisplayName;
        Bus->PublishThrow(Throw);
        FPSTelemetryCatchEvent Pick;
        Pick.ReceiverName = AwayDefense()[0].DisplayName;
        Pick.CatchLocation = FVector(CatchX, 0.f, 100.f);
        Pick.bIsInterception = true;
        Bus->PublishCatch(Pick);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSimInterceptionTurnover,
    "PlaySports.C2.InterceptionIsATurnover",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSimInterceptionTurnover::RunTest(const FString& Parameters)
{
    using namespace PSSimInterceptionTests;

    // 1. Picked off at the 45 and returned to the 38, where the receiver tackles the corner.
    {
        UWorld* World = CreateTestWorld();
        UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
        if (!TestNotNull(TEXT("Bus"), Bus))
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
            return false;
        }
        UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
        Stats->BindToBus(Bus);
        Stats->BeginGame(1, FName(TEXT("Home")), FName(TEXT("Away")));
        TArray<FPSTelemetryPlayResultEvent> Plays;
        Bus->OnPlayResultMC.AddLambda([&Plays](const FPSTelemetryPlayResultEvent& Event) { Plays.Add(Event); });

        UPSPlaySimulation* Sim = SnapNewSimulation(World);
        PublishPick(Bus, 4500.f);
        TestEqual(TEXT("The interceptor runs with it"), Sim->GetPlayState().Phase, EPlayPhase::BallCarrierMovement);
        TestEqual(TEXT("...and the play is an interception"), Sim->GetPlayResult().ResultType, EPlayResultType::Interception);

        FPSTelemetryTackleEvent ReturnTackle;
        ReturnTackle.TacklerName = HomeOffense()[1].DisplayName;
        ReturnTackle.BallCarrierName = AwayDefense()[0].DisplayName;
        ReturnTackle.YardLine = 38;
        ReturnTackle.YardsGained = -7;
        Bus->PublishTackle(ReturnTackle);
        TestEqual(TEXT("The tackle ends the return"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);
        TestEqual(TEXT("...and the turnover stands"), Sim->GetPlayResult().ResultType, EPlayResultType::Interception);

        Sim->EndPlayAndPrepareNext();
        const FPlayState State = Sim->GetPlayState();
        TestFalse(TEXT("The away team has the ball"), State.bHomeHasPossession);
        TestEqual(TEXT("...where the return ended: the home 38 is the away 62"), State.YardLine, 62);
        TestTrue(TEXT("...1st and 10"), State.Down == 1 && State.Distance == 10 && State.YardLineToGain == 72);
        TestFalse(TEXT("The change of possession stops the clock"), State.bIsClockRunning);
        TestTrue(TEXT("The away players are the offense now"),
            Sim->GetOffenseRoster().Num() > 0 && Sim->GetOffenseRoster()[0].PlayerId == AwayDefense()[0].PlayerId);
        if (TestEqual(TEXT("One play announced"), Plays.Num(), 1))
        {
            const FPSTelemetryPlayResultEvent& Played = Plays[0];
            TestEqual(TEXT("...an interception"), Played.Result, FString(TEXT("Interception")));
            TestTrue(TEXT("...of a pass"), Played.bInterception && Played.bPass && !Played.bComplete);
            TestEqual(TEXT("...thrown by the home passer"), Played.PasserId, HomeOffense()[0].PlayerId);
            TestEqual(TEXT("...caught by the away corner"), Played.InterceptorId, AwayDefense()[0].PlayerId);
            TestTrue(TEXT("...with no defensive tackle credited for the return"), Played.TacklerId.IsNone());
            TestTrue(TEXT("...not a turnover on downs, not a first down"), !Played.bTurnoverOnDowns && !Played.bFirstDown);
        }
        const FPSBoxScore& Game = Stats->GetCurrentGame();
        TestEqual(TEXT("The home team turned it over"), Game.Home.Turnovers, 1);
        TestEqual(TEXT("The away team took it away"), Game.Away.Takeaways, 1);
        const FPSPlayerStatLine* Passer = Game.FindPlayer(HomeOffense()[0].PlayerId);
        TestTrue(TEXT("The passer threw an interception"), Passer && Passer->InterceptionsThrown == 1);
        const FPSPlayerStatLine* Corner = Game.FindPlayer(AwayDefense()[0].PlayerId);
        TestTrue(TEXT("The corner has it, for the away team"), Corner && Corner->Interceptions == 1 && Corner->TeamId == FName(TEXT("Away")));
        const FPSPlayerStatLine* Receiver = Game.FindPlayer(HomeOffense()[1].PlayerId);
        TestTrue(TEXT("The receiver's tackle on the return isn't a defensive tackle"), !Receiver || Receiver->Tackles == 0);

        Stats->UnbindFromBus();
        DestroyTestWorld(World);
    }

    // 2. Picked off at the 97 and carried into the end zone he defends (the end-zone volume calls
    //    RecordTouchdown): a touchback for the away team, not a home touchdown.
    {
        UWorld* World = CreateTestWorld();
        UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
        if (!TestNotNull(TEXT("Bus"), Bus))
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
            return false;
        }
        UPSPlaySimulation* Sim = SnapNewSimulation(World);
        PublishPick(Bus, 9700.f);
        Sim->RecordTouchdown();
        TestEqual(TEXT("Down in his end zone: the turnover stands"), Sim->GetPlayResult().ResultType, EPlayResultType::Interception);
        Sim->EndPlayAndPrepareNext();
        const FPlayState State = Sim->GetPlayState();
        TestEqual(TEXT("No home touchdown"), State.HomeScore, 0);
        TestFalse(TEXT("The away team has the ball"), State.bHomeHasPossession);
        TestEqual(TEXT("...at the touchback line"), State.YardLine, Sim->GetSpecialTeams()->GetTuning().PuntTouchbackYardLine);
        DestroyTestWorld(World);
    }

    // 3. Picked off at the 30 and run out of bounds, but the defense jumped offside: the offense
    //    accepts, the interception is wiped out and the home team keeps the ball 5 yards on.
    {
        UWorld* World = CreateTestWorld();
        UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
        if (!TestNotNull(TEXT("Bus"), Bus))
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
            return false;
        }
        TArray<FPSTelemetryPlayResultEvent> Plays;
        Bus->OnPlayResultMC.AddLambda([&Plays](const FPSTelemetryPlayResultEvent& Event) { Plays.Add(Event); });
        UPSPlaySimulation* Sim = SnapNewSimulation(World);
        Sim->ActivePenalty = EPSPenaltyType::Offsides;
        PublishPick(Bus, 3000.f);
        Sim->RecordOutOfBounds(15);
        TestEqual(TEXT("Out of bounds ends the return: the turnover stands"), Sim->GetPlayResult().ResultType, EPlayResultType::Interception);
        Sim->EndPlayAndPrepareNext();
        TestTrue(TEXT("The flag wipes it out: the home team keeps the ball"), Sim->GetPlayState().bHomeHasPossession);
        TestEqual(TEXT("...5 yards on"), Sim->GetPlayState().YardLine, 25);
        if (TestEqual(TEXT("One play announced"), Plays.Num(), 1))
        {
            TestFalse(TEXT("...with no interception in it"), Plays[0].bInterception);
        }
        DestroyTestWorld(World);
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
