// PSLeagueHistoryTests.cpp -- Epic 94 (the league's history, retirements and the hall of fame)
//
// Tests covered:
//   1. Data/legacy.json loads through UPSDataIngestion, validates clean and equals the hall of
//      fame tuning's defaults; a broken tuning's problems are each reported.
//   2. The season archive: final standings in finishing order, the champion, each category's
//      leader from the statistics engine; a season is archived once; a franchise's seasons,
//      finishes, championships and record add up.
//   3. Retirement and the hall of fame: a retiree's career, seasons and primary team come from the
//      statistics engine; the vote waits WaitSeasons, needs MinSeasons and InductionScore (the best
//      career total against its threshold), takes the best first up to MaxInducteesPerSeason;
//      hall of famers become their franchise's legends.
//   4. The franchise: two seasons through UPSFranchiseFlow archive themselves with their
//      leaders; the history round-trips through the franchise save.
//   5. Awards (Epic 93): each award a player won adds its score to his hall score; an award
//      alone can carry a career into the hall.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "PSDataIngestion.h"
#include "PSFranchiseFlow.h"
#include "PSFranchiseSaveGame.h"
#include "PSFranchiseSeason.h"
#include "PSLeagueData.h"
#include "PSLeagueHistory.h"
#include "PSLeagueNarrative.h"
#include "PSLegacyData.h"
#include "PSNarrativeTypes.h"
#include "PSRoster.h"
#include "PSSaveSubsystem.h"
#include "PSScheduleEngine.h"
#include "PSStaffManager.h"
#include "PSStatsData.h"
#include "PSStatsEngine.h"
#include "PSTelemetryBus.h"
#include "PSUITeamCatalog.h"
#include "Engine/GameInstance.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSLeagueHistoryTests
{
    static UPSLeagueHistory* MakeHistory()
    {
        UPSLeagueHistory* History = NewObject<UPSLeagueHistory>();
        History->LoadTuningFromJson(UPSLeagueHistory::GetDefaultTuningPath());
        return History;
    }

    /** A completed pass by the offense (the home team when bHomeOffense) for Yards, tackled by
     *  TacklerId when given. */
    static FPSTelemetryPlayResultEvent MakeLegacyPass(bool bHomeOffense, const TCHAR* PasserId, const TCHAR* ReceiverId, int32 Yards, const TCHAR* TacklerId = nullptr)
    {
        FPSTelemetryPlayResultEvent Play;
        Play.bHomeOffense = bHomeOffense;
        Play.Result = TEXT("Tackle");
        Play.YardsGained = Yards;
        Play.Down = 1;
        Play.YardLine = 30;
        Play.bPass = true;
        Play.bComplete = true;
        Play.PasserId = FName(PasserId);
        Play.ReceiverId = FName(ReceiverId);
        Play.TacklerId = TacklerId ? FName(TacklerId) : FName();
        return Play;
    }

    static void PlayLegacyGame(UPSStatsEngine* Stats, int32 Week, const TCHAR* Home, const TCHAR* Away, const TArray<FPSTelemetryPlayResultEvent>& Plays)
    {
        Stats->BeginGame(Week, FName(Home), FName(Away));
        for (const FPSTelemetryPlayResultEvent& Play : Plays)
        {
            Stats->RecordPlay(Play);
        }
        Stats->FinishGame();
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

    static FPlayerAttributes MakeRetiree(const TCHAR* PlayerId, EPlayerRole Role)
    {
        FPlayerAttributes Player;
        Player.PlayerId = FName(PlayerId);
        Player.DisplayName = PlayerId;
        Player.Role = Role;
        Player.Awareness = 80.f;
        return Player;
    }

    static FPSHallOfFameThreshold MakeThreshold(EPSStatCategory Category, int32 CareerValue)
    {
        FPSHallOfFameThreshold Threshold;
        Threshold.Category = Category;
        Threshold.CareerValue = CareerValue;
        return Threshold;
    }

    static bool HasLine(const TArray<FString>& Lines, const TCHAR* Fragment)
    {
        return Lines.ContainsByPredicate([Fragment](const FString& Line) { return Line.Contains(Fragment); });
    }

    static const FPSSeasonLeader* FindLeader(const FPSSeasonArchive& Archive, EPSStatCategory Category)
    {
        return Archive.Leaders.FindByPredicate([Category](const FPSSeasonLeader& Leader) { return Leader.Category == Category; });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The shipped tuning
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLegacyTuningTest,
    "PlaySports.Legacy.TuningLoadsAndValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLegacyTuningTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSLegacyTuning FromFile;
    if (!TestTrue(TEXT("Data/legacy.json loads"), Ingestion->LoadLegacyTuningFromJson(UPSLeagueHistory::GetDefaultTuningPath(), FromFile)))
    {
        return false;
    }
    const TArray<FString> Problems = UPSLeagueHistory::ValidateTuning(FromFile);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    TestEqual(TEXT("The shipped tuning is sound"), Problems.Num(), 0);
    TestEqual(TEXT("Ten career thresholds"), FromFile.HallOfFame.Thresholds.Num(), 10);
    TestEqual(TEXT("Eight archived leader categories"), FromFile.LeaderCategories.Num(), 8);
    const FPSHallOfFameTuning Defaults;
    for (TFieldIterator<FProperty> It(FPSHallOfFameTuning::StaticStruct()); It; ++It)
    {
        if (It->GetFName() != GET_MEMBER_NAME_CHECKED(FPSHallOfFameTuning, Thresholds))
        {
            TestTrue(*FString::Printf(TEXT("HallOfFame.%s matches the default"), *It->GetName()), It->Identical_InContainer(&FromFile.HallOfFame, &Defaults));
        }
    }
    const FPSRetirementTuning RetirementDefaults;
    for (TFieldIterator<FProperty> It(FPSRetirementTuning::StaticStruct()); It; ++It)
    {
        TestTrue(*FString::Printf(TEXT("Retirement.%s matches the default"), *It->GetName()), It->Identical_InContainer(&FromFile.Retirement, &RetirementDefaults));
    }
    TestEqual(TEXT("An age curve for every role"), FromFile.RoleCurves.Num(), 8);

    FPSLegacyTuning Broken = FromFile;
    Broken.HallOfFame.MaxInducteesPerSeason = 0;
    Broken.HallOfFame.Thresholds[0].Category = EPSStatCategory::TeamPoints;
    Broken.HallOfFame.Thresholds[1].CareerValue = 0;
    Broken.LeaderCategories.Add(EPSStatCategory::PassingYards);
    const TArray<FString> BrokenProblems = UPSLeagueHistory::ValidateTuning(Broken);
    for (const TCHAR* Expected : { TEXT("MaxInducteesPerSeason"), TEXT("TeamPoints is a team category"), TEXT("PassingTouchdowns's CareerValue"),
        TEXT("LeaderCategories: PassingYards") })
    {
        TestTrue(*FString::Printf(TEXT("Reported: %s"), Expected), PSLeagueHistoryTests::HasLine(BrokenProblems, Expected));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The season archive and a franchise's history
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLegacyArchiveTest,
    "PlaySports.Legacy.SeasonArchiveAndFranchiseHistory",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLegacyArchiveTest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueHistoryTests;

    UPSLeagueHistory* History = MakeHistory();
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    Stats->StartSeason(1);

    // Season 1: the Hawks' quarterback throws for 300, the Wolves' for 120; the Hawks win it.
    PlayLegacyGame(Stats, 1, TEXT("Hawks"), TEXT("Wolves"), { MakeLegacyPass(true, TEXT("HAW_QB"), TEXT("HAW_WR"), 100, TEXT("WLV_LB")),
        MakeLegacyPass(true, TEXT("HAW_QB"), TEXT("HAW_WR"), 200, TEXT("WLV_LB")), MakeLegacyPass(false, TEXT("WLV_QB"), TEXT("WLV_WR"), 120, TEXT("HAW_LB")) });
    Stats->EndSeason();
    const TArray<FPSTeamStanding> Season1 = { MakeStanding(TEXT("Hawks"), 1, 0, 24, 10), MakeStanding(TEXT("Wolves"), 0, 1, 10, 24) };
    TestTrue(TEXT("Season 1 is archived"), History->ArchiveSeason(1, Season1, Stats));
    TestFalse(TEXT("...once"), History->ArchiveSeason(1, Season1, Stats));
    TestFalse(TEXT("A season without standings isn't"), History->ArchiveSeason(5, TArray<FPSTeamStanding>(), Stats));

    FPSSeasonArchive First;
    if (TestTrue(TEXT("Season 1 is in the archive"), History->FindSeason(1, First)))
    {
        TestEqual(TEXT("The Hawks finished first: champions"), First.ChampionTeamId, FName(TEXT("Hawks")));
        TestEqual(TEXT("Both teams' standings"), First.Standings.Num(), 2);
        const FPSSeasonLeader* Passing = FindLeader(First, EPSStatCategory::PassingYards);
        TestTrue(TEXT("The passing leader: the Hawks' quarterback, 300"), Passing && Passing->PlayerId == FName(TEXT("HAW_QB")) && Passing->TeamId == FName(TEXT("Hawks")) && Passing->Value == 300);
        const FPSSeasonLeader* Tackling = FindLeader(First, EPSStatCategory::Tackles);
        TestTrue(TEXT("The tackles leader: the Wolves' linebacker, 2"), Tackling && Tackling->PlayerId == FName(TEXT("WLV_LB")) && Tackling->Value == 2);
        TestNull(TEXT("Nobody sacked anyone: no sacks leader"), FindLeader(First, EPSStatCategory::Sacks));
    }
    TestTrue(TEXT("The season reads"), HasLine(History->DescribeSeason(1), TEXT("Season 1: champion Hawks (1-0-0)"))
        && HasLine(History->DescribeSeason(1), TEXT("Leader in PassingYards: HAW_QB (Hawks), 300")));
    TestTrue(TEXT("An unknown season says so"), HasLine(History->DescribeSeason(9), TEXT("not in the archive")));

    // Season 2: the Wolves' turn.
    PlayLegacyGame(Stats, 1, TEXT("Wolves"), TEXT("Hawks"), { MakeLegacyPass(true, TEXT("WLV_QB"), TEXT("WLV_WR"), 80) });
    Stats->EndSeason();
    TestTrue(TEXT("Season 2 is archived"), History->ArchiveSeason(2, { MakeStanding(TEXT("Wolves"), 2, 0, 40, 20), MakeStanding(TEXT("Hawks"), 0, 2, 20, 40) }, Stats));
    FPSSeasonArchive Second;
    History->FindSeason(2, Second);
    const FPSSeasonLeader* SecondPassing = FindLeader(Second, EPSStatCategory::PassingYards);
    TestTrue(TEXT("Season 2's leaders are season 2's"), SecondPassing && SecondPassing->PlayerId == FName(TEXT("WLV_QB")) && SecondPassing->Value == 80);

    // A franchise's history.
    const FPSFranchiseHistory Hawks = History->GetFranchiseHistory(FName(TEXT("Hawks")));
    TestEqual(TEXT("The Hawks have two seasons"), Hawks.Seasons.Num(), 2);
    TestEqual(TEXT("...one championship"), Hawks.Championships, 1);
    TestTrue(TEXT("...a 1-2 record"), Hawks.Wins == 1 && Hawks.Losses == 2 && Hawks.Ties == 0);
    TestTrue(TEXT("...first, then second"), Hawks.Seasons.Num() == 2 && Hawks.Seasons[0].Finish == 1 && Hawks.Seasons[0].bChampion && Hawks.Seasons[1].Finish == 2 && !Hawks.Seasons[1].bChampion);
    TestTrue(TEXT("The franchise reads"), HasLine(History->DescribeFranchise(FName(TEXT("Hawks"))), TEXT("Hawks: 2 season(s), 1 championship(s), 1-2-0 all time"))
        && HasLine(History->DescribeFranchise(FName(TEXT("Hawks"))), TEXT("Season 1: 1-0-0, 1st, champion")));
    TestEqual(TEXT("A team never in the league has no history"), History->GetFranchiseHistory(FName(TEXT("Expansion"))).Seasons.Num(), 0);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Retirement and the hall of fame
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLegacyHallOfFameTest,
    "PlaySports.Legacy.RetirementAndHallOfFame",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLegacyHallOfFameTest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueHistoryTests;

    UPSLeagueHistory* History = MakeHistory();
    FPSLegacyTuning Tuning = History->GetTuning();
    Tuning.HallOfFame.WaitSeasons = 1;
    Tuning.HallOfFame.MinSeasons = 2;
    Tuning.HallOfFame.InductionScore = 1.f;
    Tuning.HallOfFame.MaxInducteesPerSeason = 1;
    Tuning.HallOfFame.Thresholds = { MakeThreshold(EPSStatCategory::PassingYards, 500), MakeThreshold(EPSStatCategory::Tackles, 2) };
    History->SetTuning(Tuning);
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    Stats->StartSeason(1);

    // Season 1: two games for the Hawks' quarterback (300 yards) and linebacker (3 tackles); a
    // one-season passer for the Bears throws for 1000; another Bears passer for 100.
    PlayLegacyGame(Stats, 1, TEXT("Hawks"), TEXT("Wolves"), { MakeLegacyPass(true, TEXT("HAW_QB"), TEXT("HAW_WR"), 150), MakeLegacyPass(false, TEXT("WLV_QB"), TEXT("WLV_WR"), 10, TEXT("HAW_LB")) });
    PlayLegacyGame(Stats, 2, TEXT("Hawks"), TEXT("Bears"), { MakeLegacyPass(true, TEXT("HAW_QB"), TEXT("HAW_WR"), 150), MakeLegacyPass(false, TEXT("BER_QB"), TEXT("BER_WR"), 1000, TEXT("HAW_LB")),
        MakeLegacyPass(false, TEXT("BER_QB2"), TEXT("BER_WR"), 100, TEXT("HAW_LB")) });
    Stats->EndSeason();
    History->ArchiveSeason(1, { MakeStanding(TEXT("Hawks"), 2, 0, 30, 10), MakeStanding(TEXT("Wolves"), 0, 1, 0, 7), MakeStanding(TEXT("Bears"), 0, 1, 3, 23) }, Stats);

    // Season 2: the quarterback is traded to the Wolves and plays one game there (300 yards); the
    // linebacker and the second Bears passer play on.
    PlayLegacyGame(Stats, 1, TEXT("Wolves"), TEXT("Bears"), { MakeLegacyPass(true, TEXT("HAW_QB"), TEXT("WLV_WR"), 300), MakeLegacyPass(false, TEXT("BER_QB2"), TEXT("BER_WR"), 100) });
    PlayLegacyGame(Stats, 2, TEXT("Hawks"), TEXT("Bears"), { MakeLegacyPass(false, TEXT("BER_QB2"), TEXT("BER_WR"), 10, TEXT("HAW_LB")) });
    Stats->EndSeason();
    History->ArchiveSeason(2, { MakeStanding(TEXT("Wolves"), 1, 0, 14, 7), MakeStanding(TEXT("Hawks"), 1, 0, 7, 3), MakeStanding(TEXT("Bears"), 0, 2, 10, 21) }, Stats);

    // They all retire after season 2.
    TestTrue(TEXT("The quarterback retires from the Wolves"), History->RecordRetirement(MakeRetiree(TEXT("HAW_QB"), EPlayerRole::Quarterback), FName(TEXT("Wolves")), 2, TEXT("Age 37"), Stats));
    TestFalse(TEXT("...once"), History->RecordRetirement(MakeRetiree(TEXT("HAW_QB"), EPlayerRole::Quarterback), FName(TEXT("Wolves")), 2, TEXT("Age 37"), Stats));
    History->RecordRetirement(MakeRetiree(TEXT("HAW_LB"), EPlayerRole::Linebacker), FName(TEXT("Hawks")), 2, TEXT("Age 34"), Stats);
    History->RecordRetirement(MakeRetiree(TEXT("BER_QB"), EPlayerRole::Quarterback), FName(TEXT("Bears")), 2, TEXT("Injuries"), Stats);
    History->RecordRetirement(MakeRetiree(TEXT("BER_QB2"), EPlayerRole::Quarterback), FName(TEXT("Bears")), 2, TEXT("Declining"), Stats);

    FPSRetiredPlayer Passer;
    if (TestTrue(TEXT("The quarterback is retired"), History->FindRetiredPlayer(FName(TEXT("HAW_QB")), Passer)))
    {
        TestEqual(TEXT("His career: 600 passing yards"), Passer.Career.PassingYards, 600);
        TestEqual(TEXT("...over two seasons"), Passer.Seasons, 2);
        TestEqual(TEXT("...retired from the Wolves"), Passer.LastTeamId, FName(TEXT("Wolves")));
        TestEqual(TEXT("...a Hawk above all: two of his three games"), Passer.PrimaryTeamId, FName(TEXT("Hawks")));
        TestTrue(TEXT("...his hall score: 600 of 500"), FMath::IsNearlyEqual(Passer.HallScore, 1.2f, 1e-4f));
    }
    FPSSeasonArchive Second;
    History->FindSeason(2, Second);
    TestEqual(TEXT("Season 2's archive lists the four retirements"), Second.Retired.Num(), 4);
    TestTrue(TEXT("...with their reasons"), HasLine(History->DescribeSeason(2), TEXT("Retired: HAW_QB (Wolves): Age 37")));

    // The vote: nobody in his first season out; then the best career first, one a season.
    TestEqual(TEXT("After season 2 nobody has waited a season"), History->RunHallOfFameVote(2).Num(), 0);
    History->ArchiveSeason(3, { MakeStanding(TEXT("Hawks"), 1, 0, 7, 0) }, Stats);
    const TArray<FPSRetiredPlayer> ClassOf3 = History->RunHallOfFameVote(3);
    if (TestEqual(TEXT("After season 3, one goes in"), ClassOf3.Num(), 1))
    {
        TestEqual(TEXT("...the linebacker: 4 tackles of 2, the best score"), ClassOf3[0].Player.PlayerId, FName(TEXT("HAW_LB")));
    }
    const TArray<FPSRetiredPlayer> ClassOf4 = History->RunHallOfFameVote(4);
    if (TestEqual(TEXT("After season 4, the next"), ClassOf4.Num(), 1))
    {
        TestEqual(TEXT("...the quarterback"), ClassOf4[0].Player.PlayerId, FName(TEXT("HAW_QB")));
        TestEqual(TEXT("...voted in after season 4"), ClassOf4[0].InductedAfterSeason, 4);
    }
    TestEqual(TEXT("After season 5, nobody is left"), History->RunHallOfFameVote(5).Num(), 0);
    FPSRetiredPlayer OneSeason;
    History->FindRetiredPlayer(FName(TEXT("BER_QB")), OneSeason);
    TestTrue(TEXT("A single season of 1000 yards isn't enough seasons"), OneSeason.HallScore >= 1.f && !OneSeason.bHallOfFame);
    FPSRetiredPlayer Journeyman;
    History->FindRetiredPlayer(FName(TEXT("BER_QB2")), Journeyman);
    TestTrue(TEXT("Two seasons of 210 yards isn't enough of a career"), Journeyman.Seasons == 2 && Journeyman.HallScore < 1.f && !Journeyman.bHallOfFame);

    const TArray<FPSRetiredPlayer> Hall = History->GetHallOfFame();
    TestTrue(TEXT("The hall of fame, in the order they went in"), Hall.Num() == 2 && Hall[0].Player.PlayerId == FName(TEXT("HAW_LB")) && Hall[1].Player.PlayerId == FName(TEXT("HAW_QB")));
    FPSSeasonArchive Third;
    History->FindSeason(3, Third);
    TestTrue(TEXT("Season 3's archive keeps its class"), Third.Inducted.Num() == 1 && Third.Inducted[0] == FName(TEXT("HAW_LB")));

    // Hall of famers are the legends of the team they played the most for.
    const FPSFranchiseHistory Hawks = History->GetFranchiseHistory(FName(TEXT("Hawks")));
    TestEqual(TEXT("Both are Hawks legends"), Hawks.Legends.Num(), 2);
    TestTrue(TEXT("...the best score first"), Hawks.Legends.Num() == 2 && Hawks.Legends[0].Player.PlayerId == FName(TEXT("HAW_LB")));
    TestEqual(TEXT("The Wolves, where he finished, have none"), History->GetFranchiseHistory(FName(TEXT("Wolves"))).Legends.Num(), 0);
    TestTrue(TEXT("The franchise reads its legends"), HasLine(History->DescribeFranchise(FName(TEXT("Hawks"))), TEXT("Legend: HAW_QB (hall of fame after season 4)")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The franchise and the save
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLegacyFranchiseTest,
    "PlaySports.Legacy.FranchiseFlowAndSave",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLegacyFranchiseTest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueHistoryTests;

    UPSScheduleEngine* Schedule = NewObject<UPSScheduleEngine>();
    const TArray<FName> TeamIds = { FName(TEXT("Falcons")), FName(TEXT("Hawks")), FName(TEXT("Wolves")), FName(TEXT("Bears")) };
    UPSStaffManager* Staffs = NewObject<UPSStaffManager>();
    Staffs->LoadFromJson(UPSStaffManager::GetDefaultDataPath());
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    UPSLeagueHistory* History = MakeHistory();
    UPSFranchiseFlow* Flow = NewObject<UPSFranchiseFlow>();

    // Two one-week seasons, the statistics and the history carried from one to the next.
    for (int32 SeasonNumber = 1; SeasonNumber <= 2; ++SeasonNumber)
    {
        UPSFranchiseSeason* Season = NewObject<UPSFranchiseSeason>();
        Season->InitializeSeason(TeamIds, Schedule->GenerateSeasonSchedule(FDateTime(2026, 9, 1), 1, TArray<int32>()));
        Flow->Initialize(Season, Staffs, FName(TEXT("Falcons")));
        if (SeasonNumber == 1)
        {
            TestEqual(TEXT("Every team has a roster"), Flow->LoadLeagueRosters(UPSUITeamCatalog::GetDefaultTeamsPath()), TeamIds.Num());
            Flow->SetStats(Stats);
            Flow->SetLeagueHistory(History);
        }
        TestEqual(*FString::Printf(TEXT("Season %d's games"), SeasonNumber), Flow->SimulateWeek(true), 2);
        TestTrue(*FString::Printf(TEXT("Season %d ends"), SeasonNumber), Flow->AdvanceWeek());

        FPSSeasonArchive Archive;
        if (TestTrue(*FString::Printf(TEXT("Season %d is in the archive"), SeasonNumber), History->FindSeason(SeasonNumber, Archive)))
        {
            const TArray<FPSTeamStanding> Final = Season->GetSortedStandings();
            TestEqual(*FString::Printf(TEXT("Season %d: every team's standing"), SeasonNumber), Archive.Standings.Num(), TeamIds.Num());
            TestEqual(*FString::Printf(TEXT("Season %d: the champion finished first"), SeasonNumber), Archive.ChampionTeamId, Final[0].TeamId);
            const FPSSeasonLeader* Passing = FindLeader(Archive, EPSStatCategory::PassingYards);
            const TArray<FPSLeaderEntry> Expected = Stats->GetLeaders(EPSStatCategory::PassingYards, EPSStatScope::Season, SeasonNumber, 1);
            TestTrue(*FString::Printf(TEXT("Season %d: its passing leader, from the statistics"), SeasonNumber),
                Passing && Expected.Num() > 0 && Passing->PlayerId == Expected[0].Id && Passing->Value == Expected[0].Value);
        }
    }
    TestEqual(TEXT("Two seasons archived"), History->GetSeasons().Num(), 2);
    TestEqual(TEXT("The Hawks' history has both"), History->GetFranchiseHistory(FName(TEXT("Hawks"))).Seasons.Num(), 2);

    // The history round-trips through the franchise save.
    History->RecordRetirement(Flow->GetTeamRoster(FName(TEXT("Hawks")))->GetFullRoster()[0], FName(TEXT("Hawks")), 2, TEXT("Age 38"), Stats);
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    History->SaveTo(Save);
    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    const FString Slot = TEXT("Test_LeagueHistory");
    TestTrue(TEXT("The franchise saves"), Saves->SaveToSlot(Save, Slot));
    const UPSFranchiseSaveGame* Loaded = Cast<UPSFranchiseSaveGame>(Saves->LoadFromSlot(Slot));
    UPSLeagueHistory* Restored = MakeHistory();
    if (TestNotNull(TEXT("...and loads"), Loaded) && TestTrue(TEXT("The history loads"), Restored->LoadFrom(Loaded)))
    {
        TestEqual(TEXT("Both seasons"), Restored->GetSeasons().Num(), 2);
        TestEqual(TEXT("The retiree"), Restored->GetRetiredPlayers().Num(), 1);
        FPSSeasonArchive Before;
        FPSSeasonArchive After;
        History->FindSeason(1, Before);
        Restored->FindSeason(1, After);
        TestTrue(TEXT("Season 1's champion and leaders"), After.ChampionTeamId == Before.ChampionTeamId && After.Leaders.Num() == Before.Leaders.Num());
    }
    IFileManager::Get().Delete(*UPSSaveSubsystem::GetSlotPath(Slot), false, true, true);
    IFileManager::Get().Delete(*(UPSSaveSubsystem::GetSlotPath(Slot) + TEXT(".bak")), false, true, true);
    TestFalse(TEXT("A save from before the league's history keeps the current one"), Restored->LoadFrom(NewObject<UPSFranchiseSaveGame>()));
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Awards in the hall score (Epic 93)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLegacyAwardsTest,
    "PlaySports.Legacy.AwardsInHallScore",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLegacyAwardsTest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueHistoryTests;

    UPSLeagueHistory* History = MakeHistory();
    FPSLegacyTuning Tuning = History->GetTuning();
    Tuning.HallOfFame.WaitSeasons = 0;
    Tuning.HallOfFame.MinSeasons = 0;
    Tuning.HallOfFame.InductionScore = 0.5f;
    Tuning.HallOfFame.MaxInducteesPerSeason = 3;
    History->SetTuning(Tuning);
    auto ScoreOf = [&Tuning](EPSAwardKind Award)
    {
        const FPSHallOfFameAward* Entry = Tuning.HallOfFame.AwardScores.FindByPredicate([Award](const FPSHallOfFameAward& Candidate) { return Candidate.Award == Award; });
        return Entry ? Entry->Score : 0.f;
    };
    const float MvpScore = ScoreOf(EPSAwardKind::MostValuablePlayer);
    const float WeekScore = ScoreOf(EPSAwardKind::OffensivePlayerOfWeek);
    TestTrue(TEXT("The shipped tuning scores an MVP enough for the hall here"), MvpScore >= Tuning.HallOfFame.InductionScore);
    TestTrue(TEXT("...and two weekly honors not"), WeekScore * 2.f < Tuning.HallOfFame.InductionScore);

    // The award record (Epic 93): an MVP for one quarterback, two weekly honors for another.
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    auto Give = [Save](EPSAwardKind Award, const TCHAR* PlayerId, int32 Week)
    {
        FPSAwardRecord& Given = Save->Narrative.Awards.AddDefaulted_GetRef();
        Given.Award = Award;
        Given.Season = 1;
        Given.Week = Week;
        Given.PlayerId = FName(PlayerId);
        Given.TeamId = FName(TEXT("Hawks"));
    };
    Give(EPSAwardKind::MostValuablePlayer, TEXT("MVP_QB"), 0);
    Give(EPSAwardKind::OffensivePlayerOfWeek, TEXT("WEEK_QB"), 1);
    Give(EPSAwardKind::OffensivePlayerOfWeek, TEXT("WEEK_QB"), 2);
    UPSLeagueNarrative* Narrative = NewObject<UPSLeagueNarrative>();
    TestTrue(TEXT("The award record loads"), Narrative->LoadFrom(Save));

    TestEqual(TEXT("Without the record awards add nothing"), History->GetAwardScore(FName(TEXT("MVP_QB"))), 0.f);
    History->SetAwards(Narrative);
    TestEqual(TEXT("An MVP adds its score"), History->GetAwardScore(FName(TEXT("MVP_QB"))), MvpScore, 1e-4f);
    TestEqual(TEXT("Two weekly honors add theirs twice"), History->GetAwardScore(FName(TEXT("WEEK_QB"))), 2.f * WeekScore, 1e-4f);
    TestEqual(TEXT("No awards, nothing"), History->GetAwardScore(FName(TEXT("PLAIN_QB"))), 0.f);

    // Three retire with no statistics: their awards are their hall scores.
    for (const TCHAR* PlayerId : { TEXT("MVP_QB"), TEXT("WEEK_QB"), TEXT("PLAIN_QB") })
    {
        TestTrue(*FString::Printf(TEXT("%s retires"), PlayerId), History->RecordRetirement(MakeRetiree(PlayerId, EPlayerRole::Quarterback), FName(TEXT("Hawks")), 1, TEXT("Age 36"), nullptr));
    }
    FPSRetiredPlayer Mvp;
    if (TestTrue(TEXT("The MVP is retired"), History->FindRetiredPlayer(FName(TEXT("MVP_QB")), Mvp)))
    {
        TestEqual(TEXT("...his awards' score"), Mvp.AwardScore, MvpScore, 1e-4f);
        TestEqual(TEXT("...is his hall score"), Mvp.HallScore, MvpScore, 1e-4f);
    }
    const TArray<FPSRetiredPlayer> Inducted = History->RunHallOfFameVote(1);
    TestTrue(TEXT("The MVP alone is voted in"), Inducted.Num() == 1 && Inducted[0].Player.PlayerId == FName(TEXT("MVP_QB")));

    FPSLegacyTuning Broken = Tuning;
    Broken.HallOfFame.AwardScores.Add(Broken.HallOfFame.AwardScores[0]);
    TestTrue(TEXT("An award listed twice is reported"), UPSLeagueHistory::ValidateTuning(Broken).ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("listed twice")); }));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
