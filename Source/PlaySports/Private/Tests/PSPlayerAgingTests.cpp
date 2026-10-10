// PSPlayerAgingTests.cpp -- Epic 94 (aging curves per role and retirements)
//
// Tests covered:
//   1. Aging: each role ages on its own curve (Core 19's progression at the role's ages), a role
//      without one on Core 19's; a buried backup grows at half; everyone is a year older; a player
//      of unknown age is left alone without a contract manager and takes its default age with one.
//   2. Retirement: the chance from age, rating, injury and morale, and its biggest reason; at
//      ForcedAge a certainty. An off-season retires the forced and caps the rest at
//      MaxRetirementShare of the roster; retirees leave the roster, their contracts end (no dead
//      money for guarantees they forfeit), the
//      league's history records them; an injured veteran (Epic 90) retires as the tuning says; the
//      same season retires the same players.
//   3. The franchise: at the season's end a too-old veteran retires into the history and everyone
//      else on every roster is a year older.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSContractData.h"
#include "PSContractManager.h"
#include "PSDataIngestion.h"
#include "PSFranchiseFlow.h"
#include "PSFranchiseSaveGame.h"
#include "PSFranchiseSeason.h"
#include "PSFreeAgency.h"
#include "PSLeagueHistory.h"
#include "PSLegacyData.h"
#include "PSPlayerAging.h"
#include "PSPlayerProgression.h"
#include "PSRoster.h"
#include "PSScheduleEngine.h"
#include "PSStaffManager.h"
#include "PSStatsEngine.h"
#include "PSTrainingData.h"
#include "PSUITeamCatalog.h"
#include "PSWeeklyPreparation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPlayerAgingTests
{
    static UPSPlayerAging* MakeAging()
    {
        UPSPlayerAging* Aging = NewObject<UPSPlayerAging>();
        Aging->LoadDefaults();
        return Aging;
    }

    static FPlayerAttributes MakeAgedPlayer(const FString& PlayerId, EPlayerRole Role, int32 Age, float Rating = 70.f)
    {
        FPlayerAttributes Player;
        Player.PlayerId = FName(*PlayerId);
        Player.DisplayName = PlayerId;
        Player.Role = Role;
        Player.WeightKg = 100.f;
        Player.HeightCm = 188.f;
        Player.Speed = Rating;
        Player.Agility = Rating;
        Player.Strength = Rating;
        Player.Acceleration = Rating;
        Player.Awareness = Rating;
        Player.Stamina = Rating;
        Player.Age = Age;
        return Player;
    }

    static UPSRoster* MakeAgedRoster(const TArray<FPlayerAttributes>& Players)
    {
        UPSRoster* Roster = NewObject<UPSRoster>();
        Roster->InitializeRoster(Players);
        Roster->BuildDefaultDepthChart();
        return Roster;
    }

    static FPlayerAttributes RowOf(const UPSRoster* Roster, const TCHAR* PlayerId)
    {
        FPlayerAttributes Row;
        Roster->FindPlayerById(FName(PlayerId), Row);
        return Row;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Aging curves per role
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSAgingCurvesTest,
    "PlaySports.Legacy.AgingCurvesPerRole",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSAgingCurvesTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayerAgingTests;

    UPSPlayerAging* Aging = NewObject<UPSPlayerAging>();
    TestTrue(TEXT("The legacy tuning and Core 19's curve load"), Aging->LoadDefaults());
    TestEqual(TEXT("Every role has its curve"), Aging->GetTuning().RoleCurves.Num(), 8);
    const FPSProgressionTuning Back = Aging->GetCurve(EPlayerRole::RunningBack);
    const FPSProgressionTuning Passer = Aging->GetCurve(EPlayerRole::Quarterback);
    TestTrue(TEXT("A running back peaks before a quarterback"), Back.PeakAgeEnd < Passer.PeakAgeStart);

    // Roster order is the depth chart: QB_YOUNG starts, the sixth receiver is buried.
    TArray<FPlayerAttributes> Players = { MakeAgedPlayer(TEXT("QB_YOUNG"), EPlayerRole::Quarterback, 24), MakeAgedPlayer(TEXT("QB_PRIME"), EPlayerRole::Quarterback, 29),
        MakeAgedPlayer(TEXT("RB_PRIME"), EPlayerRole::RunningBack, 24), MakeAgedPlayer(TEXT("RB_OLD"), EPlayerRole::RunningBack, 29),
        MakeAgedPlayer(TEXT("NO_AGE"), EPlayerRole::TightEnd, 0) };
    for (int32 Index = 0; Index < 6; ++Index)
    {
        Players.Add(MakeAgedPlayer(FString::Printf(TEXT("WR_%d"), Index), EPlayerRole::WideReceiver, 22));
    }
    UPSRoster* Roster = MakeAgedRoster(Players);
    TestTrue(TEXT("A starter plays every snap"), FMath::IsNearlyEqual(UPSPlayerAging::GetSnapShare(Roster, RowOf(Roster, TEXT("WR_0"))), 1.f, 1e-5f));
    TestTrue(TEXT("The sixth receiver a sixth"), FMath::IsNearlyEqual(UPSPlayerAging::GetSnapShare(Roster, RowOf(Roster, TEXT("WR_5"))), 1.f / 6.f, 1e-5f));

    const TArray<FPSRetirementDecision> Retired = Aging->RunOffseason(FName(TEXT("Hawks")), Roster, 1, nullptr, nullptr, nullptr, nullptr, nullptr);
    TestEqual(TEXT("Nobody under MinAge retires"), Retired.Num(), 0);
    TestTrue(TEXT("A young quarterback grows his curve's GrowthPerYear"), FMath::IsNearlyEqual(RowOf(Roster, TEXT("QB_YOUNG")).Speed, 70.f + Passer.GrowthPerYear, 1e-4f));
    TestEqual(TEXT("A quarterback at 29 is in his prime"), RowOf(Roster, TEXT("QB_PRIME")).Speed, 70.f);
    TestEqual(TEXT("A running back at 24 is in his"), RowOf(Roster, TEXT("RB_PRIME")).Speed, 70.f);
    TestTrue(TEXT("A running back at 29 declines his curve's DeclinePerYear"), FMath::IsNearlyEqual(RowOf(Roster, TEXT("RB_OLD")).Speed, 70.f - Back.DeclinePerYear, 1e-4f));
    const FPSProgressionTuning Receiver = Aging->GetCurve(EPlayerRole::WideReceiver);
    TestTrue(TEXT("The starting receiver grows fully"), FMath::IsNearlyEqual(RowOf(Roster, TEXT("WR_0")).Speed, 70.f + Receiver.GrowthPerYear, 1e-4f));
    TestTrue(TEXT("The buried one at half"), FMath::IsNearlyEqual(RowOf(Roster, TEXT("WR_5")).Speed, 70.f + Receiver.GrowthPerYear * 0.5f, 1e-4f));
    TestEqual(TEXT("Everyone is a year older"), RowOf(Roster, TEXT("QB_YOUNG")).Age, 25);
    TestEqual(TEXT("...the running back too"), RowOf(Roster, TEXT("RB_OLD")).Age, 30);
    TestEqual(TEXT("Without a contract manager, an unknown age stays unknown"), RowOf(Roster, TEXT("NO_AGE")).Age, 0);
    TestEqual(TEXT("...and he doesn't change"), RowOf(Roster, TEXT("NO_AGE")).Speed, 70.f);

    // The contract manager's default age for one without.
    UPSContractManager* Contracts = NewObject<UPSContractManager>();
    Contracts->LoadTuningFromJson(UPSContractManager::GetDefaultTuningPath());
    Contracts->StartLeague();
    UPSRoster* Unknown = MakeAgedRoster({ MakeAgedPlayer(TEXT("NO_AGE"), EPlayerRole::TightEnd, 0) });
    Aging->RunOffseason(FName(TEXT("Hawks")), Unknown, 1, Contracts, nullptr, nullptr, nullptr, nullptr);
    TestEqual(TEXT("With one, he ages from its default"), RowOf(Unknown, TEXT("NO_AGE")).Age, Contracts->GetTuning().DefaultPlayerAge + 1);

    // A role without a curve of its own ages on Core 19's.
    FPSLegacyTuning Bare = Aging->GetTuning();
    Bare.RoleCurves.Reset();
    Aging->SetTuning(Bare);
    FPSProgressionTuning Core;
    TestTrue(TEXT("Data/player_progression.json loads"), NewObject<UPSDataIngestion>()->LoadProgressionTuningFromJson(UPSPlayerProgression::GetDefaultTuningPath(), Core));
    UPSRoster* Probe = MakeAgedRoster({ MakeAgedPlayer(TEXT("QB_PROBE"), EPlayerRole::Quarterback, 30) });
    TestTrue(TEXT("Core 19's curve for a role without its own"), Aging->GetCurve(EPlayerRole::Quarterback).PeakAgeStart == Core.PeakAgeStart
        && Aging->GetCurve(EPlayerRole::Quarterback).PeakAgeEnd == Core.PeakAgeEnd && Aging->GetCurve(EPlayerRole::Quarterback).DeclinePerYear == Core.DeclinePerYear);
    Aging->RunOffseason(FName(TEXT("Hawks")), Probe, 1, nullptr, nullptr, nullptr, nullptr, nullptr);
    const FPSProgressionTuning Base = Aging->GetCurve(EPlayerRole::Quarterback);
    TestTrue(TEXT("...a quarterback of 30 declines on it"), 30 > Base.PeakAgeEnd && FMath::IsNearlyEqual(RowOf(Probe, TEXT("QB_PROBE")).Speed, 70.f - Base.DeclinePerYear, 1e-4f));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Retirement
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSRetirementTest,
    "PlaySports.Legacy.RetirementDecisions",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRetirementTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayerAgingTests;

    UPSPlayerAging* Aging = MakeAging();
    const FPSRetirementTuning& R = Aging->GetTuning().Retirement;
    const FPlayerAttributes Solid = MakeAgedPlayer(TEXT("SOLID"), EPlayerRole::Linebacker, 30, 75.f);
    FString Reason;
    TestEqual(TEXT("Under MinAge: never"), Aging->GetRetirementChance(Solid, R.MinAge - 1, 0.5f, false, Reason), 0.f);
    TestTrue(TEXT("At MinAge: BaseChance"), FMath::IsNearlyEqual(Aging->GetRetirementChance(Solid, R.MinAge, 0.5f, false, Reason), R.BaseChance, 1e-5f));
    TestTrue(TEXT("Four years past it: four ChancePerYear more"), FMath::IsNearlyEqual(Aging->GetRetirementChance(Solid, R.MinAge + 4, 0.5f, false, Reason), R.BaseChance + 4 * R.ChancePerYear, 1e-5f)
        && Reason == FString::Printf(TEXT("Age %d"), R.MinAge + 4));
    TestTrue(TEXT("At ForcedAge: certain"), Aging->GetRetirementChance(Solid, R.ForcedAge, 0.9f, false, Reason) == 1.f && Reason == FString::Printf(TEXT("Age %d"), R.ForcedAge));
    const FPlayerAttributes Faded = MakeAgedPlayer(TEXT("FADED"), EPlayerRole::Linebacker, 31, R.LowRating - 10.f);
    TestTrue(TEXT("Declining: LowRatingChance more"), FMath::IsNearlyEqual(Aging->GetRetirementChance(Faded, R.MinAge, 0.5f, false, Reason), R.BaseChance + R.LowRatingChance, 1e-5f)
        && Reason.StartsWith(TEXT("Declining")));
    TestTrue(TEXT("Hurt: InjuredChance more"), FMath::IsNearlyEqual(Aging->GetRetirementChance(Solid, R.MinAge, 0.5f, true, Reason), R.BaseChance + R.InjuredChance, 1e-5f)
        && Reason == TEXT("Injured"));
    TestTrue(TEXT("Unhappy: LowMoraleChance more"), FMath::IsNearlyEqual(Aging->GetRetirementChance(Solid, R.MinAge, R.LowMorale - 0.1f, false, Reason), R.BaseChance + R.LowMoraleChance, 1e-5f)
        && Reason == TEXT("Unhappy"));

    // An off-season: two forced, ten likely, eight young; the churn cap holds the likely back.
    UPSContractManager* Contracts = NewObject<UPSContractManager>();
    Contracts->LoadTuningFromJson(UPSContractManager::GetDefaultTuningPath());
    Contracts->StartLeague();
    TArray<FPlayerAttributes> Players;
    for (int32 Index = 0; Index < 20; ++Index)
    {
        const int32 Age = Index < 2 ? R.ForcedAge : Index < 12 ? R.ForcedAge - 2 : 25;
        Players.Add(MakeAgedPlayer(FString::Printf(TEXT("VET_%02d"), Index), EPlayerRole::DefensiveLineman, Age));
    }
    UPSRoster* Roster = MakeAgedRoster(Players);
    Contracts->SignRosterAtDemand(FName(TEXT("Hawks")), Players);
    UPSLeagueHistory* History = NewObject<UPSLeagueHistory>();
    History->LoadTuningFromJson(UPSLeagueHistory::GetDefaultTuningPath());
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    Stats->StartSeason(1);
    const TArray<FPSRetirementDecision> Retired = Aging->RunOffseason(FName(TEXT("Hawks")), Roster, 1, Contracts, Stats, nullptr, nullptr, History);
    const int32 Allowed = FMath::FloorToInt(R.MaxRetirementShare * Players.Num());
    TestEqual(TEXT("MaxRetirementShare of the roster retires"), Retired.Num(), FMath::Max(Allowed, 2));
    for (const TCHAR* Forced : { TEXT("VET_00"), TEXT("VET_01") })
    {
        TestTrue(*FString::Printf(TEXT("%s, at ForcedAge, retires"), Forced), Retired.ContainsByPredicate([Forced](const FPSRetirementDecision& Decision) { return Decision.PlayerId == FName(Forced); }));
    }
    for (const FPSRetirementDecision& Decision : Retired)
    {
        const FString Label = Decision.PlayerId.ToString();
        FPlayerAttributes Gone;
        TestFalse(*(Label + TEXT(": off the roster")), Roster->FindPlayerById(Decision.PlayerId, Gone));
        TestNull(*(Label + TEXT(": his contract ended")), Contracts->FindContract(Decision.PlayerId));
        FPSRetiredPlayer Record;
        TestTrue(*(Label + TEXT(": in the league's history")), History->FindRetiredPlayer(Decision.PlayerId, Record) && Record.Reason == Decision.Reason && Record.Player.Age == Decision.Age);
        TestTrue(*(Label + TEXT(": a veteran")), Decision.Age >= R.ForcedAge - 2);
    }
    TestEqual(TEXT("Retirees forfeit their unearned guarantees: no dead money"), Contracts->GetDeadMoney(FName(TEXT("Hawks")), Contracts->GetLeagueYear()), 0);
    TestEqual(TEXT("The young stay and age"), RowOf(Roster, TEXT("VET_19")).Age, 26);

    // The same season retires the same players.
    UPSRoster* Again = MakeAgedRoster(Players);
    const TArray<FPSRetirementDecision> Repeat = MakeAging()->RunOffseason(FName(TEXT("Hawks")), Again, 1, nullptr, nullptr, nullptr, nullptr, nullptr);
    bool bSame = Repeat.Num() == Retired.Num();
    for (int32 Index = 0; bSame && Index < Repeat.Num(); ++Index)
    {
        bSame &= Repeat[Index].PlayerId == Retired[Index].PlayerId;
    }
    TestTrue(TEXT("The same retirements"), bSame);

    // A veteran hurt at the season's end (Epic 90) retires when injury alone is certain.
    FPSLegacyTuning Certain = Aging->GetTuning();
    Certain.Retirement.BaseChance = 0.f;
    Certain.Retirement.ChancePerYear = 0.f;
    Certain.Retirement.InjuredChance = 1.f;
    Certain.Retirement.MaxRetirementShare = 1.f;
    UPSPlayerAging* Strict = NewObject<UPSPlayerAging>();
    Strict->SetTuning(Certain);
    UPSWeeklyPreparation* Prep = NewObject<UPSWeeklyPreparation>();
    UPSFranchiseSaveGame* Hurt = NewObject<UPSFranchiseSaveGame>();
    FPSPlayerCondition& Condition = Hurt->Training.Players.AddDefaulted_GetRef();
    Condition.PlayerId = FName(TEXT("VET_HURT"));
    Condition.InjuryWeeks = 3;
    TestTrue(TEXT("The preparation knows he is hurt"), Prep->LoadFrom(Hurt) && Prep->IsInjured(FName(TEXT("VET_HURT"))));
    UPSRoster* Veterans = MakeAgedRoster({ MakeAgedPlayer(TEXT("VET_HURT"), EPlayerRole::Linebacker, R.MinAge + 1), MakeAgedPlayer(TEXT("VET_FINE"), EPlayerRole::Linebacker, R.MinAge + 1) });
    const TArray<FPSRetirementDecision> HurtRetired = Strict->RunOffseason(FName(TEXT("Wolves")), Veterans, 1, nullptr, nullptr, Prep, nullptr, nullptr);
    TestTrue(TEXT("The hurt veteran retires, injured"), HurtRetired.Num() == 1 && HurtRetired[0].PlayerId == FName(TEXT("VET_HURT")) && HurtRetired[0].Reason == TEXT("Injured"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The franchise
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSAgingFranchiseTest,
    "PlaySports.Legacy.FranchiseAgingAndRetirement",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSAgingFranchiseTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayerAgingTests;

    UPSScheduleEngine* Schedule = NewObject<UPSScheduleEngine>();
    UPSFranchiseSeason* Season = NewObject<UPSFranchiseSeason>();
    const TArray<FName> TeamIds = { FName(TEXT("Falcons")), FName(TEXT("Hawks")), FName(TEXT("Wolves")), FName(TEXT("Bears")) };
    Season->InitializeSeason(TeamIds, Schedule->GenerateSeasonSchedule(FDateTime(2026, 9, 1), 1, TArray<int32>()));
    UPSStaffManager* Staffs = NewObject<UPSStaffManager>();
    Staffs->LoadFromJson(UPSStaffManager::GetDefaultDataPath());
    UPSFranchiseFlow* Flow = NewObject<UPSFranchiseFlow>();
    Flow->Initialize(Season, Staffs, FName(TEXT("Falcons")));
    Flow->LoadLeagueRosters(UPSUITeamCatalog::GetDefaultTeamsPath());
    UPSContractManager* Contracts = NewObject<UPSContractManager>();
    Contracts->LoadTuningFromJson(UPSContractManager::GetDefaultTuningPath());
    Contracts->StartLeague();
    Flow->SetContracts(Contracts);
    Flow->SignLeagueContracts();
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    Flow->SetStats(Stats);
    UPSLeagueHistory* History = NewObject<UPSLeagueHistory>();
    History->LoadTuningFromJson(UPSLeagueHistory::GetDefaultTuningPath());
    Flow->SetLeagueHistory(History);
    UPSPlayerAging* Aging = MakeAging();
    Flow->SetPlayerAging(Aging);

    // The Hawks' quarterback is past ForcedAge; everyone else's age is unknown (the contracts' default).
    UPSRoster* Hawks = Flow->GetTeamRoster(FName(TEXT("Hawks")));
    const FName Veteran(TEXT("HAW_QB_001"));
    for (FPlayerAttributes& Player : Hawks->GetMutableFullRoster())
    {
        if (Player.PlayerId == Veteran)
        {
            Player.Age = Aging->GetTuning().Retirement.ForcedAge + 5;
        }
    }
    TMap<FName, int32> AgesBefore;
    for (const FName& TeamId : TeamIds)
    {
        for (const FPlayerAttributes& Player : Flow->GetTeamRoster(TeamId)->GetFullRoster())
        {
            AgesBefore.Add(Player.PlayerId, Contracts->GetPlayerAge(Player));
        }
    }

    // The statistics number the season by the contracts' league year.
    const int32 SeasonNumber = Stats->GetSeason();
    TestEqual(TEXT("The week's games"), Flow->SimulateWeek(true), 2);
    TestTrue(TEXT("The season ends"), Flow->AdvanceWeek());

    FPlayerAttributes Gone;
    TestFalse(TEXT("The veteran has retired"), Hawks->FindPlayerById(Veteran, Gone));
    TestTrue(TEXT("...the flow lists him"), Flow->GetRetirements().ContainsByPredicate([&Veteran](const FPSRetirementDecision& Decision) { return Decision.PlayerId == Veteran; }));
    FPSSeasonArchive First;
    TestTrue(TEXT("...the season's archive keeps him"), History->FindSeason(SeasonNumber, First) && First.Retired.Contains(Veteran));
    FPSRetiredPlayer Record;
    TestTrue(TEXT("...with his career"), History->FindRetiredPlayer(Veteran, Record) && Record.Career.PassAttempts > 0 && Record.LastTeamId == FName(TEXT("Hawks"))
        && Record.RetiredAfterSeason == SeasonNumber);
    FPSFreeAgent NotAFreeAgent;
    TestFalse(TEXT("...and not as a free agent"), Flow->GetFreeAgency() && Flow->GetFreeAgency()->GetFreeAgent(Veteran, NotAFreeAgent));
    for (const FName& TeamId : TeamIds)
    {
        for (const FPlayerAttributes& Player : Flow->GetTeamRoster(TeamId)->GetFullRoster())
        {
            const int32* Before = AgesBefore.Find(Player.PlayerId);
            TestTrue(*FString::Printf(TEXT("%s is a year older"), *Player.PlayerId.ToString()), Before && Player.Age == *Before + 1);
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
