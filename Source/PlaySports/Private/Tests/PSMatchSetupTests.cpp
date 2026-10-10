// PSMatchSetupTests.cpp -- the match's teams (UPSMatchSetup), the staffs at kickoff and the
// franchise flow's season end (UPSFranchiseFlow)
//
// Tests covered:
//   1. Team select's pick travels into the match: the menu's travel options name the player's
//      team, the match puts it at home against the next team in league order; explicit home and
//      away options, a missing or unknown team, and the options' round trip.
//   2. A season game comes from the franchise schedule: each week's matchup with the player's
//      team, Franchise mode and the week, through the travel options and back; a week past the
//      season has no game.
//   3. At kickoff both teams' staffs take over: each team's plan goes to the play-call authority
//      and each team's players play at their own scheme fit, the lineman misfit on the Power Run
//      team and fit on the Air Raid one; a team with no staff plays the whole playbook unfitted.
//   4. The franchise flow plays a season week by week through the quick sim and, once the last
//      week is played, ends it: the coaching carousel runs once on the final standings.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSFranchiseFlow.h"
#include "PSFranchiseSeason.h"
#include "PSMatchSetup.h"
#include "PSMenuComponent.h"
#include "PSPlayCallSubsystem.h"
#include "PSRoster.h"
#include "PSScheduleEngine.h"
#include "PSStaffManager.h"
#include "PSUITeamCatalog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSMatchSetupTests
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

    /** The shipped league's teams, in file order: Falcons, Hawks, Wolves, Bears. */
    static TArray<FName> LeagueTeams()
    {
        return UPSMatchSetup::LoadLeagueTeamIds(UPSUITeamCatalog::GetDefaultTeamsPath());
    }

    static UPSStaffManager* LoadStaffs()
    {
        UPSStaffManager* Staffs = NewObject<UPSStaffManager>();
        Staffs->LoadFromJson(UPSStaffManager::GetDefaultDataPath());
        return Staffs;
    }

    /** A season of NumWeeks (no byes) for the shipped league. */
    static UPSFranchiseSeason* MakeSeason(int32 NumWeeks)
    {
        UPSScheduleEngine* Schedule = NewObject<UPSScheduleEngine>();
        UPSFranchiseSeason* Season = NewObject<UPSFranchiseSeason>();
        Season->InitializeSeason(LeagueTeams(), Schedule->GenerateSeasonSchedule(FDateTime(2026, 9, 1), NumWeeks, TArray<int32>()));
        return Season;
    }

    static FPlayerAttributes MakeMatchPlayer(const TCHAR* PlayerId, EPlayerRole Role, float Speed, float Agility, float Strength, float Acceleration, float Awareness)
    {
        FPlayerAttributes Player;
        Player.PlayerId = FName(PlayerId);
        Player.DisplayName = PlayerId;
        Player.Role = Role;
        Player.WeightKg = 140.f;
        Player.HeightCm = 193.f;
        Player.Speed = Speed;
        Player.Agility = Agility;
        Player.Strength = Strength;
        Player.Acceleration = Acceleration;
        Player.Awareness = Awareness;
        Player.Stamina = 85.f;
        return Player;
    }

    /** An agile zone-blocking lineman, a quarterback and a linebacker. */
    static TArray<FPlayerAttributes> MakeSide()
    {
        return {
            MakeMatchPlayer(TEXT("ZoneOL"), EPlayerRole::OffensiveLineman, 80.f, 90.f, 60.f, 78.f, 75.f),
            MakeMatchPlayer(TEXT("PasserQB"), EPlayerRole::Quarterback, 75.f, 78.f, 72.f, 76.f, 88.f),
            MakeMatchPlayer(TEXT("MikeLB"), EPlayerRole::Linebacker, 84.f, 80.f, 82.f, 85.f, 79.f),
        };
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Team select's pick travels into the match
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSMatchSetupTeamSelectTest,
    "PlaySports.Match.TeamSelectPickTravelsIntoTheMatch",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSMatchSetupTeamSelectTest::RunTest(const FString& Parameters)
{
    using namespace PSMatchSetupTests;

    const TArray<FName> League = LeagueTeams();
    if (!TestEqual(TEXT("The shipped league has four teams"), League.Num(), 4))
    {
        return false;
    }
    const FName Falcons(TEXT("Falcons"));
    const FName Hawks(TEXT("Hawks"));
    const FName Wolves(TEXT("Wolves"));
    const FName Bears(TEXT("Bears"));

    // The menu's Play Now options for the Hawks, as the engine hands them to the game mode.
    UPSMenuComponent* Menu = NewObject<UPSMenuComponent>();
    const FString Travel = Menu->BuildTravelOptions(EPSMenuCommand::StartPlayNow, Hawks);
    TestEqual(TEXT("Team select travels with the pick"), Travel, FString(TEXT("mode=PlayNow?team=Hawks")));

    UPSMatchSetup* Match = NewObject<UPSMatchSetup>();
    TestTrue(TEXT("The match reads the options"), Match->InitializeFromOptions(TEXT("?") + Travel, League));
    TestTrue(TEXT("Play Now"), Match->GetMode() == EPSMatchMode::PlayNow);
    TestEqual(TEXT("The player's team is home"), Match->GetHomeTeamId(), Hawks);
    TestEqual(TEXT("...against the next team in league order"), Match->GetAwayTeamId(), Wolves);
    TestEqual(TEXT("The player controls the Hawks"), Match->GetUserTeamId(), Hawks);
    TestEqual(TEXT("No season week"), Match->GetSeasonWeek(), 0);
    TestEqual(TEXT("GetTeamId(home)"), Match->GetTeamId(true), Hawks);

    // The league's last team plays its first.
    TestTrue(TEXT("The Bears pick"), Match->InitializeFromOptions(Menu->BuildTravelOptions(EPSMenuCommand::StartPlayNow, Bears), League));
    TestTrue(TEXT("The Bears host the Falcons"), Match->GetHomeTeamId() == Bears && Match->GetAwayTeamId() == Falcons);

    // No pick (a level opened in the editor): the league's first two teams, CPU against CPU.
    TestTrue(TEXT("No options"), Match->InitializeFromOptions(FString(), League));
    TestTrue(TEXT("The Falcons host the Hawks"), Match->GetHomeTeamId() == Falcons && Match->GetAwayTeamId() == Hawks);
    TestEqual(TEXT("Nobody's team"), Match->GetUserTeamId(), FName());

    // A team the league doesn't have is ignored.
    TestTrue(TEXT("An unknown pick"), Match->InitializeFromOptions(TEXT("mode=PlayNow?team=Sharks"), League));
    TestTrue(TEXT("...plays the league's first two"), Match->GetHomeTeamId() == Falcons && Match->GetAwayTeamId() == Hawks && Match->GetUserTeamId().IsNone());

    // Explicit home and away; the player's team may be the visitor.
    TestTrue(TEXT("Explicit teams"), Match->InitializeFromOptions(TEXT("mode=Franchise?home=Wolves?away=Falcons?team=Falcons?week=3"), League));
    TestTrue(TEXT("The Wolves host the Falcons"), Match->GetHomeTeamId() == Wolves && Match->GetAwayTeamId() == Falcons);
    TestEqual(TEXT("The player has the visiting Falcons"), Match->GetUserTeamId(), Falcons);
    TestTrue(TEXT("Franchise mode"), Match->GetMode() == EPSMatchMode::Franchise);
    TestEqual(TEXT("Week 3"), Match->GetSeasonWeek(), 3);

    // The options round-trip.
    UPSMatchSetup* Copy = NewObject<UPSMatchSetup>();
    TestTrue(TEXT("The setup's own options read back"), Copy->InitializeFromOptions(Match->ToOptions(), League));
    TestTrue(TEXT("...to the same match"), Copy->GetHomeTeamId() == Wolves && Copy->GetAwayTeamId() == Falcons && Copy->GetUserTeamId() == Falcons
        && Copy->GetSeasonWeek() == 3 && Copy->GetMode() == EPSMatchMode::Franchise);

    // A team can't play itself; a league of one has no match.
    TestTrue(TEXT("The same team twice"), Match->InitializeFromOptions(TEXT("home=Hawks?away=Hawks"), League));
    TestTrue(TEXT("...plays the next team instead"), Match->GetHomeTeamId() == Hawks && Match->GetAwayTeamId() == Wolves);
    TestFalse(TEXT("SetTeams refuses a team against itself"), Match->SetTeams(Hawks, Hawks));
    TestFalse(TEXT("...and a missing team"), Match->SetTeams(Hawks, NAME_None));
    TestTrue(TEXT("The match is unchanged"), Match->GetHomeTeamId() == Hawks && Match->GetAwayTeamId() == Wolves);
    TestFalse(TEXT("A league of one team"), NewObject<UPSMatchSetup>()->InitializeFromOptions(TEXT("team=Hawks"), { Hawks }));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- A season game comes from the franchise schedule
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSMatchSetupSeasonGameTest,
    "PlaySports.Match.SeasonGameFromTheSchedule",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSMatchSetupSeasonGameTest::RunTest(const FString& Parameters)
{
    using namespace PSMatchSetupTests;

    const FName Hawks(TEXT("Hawks"));
    const TArray<FName> League = LeagueTeams();
    UPSFranchiseSeason* Season = MakeSeason(6);
    UPSMatchSetup* Match = NewObject<UPSMatchSetup>();

    for (int32 Week = 1; Week <= 6; ++Week)
    {
        const TArray<FPSWeekMatchup> WeekMatchups = Season->GetMatchupsForWeek(Week);
        const FPSWeekMatchup* Scheduled = WeekMatchups.FindByPredicate([Hawks](const FPSWeekMatchup& Matchup)
        {
            return Matchup.HomeTeamId == Hawks || Matchup.AwayTeamId == Hawks;
        });
        if (!TestTrue(*FString::Printf(TEXT("Week %d: the Hawks play"), Week), Scheduled != nullptr))
        {
            continue;
        }
        const FName ScheduledHome = Scheduled->HomeTeamId;
        const FName ScheduledAway = Scheduled->AwayTeamId;

        TestTrue(*FString::Printf(TEXT("Week %d: the match comes from the schedule"), Week), Match->InitializeForSeasonGame(Season, Week, Hawks));
        TestTrue(*FString::Printf(TEXT("Week %d: the schedule's home and away"), Week),
            Match->GetHomeTeamId() == ScheduledHome && Match->GetAwayTeamId() == ScheduledAway);
        TestTrue(*FString::Printf(TEXT("Week %d: a franchise game"), Week), Match->GetMode() == EPSMatchMode::Franchise && Match->GetSeasonWeek() == Week);

        // The game's level reads the same match from the travel options.
        UPSMatchSetup* Travelled = NewObject<UPSMatchSetup>();
        TestTrue(*FString::Printf(TEXT("Week %d: travels"), Week), Travelled->InitializeFromOptions(TEXT("?") + Match->ToOptions(), League));
        TestTrue(*FString::Printf(TEXT("Week %d: ...as the same game"), Week), Travelled->GetHomeTeamId() == ScheduledHome && Travelled->GetAwayTeamId() == ScheduledAway
            && Travelled->GetUserTeamId() == Hawks && Travelled->GetSeasonWeek() == Week && Travelled->GetMode() == EPSMatchMode::Franchise);
    }

    // The franchise flow builds the player's game for the season's current week.
    UPSFranchiseFlow* Flow = NewObject<UPSFranchiseFlow>();
    Flow->Initialize(Season, LoadStaffs(), Hawks);
    UPSMatchSetup* FromFlow = NewObject<UPSMatchSetup>();
    TestTrue(TEXT("The flow builds this week's game"), Flow->BuildUserMatch(FromFlow));
    TestEqual(TEXT("...for week 1"), FromFlow->GetSeasonWeek(), Season->GetCurrentWeek());
    TestTrue(TEXT("...with the Hawks in it"), FromFlow->GetHomeTeamId() == Hawks || FromFlow->GetAwayTeamId() == Hawks);

    // Past the season there is no game, and the match is left as it was.
    const FName Before = Match->GetHomeTeamId();
    TestFalse(TEXT("No week 7"), Match->InitializeForSeasonGame(Season, 7, Hawks));
    TestEqual(TEXT("The match is unchanged"), Match->GetHomeTeamId(), Before);
    TestFalse(TEXT("A team not in the league plays no week"), Match->InitializeForSeasonGame(Season, 1, FName(TEXT("Sharks"))));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- At kickoff both teams' staffs take over
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSMatchSetupStaffsTest,
    "PlaySports.Match.StaffsTakeOverAtKickoff",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSMatchSetupStaffsTest::RunTest(const FString& Parameters)
{
    using namespace PSMatchSetupTests;

    UWorld* World = CreateTestWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Play-call subsystem"), PlayCall))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    // The Hawks (Air Raid) host the Wolves (Power Run).
    const FName Hawks(TEXT("Hawks"));
    const FName Wolves(TEXT("Wolves"));
    UPSStaffManager* Staffs = LoadStaffs();
    UPSMatchSetup* Match = NewObject<UPSMatchSetup>();
    TestTrue(TEXT("Hawks vs Wolves"), Match->SetTeams(Hawks, Wolves));

    const TArray<FPlayerAttributes> Roster = MakeSide();
    TArray<FPlayerAttributes> HomePlayers = Roster;
    TArray<FPlayerAttributes> AwayPlayers = Roster;
    TestTrue(TEXT("Both teams have a staff"), Match->ApplyStaffs(Staffs, PlayCall, HomePlayers, AwayPlayers));

    // The play-call authority has each team's plan.
    TestEqual(TEXT("The home plan is the Hawks'"), PlayCall->GetTeamPlan(true).TeamId, Hawks);
    TestEqual(TEXT("The away plan is the Wolves'"), PlayCall->GetTeamPlan(false).TeamId, Wolves);
    TestTrue(TEXT("Each keeps its own playbook"), PlayCall->GetTeamPlan(true).PlayIds.Num() > 0 && PlayCall->GetTeamPlan(false).PlayIds.Num() > 0
        && PlayCall->GetTeamPlan(true).PlayIds != PlayCall->GetTeamPlan(false).PlayIds);

    // Each player plays at his own team's scheme fit.
    for (int32 Index = 0; Index < Roster.Num(); ++Index)
    {
        const FPlayerAttributes& Own = Roster[Index];
        const float HomeMultiplier = Staffs->GetSchemeFit(Hawks, Own).Multiplier;
        const float AwayMultiplier = Staffs->GetSchemeFit(Wolves, Own).Multiplier;
        TestTrue(*FString::Printf(TEXT("%s plays at the Hawks' fit"), *Own.PlayerId.ToString()),
            FMath::IsNearlyEqual(HomePlayers[Index].Agility, Own.Agility * HomeMultiplier) && FMath::IsNearlyEqual(HomePlayers[Index].Strength, Own.Strength * HomeMultiplier));
        TestTrue(*FString::Printf(TEXT("%s plays at the Wolves' fit"), *Own.PlayerId.ToString()),
            FMath::IsNearlyEqual(AwayPlayers[Index].Agility, Own.Agility * AwayMultiplier) && FMath::IsNearlyEqual(AwayPlayers[Index].Speed, Own.Speed * AwayMultiplier));
        TestEqual(*FString::Printf(TEXT("%s keeps his weight"), *Own.PlayerId.ToString()), HomePlayers[Index].WeightKg, Own.WeightKg);
    }
    const float ZoneInAirRaid = Staffs->GetSchemeFit(Hawks, Roster[0]).Multiplier;
    const float ZoneInPowerRun = Staffs->GetSchemeFit(Wolves, Roster[0]).Multiplier;
    TestTrue(TEXT("The agile lineman misfits the Power Run"), ZoneInPowerRun < 1.f);
    TestTrue(TEXT("...and plays better for the Air Raid Hawks"), ZoneInAirRaid > ZoneInPowerRun);
    TestTrue(TEXT("So the same lineman is weaker for the Wolves"), AwayPlayers[0].Agility < HomePlayers[0].Agility);

    // A team with no staff: the whole playbook, its players at their ratings.
    TestTrue(TEXT("Hawks vs a team with no staff"), Match->SetTeams(Hawks, FName(TEXT("Sharks"))));
    TArray<FPlayerAttributes> HawksPlayers = Roster;
    TArray<FPlayerAttributes> SharksPlayers = Roster;
    TestFalse(TEXT("Not both teams have a staff"), Match->ApplyStaffs(Staffs, PlayCall, HawksPlayers, SharksPlayers));
    TestEqual(TEXT("The Sharks call from the whole playbook"), PlayCall->GetTeamPlan(false).PlayIds.Num(), 0);
    TestEqual(TEXT("...with their own ratings"), SharksPlayers[0].Agility, Roster[0].Agility);
    TestFalse(TEXT("No staffs at all"), Match->ApplyStaffs(nullptr, PlayCall, HawksPlayers, SharksPlayers));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The franchise flow ends the season with the coaching carousel
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFranchiseFlowSeasonEndTest,
    "PlaySports.Franchise.FlowEndsSeasonWithCarousel",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFranchiseFlowSeasonEndTest::RunTest(const FString& Parameters)
{
    using namespace PSMatchSetupTests;

    constexpr int32 NumWeeks = 3;
    const FName Falcons(TEXT("Falcons"));
    UPSFranchiseSeason* Season = MakeSeason(NumWeeks);
    UPSStaffManager* Staffs = LoadStaffs();
    UPSFranchiseFlow* Flow = NewObject<UPSFranchiseFlow>();
    Flow->Initialize(Season, Staffs, Falcons);
    TestEqual(TEXT("Every team gets its shipped roster"), Flow->LoadLeagueRosters(UPSUITeamCatalog::GetDefaultTeamsPath()), 4);
    TestNotNull(TEXT("The Falcons' roster"), Flow->GetTeamRoster(Falcons));
    TestEqual(TEXT("The final week"), Flow->GetFinalWeek(), NumWeeks);
    TestFalse(TEXT("The season can't end before it is played"), Flow->EndSeason());

    int32 SeasonEndedOnWeek = 0;
    for (int32 Week = 1; Week <= NumWeeks; ++Week)
    {
        UPSMatchSetup* UserMatch = NewObject<UPSMatchSetup>();
        TestTrue(*FString::Printf(TEXT("Week %d: the player has a game"), Week), Flow->BuildUserMatch(UserMatch));
        TestEqual(*FString::Printf(TEXT("Week %d: the other game is simulated"), Week), Flow->SimulateWeek(false), 1);
        TestEqual(*FString::Printf(TEXT("Week %d: then the player's"), Week), Flow->SimulateWeek(true), 1);
        TestEqual(*FString::Printf(TEXT("Week %d: nothing is left to play"), Week), Flow->SimulateWeek(true), 0);
        TestFalse(*FString::Printf(TEXT("Week %d: no carousel yet"), Week), Flow->HasSeasonEnded());
        if (Flow->AdvanceWeek())
        {
            SeasonEndedOnWeek = Week;
        }
    }

    TestEqual(TEXT("Advancing past the last week ends the season"), SeasonEndedOnWeek, NumWeeks);
    TestTrue(TEXT("Every game has a result"), Flow->IsRegularSeasonComplete());
    int32 Decisions = 0;
    for (const FPSTeamStanding& Standing : Season->GetStandings())
    {
        Decisions += Standing.Wins + Standing.Losses + Standing.Ties;
    }
    TestEqual(TEXT("Two teams' records per game"), Decisions, 2 * 2 * NumWeeks);

    // The carousel ran on the final standings: the same moves a fresh league makes on them.
    TestTrue(TEXT("The season has ended"), Flow->HasSeasonEnded());
    const TArray<FPSCarouselEvent> Expected = LoadStaffs()->RunCarousel(Season->GetStandings());
    const TArray<FPSCarouselEvent>& Ran = Flow->GetCarouselEvents();
    bool bSameMoves = Ran.Num() == Expected.Num();
    for (int32 Index = 0; bSameMoves && Index < Ran.Num(); ++Index)
    {
        bSameMoves = Ran[Index].Description == Expected[Index].Description;
        AddInfo(Ran[Index].Description);
    }
    TestTrue(TEXT("The carousel's moves are the final standings'"), bSameMoves);

    // It runs once a season.
    TestFalse(TEXT("The season ends once"), Flow->EndSeason());
    TestFalse(TEXT("...and doesn't advance again"), Flow->AdvanceWeek());

    // A season with games unplayed doesn't end when the weeks run out.
    UPSFranchiseFlow* Unplayed = NewObject<UPSFranchiseFlow>();
    Unplayed->Initialize(MakeSeason(NumWeeks), LoadStaffs(), Falcons);
    bool bEnded = false;
    for (int32 Week = 1; Week <= NumWeeks; ++Week)
    {
        bEnded = Unplayed->AdvanceWeek() || bEnded;
    }
    TestFalse(TEXT("Unplayed games keep the season open"), bEnded || Unplayed->HasSeasonEnded());
    TestEqual(TEXT("...with no carousel"), Unplayed->GetCarouselEvents().Num(), 0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
