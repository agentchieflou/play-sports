// PSStaffTests.cpp -- Epic 89 (coaching staffs and scheme identity)
//
// Tests covered:
//   1. Data/coaching_staffs.json loads through UPSDataIngestion, validates clean, gives every team a
//      full staff, and its tuning equals the defaults, field by field.
//   2. Scheme-playbook binding: a team's playbook is its coordinators' formations plus every
//      special-teams and clock play; its tendency is the head coach's aggression and the scheme's
//      lean, scaled by the coordinator's play calling; the coaching AI then calls the scheme's way.
//   3. Player-scheme fit: an agile zone lineman misfits in a power scheme and fits a zone one (and a
//      strong power lineman the other way round); a developing coordinator softens a misfit; other
//      positions and the other side are neutral.
//   4. The carousel: a losing head coach fired after his grace seasons, the worst offense's
//      coordinator fired, a winning team's coordinator hired away as head coach, the scheme churn
//      that follows, the vacancies filled, deterministically.
//   5. The play-call authority calls with each team's plan: its playbook, its tendencies, the
//      scheme on the call screen, and which team has the ball.
//   6. The staffs round-trip through the franchise save.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSCoachingAI.h"
#include "PSDataIngestion.h"
#include "PSFranchiseSaveGame.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlaySimulation.h"
#include "PSPlaybookData.h"
#include "PSPlaybookIngestion.h"
#include "PSStaffData.h"
#include "PSStaffManager.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSStaffTests
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

    static UPSStaffManager* LoadManager()
    {
        UPSStaffManager* Manager = NewObject<UPSStaffManager>();
        Manager->LoadFromJson(UPSStaffManager::GetDefaultDataPath());
        return Manager;
    }

    static TArray<FPSPlayDefinition> LoadPlaybook()
    {
        UDataTable* Table = NewObject<UDataTable>();
        Table->RowStruct = FPSPlayDefinition::StaticStruct();
        UPSPlaybookIngestion* Ingestion = NewObject<UPSPlaybookIngestion>();
        Ingestion->LoadPlaysFromJson(UPSPlayCallSubsystem::GetDefaultPlaybookPath(), Table);
        TArray<FPSPlayDefinition> Plays;
        for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
        {
            Plays.Add(*reinterpret_cast<const FPSPlayDefinition*>(Row.Value));
        }
        return Plays;
    }

    static bool HasPlay(const TArray<FName>& PlayIds, const TCHAR* PlayId)
    {
        return PlayIds.Contains(FName(PlayId));
    }

    static bool HasPlay(const TArray<FPSPlayDefinition>& Plays, const TCHAR* PlayId)
    {
        const FName Id(PlayId);
        return Plays.ContainsByPredicate([Id](const FPSPlayDefinition& Play) { return Play.PlayId == Id; });
    }

    static FPlayerAttributes MakePlayer(EPlayerRole Role, float Speed, float Agility, float Strength, float Acceleration, float Awareness)
    {
        FPlayerAttributes Player;
        Player.PlayerId = FName(TEXT("TestPlayer"));
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

    static FPSCoachDef MakeCoach(const TCHAR* CoachId, EPSCoachRole Role, const TCHAR* SchemeId, float PlayCalling, float Development)
    {
        FPSCoachDef Coach;
        Coach.CoachId = FName(CoachId);
        Coach.DisplayName = CoachId;
        Coach.Role = Role;
        Coach.SchemeId = FName(SchemeId);
        Coach.PlayCalling = PlayCalling;
        Coach.Development = Development;
        return Coach;
    }

    static FPSTeamStaffDef MakeStaff(const TCHAR* TeamId, const TCHAR* HeadCoach, const TCHAR* Offense, const TCHAR* Defense, int32 Seasons)
    {
        FPSTeamStaffDef Staff;
        Staff.TeamId = FName(TeamId);
        Staff.HeadCoachId = FName(HeadCoach);
        Staff.OffensiveCoordinatorId = FName(Offense);
        Staff.DefensiveCoordinatorId = FName(Defense);
        Staff.HeadCoachSeasons = Seasons;
        return Staff;
    }

    static FPSTeamStanding MakeStanding(const TCHAR* TeamId, int32 Wins, int32 Losses, int32 PointsFor, int32 PointsAgainst)
    {
        FPSTeamStanding Standing;
        Standing.TeamId = FName(TeamId);
        Standing.Wins = Wins;
        Standing.Losses = Losses;
        Standing.PointsFor = PointsFor;
        Standing.PointsAgainst = PointsAgainst;
        return Standing;
    }

    static FPSSituationContext MakeContext(int32 Down, int32 Distance, int32 YardLine, bool bHomeHasPossession)
    {
        FPSSituationContext Context;
        Context.Down = Down;
        Context.Distance = Distance;
        Context.YardLine = YardLine;
        Context.bHomeHasPossession = bHomeHasPossession;
        return Context;
    }

    /** The data file's schemes with coaches and staffs of the test's own: four teams, each head
     *  coach running his coordinator's scheme, and free coaches to hire. */
    static FPSCoachingLeague MakeCarouselLeague(const FPSCoachingLeague& FromData)
    {
        FPSCoachingLeague League;
        League.Schemes = FromData.Schemes;
        League.Tuning = FPSStaffTuning();
        League.Coaches = {
            MakeCoach(TEXT("Coach_Grant"), EPSCoachRole::HeadCoach, TEXT("WestCoast"), 72.f, 70.f),
            MakeCoach(TEXT("Coach_Hale"), EPSCoachRole::OffensiveCoordinator, TEXT("WestCoast"), 74.f, 62.f),
            MakeCoach(TEXT("Coach_Ortiz"), EPSCoachRole::DefensiveCoordinator, TEXT("Cover2Zone"), 68.f, 66.f),
            MakeCoach(TEXT("Coach_Mercer"), EPSCoachRole::HeadCoach, TEXT("AirRaid"), 70.f, 64.f),
            MakeCoach(TEXT("Coach_Pike"), EPSCoachRole::OffensiveCoordinator, TEXT("AirRaid"), 78.f, 58.f),
            MakeCoach(TEXT("Coach_Vance"), EPSCoachRole::DefensiveCoordinator, TEXT("NickelMan"), 66.f, 70.f),
            MakeCoach(TEXT("Coach_Stone"), EPSCoachRole::HeadCoach, TEXT("PowerRun"), 64.f, 74.f),
            MakeCoach(TEXT("Coach_Bishop"), EPSCoachRole::OffensiveCoordinator, TEXT("PowerRun"), 70.f, 72.f),
            MakeCoach(TEXT("Coach_Kerr"), EPSCoachRole::DefensiveCoordinator, TEXT("Pressure34"), 72.f, 60.f),
            MakeCoach(TEXT("Coach_Rowe"), EPSCoachRole::HeadCoach, TEXT("Pressure34"), 66.f, 68.f),
            MakeCoach(TEXT("Coach_Lowell"), EPSCoachRole::OffensiveCoordinator, TEXT("ZoneRun"), 68.f, 70.f),
            MakeCoach(TEXT("Coach_Abbott"), EPSCoachRole::DefensiveCoordinator, TEXT("Pressure34"), 74.f, 64.f),
            // Free coaches.
            MakeCoach(TEXT("Coach_Dunn"), EPSCoachRole::HeadCoach, TEXT("ZoneRun"), 68.f, 72.f),
            MakeCoach(TEXT("Coach_Quinn"), EPSCoachRole::OffensiveCoordinator, TEXT("ZoneRun"), 66.f, 68.f),
            MakeCoach(TEXT("Coach_Reyes"), EPSCoachRole::OffensiveCoordinator, TEXT("WestCoast"), 64.f, 70.f),
            MakeCoach(TEXT("Coach_Sato"), EPSCoachRole::OffensiveCoordinator, TEXT("AirRaid"), 72.f, 52.f),
            MakeCoach(TEXT("Coach_Tate"), EPSCoachRole::DefensiveCoordinator, TEXT("Cover2Zone"), 64.f, 68.f),
        };
        League.Staffs = {
            MakeStaff(TEXT("Falcons"), TEXT("Coach_Grant"), TEXT("Coach_Hale"), TEXT("Coach_Ortiz"), 3),
            MakeStaff(TEXT("Hawks"), TEXT("Coach_Mercer"), TEXT("Coach_Pike"), TEXT("Coach_Vance"), 1),
            MakeStaff(TEXT("Wolves"), TEXT("Coach_Stone"), TEXT("Coach_Bishop"), TEXT("Coach_Kerr"), 4),
            MakeStaff(TEXT("Bears"), TEXT("Coach_Rowe"), TEXT("Coach_Lowell"), TEXT("Coach_Abbott"), 2),
        };
        return League;
    }

    /** Falcons 2-15 (the most points allowed), Wolves 8-9 (the fewest points), Bears 11-6, Hawks 13-4. */
    static TArray<FPSTeamStanding> MakeCarouselStandings()
    {
        return {
            MakeStanding(TEXT("Falcons"), 2, 15, 250, 420),
            MakeStanding(TEXT("Hawks"), 13, 4, 480, 300),
            MakeStanding(TEXT("Wolves"), 8, 9, 240, 330),
            MakeStanding(TEXT("Bears"), 11, 6, 400, 410),
        };
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The data file
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStaffDataFileTest,
    "PlaySports.Staff.DataFileLoadsAndValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStaffDataFileTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSCoachingLeague FromFile;
    if (!TestTrue(TEXT("coaching_staffs.json loads"), Ingestion->LoadCoachingLeagueFromJson(UPSStaffManager::GetDefaultDataPath(), FromFile)))
    {
        return false;
    }

    for (const FString& Problem : UPSStaffManager::Validate(FromFile))
    {
        AddError(FString::Printf(TEXT("coaching_staffs.json: %s"), *Problem));
    }

    int32 OffenseSchemes = 0;
    for (const FPSSchemeDef& Scheme : FromFile.Schemes)
    {
        OffenseSchemes += Scheme.bOffense ? 1 : 0;
    }
    TestTrue(TEXT("At least two offensive schemes"), OffenseSchemes >= 2);
    TestTrue(TEXT("At least two defensive schemes"), FromFile.Schemes.Num() - OffenseSchemes >= 2);

    UPSStaffManager* Manager = NewObject<UPSStaffManager>();
    Manager->SetLeague(FromFile);
    for (const TCHAR* Team : { TEXT("Falcons"), TEXT("Hawks"), TEXT("Wolves"), TEXT("Bears") })
    {
        for (const EPSCoachRole Role : { EPSCoachRole::HeadCoach, EPSCoachRole::OffensiveCoordinator, EPSCoachRole::DefensiveCoordinator })
        {
            TestNotNull(*FString::Printf(TEXT("The %s have a %s"), Team, PSStaff::DescribeRole(Role)), Manager->FindTeamCoach(FName(Team), Role));
        }
    }

    const FPSStaffTuning Defaults;
    int32 Compared = 0;
    for (TFieldIterator<FProperty> It(FPSStaffTuning::StaticStruct()); It; ++It)
    {
        TestTrue(*FString::Printf(TEXT("%s matches the default"), *It->GetName()), It->Identical_InContainer(&FromFile.Tuning, &Defaults));
        ++Compared;
    }
    TestTrue(TEXT("Every tuning field was compared"), Compared >= 14);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Scheme-playbook binding and tendencies
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStaffSchemeBindingTest,
    "PlaySports.Staff.SchemeShapesPlaybookAndCalls",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStaffSchemeBindingTest::RunTest(const FString& Parameters)
{
    using namespace PSStaffTests;

    UPSStaffManager* Manager = LoadManager();
    const TArray<FPSPlayDefinition> Plays = LoadPlaybook();
    if (!TestTrue(TEXT("The playbook loads"), Plays.Num() > 0))
    {
        return false;
    }
    const FName Hawks(TEXT("Hawks"));
    const FName Wolves(TEXT("Wolves"));
    TestEqual(TEXT("The Hawks run the Air Raid"), Manager->GetTeamScheme(Hawks, true), FName(TEXT("AirRaid")));
    TestEqual(TEXT("The Wolves run the Power Run"), Manager->GetTeamScheme(Wolves, true), FName(TEXT("PowerRun")));

    // The playbook derives from the coordinators.
    const TArray<FName> AirRaid = Manager->BuildPlaybook(Hawks, Plays);
    const TArray<FName> PowerRun = Manager->BuildPlaybook(Wolves, Plays);
    TestTrue(TEXT("The Air Raid keeps four verticals"), HasPlay(AirRaid, TEXT("Offense_FourVerts")));
    TestFalse(TEXT("...but not the power sweep"), HasPlay(AirRaid, TEXT("Offense_PowerOSweep")));
    TestTrue(TEXT("The Power Run keeps the power sweep"), HasPlay(PowerRun, TEXT("Offense_PowerOSweep")));
    TestFalse(TEXT("...but not four verticals"), HasPlay(PowerRun, TEXT("Offense_FourVerts")));
    for (const TCHAR* Universal : { TEXT("Offense_Punt"), TEXT("Offense_FieldGoal"), TEXT("Offense_Kickoff"), TEXT("Offense_Spike"), TEXT("Offense_Kneel"), TEXT("Defense_ReturnWall"), TEXT("Defense_HandsTeam") })
    {
        TestTrue(*FString::Printf(TEXT("Both teams keep %s"), Universal), HasPlay(AirRaid, Universal) && HasPlay(PowerRun, Universal));
    }
    const TArray<FName> Pressure = Manager->BuildPlaybook(FName(TEXT("Bears")), Plays);
    const TArray<FName> CoverTwo = Manager->BuildPlaybook(FName(TEXT("Falcons")), Plays);
    TestTrue(TEXT("The 3-4 Pressure keeps the 3-4 and the blitz package"), HasPlay(Pressure, TEXT("Defense_34Cover3")) && HasPlay(Pressure, TEXT("Defense_DoubleABlitz")));
    TestFalse(TEXT("...and never sits back in quarters prevent"), HasPlay(Pressure, TEXT("Defense_Cover4Quarters")));
    TestTrue(TEXT("The Cover 2 Zone keeps quarters prevent"), HasPlay(CoverTwo, TEXT("Defense_Cover4Quarters")));
    TestFalse(TEXT("...but not the 3-4"), HasPlay(CoverTwo, TEXT("Defense_34Cover3")));

    // The tendency: the head coach's aggression, the scheme's lean.
    const FPSTendencyProfile HawksOffense = Manager->BuildTendency(Hawks, true);
    const FPSTendencyProfile WolvesOffense = Manager->BuildTendency(Wolves, true);
    TestEqual(TEXT("The Air Raid names itself"), HawksOffense.Label, FString(TEXT("Air Raid")));
    TestTrue(TEXT("The Air Raid leans deep"), HawksOffense.CategoryWeights.FindRef(TEXT("DeepPass")) > 1.f);
    TestTrue(TEXT("...and away from the run"), HawksOffense.CategoryWeights.FindRef(TEXT("Run")) < 1.f);
    TestTrue(TEXT("The Power Run leans on the run"), WolvesOffense.CategoryWeights.FindRef(TEXT("Run")) > 1.f);
    TestEqual(TEXT("Aggression is the head coach's"), HawksOffense.AggressionScore, Manager->FindTeamCoach(Hawks, EPSCoachRole::HeadCoach)->Aggression);
    TestTrue(TEXT("The Hawks' head coach is bolder than the Wolves'"), HawksOffense.AggressionScore > WolvesOffense.AggressionScore);

    // A better play caller follows his scheme further.
    FPSCoachingLeague Edited = Manager->GetLeague();
    FPSCoachDef* Coordinator = Edited.Coaches.FindByPredicate([](const FPSCoachDef& Coach) { return Coach.CoachId == FName(TEXT("Coach_Pike")); });
    if (!TestNotNull(TEXT("The Hawks' coordinator"), Coordinator))
    {
        return false;
    }
    UPSStaffManager* Edit = NewObject<UPSStaffManager>();
    Coordinator->PlayCalling = 100.f;
    Edit->SetLeague(Edited);
    const float Faithful = Edit->BuildTendency(Hawks, true).CategoryWeights.FindRef(TEXT("DeepPass"));
    Coordinator->PlayCalling = 0.f;
    Edit->SetLeague(Edited);
    const float Loose = Edit->BuildTendency(Hawks, true).CategoryWeights.FindRef(TEXT("DeepPass"));
    TestTrue(TEXT("Play calling 100 leans deeper than play calling 0"), Faithful > Loose && Loose > 1.f);

    // Without a coordinator: the whole book on that side, no lean.
    FPSTeamStaffDef* HawksStaff = Edited.Staffs.FindByPredicate([Hawks](const FPSTeamStaffDef& Staff) { return Staff.TeamId == Hawks; });
    if (!TestNotNull(TEXT("The Hawks' staff"), HawksStaff))
    {
        return false;
    }
    HawksStaff->OffensiveCoordinatorId = NAME_None;
    Edit->SetLeague(Edited);
    TestEqual(TEXT("No coordinator, no lean"), Edit->BuildTendency(Hawks, true).CategoryWeights.Num(), 0);
    const TArray<FName> WholeBook = Edit->BuildPlaybook(Hawks, Plays);
    TestTrue(TEXT("No coordinator, the whole offense"), HasPlay(WholeBook, TEXT("Offense_FourVerts")) && HasPlay(WholeBook, TEXT("Offense_PowerOSweep")));

    // The coaching AI calls the scheme's way: on 2nd and 2 the power team's best call is a run,
    // the air raid's a throw.
    UPSCoachingAI* Coaching = NewObject<UPSCoachingAI>();
    const FPSSituationContext ShortYardage = MakeContext(2, 2, 40, true);
    const auto OffenseOf = [&Plays](const TArray<FName>& Kept)
    {
        return Plays.FilterByPredicate([&Kept](const FPSPlayDefinition& Play) { return Play.bIsOffensivePlay && Kept.Contains(Play.PlayId); });
    };
    const TArray<FPSPlaySuggestion> PowerCalls = Coaching->RankPlays(ShortYardage, WolvesOffense, OffenseOf(PowerRun), true);
    const TArray<FPSPlaySuggestion> AirCalls = Coaching->RankPlays(ShortYardage, HawksOffense, OffenseOf(AirRaid), true);
    if (TestTrue(TEXT("Both teams have calls"), PowerCalls.Num() > 0 && AirCalls.Num() > 0))
    {
        TestEqual(TEXT("The Power Run's best call is a run"), PowerCalls[0].Category, FString(TEXT("Run")));
        TestTrue(TEXT("The Air Raid's best call is a throw"), AirCalls[0].Category != TEXT("Run"));
        TestTrue(TEXT("The reasons name the scheme"), AirCalls[0].Reasons.ContainsByPredicate([](const FString& Reason) { return Reason.StartsWith(TEXT("Air Raid scheme (x")); }));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Player-scheme fit
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStaffSchemeFitTest,
    "PlaySports.Staff.PlayerSchemeFit",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStaffSchemeFitTest::RunTest(const FString& Parameters)
{
    using namespace PSStaffTests;

    UPSStaffManager* Manager = LoadManager();
    const FPSStaffTuning& Tuning = Manager->GetTuning();
    const FName Power(TEXT("Wolves"));
    const FName Zone(TEXT("Bears"));
    TestEqual(TEXT("The Bears run the Zone Run"), Manager->GetTeamScheme(Zone, true), FName(TEXT("ZoneRun")));

    // An agile zone lineman and a strong power lineman.
    const FPlayerAttributes ZoneLineman = MakePlayer(EPlayerRole::OffensiveLineman, 70.f, 88.f, 66.f, 75.f, 80.f);
    const FPlayerAttributes PowerLineman = MakePlayer(EPlayerRole::OffensiveLineman, 60.f, 62.f, 94.f, 65.f, 78.f);

    const FPSSchemeFitResult ZoneInPower = Manager->GetSchemeFit(Power, ZoneLineman);
    TestTrue(TEXT("The zone lineman misfits in the power scheme"), ZoneInPower.Fit < 0.f && ZoneInPower.Multiplier < 1.f);
    TestEqual(TEXT("...and is called a misfit"), ZoneInPower.Description, FString(TEXT("Misfit in the Power Run")));
    TestTrue(TEXT("...though never below the worst multiplier"), ZoneInPower.Multiplier >= Tuning.WorstFitMultiplier);
    const FPSSchemeFitResult ZoneInZone = Manager->GetSchemeFit(Zone, ZoneLineman);
    TestTrue(TEXT("He fits the zone scheme"), ZoneInZone.Fit > 0.f && ZoneInZone.Multiplier > 1.f);
    TestEqual(TEXT("...and is called a fit"), ZoneInZone.Description, FString(TEXT("Fits the Zone Run")));
    TestTrue(TEXT("...though never above the best multiplier"), ZoneInZone.Multiplier <= Tuning.BestFitMultiplier);

    const FPSSchemeFitResult PowerInPower = Manager->GetSchemeFit(Power, PowerLineman);
    const FPSSchemeFitResult PowerInZone = Manager->GetSchemeFit(Zone, PowerLineman);
    TestTrue(TEXT("The power lineman fits the power scheme"), PowerInPower.Multiplier > 1.f);
    TestTrue(TEXT("...and misfits in the zone"), PowerInZone.Multiplier < 1.f);

    // He plays below his ratings there; his size and stamina don't change.
    const FPlayerAttributes Fitted = Manager->ApplySchemeFit(Power, ZoneLineman);
    TestTrue(TEXT("His strength drops by the multiplier"), FMath::IsNearlyEqual(Fitted.Strength, ZoneLineman.Strength * ZoneInPower.Multiplier));
    TestTrue(TEXT("...and his agility"), FMath::IsNearlyEqual(Fitted.Agility, ZoneLineman.Agility * ZoneInPower.Multiplier));
    TestEqual(TEXT("His weight stays"), Fitted.WeightKg, ZoneLineman.WeightKg);
    TestEqual(TEXT("His stamina stays"), Fitted.Stamina, ZoneLineman.Stamina);

    // A coordinator who develops players softens the misfit; a fit is the same under anyone.
    const FPSSchemeFitResult Untaught = Manager->GetSchemeFitIn(FName(TEXT("PowerRun")), ZoneLineman, 0.f);
    const FPSSchemeFitResult Taught = Manager->GetSchemeFitIn(FName(TEXT("PowerRun")), ZoneLineman, 100.f);
    TestTrue(TEXT("Development 100 softens the misfit"), Taught.Multiplier > Untaught.Multiplier && Taught.Multiplier < 1.f);
    TestEqual(TEXT("The fit itself is the same"), Taught.Fit, Untaught.Fit);

    // Neutral: a position the scheme asks nothing of, the other side, a vacant coordinator.
    const FPlayerAttributes Quarterback = MakePlayer(EPlayerRole::Quarterback, 80.f, 80.f, 80.f, 80.f, 80.f);
    const FPlayerAttributes Linebacker = MakePlayer(EPlayerRole::Linebacker, 80.f, 80.f, 80.f, 80.f, 80.f);
    TestEqual(TEXT("The Power Run asks nothing of a quarterback"), Manager->GetSchemeFit(Power, Quarterback).Multiplier, 1.f);
    TestEqual(TEXT("An offensive scheme leaves a linebacker alone"), Manager->GetSchemeFitIn(FName(TEXT("PowerRun")), Linebacker, 50.f).Multiplier, 1.f);
    TestEqual(TEXT("A linebacker's fit is his defensive coordinator's"), Manager->GetSchemeFit(Power, Linebacker).SchemeId, FName(TEXT("Pressure34")));
    TestEqual(TEXT("An unknown team leaves him alone"), Manager->GetSchemeFit(FName(TEXT("Nobody")), ZoneLineman).Multiplier, 1.f);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The coaching carousel
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStaffCarouselTest,
    "PlaySports.Staff.CarouselFiringsHiringsAndChurn",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStaffCarouselTest::RunTest(const FString& Parameters)
{
    using namespace PSStaffTests;

    const FPSCoachingLeague Start = MakeCarouselLeague(LoadManager()->GetLeague());
    if (!TestEqual(TEXT("The test league is valid"), UPSStaffManager::Validate(Start).Num(), 0))
    {
        return false;
    }

    UPSStaffManager* Manager = NewObject<UPSStaffManager>();
    Manager->SetLeague(Start);
    const TArray<FPSCarouselEvent> Events = Manager->RunCarousel(MakeCarouselStandings());
    for (const FPSCarouselEvent& Event : Events)
    {
        AddInfo(Event.Description);
    }
    if (!TestEqual(TEXT("Seven moves"), Events.Num(), 7))
    {
        return false;
    }

    const auto Expect = [this, &Events](int32 Index, EPSCarouselAction Action, const TCHAR* TeamId, const TCHAR* CoachId, EPSCoachRole Role)
    {
        const FPSCarouselEvent& Event = Events[Index];
        TestTrue(*FString::Printf(TEXT("Move %d: %s"), Index, *Event.Description),
            Event.Action == Action && Event.TeamId == FName(TeamId) && Event.CoachId == FName(CoachId) && Event.Role == Role);
    };

    // The 2-15 Falcons fire their head coach, four seasons in; the Wolves the league's worst
    // offense's coordinator. The Bears allowed many points but won: their coordinator stays.
    Expect(0, EPSCarouselAction::Fired, TEXT("Falcons"), TEXT("Coach_Grant"), EPSCoachRole::HeadCoach);
    TestTrue(TEXT("...after a 2-15 season"), Events[0].Description.Contains(TEXT("2-15")));
    Expect(1, EPSCarouselAction::Fired, TEXT("Wolves"), TEXT("Coach_Bishop"), EPSCoachRole::OffensiveCoordinator);
    TestEqual(TEXT("The winning Bears keep their defensive coordinator"), Manager->FindStaff(FName(TEXT("Bears")))->DefensiveCoordinatorId, FName(TEXT("Coach_Abbott")));

    // The Falcons hire the 13-4 Hawks' offensive coordinator as head coach ...
    Expect(2, EPSCarouselAction::Promoted, TEXT("Falcons"), TEXT("Coach_Pike"), EPSCoachRole::HeadCoach);
    TestEqual(TEXT("He is a head coach now"), Manager->FindCoach(FName(TEXT("Coach_Pike")))->Role, EPSCoachRole::HeadCoach);
    TestEqual(TEXT("His tenure starts over"), Manager->FindStaff(FName(TEXT("Falcons")))->HeadCoachSeasons, 0);
    TestEqual(TEXT("The Hawks' coach has another season"), Manager->FindStaff(FName(TEXT("Hawks")))->HeadCoachSeasons, 2);

    // ... who brings his Air Raid: the West Coast coordinator goes, an Air Raid one comes in.
    Expect(3, EPSCarouselAction::Fired, TEXT("Falcons"), TEXT("Coach_Hale"), EPSCoachRole::OffensiveCoordinator);
    TestTrue(TEXT("...because the new head coach brings the Air Raid"), Events[3].Description.Contains(TEXT("Air Raid")));
    Expect(4, EPSCarouselAction::Hired, TEXT("Falcons"), TEXT("Coach_Sato"), EPSCoachRole::OffensiveCoordinator);
    TestTrue(TEXT("Scheme churn: West Coast to Air Raid"),
        Events[4].PreviousSchemeId == FName(TEXT("WestCoast")) && Events[4].NewSchemeId == FName(TEXT("AirRaid")));
    TestEqual(TEXT("The Falcons run the Air Raid now"), Manager->GetTeamScheme(FName(TEXT("Falcons")), true), FName(TEXT("AirRaid")));

    // The vacancies fill, worst team first; a coach let go can work elsewhere, not back home.
    Expect(5, EPSCarouselAction::Hired, TEXT("Wolves"), TEXT("Coach_Hale"), EPSCoachRole::OffensiveCoordinator);
    Expect(6, EPSCarouselAction::Hired, TEXT("Hawks"), TEXT("Coach_Bishop"), EPSCoachRole::OffensiveCoordinator);
    for (const FPSTeamStaffDef& Staff : Manager->GetLeague().Staffs)
    {
        TestTrue(*FString::Printf(TEXT("The %s have a full staff"), *Staff.TeamId.ToString()),
            !Staff.HeadCoachId.IsNone() && !Staff.OffensiveCoordinatorId.IsNone() && !Staff.DefensiveCoordinatorId.IsNone());
    }
    TestEqual(TEXT("The league is still valid"), UPSStaffManager::Validate(Manager->GetLeague()).Num(), 0);

    // The same league and season make the same moves.
    UPSStaffManager* Again = NewObject<UPSStaffManager>();
    Again->SetLeague(Start);
    const TArray<FPSCarouselEvent> Replay = Again->RunCarousel(MakeCarouselStandings());
    bool bSame = Replay.Num() == Events.Num();
    for (int32 Index = 0; bSame && Index < Events.Num(); ++Index)
    {
        bSame = Replay[Index].Description == Events[Index].Description;
    }
    TestTrue(TEXT("The carousel is deterministic"), bSame);

    // A first-year head coach is in his grace season: the same record keeps him.
    FPSCoachingLeague Fresh = Start;
    Fresh.Staffs[0].HeadCoachSeasons = 0;
    UPSStaffManager* Grace = NewObject<UPSStaffManager>();
    Grace->SetLeague(Fresh);
    Grace->RunCarousel(MakeCarouselStandings());
    TestEqual(TEXT("A head coach in his grace season keeps the job"), Grace->FindStaff(FName(TEXT("Falcons")))->HeadCoachId, FName(TEXT("Coach_Grant")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The play-call authority calls with each team's plan
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStaffPlayCallTest,
    "PlaySports.Staff.PlayCallUsesTheTeamPlan",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStaffPlayCallTest::RunTest(const FString& Parameters)
{
    using namespace PSStaffTests;

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

    UPSStaffManager* Manager = LoadManager();
    TestTrue(TEXT("Both teams have a staff"), Manager->ApplyToPlayCall(PlayCall, FName(TEXT("Hawks")), FName(TEXT("Wolves"))));
    TestEqual(TEXT("The home plan is the Hawks'"), PlayCall->GetTeamPlan(true).TeamId, FName(TEXT("Hawks")));

    // The home Hawks have the ball: their Air Raid against the Wolves' 3-4 Pressure.
    PlayCall->OpenPlayCall(MakeContext(1, 10, 30, true));
    TestTrue(TEXT("The Hawks' offense has four verticals"), HasPlay(PlayCall->GetPlays(true), TEXT("Offense_FourVerts")));
    TestFalse(TEXT("...and no power sweep"), HasPlay(PlayCall->GetPlays(true), TEXT("Offense_PowerOSweep")));
    TestTrue(TEXT("The Wolves' defense has the 3-4"), HasPlay(PlayCall->GetPlays(false), TEXT("Defense_34Cover3")));
    TestFalse(TEXT("...and no 4-3"), HasPlay(PlayCall->GetPlays(false), TEXT("Defense_43Cover2")));
    TestFalse(TEXT("A play the team doesn't run can't be called"), PlayCall->CallPlay(FName(TEXT("Offense_PowerOSweep")), EPSPlayCaller::Human));
    TestTrue(TEXT("...one it runs can"), PlayCall->CallPlay(FName(TEXT("Offense_FourVerts")), EPSPlayCaller::Human));
    TestTrue(TEXT("The call screen shows the offense's scheme"), PlayCall->BuildCallScreenBody(true).Contains(TEXT("Scheme: Air Raid")));
    TestTrue(TEXT("...and the defense's"), PlayCall->BuildCallScreenBody(false).Contains(TEXT("Scheme: 3-4 Pressure")));
    const TArray<FPSPlaySuggestion> Suggestions = PlayCall->RankPlays(true);
    TestTrue(TEXT("The suggestions carry the scheme's reasons"), Suggestions.ContainsByPredicate([](const FPSPlaySuggestion& Suggestion)
    {
        return Suggestion.Reasons.ContainsByPredicate([](const FString& Reason) { return Reason.StartsWith(TEXT("Air Raid scheme")); });
    }));

    // The CPU calls from the teams' playbooks.
    PlayCall->OpenPlayCall(MakeContext(1, 10, 30, true));
    PlayCall->PollReadyToSnap(0.f);
    TestTrue(TEXT("The CPU offense calls a Hawks play"), PlayCall->GetTeamPlan(true).PlayIds.Contains(PlayCall->GetCall(true).PlayId));
    TestTrue(TEXT("The CPU defense calls a Wolves play"), PlayCall->GetTeamPlan(false).PlayIds.Contains(PlayCall->GetCall(false).PlayId));

    // The Wolves have the ball: their Power Run against the Hawks' Nickel Man.
    FPlayState State;
    State.bHomeHasPossession = false;
    const FPSSituationContext AwayBall = UPSPlayCallSubsystem::MakeSituation(State);
    TestFalse(TEXT("The situation knows the away team has the ball"), AwayBall.bHomeHasPossession);
    PlayCall->OpenPlayCall(AwayBall);
    TestTrue(TEXT("The Wolves' offense has the power sweep"), HasPlay(PlayCall->GetPlays(true), TEXT("Offense_PowerOSweep")));
    TestFalse(TEXT("...and no four verticals"), HasPlay(PlayCall->GetPlays(true), TEXT("Offense_FourVerts")));
    TestTrue(TEXT("The Hawks' defense plays nickel"), HasPlay(PlayCall->GetPlays(false), TEXT("Defense_NickelManFree")));
    TestFalse(TEXT("...and no 3-4"), HasPlay(PlayCall->GetPlays(false), TEXT("Defense_34Cover3")));

    // No plans: the whole playbook again. A team without a staff gets none.
    PlayCall->ClearTeamPlans();
    PlayCall->OpenPlayCall(MakeContext(1, 10, 30, true));
    TestTrue(TEXT("Without plans the offense has every play"), HasPlay(PlayCall->GetPlays(true), TEXT("Offense_FourVerts")) && HasPlay(PlayCall->GetPlays(true), TEXT("Offense_PowerOSweep")));
    TestFalse(TEXT("A team with no staff"), Manager->ApplyToPlayCall(PlayCall, FName(TEXT("Hawks")), FName(TEXT("Sharks"))));
    TestEqual(TEXT("...calls from the whole playbook"), PlayCall->GetTeamPlan(false).PlayIds.Num(), 0);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- The franchise save
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStaffSaveTest,
    "PlaySports.Staff.FranchiseSaveRoundTrip",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStaffSaveTest::RunTest(const FString& Parameters)
{
    using namespace PSStaffTests;

    // A season's carousel changes the staffs ...
    UPSStaffManager* Season = LoadManager();
    Season->RunCarousel({
        MakeStanding(TEXT("Falcons"), 1, 16, 200, 450),
        MakeStanding(TEXT("Hawks"), 14, 3, 500, 280),
        MakeStanding(TEXT("Wolves"), 9, 8, 300, 320),
        MakeStanding(TEXT("Bears"), 10, 7, 350, 330),
    });
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    Season->SaveTo(Save);
    TestEqual(TEXT("Every coach is saved"), Save->Coaches.Num(), Season->GetLeague().Coaches.Num());

    // ... and a new session reads them back over the data file's.
    UPSStaffManager* Loaded = LoadManager();
    TestTrue(TEXT("The save loads"), Loaded->LoadFrom(Save));
    for (const FPSTeamStaffDef& Staff : Season->GetLeague().Staffs)
    {
        const FPSTeamStaffDef* Read = Loaded->FindStaff(Staff.TeamId);
        TestTrue(*FString::Printf(TEXT("The %s staff round-trips"), *Staff.TeamId.ToString()), Read
            && Read->HeadCoachId == Staff.HeadCoachId && Read->OffensiveCoordinatorId == Staff.OffensiveCoordinatorId
            && Read->DefensiveCoordinatorId == Staff.DefensiveCoordinatorId && Read->HeadCoachSeasons == Staff.HeadCoachSeasons);
    }
    for (const FPSCoachDef& Coach : Season->GetLeague().Coaches)
    {
        const FPSCoachDef* Read = Loaded->FindCoach(Coach.CoachId);
        TestTrue(*FString::Printf(TEXT("%s round-trips"), *Coach.CoachId.ToString()), Read && Read->Role == Coach.Role && Read->SchemeId == Coach.SchemeId);
    }

    // A save from before coaching staffs keeps the data file's.
    UPSStaffManager* Older = LoadManager();
    const FName Before = Older->FindStaff(FName(TEXT("Falcons")))->HeadCoachId;
    TestFalse(TEXT("An older save has no staffs"), Older->LoadFrom(NewObject<UPSFranchiseSaveGame>()));
    TestEqual(TEXT("...and the data file's stand"), Older->FindStaff(FName(TEXT("Falcons")))->HeadCoachId, Before);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
