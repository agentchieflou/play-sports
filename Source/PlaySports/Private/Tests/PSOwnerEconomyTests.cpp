// PSOwnerEconomyTests.cpp -- Epic 95 (owner mode and league economics)
//
// Tests covered:
//   1. Data/owner_economics.json loads through UPSDataIngestion, validates clean and equals
//      FPSEconomyTuning's defaults field by field; a broken tuning's problems are each reported.
//   2. The gate: a crowd from the record, the fans and the price, held to capacity and the floor;
//      prices held to the owner's range; a game's gate and concessions; wins, losses and a high
//      price moving the fans.
//   3. The books at the season's end: the media share, payroll from the contract manager, budget
//      and profit; winning and losing seasons; relocation pressure after losing seasons with fans
//      who gave up, lifted by a winning one; budgets within the limit and the funding they buy.
//   4. The franchise: a season simulated through UPSFranchiseFlow fills each home team's gate and
//      closes the books; the economy round-trips through the franchise save.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "PSContractManager.h"
#include "PSDataIngestion.h"
#include "PSEconomyData.h"
#include "PSFranchiseFlow.h"
#include "PSFranchiseSaveGame.h"
#include "PSFranchiseSeason.h"
#include "PSOwnerEconomy.h"
#include "PSSaveSubsystem.h"
#include "PSScheduleEngine.h"
#include "PSStaffManager.h"
#include "PSUITeamCatalog.h"
#include "Engine/GameInstance.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSOwnerEconomyTests
{
    static UPSOwnerEconomy* MakeEconomy()
    {
        UPSOwnerEconomy* Economy = NewObject<UPSOwnerEconomy>();
        Economy->LoadTuningFromJson(UPSOwnerEconomy::GetDefaultTuningPath());
        return Economy;
    }

    static FPSTeamEconomy TeamOf(const UPSOwnerEconomy* Economy, const TCHAR* TeamId)
    {
        FPSTeamEconomy Team;
        Economy->GetTeam(FName(TeamId), Team);
        return Team;
    }

    static FPSTeamStanding MakeRecord(const TCHAR* TeamId, int32 Wins, int32 Losses)
    {
        FPSTeamStanding Standing;
        Standing.TeamId = FName(TeamId);
        Standing.Wins = Wins;
        Standing.Losses = Losses;
        return Standing;
    }

    static const FPSEconomySeasonReport* FindReport(const TArray<FPSEconomySeasonReport>& Reports, const TCHAR* TeamId)
    {
        const FName Id(TeamId);
        return Reports.FindByPredicate([Id](const FPSEconomySeasonReport& Report) { return Report.TeamId == Id; });
    }

    static bool HasLine(const TArray<FString>& Lines, const TCHAR* Fragment)
    {
        return Lines.ContainsByPredicate([Fragment](const FString& Line) { return Line.Contains(Fragment); });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The shipped tuning
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSEconomyTuningTest,
    "PlaySports.Economy.TuningLoadsAndValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSEconomyTuningTest::RunTest(const FString& Parameters)
{
    using namespace PSOwnerEconomyTests;

    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSEconomyTuning FromFile;
    if (!TestTrue(TEXT("Data/owner_economics.json loads"), Ingestion->LoadEconomyTuningFromJson(UPSOwnerEconomy::GetDefaultTuningPath(), FromFile)))
    {
        return false;
    }
    const TArray<FString> Problems = UPSOwnerEconomy::ValidateTuning(FromFile);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    TestEqual(TEXT("The shipped tuning is sound"), Problems.Num(), 0);

    const FPSEconomyTuning Defaults;
    for (TFieldIterator<FProperty> It(FPSEconomyTuning::StaticStruct()); It; ++It)
    {
        TestTrue(*FString::Printf(TEXT("%s matches the default"), *It->GetName()), It->Identical_InContainer(&FromFile, &Defaults));
    }

    FPSEconomyTuning Broken = FromFile;
    Broken.MinTicketPrice = Broken.MaxTicketPrice + 1.f;
    Broken.MinFillRate = 0.9f;
    Broken.DefaultBudget.StaffFraction = 0.5f;
    Broken.RelocationLosingSeasons = 0;
    const TArray<FString> BrokenProblems = UPSOwnerEconomy::ValidateTuning(Broken);
    TestTrue(TEXT("Prices out of order"), HasLine(BrokenProblems, TEXT("MinTicketPrice")));
    TestTrue(TEXT("Fill rates out of order"), HasLine(BrokenProblems, TEXT("MinFillRate")));
    TestTrue(TEXT("A budget over the limit"), HasLine(BrokenProblems, TEXT("DefaultBudget")));
    TestTrue(TEXT("No losing seasons to relocate"), HasLine(BrokenProblems, TEXT("RelocationLosingSeasons")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The gate and the fans
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSEconomyGateTest,
    "PlaySports.Economy.GateAndRevenue",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSEconomyGateTest::RunTest(const FString& Parameters)
{
    using namespace PSOwnerEconomyTests;

    UPSOwnerEconomy* Economy = MakeEconomy();
    const FName Hawks(TEXT("Hawks"));
    const FName Wolves(TEXT("Wolves"));
    Economy->RegisterTeam(Hawks);
    const FPSTeamEconomy Fresh = TeamOf(Economy, TEXT("Hawks"));
    TestEqual(TEXT("A new team charges the base price"), Fresh.TicketPrice, 110.f);
    TestEqual(TEXT("...its fans start at 0.6"), Fresh.FanSatisfaction, 0.6f);
    TestEqual(TEXT("...on the default budget"), Fresh.Budget.GetTotal(), Economy->GetTuning().DefaultBudget.GetTotal());

    // The crowd: 88% full for a .500 team (85% base, +3% for fans at 0.6).
    TestEqual(TEXT(".500: 59,840"), Economy->PredictAttendance(Hawks, 0.5f), 59840);
    TestEqual(TEXT("Unbeaten: a sellout"), Economy->PredictAttendance(Hawks, 1.f), 68000);
    TestEqual(TEXT("Winless: 68%"), Economy->PredictAttendance(Hawks, 0.f), 46240);
    TestEqual(TEXT("Prices held to the owner's range: the most"), Economy->SetTicketPrice(Hawks, 1000.f), 300.f);
    TestEqual(TEXT("...and the least"), Economy->SetTicketPrice(Hawks, 1.f), 40.f);
    Economy->SetTicketPrice(Hawks, 165.f);
    TestEqual(TEXT("Half again the base price: 25 points fewer fill"), Economy->PredictAttendance(Hawks, 0.5f), 42840);
    Economy->SetTicketPrice(Hawks, 300.f);
    TestEqual(TEXT("Never under the floor"), Economy->PredictAttendance(Hawks, 0.f), FMath::RoundToInt(68000 * 0.25f));
    Economy->SetTicketPrice(Hawks, 110.f);

    // A home win at the base price: the gate, the concessions, the fans.
    const FPSGameGate Gate = Economy->RecordGame(Hawks, Wolves, 24, 17, 0.5f);
    TestEqual(TEXT("The crowd"), Gate.Attendance, 59840);
    TestEqual(TEXT("The gate: 59,840 at $110"), Gate.GateRevenue, 6582);
    TestEqual(TEXT("Concessions: $35 a fan"), Gate.ConcessionRevenue, 2094);
    const FPSTeamEconomy AfterWin = TeamOf(Economy, TEXT("Hawks"));
    TestTrue(TEXT("The home team's season takings"), AfterWin.HomeGames == 1 && AfterWin.Attendance == 59840 && AfterWin.GateRevenue == 6582 && AfterWin.ConcessionRevenue == 2094);
    TestTrue(TEXT("The winners' fans: +0.02"), FMath::IsNearlyEqual(AfterWin.FanSatisfaction, 0.62f, 1e-4f));
    TestTrue(TEXT("The losers' fans: -0.03"), FMath::IsNearlyEqual(TeamOf(Economy, TEXT("Wolves")).FanSatisfaction, 0.57f, 1e-4f));
    TestEqual(TEXT("The visitors take no gate"), TeamOf(Economy, TEXT("Wolves")).HomeGames, 0);

    // A home loss at double the base price stings twice.
    Economy->SetTicketPrice(Wolves, 220.f);
    Economy->RecordGame(Wolves, Hawks, 10, 20, 0.f);
    TestTrue(TEXT("Double price and a loss: -0.05"), FMath::IsNearlyEqual(TeamOf(Economy, TEXT("Wolves")).FanSatisfaction, 0.52f, 1e-4f));
    TestTrue(TEXT("A tie moves nobody"), [&]()
    {
        const float Before = TeamOf(Economy, TEXT("Hawks")).FanSatisfaction;
        Economy->RecordGame(Hawks, Wolves, 14, 14, 0.5f);
        return FMath::IsNearlyEqual(TeamOf(Economy, TEXT("Hawks")).FanSatisfaction, Before, 1e-6f);
    }());
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The books, the budget and relocation pressure
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSEconomySeasonTest,
    "PlaySports.Economy.SeasonBooksBudgetsAndRelocation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSEconomySeasonTest::RunTest(const FString& Parameters)
{
    using namespace PSOwnerEconomyTests;

    UPSOwnerEconomy* Economy = MakeEconomy();
    const FName Hawks(TEXT("Hawks"));
    const FName Wolves(TEXT("Wolves"));
    UPSContractManager* Contracts = NewObject<UPSContractManager>();
    Contracts->LoadTuningFromJson(UPSContractManager::GetDefaultTuningPath());
    Contracts->StartLeague();
    FPSContractOffer Offer;
    Offer.TeamId = Hawks;
    Offer.Years = 2;
    Offer.AnnualValue = 20000;
    TestEqual(TEXT("The Hawks' payroll"), Contracts->SignContract(Contracts->MakeContract(FName(TEXT("HAW_QB")), Offer)), EPSCapResult::Ok);

    // Before any season, a budget is a share of the media money.
    Economy->RegisterTeam(Hawks);
    Economy->RegisterTeam(Wolves);
    TestEqual(TEXT("Scouting before a first season: 2% of 300,000"), Economy->GetDepartmentFunding(Hawks, EPSBudgetDepartment::Scouting), 6000);
    TestTrue(TEXT("Equal budgets fund equally"), FMath::IsNearlyEqual(Economy->GetFundingIndex(Hawks, EPSBudgetDepartment::Scouting), 1.f, 1e-4f));

    // A season: the Hawks win at home, the Wolves lose.
    const FPSGameGate HawksGate = Economy->RecordGame(Hawks, Wolves, 31, 10, 0.5f);
    const FPSGameGate WolvesGate = Economy->RecordGame(Wolves, Hawks, 3, 27, 0.f);
    const TArray<FPSEconomySeasonReport> Reports = Economy->EndSeason({ MakeRecord(TEXT("Hawks"), 10, 7), MakeRecord(TEXT("Wolves"), 3, 14) }, Contracts);
    const FPSEconomySeasonReport* HawksBooks = FindReport(Reports, TEXT("Hawks"));
    if (TestNotNull(TEXT("The Hawks' books"), HawksBooks))
    {
        TestEqual(TEXT("Gate"), HawksBooks->GateRevenue, HawksGate.GateRevenue);
        TestEqual(TEXT("Concessions"), HawksBooks->ConcessionRevenue, HawksGate.ConcessionRevenue);
        TestEqual(TEXT("The media share"), HawksBooks->MediaRevenue, 300000);
        TestEqual(TEXT("Revenue: all three"), HawksBooks->Revenue, HawksGate.GateRevenue + HawksGate.ConcessionRevenue + 300000);
        TestEqual(TEXT("Payroll: the cap the contracts used"), HawksBooks->Payroll, 20000);
        TestEqual(TEXT("Budget: 8% of revenue"), HawksBooks->BudgetSpend, FMath::RoundToInt(Economy->GetTuning().DefaultBudget.GetTotal() * HawksBooks->Revenue));
        TestEqual(TEXT("Profit"), HawksBooks->Profit, HawksBooks->Revenue - HawksBooks->Payroll - HawksBooks->BudgetSpend);
        TestEqual(TEXT("The average crowd"), HawksBooks->AverageAttendance, HawksGate.Attendance);
        TestFalse(TEXT("A winning team isn't under pressure"), HawksBooks->bRelocationPressure);
    }
    const FPSTeamEconomy HawksAfter = TeamOf(Economy, TEXT("Hawks"));
    TestTrue(TEXT("A winning season lifts the fans"), FMath::IsNearlyEqual(HawksAfter.FanSatisfaction, 0.69f, 1e-4f));
    TestTrue(TEXT("The season's takings are banked"), HawksAfter.HomeGames == 0 && HawksAfter.GateRevenue == 0 && HawksBooks && HawksAfter.LastSeasonRevenue == HawksBooks->Revenue);
    const FPSTeamEconomy WolvesAfter = TeamOf(Economy, TEXT("Wolves"));
    TestTrue(TEXT("A losing season sinks them"), FMath::IsNearlyEqual(WolvesAfter.FanSatisfaction, 0.46f, 1e-4f));
    TestEqual(TEXT("One losing season"), WolvesAfter.ConsecutiveLosingSeasons, 1);
    TestTrue(TEXT("The Wolves' gate"), FindReport(Reports, TEXT("Wolves")) && FindReport(Reports, TEXT("Wolves"))->GateRevenue == WolvesGate.GateRevenue);

    // Budgets: within the limit only; a bigger budget buys more than the league's average.
    FPSTeamBudget Rich;
    Rich.ScoutingFraction = 0.05f;
    Rich.TrainingFraction = 0.05f;
    Rich.StaffFraction = 0.05f;
    TestTrue(TEXT("15% of revenue is allowed"), Economy->SetBudget(Hawks, Rich));
    FPSTeamBudget TooRich = Rich;
    TooRich.StaffFraction = 0.2f;
    TestFalse(TEXT("More is not"), Economy->SetBudget(Hawks, TooRich));
    FPSTeamBudget Negative;
    Negative.ScoutingFraction = -0.01f;
    TestFalse(TEXT("Nor a negative share"), Economy->SetBudget(Hawks, Negative));
    TestEqual(TEXT("Scouting: 5% of last season's revenue"), Economy->GetDepartmentFunding(Hawks, EPSBudgetDepartment::Scouting), FMath::RoundToInt(0.05f * HawksAfter.LastSeasonRevenue));
    TestTrue(TEXT("The Hawks out-fund the league"), Economy->GetFundingIndex(Hawks, EPSBudgetDepartment::Scouting) > 1.f);
    TestTrue(TEXT("...and the Wolves fall behind"), Economy->GetFundingIndex(Wolves, EPSBudgetDepartment::Scouting) < 1.f);

    // Relocation pressure: three losing seasons in a row with fans who have given up.
    TArray<FPSEconomySeasonReport> Pressure;
    for (int32 Losing = 2; Losing <= 3; ++Losing)
    {
        for (int32 Game = 0; Game < 5; ++Game)
        {
            Economy->RecordGame(Wolves, Hawks, 0, 21, 0.f);
        }
        Pressure = Economy->EndSeason({ MakeRecord(TEXT("Hawks"), 12, 5), MakeRecord(TEXT("Wolves"), 2, 15) }, nullptr);
        const FPSEconomySeasonReport* Wolf = FindReport(Pressure, TEXT("Wolves"));
        TestTrue(*FString::Printf(TEXT("Losing season %d: pressure only from the third"), Losing), Wolf && Wolf->bRelocationPressure == (Losing == 3));
        TestTrue(*FString::Printf(TEXT("Losing season %d: it starts once"), Losing), Wolf && Wolf->bRelocationPressureStarted == (Losing == 3));
    }
    TestTrue(TEXT("The fans have given up"), TeamOf(Economy, TEXT("Wolves")).FanSatisfaction < Economy->GetTuning().RelocationSatisfactionThreshold);
    TestEqual(TEXT("Three losing seasons"), TeamOf(Economy, TEXT("Wolves")).ConsecutiveLosingSeasons, 3);
    const TArray<FPSEconomySeasonReport> Turnaround = Economy->EndSeason({ MakeRecord(TEXT("Hawks"), 8, 9), MakeRecord(TEXT("Wolves"), 11, 6) }, nullptr);
    TestTrue(TEXT("A winning season lifts the pressure"), FindReport(Turnaround, TEXT("Wolves")) && !FindReport(Turnaround, TEXT("Wolves"))->bRelocationPressure);
    TestEqual(TEXT("...and resets the streak"), TeamOf(Economy, TEXT("Wolves")).ConsecutiveLosingSeasons, 0);
    TestEqual(TEXT("Four seasons on the books"), Economy->GetLeague().SeasonsCompleted, 4);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The franchise and the save
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSEconomyFranchiseTest,
    "PlaySports.Economy.FranchiseFlowAndSave",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSEconomyFranchiseTest::RunTest(const FString& Parameters)
{
    using namespace PSOwnerEconomyTests;

    UPSScheduleEngine* Schedule = NewObject<UPSScheduleEngine>();
    UPSFranchiseSeason* Season = NewObject<UPSFranchiseSeason>();
    const TArray<FName> TeamIds = { FName(TEXT("Falcons")), FName(TEXT("Hawks")), FName(TEXT("Wolves")), FName(TEXT("Bears")) };
    Season->InitializeSeason(TeamIds, Schedule->GenerateSeasonSchedule(FDateTime(2026, 9, 1), 1, TArray<int32>()));
    UPSStaffManager* Staffs = NewObject<UPSStaffManager>();
    Staffs->LoadFromJson(UPSStaffManager::GetDefaultDataPath());
    UPSFranchiseFlow* Flow = NewObject<UPSFranchiseFlow>();
    Flow->Initialize(Season, Staffs, FName(TEXT("Falcons")));
    Flow->LoadLeagueRosters(UPSUITeamCatalog::GetDefaultTeamsPath());
    UPSOwnerEconomy* Economy = MakeEconomy();
    Flow->SetEconomy(Economy);

    TestEqual(TEXT("Week 1's two games"), Flow->SimulateWeek(true), 2);
    for (const FPSWeekMatchup& Matchup : Season->GetMatchupsForWeek(1))
    {
        const FPSTeamEconomy Home = TeamOf(Economy, *Matchup.HomeTeamId.ToString());
        TestTrue(*FString::Printf(TEXT("%s drew a crowd at home"), *Matchup.HomeTeamId.ToString()), Home.HomeGames == 1 && Home.Attendance > 0 && Home.GateRevenue > 0);
        TestEqual(*FString::Printf(TEXT("%s played away"), *Matchup.AwayTeamId.ToString()), TeamOf(Economy, *Matchup.AwayTeamId.ToString()).HomeGames, 0);
    }

    TestTrue(TEXT("The season ends"), Flow->AdvanceWeek());
    const TArray<FPSEconomySeasonReport>& Reports = Flow->GetEconomyReports();
    TestEqual(TEXT("Every team's books"), Reports.Num(), TeamIds.Num());
    for (const FPSEconomySeasonReport& Report : Reports)
    {
        TestEqual(*FString::Printf(TEXT("%s: revenue is gate, concessions and media"), *Report.TeamId.ToString()),
            Report.Revenue, Report.GateRevenue + Report.ConcessionRevenue + Report.MediaRevenue);
        TestEqual(*FString::Printf(TEXT("%s: no contracts, no payroll"), *Report.TeamId.ToString()), Report.Payroll, 0);
    }

    // The economy round-trips through the franchise save.
    Economy->SetTicketPrice(FName(TEXT("Bears")), 150.f);
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    Economy->SaveTo(Save);
    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    const FString Slot = TEXT("Test_OwnerEconomy");
    TestTrue(TEXT("The franchise saves"), Saves->SaveToSlot(Save, Slot));
    const UPSFranchiseSaveGame* Loaded = Cast<UPSFranchiseSaveGame>(Saves->LoadFromSlot(Slot));
    UPSOwnerEconomy* Restored = MakeEconomy();
    if (TestNotNull(TEXT("...and loads"), Loaded) && TestTrue(TEXT("The economy loads"), Restored->LoadFrom(Loaded)))
    {
        TestEqual(TEXT("A season on the books"), Restored->GetLeague().SeasonsCompleted, 1);
        TestEqual(TEXT("The Bears' price"), TeamOf(Restored, TEXT("Bears")).TicketPrice, 150.f);
        for (const FName& TeamId : TeamIds)
        {
            TestEqual(*FString::Printf(TEXT("%s's revenue"), *TeamId.ToString()), TeamOf(Restored, *TeamId.ToString()).LastSeasonRevenue, TeamOf(Economy, *TeamId.ToString()).LastSeasonRevenue);
        }
    }
    IFileManager::Get().Delete(*UPSSaveSubsystem::GetSlotPath(Slot), false, true, true);
    IFileManager::Get().Delete(*(UPSSaveSubsystem::GetSlotPath(Slot) + TEXT(".bak")), false, true, true);
    TestFalse(TEXT("A save from before owner mode keeps the current economy"), Restored->LoadFrom(NewObject<UPSFranchiseSaveGame>()));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
