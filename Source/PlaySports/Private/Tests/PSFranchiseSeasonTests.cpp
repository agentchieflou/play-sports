#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSFranchiseSeason.h"
#include "PSQuickSimRunner.h"
#include "PSPlayoffBracket.h"
#include "PSScheduleEngine.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Season schedule generates matchups and standings track results
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFranchiseSeasonScheduleTest,
    "PlaySports.Franchise.SeasonScheduleAndStandings",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFranchiseSeasonScheduleTest::RunTest(const FString& Parameters)
{
    UPSScheduleEngine* ScheduleEngine = NewObject<UPSScheduleEngine>();
    const TArray<FSeasonWeek> Weeks = ScheduleEngine->GenerateSeasonSchedule(FDateTime(2026, 9, 1), 6, TArray<int32>());

    TArray<FName> TeamIds = { FName("Falcons"), FName("Hawks"), FName("Wolves"), FName("Bears") };

    UPSFranchiseSeason* Season = NewObject<UPSFranchiseSeason>();
    Season->InitializeSeason(TeamIds, Weeks);

    TestEqual(TEXT("Standings seeded for every team"), Season->GetStandings().Num(), TeamIds.Num());

    const TArray<FPSWeekMatchup> Week1Matchups = Season->GetMatchupsForWeek(1);
    TestTrue(TEXT("Week 1 has at least one matchup"), Week1Matchups.Num() > 0);

    if (Week1Matchups.Num() > 0)
    {
        const FPSWeekMatchup& Matchup = Week1Matchups[0];
        const bool bRecorded = Season->RecordGameResult(Matchup.WeekNumber, Matchup.HomeTeamId, Matchup.AwayTeamId, 24, 17);
        TestTrue(TEXT("RecordGameResult succeeds for a valid unplayed matchup"), bRecorded);

        const bool bDoubleRecorded = Season->RecordGameResult(Matchup.WeekNumber, Matchup.HomeTeamId, Matchup.AwayTeamId, 10, 10);
        TestFalse(TEXT("RecordGameResult rejects an already-played matchup"), bDoubleRecorded);

        const TArray<FPSTeamStanding> Standings = Season->GetStandings();
        const FPSTeamStanding* HomeStanding = Standings.FindByPredicate([&](const FPSTeamStanding& S) { return S.TeamId == Matchup.HomeTeamId; });
        if (TestNotNull(TEXT("Home team standing found"), HomeStanding))
        {
            TestEqual(TEXT("Home team credited with a win"), HomeStanding->Wins, 1);
            TestEqual(TEXT("Home team points-for updated"), HomeStanding->PointsFor, 24);
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Quick-sim resolves a full game with a final score
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FQuickSimRunnerTest,
    "PlaySports.Franchise.QuickSimResolvesGame",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FQuickSimRunnerTest::RunTest(const FString& Parameters)
{
    UPSQuickSimRunner* Runner = NewObject<UPSQuickSimRunner>();

    TArray<FPlayerAttributes> HomeRoster;
    FPlayerAttributes QB;
    QB.Role = EPlayerRole::Quarterback;
    QB.Awareness = 85.f;
    HomeRoster.Add(QB);

    TArray<FPlayerAttributes> AwayRoster;
    FPlayerAttributes DB;
    DB.Role = EPlayerRole::DefensiveBack;
    DB.Awareness = 70.f;
    AwayRoster.Add(DB);

    const FPSQuickSimResult Result = Runner->SimulateGame(HomeRoster, AwayRoster);

    TestTrue(TEXT("Quick-sim produces a non-negative home score"), Result.HomeScore >= 0);
    TestTrue(TEXT("Quick-sim produces a non-negative away score"), Result.AwayScore >= 0);

    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Playoff bracket seeds by win percentage
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPlayoffBracketSeedingTest,
    "PlaySports.Franchise.PlayoffBracketSeeding",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlayoffBracketSeedingTest::RunTest(const FString& Parameters)
{
    UPSPlayoffBracket* Bracket = NewObject<UPSPlayoffBracket>();

    TArray<FPSTeamStanding> Standings;

    FPSTeamStanding First;
    First.TeamId = FName("BestTeam");
    First.Wins = 14;
    First.Losses = 3;
    Standings.Add(First);

    FPSTeamStanding Second;
    Second.TeamId = FName("SecondTeam");
    Second.Wins = 11;
    Second.Losses = 6;
    Standings.Add(Second);

    FPSTeamStanding Third;
    Third.TeamId = FName("ThirdTeam");
    Third.Wins = 9;
    Third.Losses = 8;
    Standings.Add(Third);

    FPSTeamStanding Fourth;
    Fourth.TeamId = FName("FourthTeam");
    Fourth.Wins = 8;
    Fourth.Losses = 9;
    Standings.Add(Fourth);

    const TArray<FPSPlayoffMatchup> Bracket4Team = Bracket->GenerateBracket(Standings, 4);

    TestEqual(TEXT("4-team bracket produces 2 round-1 matchups"), Bracket4Team.Num(), 2);
    if (Bracket4Team.Num() == 2)
    {
        TestEqual(TEXT("1-seed faces 4-seed"), Bracket4Team[0].HigherSeedTeamId, FName("BestTeam"));
        TestEqual(TEXT("1-seed's opponent is the 4-seed"), Bracket4Team[0].LowerSeedTeamId, FName("FourthTeam"));
        TestEqual(TEXT("2-seed faces 3-seed"), Bracket4Team[1].HigherSeedTeamId, FName("SecondTeam"));
        TestEqual(TEXT("2-seed's opponent is the 3-seed"), Bracket4Team[1].LowerSeedTeamId, FName("ThirdTeam"));
    }

    return true;
}

#endif
