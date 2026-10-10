// PSStatsTests.cpp -- Epic 92 (statistics engine and record book)
//
// Tests covered:
//   1. A simulated game (Epic 24's quick sim, seeded): every play the simulation resolves reaches
//      the box score through UPSQuickSimRunner::OnPlayResolved; the box score's points are the
//      final score, passing and receiving agree, every credited player is on his team's roster,
//      and the same seed gives the same box score.
//   2. Per-play attribution from the bus: crafted PlayResult events credit the passer, receiver,
//      rusher, tackler and interceptor, a sack, a touchdown and a field goal exactly; and the play
//      simulation itself announces a played snap on the bus with its players named.
//   3. The aggregation layers: seasons from box scores, finished seasons kept as totals, careers,
//      a franchise's history and the league; leaderboards over games, seasons and careers.
//   4. The record book: a first mark set quietly, a single-game record broken each time, a season
//      record broken by a new holder or the holder in a new season, a career record only by a new
//      holder; each announced on the bus and the engine's delegate.
//   5. Derived metrics: the passer rating and the per-attempt rates, and a team's third-down,
//      red-zone, field-goal and turnover numbers from its splits.
//   6. The franchise: a season simulated through UPSFranchiseFlow fills the stat book, which agrees
//      with the standings, is archived at season end and round-trips through the franchise save.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "PSFranchiseFlow.h"
#include "PSFranchiseSaveGame.h"
#include "PSFranchiseSeason.h"
#include "PSPlaySimulation.h"
#include "PSQuickSimRunner.h"
#include "PSSaveSubsystem.h"
#include "PSScheduleEngine.h"
#include "PSStaffManager.h"
#include "PSStatsData.h"
#include "PSStatsEngine.h"
#include "PSTelemetryBus.h"
#include "PSUITeamCatalog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSStatsTests
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

    /** Eleven players with every rating at Rating (QB, RB, 2 WR, TE, 2 OL, DL, LB, 2 DB), ids
     *  Prefix_00 .. Prefix_10 and display names "Prefix Name NN". */
    static TArray<FPlayerAttributes> MakeSideRoster(const TCHAR* Prefix, float Rating)
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
            Player.DisplayName = FString::Printf(TEXT("%s Name %02d"), Prefix, Index);
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

    static bool IsOnRoster(const TArray<FPlayerAttributes>& Roster, FName PlayerId)
    {
        return Roster.ContainsByPredicate([PlayerId](const FPlayerAttributes& Player) { return Player.PlayerId == PlayerId; });
    }

    /** One quick-sim game, seeded, into a fresh engine's box score. */
    static FPSBoxScore SimulateIntoBoxScore(int32 Seed, const TArray<FPlayerAttributes>& Home, const TArray<FPlayerAttributes>& Away, FPSQuickSimResult& OutResult, int32& OutEvents)
    {
        UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
        UPSQuickSimRunner* Runner = NewObject<UPSQuickSimRunner>();
        Runner->OnPlayResolved.AddUObject(Stats, &UPSStatsEngine::RecordPlay);
        int32 Events = 0;
        Runner->OnPlayResolved.AddLambda([&Events](const FPSTelemetryPlayResultEvent&) { ++Events; });

        FMath::RandInit(Seed);
        Stats->BeginGame(1, FName(TEXT("Home")), FName(TEXT("Away")));
        OutResult = Runner->SimulateGame(Home, Away);
        Stats->FinishGame();
        FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()));
        OutEvents = Events;
        return Stats->GetCurrentGame();
    }

    static int32 SumPlayers(const FPSBoxScore& Game, FName TeamId, EPSStatCategory Category)
    {
        int32 Sum = 0;
        for (const FPSPlayerStatLine& Line : Game.Players)
        {
            Sum += Line.TeamId == TeamId ? Line.GetValue(Category) : 0;
        }
        return Sum;
    }

    /** A crafted play: the home team's (or away's) Result for Yards from Down at YardLine. */
    static FPSTelemetryPlayResultEvent MakePlay(bool bHomeOffense, const TCHAR* Result, int32 Yards, int32 Down = 1, int32 YardLine = 30)
    {
        FPSTelemetryPlayResultEvent Play;
        Play.bHomeOffense = bHomeOffense;
        Play.Result = Result;
        Play.YardsGained = Yards;
        Play.Down = Down;
        Play.YardLine = YardLine;
        return Play;
    }

    static FPSTelemetryPlayResultEvent MakePass(bool bHomeOffense, const TCHAR* PasserId, const TCHAR* ReceiverId, int32 Yards, const TCHAR* TacklerId = nullptr)
    {
        FPSTelemetryPlayResultEvent Play = MakePlay(bHomeOffense, TEXT("Tackle"), Yards);
        Play.bPass = true;
        Play.bComplete = true;
        Play.PasserId = FName(PasserId);
        Play.ReceiverId = FName(ReceiverId);
        Play.TacklerId = TacklerId ? FName(TacklerId) : FName();
        return Play;
    }

    /** A game in Week between Home and Away in which each pass in Passes is recorded. */
    static void PlayGame(UPSStatsEngine* Stats, int32 Week, const TCHAR* Home, const TCHAR* Away, const TArray<FPSTelemetryPlayResultEvent>& Plays)
    {
        Stats->BeginGame(Week, FName(Home), FName(Away));
        for (const FPSTelemetryPlayResultEvent& Play : Plays)
        {
            Stats->RecordPlay(Play);
        }
        Stats->FinishGame();
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- A simulated game's box score
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStatsQuickSimTest,
    "PlaySports.Stats.QuickSimGameBoxScore",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStatsQuickSimTest::RunTest(const FString& Parameters)
{
    using namespace PSStatsTests;

    const TArray<FPlayerAttributes> Home = MakeSideRoster(TEXT("HOM"), 80.f);
    const TArray<FPlayerAttributes> Away = MakeSideRoster(TEXT("AWY"), 74.f);
    FPSQuickSimResult Result;
    int32 Events = 0;
    const FPSBoxScore Game = SimulateIntoBoxScore(2026, Home, Away, Result, Events);
    AddInfo(FString::Printf(TEXT("Final %d-%d, %d plays"), Result.HomeScore, Result.AwayScore, Game.PlayCount));

    const FName HomeId(TEXT("Home"));
    const FName AwayId(TEXT("Away"));
    TestTrue(TEXT("A whole game of plays"), Game.PlayCount > 50);
    TestEqual(TEXT("Every announced play is in the box score"), Game.PlayCount, Events);
    TestTrue(TEXT("The game is final"), Game.bFinal);
    TestEqual(TEXT("The home points are the final score"), Game.Home.Points, Result.HomeScore);
    TestEqual(TEXT("The away points are the final score"), Game.Away.Points, Result.AwayScore);
    TestEqual(TEXT("Points allowed mirror"), Game.Home.PointsAllowed, Result.AwayScore);

    for (const FName TeamId : { HomeId, AwayId })
    {
        const FPSTeamStatLine& Team = TeamId == HomeId ? Game.Home : Game.Away;
        const FString Side = TeamId.ToString();
        TestTrue(*(Side + TEXT(": passes were thrown")), SumPlayers(Game, TeamId, EPSStatCategory::Completions) > 0);
        TestEqual(*(Side + TEXT(": completions are receptions")), SumPlayers(Game, TeamId, EPSStatCategory::Completions), SumPlayers(Game, TeamId, EPSStatCategory::Receptions));
        TestEqual(*(Side + TEXT(": passing yards are receiving yards")), SumPlayers(Game, TeamId, EPSStatCategory::PassingYards), SumPlayers(Game, TeamId, EPSStatCategory::ReceivingYards));
        TestEqual(*(Side + TEXT(": the team's passing yards are its passers'")), Team.PassingYards, SumPlayers(Game, TeamId, EPSStatCategory::PassingYards));
        TestEqual(*(Side + TEXT(": touchdown passes are touchdown catches")), SumPlayers(Game, TeamId, EPSStatCategory::PassingTouchdowns), SumPlayers(Game, TeamId, EPSStatCategory::ReceivingTouchdowns));
        const FPSSplitLine* FirstDowns = Team.FindSplit(UPSStatsEngine::DownSplit(1));
        TestTrue(*(Side + TEXT(": a 1st-down split")), FirstDowns && FirstDowns->Plays > 0);
    }
    TestTrue(TEXT("The defenses made tackles"), SumPlayers(Game, HomeId, EPSStatCategory::Tackles) + SumPlayers(Game, AwayId, EPSStatCategory::Tackles) > 0);

    // Each credited player played for the team he is credited to.
    for (const FPSPlayerStatLine& Line : Game.Players)
    {
        const TArray<FPlayerAttributes>& Roster = Line.TeamId == HomeId ? Home : Away;
        TestTrue(*FString::Printf(TEXT("%s is on the %s roster"), *Line.PlayerId.ToString(), *Line.TeamId.ToString()), IsOnRoster(Roster, Line.PlayerId));
        TestEqual(*FString::Printf(TEXT("%s played one game"), *Line.PlayerId.ToString()), Line.Games, 1);
    }
    const FPSPlayerStatLine* Quarterback = Game.FindPlayer(FName(TEXT("HOM_00")));
    TestTrue(TEXT("The home quarterback threw"), Quarterback && Quarterback->PassAttempts > 0 && Quarterback->PassAttempts >= Quarterback->Completions);

    // The same seed, the same box score.
    FPSQuickSimResult Again;
    int32 AgainEvents = 0;
    const FPSBoxScore Replay = SimulateIntoBoxScore(2026, Home, Away, Again, AgainEvents);
    TestTrue(TEXT("The same seed gives the same box score"), Replay.PlayCount == Game.PlayCount && Replay.Home.PassingYards == Game.Home.PassingYards
        && Replay.Away.TotalYards == Game.Away.TotalYards && Replay.Players.Num() == Game.Players.Num());
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Per-play attribution from the bus
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStatsAttributionTest,
    "PlaySports.Stats.PlayAttributionFromTheBus",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStatsAttributionTest::RunTest(const FString& Parameters)
{
    using namespace PSStatsTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestNotNull(TEXT("Telemetry bus"), Bus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    Stats->StartSeason(2026);
    Stats->BindToBus(Bus);
    Stats->BeginGame(3, FName(TEXT("Hawks")), FName(TEXT("Wolves")));

    // A 25-yard completion on 3rd down: a conversion.
    FPSTelemetryPlayResultEvent Conversion = MakePass(true, TEXT("HAW_QB"), TEXT("HAW_WR"), 25, TEXT("WOL_LB"));
    Conversion.Down = 3;
    Conversion.bFirstDown = true;
    Bus->PublishPlayResult(Conversion);
    // A 4-yard run.
    FPSTelemetryPlayResultEvent Run = MakePlay(true, TEXT("Tackle"), 4, 1);
    Run.RusherId = FName(TEXT("HAW_RB"));
    Run.TacklerId = FName(TEXT("WOL_DL"));
    Bus->PublishPlayResult(Run);
    // An incompletion, then an interception.
    FPSTelemetryPlayResultEvent Incomplete = MakePlay(true, TEXT("Incomplete"), 0, 2);
    Incomplete.bPass = true;
    Incomplete.PasserId = FName(TEXT("HAW_QB"));
    Incomplete.ReceiverId = FName(TEXT("HAW_TE"));
    Bus->PublishPlayResult(Incomplete);
    FPSTelemetryPlayResultEvent Picked = MakePlay(true, TEXT("Tackle"), 0, 3);
    Picked.bPass = true;
    Picked.bInterception = true;
    Picked.PasserId = FName(TEXT("HAW_QB"));
    Picked.ReceiverId = FName(TEXT("HAW_WR"));
    Picked.InterceptorId = FName(TEXT("WOL_CB"));
    Bus->PublishPlayResult(Picked);
    // The Wolves' quarterback sacked for 7.
    FPSTelemetryPlayResultEvent Sack = MakePlay(false, TEXT("Tackle"), -7, 2);
    Sack.bPass = true;
    Sack.bSack = true;
    Sack.PasserId = FName(TEXT("WOL_QB"));
    Sack.TacklerId = FName(TEXT("HAW_DE"));
    Bus->PublishPlayResult(Sack);
    // A 15-yard touchdown pass from the red zone, and a Wolves field goal.
    FPSTelemetryPlayResultEvent Touchdown = MakePass(true, TEXT("HAW_QB"), TEXT("HAW_WR"), 15, TEXT("WOL_CB"));
    Touchdown.Result = TEXT("Touchdown");
    Touchdown.YardLine = 85;
    Touchdown.bFirstDown = true;
    Touchdown.HomePoints = 7;
    Bus->PublishPlayResult(Touchdown);
    FPSTelemetryPlayResultEvent FieldGoal = MakePlay(false, TEXT("FieldGoalGood"), 0, 4, 70);
    FieldGoal.AwayPoints = 3;
    Bus->PublishPlayResult(FieldGoal);
    Stats->FinishGame();

    FPSBoxScore Game;
    if (!TestTrue(TEXT("The Hawks' week-3 game"), Stats->FindGame(3, FName(TEXT("Hawks")), Game)))
    {
        DestroyTestWorld(World);
        return false;
    }
    TestEqual(TEXT("Seven plays"), Game.PlayCount, 7);
    const FPSPlayerStatLine* Passer = Game.FindPlayer(FName(TEXT("HAW_QB")));
    if (TestNotNull(TEXT("The Hawks' passer"), Passer))
    {
        TestEqual(TEXT("Four attempts"), Passer->PassAttempts, 4);
        TestEqual(TEXT("Two completions"), Passer->Completions, 2);
        TestEqual(TEXT("40 passing yards"), Passer->PassingYards, 40);
        TestEqual(TEXT("A touchdown pass"), Passer->PassingTouchdowns, 1);
        TestEqual(TEXT("An interception"), Passer->InterceptionsThrown, 1);
        TestEqual(TEXT("His team"), Passer->TeamId, FName(TEXT("Hawks")));
    }
    const FPSPlayerStatLine* Receiver = Game.FindPlayer(FName(TEXT("HAW_WR")));
    TestTrue(TEXT("The receiver: 3 targets, 2 catches, 40 yards, a touchdown"), Receiver && Receiver->Targets == 3 && Receiver->Receptions == 2
        && Receiver->ReceivingYards == 40 && Receiver->ReceivingTouchdowns == 1);
    const FPSPlayerStatLine* TightEnd = Game.FindPlayer(FName(TEXT("HAW_TE")));
    TestTrue(TEXT("The incompletion's target"), TightEnd && TightEnd->Targets == 1 && TightEnd->Receptions == 0);
    const FPSPlayerStatLine* Rusher = Game.FindPlayer(FName(TEXT("HAW_RB")));
    TestTrue(TEXT("The rusher: a carry for 4"), Rusher && Rusher->RushAttempts == 1 && Rusher->RushingYards == 4);
    const FPSPlayerStatLine* Corner = Game.FindPlayer(FName(TEXT("WOL_CB")));
    TestTrue(TEXT("The interception, and no tackle on the touchdown"), Corner && Corner->Interceptions == 1 && Corner->Tackles == 0 && Corner->TeamId == FName(TEXT("Wolves")));
    const FPSPlayerStatLine* PassRusher = Game.FindPlayer(FName(TEXT("HAW_DE")));
    TestTrue(TEXT("The sack is a tackle too"), PassRusher && PassRusher->Sacks == 1 && PassRusher->Tackles == 1);
    const FPSPlayerStatLine* Sacked = Game.FindPlayer(FName(TEXT("WOL_QB")));
    TestTrue(TEXT("The sacked quarterback: no attempt"), Sacked && Sacked->TimesSacked == 1 && Sacked->PassAttempts == 0);
    const FPSPlayerStatLine* Linebacker = Game.FindPlayer(FName(TEXT("WOL_LB")));
    const FPSPlayerStatLine* Lineman = Game.FindPlayer(FName(TEXT("WOL_DL")));
    TestTrue(TEXT("Tackles for the linebacker and lineman"), Linebacker && Lineman && Linebacker->Tackles == 1 && Lineman->Tackles == 1);

    TestEqual(TEXT("Hawks: 7 points"), Game.Home.Points, 7);
    TestEqual(TEXT("Wolves: 3 points"), Game.Away.Points, 3);
    TestEqual(TEXT("Hawks: 5 scrimmage plays"), Game.Home.Plays, 5);
    TestEqual(TEXT("Hawks: 44 yards (40 passing, 4 rushing)"), Game.Home.TotalYards, 44);
    TestEqual(TEXT("...4 rushing"), Game.Home.RushingYards, 4);
    TestEqual(TEXT("Hawks: two first downs"), Game.Home.FirstDowns, 2);
    TestEqual(TEXT("Hawks: a turnover"), Game.Home.Turnovers, 1);
    TestEqual(TEXT("Wolves: a takeaway"), Game.Away.Takeaways, 1);
    TestEqual(TEXT("Wolves: the sack's yards"), Game.Away.PassingYards, -7);
    TestTrue(TEXT("Wolves: a field goal"), Game.Away.FieldGoalsAttempted == 1 && Game.Away.FieldGoalsMade == 1 && Game.Away.Plays == 1);
    const FPSSplitLine* ThirdDown = Game.Home.FindSplit(UPSStatsEngine::DownSplit(3));
    TestTrue(TEXT("Hawks: 3rd down twice, converted once"), ThirdDown && ThirdDown->Plays == 2 && ThirdDown->Conversions == 1);
    const FPSSplitLine* RedZone = Game.Home.FindSplit(UPSStatsEngine::RedZoneSplit);
    TestTrue(TEXT("Hawks: a red-zone touchdown"), RedZone && RedZone->Plays == 1 && RedZone->Touchdowns == 1);

    // Unbound, the bus's plays no longer reach the engine.
    Stats->UnbindFromBus();
    Stats->BeginGame(4, FName(TEXT("Hawks")), FName(TEXT("Bears")));
    Bus->PublishPlayResult(Run);
    TestEqual(TEXT("Unbound: nothing recorded"), Stats->GetCurrentGame().PlayCount, 0);

    // The play simulation announces a played snap on the bus, its players named from the rosters.
    TArray<FPSTelemetryPlayResultEvent> Announced;
    Bus->OnPlayResultMC.AddLambda([&Announced](const FPSTelemetryPlayResultEvent& Event) { Announced.Add(Event); });
    const TArray<FPlayerAttributes> Offense = MakeSideRoster(TEXT("OFF"), 80.f);
    const TArray<FPlayerAttributes> Defense = MakeSideRoster(TEXT("DEF"), 80.f);
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    Sim->InitializePlay(Offense, Defense);
    Sim->InitializeWithWorld(World);
    Sim->TriggerSnap();
    FPSTelemetryThrowEvent Throw;
    Throw.PasserName = Offense[0].DisplayName;
    Throw.TargetReceiverName = Offense[2].DisplayName;
    Bus->PublishThrow(Throw);
    FPSTelemetryCatchEvent Catch;
    Catch.ReceiverName = Offense[2].DisplayName;
    Catch.YardsGained = 12;
    Bus->PublishCatch(Catch);
    FPSTelemetryTackleEvent Tackle;
    Tackle.TacklerName = Defense[9].DisplayName;
    Tackle.BallCarrierName = Offense[2].DisplayName;
    Tackle.YardsGained = 12;
    Bus->PublishTackle(Tackle);
    for (int32 Tick = 0; Tick < 20 && Announced.Num() == 0; ++Tick)
    {
        Sim->AdvancePlay(0.5f);
    }
    if (TestEqual(TEXT("The simulation announced the play"), Announced.Num(), 1))
    {
        const FPSTelemetryPlayResultEvent& Played = Announced[0];
        TestEqual(TEXT("Its first play"), Played.PlayNumber, 1);
        TestTrue(TEXT("A completed pass"), Played.bPass && Played.bComplete && !Played.bInterception);
        TestEqual(TEXT("The passer, by id"), Played.PasserId, Offense[0].PlayerId);
        TestEqual(TEXT("The receiver, by id"), Played.ReceiverId, Offense[2].PlayerId);
        TestEqual(TEXT("The tackler, by id"), Played.TacklerId, Defense[9].PlayerId);
        TestTrue(TEXT("The home offense, from 1st and 10 at its 20"), Played.bHomeOffense && Played.Down == 1 && Played.YardLine == 20);
        TestEqual(TEXT("The result by name"), Played.Result, FString(TEXT("Tackle")));
    }

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Seasons, careers, franchises, the league, leaderboards
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStatsAggregationTest,
    "PlaySports.Stats.SeasonCareerFranchiseLeague",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStatsAggregationTest::RunTest(const FString& Parameters)
{
    using namespace PSStatsTests;

    const FName HawksQB(TEXT("HAW_QB"));
    const FName WolvesQB(TEXT("WOL_QB"));
    const FName Hawks(TEXT("Hawks"));
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    TestTrue(TEXT("The book starts in 2026"), Stats->StartSeason(2026));

    PlayGame(Stats, 1, TEXT("Hawks"), TEXT("Wolves"), {
        MakePass(true, TEXT("HAW_QB"), TEXT("HAW_WR"), 10), MakePass(true, TEXT("HAW_QB"), TEXT("HAW_WR"), 10), MakePass(true, TEXT("HAW_QB"), TEXT("HAW_TE"), 10),
        MakePass(false, TEXT("WOL_QB"), TEXT("WOL_WR"), 40) });
    TestFalse(TEXT("A season with games can't be renumbered"), Stats->StartSeason(2030));
    PlayGame(Stats, 2, TEXT("Bears"), TEXT("Hawks"), { MakePass(false, TEXT("HAW_QB"), TEXT("HAW_WR"), 20) });

    const FPSPlayerStatLine Season2026 = Stats->GetPlayerSeason(HawksQB, 2026);
    TestEqual(TEXT("2026: 50 yards"), Season2026.PassingYards, 50);
    TestEqual(TEXT("2026: two games"), Season2026.Games, 2);
    TestEqual(TEXT("2026: the Hawks' passing"), Stats->GetTeamSeason(Hawks, 2026).PassingYards, 50);
    TestEqual(TEXT("2026: the league, two games of two teams"), Stats->GetLeagueTotals(2026).Games, 4);
    TestEqual(TEXT("2026: the league's passing"), Stats->GetLeagueTotals(2026).PassingYards, 90);

    // Leaderboards: games this season, the season, careers; teams too.
    const TArray<FPSLeaderEntry> SeasonLeaders = Stats->GetLeaders(EPSStatCategory::PassingYards, EPSStatScope::Season, 2026, 1);
    TestTrue(TEXT("The season's passing leader"), SeasonLeaders.Num() == 1 && SeasonLeaders[0].Id == HawksQB && SeasonLeaders[0].Value == 50);
    const TArray<FPSLeaderEntry> GameLeaders = Stats->GetLeaders(EPSStatCategory::PassingYards, EPSStatScope::Game, 2026, 0);
    TestTrue(TEXT("The best single game: the Wolves' 40"), GameLeaders.Num() == 3 && GameLeaders[0].Id == WolvesQB && GameLeaders[0].Value == 40 && GameLeaders[0].Week == 1);
    const TArray<FPSLeaderEntry> ReceivingLeaders = Stats->GetLeaders(EPSStatCategory::Receptions, EPSStatScope::Season, 2026, 0);
    TestTrue(TEXT("Receptions: HAW_WR's 3 first"), ReceivingLeaders.Num() == 3 && ReceivingLeaders[0].Id == FName(TEXT("HAW_WR")) && ReceivingLeaders[0].Value == 3);
    const TArray<FPSLeaderEntry> TeamLeaders = Stats->GetLeaders(EPSStatCategory::TeamTotalYards, EPSStatScope::Season, 2026, 0);
    TestTrue(TEXT("Team yards: the Hawks first"), TeamLeaders.Num() == 2 && TeamLeaders[0].Id == Hawks && TeamLeaders[0].Value == 50);

    // The season ends: its totals are kept, its box scores let go.
    Stats->EndSeason();
    TestEqual(TEXT("2027 now"), Stats->GetSeason(), 2027);
    TestEqual(TEXT("No 2027 games yet"), Stats->GetSeasonGames().Num(), 0);
    TestEqual(TEXT("2026 is in the history"), Stats->GetStatBook().History.Num(), 1);
    TestEqual(TEXT("2026's totals stand"), Stats->GetPlayerSeason(HawksQB, 2026).PassingYards, 50);

    PlayGame(Stats, 1, TEXT("Hawks"), TEXT("Bears"), { MakePass(true, TEXT("HAW_QB"), TEXT("HAW_WR"), 10) });
    TestEqual(TEXT("2027: 10 yards"), Stats->GetPlayerSeason(HawksQB, 2027).PassingYards, 10);
    const FPSPlayerStatLine Career = Stats->GetPlayerCareer(HawksQB);
    TestEqual(TEXT("Career: 60 yards"), Career.PassingYards, 60);
    TestEqual(TEXT("Career: three games"), Career.Games, 3);
    TestEqual(TEXT("Career: 5 completions"), Career.Completions, 5);
    TestEqual(TEXT("The Hawks' franchise history: three games"), Stats->GetFranchiseTotals(Hawks).Games, 3);
    TestEqual(TEXT("...60 passing yards"), Stats->GetFranchiseTotals(Hawks).PassingYards, 60);
    const TArray<FPSLeaderEntry> CareerLeaders = Stats->GetLeaders(EPSStatCategory::PassingYards, EPSStatScope::Career, 0, 0);
    TestTrue(TEXT("Career leaders: the Hawks' passer, then the Wolves'"), CareerLeaders.Num() == 2 && CareerLeaders[0].Id == HawksQB && CareerLeaders[1].Id == WolvesQB);
    FPSBoxScore Week1;
    TestTrue(TEXT("This season's week-1 box score"), Stats->FindGame(1, FName(TEXT("Bears")), Week1) && Week1.Season == 2027);
    TestTrue(TEXT("Nobody's season is an empty line"), Stats->GetPlayerSeason(FName(TEXT("Nobody")), 2026).Games == 0);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The record book
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStatsRecordBookTest,
    "PlaySports.Stats.RecordBookAndBrokenRecords",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStatsRecordBookTest::RunTest(const FString& Parameters)
{
    using namespace PSStatsTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestNotNull(TEXT("Telemetry bus"), Bus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    Stats->StartSeason(2026);
    Stats->BindToBus(Bus);
    TArray<FPSTelemetryRecordBrokenEvent> Broken;
    Stats->OnRecordBroken.AddLambda([&Broken](const FPSTelemetryRecordBrokenEvent& Event)
    {
        if (Event.Category == EPSStatCategory::PassingYards)
        {
            Broken.Add(Event);
        }
    });
    int32 OnBus = 0;
    Bus->OnRecordBrokenMC.AddLambda([&OnBus](const FPSTelemetryRecordBrokenEvent& Event)
    {
        OnBus += Event.Category == EPSStatCategory::PassingYards ? 1 : 0;
    });
    const auto BrokenIn = [&Broken](EPSStatScope Scope)
    {
        return Broken.FilterByPredicate([Scope](const FPSTelemetryRecordBrokenEvent& Event) { return Event.Scope == Scope; });
    };

    // Week 1: the first marks set the records quietly.
    PlayGame(Stats, 1, TEXT("Hawks"), TEXT("Wolves"), { MakePass(true, TEXT("HAW_QB"), TEXT("HAW_WR"), 100) });
    FPSRecordEntry Record;
    TestTrue(TEXT("A single-game passing record"), Stats->FindRecord(EPSStatCategory::PassingYards, EPSStatScope::Game, Record) && Record.HolderId == FName(TEXT("HAW_QB")) && Record.Value == 100);
    TestEqual(TEXT("Nothing was broken"), Broken.Num(), 0);

    // Week 2: 150 passes 100 in a game, a season and a career.
    PlayGame(Stats, 2, TEXT("Wolves"), TEXT("Bears"), { MakePass(true, TEXT("WOL_QB"), TEXT("WOL_WR"), 150) });
    TestEqual(TEXT("Three records fall"), Broken.Num(), 3);
    const TArray<FPSTelemetryRecordBrokenEvent> GameRecords = BrokenIn(EPSStatScope::Game);
    if (TestEqual(TEXT("The single-game record"), GameRecords.Num(), 1))
    {
        TestEqual(TEXT("...to the Wolves' passer"), GameRecords[0].HolderId, FName(TEXT("WOL_QB")));
        TestEqual(TEXT("...150"), GameRecords[0].Value, 150);
        TestEqual(TEXT("...from the Hawks' passer"), GameRecords[0].PreviousHolderId, FName(TEXT("HAW_QB")));
        TestEqual(TEXT("...and his 100"), GameRecords[0].PreviousValue, 100);
        TestTrue(TEXT("...in week 2 of 2026"), GameRecords[0].Season == 2026 && GameRecords[0].Week == 2 && !GameRecords[0].bTeamRecord);
        TestTrue(TEXT("...and says so"), GameRecords[0].Description.Contains(TEXT("WOL_QB")));
    }
    TestEqual(TEXT("The bus heard them too"), OnBus, 3);

    // Week 3: 120 is no single-game record; his season total of 270 only adds to his own mark.
    PlayGame(Stats, 3, TEXT("Wolves"), TEXT("Hawks"), { MakePass(true, TEXT("WOL_QB"), TEXT("WOL_WR"), 120) });
    TestEqual(TEXT("Nothing new falls"), Broken.Num(), 3);
    TestTrue(TEXT("The season record is now 270"), Stats->FindRecord(EPSStatCategory::PassingYards, EPSStatScope::Season, Record) && Record.Value == 270);

    // 2027: 300 in a game is a new single-game record and, in a new season, a new season record;
    // his career total only adds to his own.
    Stats->EndSeason();
    PlayGame(Stats, 1, TEXT("Wolves"), TEXT("Bears"), { MakePass(true, TEXT("WOL_QB"), TEXT("WOL_WR"), 300) });
    TestEqual(TEXT("Two more fall"), Broken.Num(), 5);
    TestEqual(TEXT("A new single-game record"), BrokenIn(EPSStatScope::Game).Num(), 2);
    TestEqual(TEXT("A new season record, the holder's own"), BrokenIn(EPSStatScope::Season).Num(), 2);
    TestEqual(TEXT("The career record moved once (to him)"), BrokenIn(EPSStatScope::Career).Num(), 1);
    TestTrue(TEXT("Career record: 570"), Stats->FindRecord(EPSStatCategory::PassingYards, EPSStatScope::Career, Record) && Record.Value == 570 && Record.HolderId == FName(TEXT("WOL_QB")));
    TestTrue(TEXT("Team records are kept too"), Stats->FindRecord(EPSStatCategory::TeamTotalYards, EPSStatScope::Game, Record) && Record.HolderId == FName(TEXT("Wolves")) && Record.Value == 300);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Derived metrics and splits
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStatsMetricsTest,
    "PlaySports.Stats.DerivedMetricsAndSplits",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStatsMetricsTest::RunTest(const FString& Parameters)
{
    using namespace PSStatsTests;

    FPSPlayerStatLine Passer;
    Passer.PassAttempts = 20;
    Passer.Completions = 15;
    Passer.PassingYards = 250;
    Passer.PassingTouchdowns = 2;
    Passer.InterceptionsThrown = 1;
    const FPSPlayerMetrics Metrics = UPSStatsEngine::ComputePlayerMetrics(Passer);
    TestTrue(TEXT("Passer rating 129.2"), FMath::IsNearlyEqual(Metrics.PasserRating, 129.1667f, 0.01f));
    TestTrue(TEXT("75% completions"), FMath::IsNearlyEqual(Metrics.CompletionPercentage, 75.f));
    TestTrue(TEXT("12.5 yards an attempt"), FMath::IsNearlyEqual(Metrics.YardsPerAttempt, 12.5f));
    TestTrue(TEXT("10% touchdowns, 5% interceptions"), FMath::IsNearlyEqual(Metrics.TouchdownPercentage, 10.f) && FMath::IsNearlyEqual(Metrics.InterceptionPercentage, 5.f));

    FPSPlayerStatLine Perfect;
    Perfect.PassAttempts = 10;
    Perfect.Completions = 10;
    Perfect.PassingYards = 200;
    Perfect.PassingTouchdowns = 3;
    TestTrue(TEXT("A perfect rating, 158.3"), FMath::IsNearlyEqual(UPSStatsEngine::ComputePasserRating(Perfect), 158.333f, 0.01f));
    TestEqual(TEXT("No attempts, no rating"), UPSStatsEngine::ComputePasserRating(FPSPlayerStatLine()), 0.f);

    FPSPlayerStatLine Back;
    Back.RushAttempts = 4;
    Back.RushingYards = 18;
    Back.Targets = 4;
    Back.Receptions = 3;
    Back.ReceivingYards = 30;
    const FPSPlayerMetrics BackMetrics = UPSStatsEngine::ComputePlayerMetrics(Back);
    TestTrue(TEXT("4.5 yards a carry"), FMath::IsNearlyEqual(BackMetrics.YardsPerCarry, 4.5f));
    TestTrue(TEXT("10 yards a catch, 75% caught"), FMath::IsNearlyEqual(BackMetrics.YardsPerReception, 10.f) && FMath::IsNearlyEqual(BackMetrics.CatchRate, 0.75f));

    // A team's splits from its plays: two 3rd downs (one converted), two red-zone snaps (one
    // touchdown), a field goal made and one missed, an interception each way.
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    FPSTelemetryPlayResultEvent ThirdConverted = MakePass(true, TEXT("HAW_QB"), TEXT("HAW_WR"), 12);
    ThirdConverted.Down = 3;
    ThirdConverted.bFirstDown = true;
    FPSTelemetryPlayResultEvent ThirdStopped = MakePass(true, TEXT("HAW_QB"), TEXT("HAW_WR"), 2);
    ThirdStopped.Down = 3;
    FPSTelemetryPlayResultEvent RedZoneScore = MakePass(true, TEXT("HAW_QB"), TEXT("HAW_WR"), 10);
    RedZoneScore.Result = TEXT("Touchdown");
    RedZoneScore.YardLine = 90;
    RedZoneScore.HomePoints = 7;
    FPSTelemetryPlayResultEvent RedZoneStopped = MakePass(true, TEXT("HAW_QB"), TEXT("HAW_WR"), 0);
    RedZoneStopped.YardLine = 85;
    FPSTelemetryPlayResultEvent ThrownAway = MakePlay(true, TEXT("Tackle"), 0, 2);
    ThrownAway.bPass = true;
    ThrownAway.bInterception = true;
    FPSTelemetryPlayResultEvent PickedOff = MakePlay(false, TEXT("Tackle"), 0, 2);
    PickedOff.bPass = true;
    PickedOff.bInterception = true;
    FPSTelemetryPlayResultEvent Good = MakePlay(true, TEXT("FieldGoalGood"), 0, 4, 75);
    Good.HomePoints = 3;
    const FPSTelemetryPlayResultEvent Missed = MakePlay(true, TEXT("FieldGoalMissed"), 0, 4, 60);
    PlayGame(Stats, 1, TEXT("Hawks"), TEXT("Wolves"), { ThirdConverted, ThirdStopped, RedZoneScore, RedZoneStopped, ThrownAway, PickedOff, Good, Missed });

    const FPSTeamMetrics Team = UPSStatsEngine::ComputeTeamMetrics(Stats->GetTeamSeason(FName(TEXT("Hawks")), Stats->GetSeason()));
    TestTrue(TEXT("Half the 3rd downs converted"), FMath::IsNearlyEqual(Team.ThirdDownConversionRate, 0.5f));
    TestTrue(TEXT("Half the red-zone snaps scored"), FMath::IsNearlyEqual(Team.RedZoneTouchdownRate, 0.5f));
    TestTrue(TEXT("Half the field goals made"), FMath::IsNearlyEqual(Team.FieldGoalPercentage, 50.f));
    TestEqual(TEXT("An even turnover margin"), Team.TurnoverMargin, 0);
    TestTrue(TEXT("10 points in one game"), FMath::IsNearlyEqual(Team.PointsPerGame, 10.f));
    TestTrue(TEXT("24 yards on 5 plays"), FMath::IsNearlyEqual(Team.YardsPerPlay, 24.f / 5.f));
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- The franchise's stat book
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStatsFranchiseTest,
    "PlaySports.Stats.FranchiseSeasonAndSave",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStatsFranchiseTest::RunTest(const FString& Parameters)
{
    using namespace PSStatsTests;

    UPSScheduleEngine* Schedule = NewObject<UPSScheduleEngine>();
    UPSFranchiseSeason* Season = NewObject<UPSFranchiseSeason>();
    const TArray<FName> TeamIds = { FName(TEXT("Falcons")), FName(TEXT("Hawks")), FName(TEXT("Wolves")), FName(TEXT("Bears")) };
    Season->InitializeSeason(TeamIds, Schedule->GenerateSeasonSchedule(FDateTime(2026, 9, 1), 2, TArray<int32>()));
    UPSStaffManager* Staffs = NewObject<UPSStaffManager>();
    Staffs->LoadFromJson(UPSStaffManager::GetDefaultDataPath());
    UPSFranchiseFlow* Flow = NewObject<UPSFranchiseFlow>();
    Flow->Initialize(Season, Staffs, FName(TEXT("Falcons")));
    Flow->LoadLeagueRosters(UPSUITeamCatalog::GetDefaultTeamsPath());
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    Flow->SetStats(Stats);
    TestEqual(TEXT("Without contracts the book starts at season 1"), Stats->GetSeason(), 1);

    // Two weeks, every game simulated: each box score agrees with the season's result.
    for (int32 Week = 1; Week <= 2; ++Week)
    {
        TestEqual(*FString::Printf(TEXT("Week %d: two games"), Week), Flow->SimulateWeek(true), 2);
        for (const FPSWeekMatchup& Matchup : Season->GetMatchupsForWeek(Week))
        {
            FPSBoxScore Game;
            if (TestTrue(*FString::Printf(TEXT("Week %d: %s's box score"), Week, *Matchup.HomeTeamId.ToString()), Stats->FindGame(Week, Matchup.HomeTeamId, Game)))
            {
                TestTrue(*FString::Printf(TEXT("Week %d: the box score is the result"), Week),
                    Game.Home.TeamId == Matchup.HomeTeamId && Game.Home.Points == Matchup.HomeScore && Game.Away.Points == Matchup.AwayScore);
            }
        }
        Flow->AdvanceWeek();
    }
    TestTrue(TEXT("The season ended"), Flow->HasSeasonEnded());
    TestEqual(TEXT("...and the book moved on to season 2"), Stats->GetSeason(), 2);
    TestEqual(TEXT("Season 1 is in the history"), Stats->GetStatBook().History.Num(), 1);
    for (const FPSTeamStanding& Standing : Season->GetStandings())
    {
        TestEqual(*FString::Printf(TEXT("%s: the season's points are the standings'"), *Standing.TeamId.ToString()),
            Stats->GetTeamSeason(Standing.TeamId, 1).Points, Standing.PointsFor);
    }
    TestTrue(TEXT("A passing leader for the season"), Stats->GetLeaders(EPSStatCategory::PassingYards, EPSStatScope::Season, 1, 1).Num() == 1);
    TestTrue(TEXT("Records were set"), Stats->GetRecords().Num() > 0);

    // The stat book round-trips through the franchise save.
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    Stats->SaveTo(Save);
    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    const FString Slot = TEXT("Test_StatBook");
    TestTrue(TEXT("The franchise saves"), Saves->SaveToSlot(Save, Slot));
    const UPSFranchiseSaveGame* Loaded = Cast<UPSFranchiseSaveGame>(Saves->LoadFromSlot(Slot));
    UPSStatsEngine* Restored = NewObject<UPSStatsEngine>();
    if (TestNotNull(TEXT("...and loads"), Loaded) && TestTrue(TEXT("The stat book loads"), Restored->LoadFrom(Loaded)))
    {
        TestEqual(TEXT("The season"), Restored->GetSeason(), 2);
        TestEqual(TEXT("The history"), Restored->GetStatBook().History.Num(), 1);
        TestEqual(TEXT("The records"), Restored->GetRecords().Num(), Stats->GetRecords().Num());
        for (const FName& TeamId : TeamIds)
        {
            TestEqual(*FString::Printf(TEXT("%s's season-1 yards"), *TeamId.ToString()), Restored->GetTeamSeason(TeamId, 1).TotalYards, Stats->GetTeamSeason(TeamId, 1).TotalYards);
        }
    }
    IFileManager::Get().Delete(*UPSSaveSubsystem::GetSlotPath(Slot), false, true, true);
    IFileManager::Get().Delete(*(UPSSaveSubsystem::GetSlotPath(Slot) + TEXT(".bak")), false, true, true);
    TestFalse(TEXT("A save from before statistics keeps the current book"), Restored->LoadFrom(NewObject<UPSFranchiseSaveGame>()));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
