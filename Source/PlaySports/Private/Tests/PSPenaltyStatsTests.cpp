// PSPenaltyStatsTests.cpp -- Epic 92's statistics know penalties: a team stat, not a play's
//
// Tests covered:
//   1. In a played game the stats engine hears each flag's ruling on the bus (Epic 23's Penalty
//      event) just before its play. An accepted holding on a completed pass is the offense's
//      penalty for its 10 yards; the pass it wiped out credits no completion, attempt, passing,
//      receiving or team yards and no tackle. A declined flag lets its play stand. An accepted
//      defensive offside is the defense's penalty for 5 yards and the run credits nobody.
//   2. A quick-sim game hears the same rulings through UPSQuickSimRunner::OnPenaltyRuled: each
//      accepted one is a penalty against the fouling team for its yards, and no player is
//      credited the yards of a play a flag wiped out.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "PSPlaySimulation.h"
#include "PSPlayerAttributes.h"
#include "PSQuickSimRunner.h"
#include "PSStatsData.h"
#include "PSStatsEngine.h"
#include "PSTelemetryBus.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPenaltyStatsTests
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

    /** A side of 11: QB 0, RB 1, WR 2-3, TE 4, OL 5-6, DL 7, LB 8, DB 9-10. */
    static TArray<FPlayerAttributes> MakeSide(const TCHAR* Prefix, float Rating)
    {
        static const TArray<EPlayerRole> Roles = {
            EPlayerRole::Quarterback, EPlayerRole::RunningBack, EPlayerRole::WideReceiver, EPlayerRole::WideReceiver,
            EPlayerRole::TightEnd, EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman, EPlayerRole::DefensiveLineman,
            EPlayerRole::Linebacker, EPlayerRole::DefensiveBack, EPlayerRole::DefensiveBack };
        TArray<FPlayerAttributes> Side;
        for (int32 Index = 0; Index < Roles.Num(); ++Index)
        {
            FPlayerAttributes& Player = Side.AddDefaulted_GetRef();
            Player.PlayerId = FName(*FString::Printf(TEXT("%s_%02d"), Prefix, Index));
            Player.DisplayName = Player.PlayerId.ToString();
            Player.Role = Roles[Index];
            Player.Speed = Rating;
            Player.Agility = Rating;
            Player.Strength = Rating;
            Player.Acceleration = Rating;
            Player.Awareness = Rating;
            Player.Stamina = Rating;
        }
        return Side;
    }

    /** The next snap with Flag already thrown (or none): the snap's own random flags replaced. */
    static void SnapWithFlag(UPSPlaySimulation* Sim, EPSPenaltyType Flag)
    {
        Sim->TriggerSnap();
        Sim->ActivePenalty = Flag;
    }

    /** Down at YardLine, in the offense's yard lines. */
    static void PublishTackle(UPSTelemetryBus* Bus, const FPlayerAttributes& Carrier, const FPlayerAttributes& Tackler, int32 YardLine)
    {
        FPSTelemetryTackleEvent Tackle;
        Tackle.TacklerName = Tackler.DisplayName;
        Tackle.BallCarrierName = Carrier.DisplayName;
        Tackle.YardLine = YardLine;
        Bus->PublishTackle(Tackle);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- A played game's penalties
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPenaltyStatsLiveTest,
    "PlaySports.Stats.PenaltiesAreTeamStats",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPenaltyStatsLiveTest::RunTest(const FString& Parameters)
{
    using namespace PSPenaltyStatsTests;

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

    const TArray<FPlayerAttributes> Offense = MakeSide(TEXT("HOME"), 80.f);
    const TArray<FPlayerAttributes> Defense = MakeSide(TEXT("AWAY"), 80.f);
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    Sim->InitializePlay(Offense, Defense);
    Sim->InitializeWithWorld(World);
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    Stats->BindToBus(Bus);
    Stats->BeginGame(1, FName(TEXT("Home")), FName(TEXT("Away")));
    TArray<FPSTelemetryPenaltyEvent> Rulings;
    Bus->OnPenaltyMC.AddLambda([&Rulings](const FPSTelemetryPenaltyEvent& Event)
    {
        if (Event.Kind != EPSPenaltyEventKind::Flag)
        {
            Rulings.Add(Event);
        }
    });

    // 1. A 12-yard completion from the 20, flagged for offensive holding: accepted.
    SnapWithFlag(Sim, EPSPenaltyType::Holding);
    FPSTelemetryThrowEvent Throw;
    Throw.PasserName = Offense[0].DisplayName;
    Throw.TargetReceiverName = Offense[2].DisplayName;
    Bus->PublishThrow(Throw);
    FPSTelemetryCatchEvent Catch;
    Catch.ReceiverName = Offense[2].DisplayName;
    Bus->PublishCatch(Catch);
    PublishTackle(Bus, Offense[2], Defense[9], 32);
    Sim->EndPlayAndPrepareNext();
    if (TestEqual(TEXT("The flag is ruled on the bus"), Rulings.Num(), 1))
    {
        TestTrue(TEXT("...holding accepted, against the home offense, 10 yards back"),
            Rulings[0].Kind == EPSPenaltyEventKind::Accepted && Rulings[0].Penalty == TEXT("Holding") && Rulings[0].bHomeTeam && Rulings[0].Yards == -10);
    }
    const FPSBoxScore& Game = Stats->GetCurrentGame();
    TestEqual(TEXT("The home team's penalty"), Game.Home.Penalties, 1);
    TestEqual(TEXT("...for 10 yards"), Game.Home.PenaltyYards, 10);
    TestEqual(TEXT("No passing yards: not -10, not 12"), Game.Home.PassingYards, 0);
    TestEqual(TEXT("...no team yards"), Game.Home.TotalYards, 0);
    TestEqual(TEXT("...and no play from scrimmage"), Game.Home.Plays, 0);
    const FPSPlayerStatLine* Passer = Game.FindPlayer(Offense[0].PlayerId);
    TestTrue(TEXT("The passer has no completion, attempt or yards"), !Passer || (Passer->Completions == 0 && Passer->PassAttempts == 0 && Passer->PassingYards == 0));
    const FPSPlayerStatLine* Receiver = Game.FindPlayer(Offense[2].PlayerId);
    TestTrue(TEXT("The receiver no catch"), !Receiver || (Receiver->Receptions == 0 && Receiver->ReceivingYards == 0));
    const FPSPlayerStatLine* Tackler = Game.FindPlayer(Defense[9].PlayerId);
    TestTrue(TEXT("The tackler no tackle"), !Tackler || Tackler->Tackles == 0);
    TestEqual(TEXT("The away team committed none"), Game.Away.Penalties, 0);

    // 2. Holding on a run stopped for a loss at the 8 (the line is the 10 now): declined.
    SnapWithFlag(Sim, EPSPenaltyType::Holding);
    PublishTackle(Bus, Offense[1], Defense[8], 8);
    Sim->EndPlayAndPrepareNext();
    TestTrue(TEXT("Declined"), Rulings.Num() == 2 && Rulings[1].Kind == EPSPenaltyEventKind::Declined);
    TestEqual(TEXT("A declined flag is no penalty"), Stats->GetCurrentGame().Home.Penalties, 1);
    const FPSPlayerStatLine* Runner = Stats->GetCurrentGame().FindPlayer(Offense[1].PlayerId);
    TestTrue(TEXT("...and the run stands: a carry for -2"), Runner && Runner->RushAttempts == 1 && Runner->RushingYards == -2);
    TestEqual(TEXT("...one play from scrimmage"), Stats->GetCurrentGame().Home.Plays, 1);

    // 3. A 2-yard run, the defense offside: accepted, 5 yards on the defense.
    SnapWithFlag(Sim, EPSPenaltyType::Offsides);
    PublishTackle(Bus, Offense[1], Defense[8], Sim->GetPlayState().YardLine + 2);
    Sim->EndPlayAndPrepareNext();
    TestTrue(TEXT("Offside accepted, on the away defense"), Rulings.Num() == 3 && Rulings[2].Kind == EPSPenaltyEventKind::Accepted && !Rulings[2].bHomeTeam);
    TestEqual(TEXT("The away team's penalty"), Stats->GetCurrentGame().Away.Penalties, 1);
    TestEqual(TEXT("...for 5 yards"), Stats->GetCurrentGame().Away.PenaltyYards, 5);
    Runner = Stats->GetCurrentGame().FindPlayer(Offense[1].PlayerId);
    TestTrue(TEXT("The run it wiped out isn't the runner's"), Runner && Runner->RushAttempts == 1 && Runner->RushingYards == -2);
    TestEqual(TEXT("The home rushing yards are the declined play's alone"), Stats->GetCurrentGame().Home.RushingYards, -2);

    // The game's totals keep them.
    Stats->FinishGame();
    TestEqual(TEXT("The season's home penalty yards"), Stats->GetSeasonGames().Num() > 0 ? Stats->GetSeasonGames().Last().Home.PenaltyYards : -1, 10);
    FPSTeamStatLine Season;
    for (const FPSBoxScore& Box : Stats->GetSeasonGames())
    {
        Season.Accumulate(Box.Away);
    }
    TestEqual(TEXT("Team lines accumulate penalties"), Season.Penalties, 1);
    TestEqual(TEXT("...and their yards"), Season.PenaltyYards, 5);

    Stats->UnbindFromBus();
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- A quick-sim game's penalties
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPenaltyStatsQuickSimTest,
    "PlaySports.Stats.QuickSimPenaltiesAreTeamStats",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPenaltyStatsQuickSimTest::RunTest(const FString& Parameters)
{
    using namespace PSPenaltyStatsTests;

    TArray<FPlayerAttributes> Home = MakeSide(TEXT("QHOME"), 75.f);
    Home.Append(MakeSide(TEXT("QHOMED"), 75.f));
    TArray<FPlayerAttributes> Away = MakeSide(TEXT("QAWAY"), 75.f);
    Away.Append(MakeSide(TEXT("QAWAYD"), 75.f));

    // The franchise's wiring: the runner's plays and rulings into the box score.
    UPSQuickSimRunner* Runner = NewObject<UPSQuickSimRunner>();
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    Runner->OnPlayResolved.AddUObject(Stats, &UPSStatsEngine::RecordPlay);
    Runner->OnPenaltyRuled.AddUObject(Stats, &UPSStatsEngine::RecordPenalty);

    // What the rulings and plays say, to check the box score against.
    int32 HomePenalties = 0;
    int32 HomePenaltyYards = 0;
    int32 AwayPenalties = 0;
    int32 AwayPenaltyYards = 0;
    bool bNextPlayWiped = false;
    int32 CreditedPassingYards = 0;
    int32 WipedPasses = 0;
    Runner->OnPenaltyRuled.AddLambda([&](const FPSTelemetryPenaltyEvent& Ruling)
    {
        if (Ruling.Kind != EPSPenaltyEventKind::Accepted)
        {
            return;
        }
        (Ruling.bHomeTeam ? HomePenalties : AwayPenalties) += 1;
        (Ruling.bHomeTeam ? HomePenaltyYards : AwayPenaltyYards) += FMath::Abs(Ruling.Yards);
        bNextPlayWiped = true;
    });
    Runner->OnPlayResolved.AddLambda([&](const FPSTelemetryPlayResultEvent& Play)
    {
        const bool bWiped = bNextPlayWiped;
        bNextPlayWiped = false;
        if (Play.bPass && Play.bComplete && !Play.bSack && !Play.bInterception)
        {
            if (bWiped)
            {
                ++WipedPasses;
            }
            else
            {
                CreditedPassingYards += Play.YardsGained;
            }
        }
    });

    Stats->BeginGame(1, FName(TEXT("QHOME")), FName(TEXT("QAWAY")));
    FMath::RandInit(9201);
    Runner->SimulateGame(Home, Away);
    FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()));
    const FPSBoxScore& Game = Stats->GetCurrentGame();
    AddInfo(FString::Printf(TEXT("Penalties: home %d for %d, away %d for %d; %d completed passes wiped out"),
        Game.Home.Penalties, Game.Home.PenaltyYards, Game.Away.Penalties, Game.Away.PenaltyYards, WipedPasses));
    TestTrue(TEXT("A game's worth of flags were accepted"), HomePenalties + AwayPenalties > 0);
    TestEqual(TEXT("Each accepted flag is the fouling team's: home"), Game.Home.Penalties, HomePenalties);
    TestEqual(TEXT("...for its yards"), Game.Home.PenaltyYards, HomePenaltyYards);
    TestEqual(TEXT("...and away"), Game.Away.Penalties, AwayPenalties);
    TestEqual(TEXT("...for its yards"), Game.Away.PenaltyYards, AwayPenaltyYards);
    TestEqual(TEXT("Passing yards are the completions' that stood, not a penalty's"), Game.Home.PassingYards + Game.Away.PassingYards, CreditedPassingYards);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
