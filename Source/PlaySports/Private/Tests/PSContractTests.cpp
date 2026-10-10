// PSContractTests.cpp -- Epic 87 (contracts and the salary cap)
//
// Tests covered:
//   1. Data/contracts.json loads through UPSDataIngestion, validates clean, has a market for every
//      role and equals FPSContractTuning's defaults field by field; a broken tuning's problems are
//      each reported.
//   2. The contract model and the cap: an offer becomes flat base salaries, a prorated bonus and
//      front-loaded guarantees; signing spends cap space; a second deal, an invalid deal and one
//      over the cap are refused.
//   3. The cap tools and their previews: a cut's dead money all now or spread over two years, a
//      restructure turning base salary into a prorated bonus, an extension adding years and a
//      bonus; previews change nothing, a move outside the rules (too many years, over the cap) is
//      refused with its reason.
//   4. The league year: rollover grows the cap, carries unused space over, expires finished deals
//      and drops past dead money; a CPU team over the new cap cuts the least value per cap dollar
//      until it complies, off its roster too.
//   5. Negotiation: demands from rating, age and market; offers accepted, countered or rejected by
//      value, guarantees, years and morale; an extension the player accepts is made, one he
//      counters isn't.
//   6. Free agency: a player released from a roster joins the pool; a user offer over the cap, too
//      low, far above the ask (signed on the spot) or at it (weighed); CPU teams outbid the user
//      for their needs and the player signs the best offer on his decision day; asks cool; the
//      period closes; the same pool signs the same way again.
//   7. The franchise: every shipped roster signed at its demands, a season played and ended through
//      UPSFranchiseFlow, the league year rolled over, expired players in free agency and signed
//      under the cap; the ledger round-trips through the franchise save.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "PSContractData.h"
#include "PSContractManager.h"
#include "PSContractNegotiation.h"
#include "PSDataIngestion.h"
#include "PSFranchiseFlow.h"
#include "PSFranchiseSaveGame.h"
#include "PSFranchiseSeason.h"
#include "PSFreeAgency.h"
#include "PSRoster.h"
#include "PSSaveSubsystem.h"
#include "PSScheduleEngine.h"
#include "PSStaffManager.h"
#include "PSUITeamCatalog.h"
#include "Engine/GameInstance.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSContractTests
{
    /** A manager with the shipped tuning and a fresh league (2026, a 255,000 cap). */
    static UPSContractManager* MakeManager()
    {
        UPSContractManager* Manager = NewObject<UPSContractManager>();
        Manager->LoadTuningFromJson(UPSContractManager::GetDefaultTuningPath());
        Manager->StartLeague();
        return Manager;
    }

    static FPSContractOffer MakeOffer(const TCHAR* TeamId, int32 Years, int32 AnnualValue, int32 SigningBonus = 0, float GuaranteedFraction = 0.f)
    {
        FPSContractOffer Offer;
        Offer.TeamId = FName(TeamId);
        Offer.Years = Years;
        Offer.AnnualValue = AnnualValue;
        Offer.SigningBonus = SigningBonus;
        Offer.GuaranteedFraction = GuaranteedFraction;
        return Offer;
    }

    /** Signs PlayerId to Offer; true when it went through. */
    static bool Sign(UPSContractManager* Manager, const TCHAR* PlayerId, const FPSContractOffer& Offer)
    {
        return Manager->SignContract(Manager->MakeContract(FName(PlayerId), Offer)) == EPSCapResult::Ok;
    }

    /** A player whose five skill ratings are all Rating (UPSContractNegotiation::RatePlayer = Rating). */
    static FPlayerAttributes MakeContractPlayer(const TCHAR* PlayerId, EPlayerRole Role, float Rating)
    {
        FPlayerAttributes Player;
        Player.PlayerId = FName(PlayerId);
        Player.DisplayName = PlayerId;
        Player.Role = Role;
        Player.WeightKg = 100.f;
        Player.HeightCm = 188.f;
        Player.Speed = Rating;
        Player.Agility = Rating;
        Player.Strength = Rating;
        Player.Acceleration = Rating;
        Player.Awareness = Rating;
        Player.Stamina = 85.f;
        return Player;
    }

    static UPSRoster* MakeRosterOf(const TArray<FPlayerAttributes>& Players)
    {
        UPSRoster* Roster = NewObject<UPSRoster>();
        Roster->InitializeRoster(Players);
        Roster->BuildDefaultDepthChart();
        return Roster;
    }

    static bool HasLine(const TArray<FString>& Lines, const TCHAR* Fragment)
    {
        return Lines.ContainsByPredicate([Fragment](const FString& Line) { return Line.Contains(Fragment); });
    }

    /** One free-agency period: the user's Falcons and the CPU's Hawks and Wolves bid for an elite
     *  quarterback, a receiver, a lineman and the Falcons' own unsigned veteran. */
    struct FFreeAgencyRun
    {
        UPSContractManager* Manager = nullptr;
        UPSFreeAgency* FreeAgency = nullptr;
        UPSRoster* Falcons = nullptr;
        UPSRoster* Hawks = nullptr;
        UPSRoster* Wolves = nullptr;
    };

    static FFreeAgencyRun StartFreeAgency()
    {
        FFreeAgencyRun Run;
        Run.Manager = MakeManager();
        Run.Falcons = MakeRosterOf({ MakeContractPlayer(TEXT("FAL_Vet"), EPlayerRole::OffensiveLineman, 70.f) });
        Run.Hawks = MakeRosterOf({});
        Run.Wolves = MakeRosterOf({});

        Run.FreeAgency = NewObject<UPSFreeAgency>();
        Run.FreeAgency->Initialize(Run.Manager);
        Run.FreeAgency->RegisterTeam(FName(TEXT("Falcons")), Run.Falcons, true);
        Run.FreeAgency->RegisterTeam(FName(TEXT("Hawks")), Run.Hawks, false);
        Run.FreeAgency->RegisterTeam(FName(TEXT("Wolves")), Run.Wolves, false);
        Run.FreeAgency->AddFreeAgent(MakeContractPlayer(TEXT("FA_QB"), EPlayerRole::Quarterback, 95.f), 27, 0.5f, NAME_None);
        Run.FreeAgency->AddFreeAgent(MakeContractPlayer(TEXT("FA_WR"), EPlayerRole::WideReceiver, 88.f), 27, 0.5f, NAME_None);
        Run.FreeAgency->AddFreeAgent(MakeContractPlayer(TEXT("FA_OL"), EPlayerRole::OffensiveLineman, 80.f), 27, 0.5f, NAME_None);
        TMap<FName, int32> Ages;
        Ages.Add(FName(TEXT("FAL_Vet")), 31);
        Run.FreeAgency->ReleaseUnsignedToPool(Ages);
        Run.FreeAgency->BeginPeriod();
        return Run;
    }

    static FPSContractOffer OfferAtDemand(const FFreeAgencyRun& Run, const TCHAR* TeamId, const TCHAR* PlayerId, float Fraction)
    {
        FPSFreeAgent FreeAgent;
        Run.FreeAgency->GetFreeAgent(FName(PlayerId), FreeAgent);
        return MakeOffer(TeamId, FreeAgent.Demand.Years, FMath::RoundToInt(FreeAgent.Demand.AnnualValue * Fraction), 0, FreeAgent.Demand.GuaranteedFraction);
    }

    static FString DescribeSignings(const TArray<FPSFreeAgentSigning>& Signings)
    {
        FString Text;
        for (const FPSFreeAgentSigning& Signing : Signings)
        {
            Text += FString::Printf(TEXT("%s>%s@%d;"), *Signing.PlayerId.ToString(), *Signing.TeamId.ToString(), Signing.Day);
        }
        return Text;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The shipped tuning
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSContractTuningTest,
    "PlaySports.Contracts.TuningLoadsAndValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSContractTuningTest::RunTest(const FString& Parameters)
{
    using namespace PSContractTests;

    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSContractTuning FromFile;
    if (!TestTrue(TEXT("Data/contracts.json loads"), Ingestion->LoadContractTuningFromJson(UPSContractManager::GetDefaultTuningPath(), FromFile)))
    {
        return false;
    }
    const TArray<FString> Problems = UPSContractManager::ValidateTuning(FromFile);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    TestEqual(TEXT("The shipped tuning is sound"), Problems.Num(), 0);
    TestEqual(TEXT("A market for each of the eight roles"), FromFile.PositionMarkets.Num(), 8);
    TestNotNull(TEXT("The quarterback's market"), FromFile.FindMarket(EPlayerRole::Quarterback));

    // The struct's defaults are the file's, so code and data can't drift apart.
    const FPSContractTuning Defaults;
    for (TFieldIterator<FProperty> It(FPSContractTuning::StaticStruct()); It; ++It)
    {
        if (It->GetFName() != GET_MEMBER_NAME_CHECKED(FPSContractTuning, PositionMarkets))
        {
            TestTrue(*FString::Printf(TEXT("%s matches the default"), *It->GetName()), It->Identical_InContainer(&FromFile, &Defaults));
        }
    }

    // A broken tuning reports each problem.
    FPSContractTuning Broken = FromFile;
    Broken.MinimumSalary = Broken.SalaryCap + 1;
    Broken.WalkAwayRatio = 1.5f;
    Broken.EliteRating = Broken.ReplacementRating;
    Broken.PositionMarkets.RemoveAt(0);
    Broken.PositionMarkets.Last().TopCapFraction = 0.f;
    const TArray<FString> BrokenProblems = UPSContractManager::ValidateTuning(Broken);
    TestTrue(TEXT("The minimum above the cap"), HasLine(BrokenProblems, TEXT("MinimumSalary")));
    TestTrue(TEXT("Walk-away above accept"), HasLine(BrokenProblems, TEXT("WalkAwayRatio")));
    TestTrue(TEXT("No rating span"), HasLine(BrokenProblems, TEXT("EliteRating")));
    TestTrue(TEXT("A role without a market"), HasLine(BrokenProblems, TEXT("Quarterback has 0 entries")));
    TestTrue(TEXT("A market worth nothing"), HasLine(BrokenProblems, TEXT("TopCapFraction")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The contract model and the cap
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSContractModelTest,
    "PlaySports.Contracts.ContractModelAndCapMath",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSContractModelTest::RunTest(const FString& Parameters)
{
    using namespace PSContractTests;

    UPSContractManager* Manager = MakeManager();
    const FName Hawks(TEXT("Hawks"));
    TestEqual(TEXT("The league starts in 2026"), Manager->GetLeagueYear(), 2026);
    TestEqual(TEXT("...under a 255,000 cap"), Manager->GetSalaryCap(), 255000);
    TestEqual(TEXT("Next year's cap grows 5%"), Manager->GetProjectedSalaryCap(2027), 267750);

    // 4 years at 10,000 a year, 8,000 of it a signing bonus, half the deal guaranteed.
    const FPSContract Deal = Manager->MakeContract(FName(TEXT("HAW_QB")), MakeOffer(TEXT("Hawks"), 4, 10000, 8000, 0.5f));
    if (!TestEqual(TEXT("Four contract years"), Deal.Years.Num(), 4))
    {
        return false;
    }
    TestEqual(TEXT("It starts this league year"), Deal.Years[0].LeagueYear, 2026);
    TestEqual(TEXT("...and runs through 2029"), Deal.GetLastYear(), 2029);
    TestEqual(TEXT("The bonus is paid"), Deal.BonusPaid, 8000);
    for (const FPSContractYear& Year : Deal.Years)
    {
        TestEqual(*FString::Printf(TEXT("%d: flat base salary"), Year.LeagueYear), Year.BaseSalary, 8000);
        TestEqual(*FString::Printf(TEXT("%d: a quarter of the bonus"), Year.LeagueYear), Year.ProratedBonus, 2000);
        TestEqual(*FString::Printf(TEXT("%d: cap hit"), Year.LeagueYear), Year.GetCapHit(), 10000);
    }
    TestEqual(TEXT("Guarantees on the earliest years: all of 2026"), Deal.Years[0].GuaranteedSalary, 8000);
    TestEqual(TEXT("...half of 2027 (20,000 guaranteed less the bonus)"), Deal.Years[1].GuaranteedSalary, 4000);
    TestEqual(TEXT("...none of 2028"), Deal.Years[2].GuaranteedSalary, 0);

    TestEqual(TEXT("It can be signed"), Manager->CanSign(Deal), EPSCapResult::Ok);
    TestEqual(TEXT("Signed"), Manager->SignContract(Deal), EPSCapResult::Ok);
    TestEqual(TEXT("It counts against the cap"), Manager->GetCapSpace(Hawks), 245000);
    TestEqual(TEXT("...this year"), Manager->GetCapUsed(Hawks, 2026), 10000);
    TestEqual(TEXT("...and next, against next year's cap"), Manager->GetCapSpaceInYear(Hawks, 2027), 257750);
    TestEqual(TEXT("His cap hit"), Manager->GetCapHit(FName(TEXT("HAW_QB")), 2028), 10000);
    TestEqual(TEXT("The team's contracts"), Manager->GetTeamContracts(Hawks).Num(), 1);

    // Refusals: a second deal, a salary under the minimum, too many years, over the cap.
    TestEqual(TEXT("He is already signed"), Manager->SignContract(Deal), EPSCapResult::AlreadySigned);
    TestEqual(TEXT("Under the minimum salary"), Manager->CanSign(Manager->MakeContract(FName(TEXT("Cheap")), MakeOffer(TEXT("Hawks"), 1, 500))), EPSCapResult::InvalidContract);
    FPSContract TooLong = Manager->MakeContract(FName(TEXT("Long")), MakeOffer(TEXT("Hawks"), 5, 2000));
    FPSContractYear& Sixth = TooLong.Years.AddDefaulted_GetRef();
    Sixth.LeagueYear = 2031;
    Sixth.BaseSalary = 2000;
    TestEqual(TEXT("Six years"), Manager->CanSign(TooLong), EPSCapResult::InvalidContract);
    TestEqual(TEXT("Over the cap"), Manager->SignContract(Manager->MakeContract(FName(TEXT("Huge")), MakeOffer(TEXT("Hawks"), 1, 250000))), EPSCapResult::OverCap);
    TestEqual(TEXT("Nothing refused was signed"), Manager->GetLedger().Contracts.Num(), 1);
    TestTrue(TEXT("The Hawks are compliant"), Manager->IsCompliant(Hawks));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Cut, restructure and extension, with previews
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSContractCapToolsTest,
    "PlaySports.Contracts.CutRestructureExtendPreviews",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSContractCapToolsTest::RunTest(const FString& Parameters)
{
    using namespace PSContractTests;

    UPSContractManager* Manager = MakeManager();
    const FName Hawks(TEXT("Hawks"));
    const FName Cut(TEXT("CutMe"));
    TestTrue(TEXT("The deal to cut"), Sign(Manager, TEXT("CutMe"), MakeOffer(TEXT("Hawks"), 4, 10000, 8000, 0.5f)));

    // A cut now: what is guaranteed (8,000 + 4,000) and the bonus not yet charged (4 x 2,000).
    const FPSCapPreview Now = Manager->PreviewCut(Cut, false);
    TestTrue(TEXT("The cut can be made"), Now.bValid);
    TestEqual(TEXT("All the dead money this year"), Now.DeadMoneyThisYear, 20000);
    TestEqual(TEXT("...none next year"), Now.DeadMoneyNextYear, 0);
    TestEqual(TEXT("His cap hit before"), Now.PlayerCapHitBefore, 10000);
    TestEqual(TEXT("...and after: his dead money"), Now.PlayerCapHitAfter, 20000);
    TestEqual(TEXT("Cap space falls this year"), Now.CapSpaceAfter - Now.CapSpaceBefore, -10000);
    TestEqual(TEXT("...and rises next year"), Now.NextYearCapSpaceAfter - Now.NextYearCapSpaceBefore, 10000);
    TestTrue(TEXT("A preview changes nothing"), Manager->FindContract(Cut) != nullptr && Manager->GetDeadMoney(Hawks, 2026) == 0);

    // Spread: this year's share now (8,000 + 2,000), the rest next year.
    const FPSCapPreview Spread = Manager->PreviewCut(Cut, true);
    TestEqual(TEXT("Spread: 10,000 this year"), Spread.DeadMoneyThisYear, 10000);
    TestEqual(TEXT("...10,000 next year"), Spread.DeadMoneyNextYear, 10000);
    const FPSCapPreview Done = Manager->CutPlayer(Cut, true);
    TestTrue(TEXT("Cut"), Done.bValid && Manager->FindContract(Cut) == nullptr);
    TestEqual(TEXT("The dead money is charged this year"), Manager->GetDeadMoney(Hawks, 2026), 10000);
    TestEqual(TEXT("...and next"), Manager->GetDeadMoney(Hawks, 2027), 10000);
    TestEqual(TEXT("The cap space is the preview's"), Manager->GetCapSpace(Hawks), Spread.CapSpaceAfter);
    TestFalse(TEXT("No second cut"), Manager->PreviewCut(Cut, false).bValid);

    // A restructure: 15,000 of a 20,000 base turned into a bonus over his three years.
    const FName Restructured(TEXT("Restructure"));
    TestTrue(TEXT("The deal to restructure"), Sign(Manager, TEXT("Restructure"), MakeOffer(TEXT("Hawks"), 3, 20000)));
    const FPSCapPreview Restructure = Manager->PreviewRestructure(Restructured, 15000);
    TestTrue(TEXT("It can be restructured"), Restructure.bValid);
    TestEqual(TEXT("This year's cap hit falls to 10,000"), Restructure.PlayerCapHitAfter, 10000);
    TestEqual(TEXT("10,000 more space this year"), Restructure.CapSpaceAfter - Restructure.CapSpaceBefore, 10000);
    TestEqual(TEXT("5,000 less next year"), Restructure.NextYearCapSpaceAfter - Restructure.NextYearCapSpaceBefore, -5000);
    TestEqual(TEXT("A preview changes nothing"), Manager->GetCapHit(Restructured, 2026), 20000);
    TestTrue(TEXT("Restructured"), Manager->RestructureContract(Restructured, 15000).bValid);
    TestEqual(TEXT("2026: 5,000 base + 5,000 bonus"), Manager->GetCapHit(Restructured, 2026), 10000);
    TestEqual(TEXT("2028: 20,000 base + 5,000 bonus"), Manager->GetCapHit(Restructured, 2028), 25000);
    FPSContract AfterRestructure;
    Manager->GetContract(Restructured, AfterRestructure);
    TestEqual(TEXT("The converted salary is bonus paid"), AfterRestructure.BonusPaid, 15000);

    // The minimum salary stays: only 4,100 more can be converted (this year's share of it 1,368,
    // the remainder on the first year), then nothing.
    const FPSCapPreview AllTheRest = Manager->PreviewRestructure(Restructured, 100000);
    TestEqual(TEXT("Down to the minimum"), AllTheRest.CapSpaceAfter - AllTheRest.CapSpaceBefore, 4100 - (4100 / 3 + 4100 % 3));
    Manager->RestructureContract(Restructured, 100000);
    const FPSCapPreview AtMinimum = Manager->PreviewRestructure(Restructured, 1000);
    TestFalse(TEXT("Nothing left to convert"), AtMinimum.bValid);
    TestTrue(TEXT("...and it says so"), AtMinimum.Problem.Contains(TEXT("minimum")));

    // An extension: 2 more years at 9,000 with a 6,000 bonus over all four remaining years.
    const FName Extended(TEXT("Extend"));
    TestTrue(TEXT("The deal to extend"), Sign(Manager, TEXT("Extend"), MakeOffer(TEXT("Hawks"), 2, 12000)));
    const FPSContractOffer Extension = MakeOffer(TEXT("Hawks"), 2, 9000, 6000);
    const FPSCapPreview ExtendPreview = Manager->PreviewExtension(Extended, Extension);
    TestTrue(TEXT("It can be extended"), ExtendPreview.bValid);
    TestEqual(TEXT("This year's hit takes a quarter of the bonus"), ExtendPreview.PlayerCapHitAfter, 13500);
    TestTrue(TEXT("Extended"), Manager->ExtendContract(Extended, Extension).bValid);
    FPSContract AfterExtension;
    Manager->GetContract(Extended, AfterExtension);
    TestEqual(TEXT("Four years now"), AfterExtension.Years.Num(), 4);
    TestEqual(TEXT("Through 2029"), AfterExtension.GetLastYear(), 2029);
    TestEqual(TEXT("2028: 6,000 base + 1,500 bonus"), Manager->GetCapHit(Extended, 2028), 7500);

    // Outside the rules: past MaxContractYears, or over the cap.
    const FPSCapPreview TooLong = Manager->PreviewExtension(Extended, MakeOffer(TEXT("Hawks"), 2, 9000));
    TestFalse(TEXT("Six years is too long"), TooLong.bValid);
    TestTrue(TEXT("...and it says so"), TooLong.Problem.Contains(TEXT("6 years")));

    const FName Bears(TEXT("Bears"));
    TestTrue(TEXT("The Bears fill their cap"), Sign(Manager, TEXT("Bulk"), MakeOffer(TEXT("Bears"), 1, 254000)));
    TestTrue(TEXT("...with 100 to spare"), Sign(Manager, TEXT("BearStar"), MakeOffer(TEXT("Bears"), 2, 900)));
    TestEqual(TEXT("100 to spare"), Manager->GetCapSpace(Bears), 100);
    FPSCapPreview OverCap;
    const FPSNegotiationResult Refused = Manager->ProposeExtension(FName(TEXT("BearStar")), Manager->MakeNegotiationContext(MakeContractPlayer(TEXT("BearStar"), EPlayerRole::Linebacker, 60.f), 25, 0.5f, Bears),
        MakeOffer(TEXT("Bears"), 1, 50000, 40000), OverCap);
    TestFalse(TEXT("A bonus the cap can't bear"), OverCap.bValid);
    TestTrue(TEXT("...is refused for the cap"), OverCap.Problem.Contains(TEXT("over the cap")) && Refused.Response == EPSNegotiationResponse::Reject);
    TestEqual(TEXT("...and the deal is unchanged"), Manager->GetCapHit(FName(TEXT("BearStar")), 2026), 900);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- League-year rollover and compliance
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSContractRolloverTest,
    "PlaySports.Contracts.RolloverAndCompliance",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSContractRolloverTest::RunTest(const FString& Parameters)
{
    using namespace PSContractTests;

    UPSContractManager* Manager = MakeManager();
    const FName Hawks(TEXT("Hawks"));
    const FName Wolves(TEXT("Wolves"));
    Manager->RegisterTeam(Wolves);
    TestTrue(TEXT("A one-year deal"), Sign(Manager, TEXT("OneYear"), MakeOffer(TEXT("Hawks"), 1, 20000)));
    TestTrue(TEXT("A three-year deal"), Sign(Manager, TEXT("ThreeYear"), MakeOffer(TEXT("Hawks"), 3, 30000, 15000)));
    TestTrue(TEXT("A deal to cut"), Sign(Manager, TEXT("CutGuy"), MakeOffer(TEXT("Hawks"), 2, 5000, 4000)));
    Manager->CutPlayer(FName(TEXT("CutGuy")), true);
    TestEqual(TEXT("2026: 20,000 + 30,000 + 2,000 dead"), Manager->GetCapUsed(Hawks, 2026), 52000);

    const FPSLeagueYearRollover Rollover = Manager->RolloverLeagueYear();
    TestEqual(TEXT("The new league year"), Rollover.LeagueYear, 2027);
    TestEqual(TEXT("The cap grows 5%"), Rollover.SalaryCap, 267750);
    TestEqual(TEXT("...and is the league's"), Manager->GetSalaryCap(), 267750);
    TestTrue(TEXT("The one-year deal expired"), Rollover.Expired.Num() == 1 && Rollover.Expired[0].PlayerId == FName(TEXT("OneYear")) && Rollover.Expired[0].TeamId == Hawks);
    TestNull(TEXT("...and is gone"), Manager->FindContract(FName(TEXT("OneYear"))));
    TestNotNull(TEXT("The three-year deal runs on"), Manager->FindContract(FName(TEXT("ThreeYear"))));
    TestEqual(TEXT("Unused space carries over, at most 10% of the cap"), Manager->GetCarryover(Hawks), 25500);
    TestEqual(TEXT("...for a team with no deals too"), Manager->GetCarryover(Wolves), 25500);
    TestEqual(TEXT("Last year's dead money is gone"), Manager->GetDeadMoney(Hawks, 2026), 0);
    TestEqual(TEXT("This year's stays"), Manager->GetDeadMoney(Hawks, 2027), 2000);
    TestEqual(TEXT("Space: cap + carryover - 30,000 - 2,000"), Manager->GetCapSpace(Hawks), 267750 + 25500 - 32000);
    TestEqual(TEXT("Nobody is over the cap"), Rollover.TeamsOverCap.Num(), 0);
    const float Market = Manager->GetLeagueCapSpaceFraction();
    TestTrue(TEXT("The market: the league's average space, a fraction"), Market > 0.f && Market <= 1.f);

    // A back-loaded deal fits this year and breaks next year's cap.
    UPSContractManager* Strapped = MakeManager();
    const FName Bears(TEXT("Bears"));
    FPSContract Balloon;
    Balloon.PlayerId = FName(TEXT("Balloon"));
    Balloon.TeamId = Bears;
    FPSContractYear& First = Balloon.Years.AddDefaulted_GetRef();
    First.LeagueYear = 2026;
    First.BaseSalary = 1000;
    FPSContractYear& Second = Balloon.Years.AddDefaulted_GetRef();
    Second.LeagueYear = 2027;
    Second.BaseSalary = 300000;
    TestEqual(TEXT("The back-loaded deal is signed"), Strapped->SignContract(Balloon), EPSCapResult::Ok);
    TestTrue(TEXT("A keeper"), Sign(Strapped, TEXT("Keeper"), MakeOffer(TEXT("Bears"), 2, 5000)));
    UPSRoster* Roster = MakeRosterOf({ MakeContractPlayer(TEXT("Balloon"), EPlayerRole::Linebacker, 80.f), MakeContractPlayer(TEXT("Keeper"), EPlayerRole::Linebacker, 90.f) });

    const FPSLeagueYearRollover Squeezed = Strapped->RolloverLeagueYear();
    TestTrue(TEXT("The Bears are over the new cap"), Squeezed.TeamsOverCap.Contains(Bears) && !Strapped->IsCompliant(Bears));
    TArray<FPlayerAttributes> Released;
    const TArray<FName> Cuts = Strapped->EnforceCompliance(Bears, Roster, Released);
    TestTrue(TEXT("The least value per cap dollar goes"), Cuts.Num() == 1 && Cuts[0] == FName(TEXT("Balloon")));
    TestTrue(TEXT("They comply now"), Strapped->IsCompliant(Bears));
    TestTrue(TEXT("He is off the roster"), Roster->FindPlayerPtr(FName(TEXT("Balloon"))) == nullptr && Roster->FindPlayerPtr(FName(TEXT("Keeper"))) != nullptr);
    TestTrue(TEXT("...and released"), Released.Num() == 1 && Released[0].PlayerId == FName(TEXT("Balloon")));
    TestEqual(TEXT("...and off the depth chart"), Roster->GetDepthChartForRole(EPlayerRole::Linebacker).Num(), 1);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Negotiation
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSContractNegotiationTest,
    "PlaySports.Contracts.NegotiationDemandsAndAnswers",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSContractNegotiationTest::RunTest(const FString& Parameters)
{
    using namespace PSContractTests;

    UPSContractManager* Manager = MakeManager();
    const FPSContractTuning& Tune = Manager->GetTuning();
    const int32 Cap = Manager->GetSalaryCap();

    // Performance: an elite young quarterback asks the top of his market, long and guaranteed.
    FPSNegotiationContext Elite;
    Elite.Role = EPlayerRole::Quarterback;
    Elite.Rating = 99.f;
    Elite.Age = 25;
    Elite.MarketCapSpaceFraction = Tune.NeutralCapSpaceFraction;
    const FPSContractDemand EliteDemand = UPSContractNegotiation::ComputeDemand(Tune, Cap, Elite);
    TestEqual(TEXT("20% of the cap"), EliteDemand.AnnualValue, 51000);
    TestEqual(TEXT("Five years"), EliteDemand.Years, 5);
    TestTrue(TEXT("The most guaranteed"), FMath::IsNearlyEqual(EliteDemand.GuaranteedFraction, Tune.MaxGuaranteeFraction));
    TestEqual(TEXT("He walks below 85%"), EliteDemand.WalkAwayValue, FMath::RoundToInt(51000 * Tune.WalkAwayRatio));

    FPSNegotiationContext Replacement = Elite;
    Replacement.Rating = 60.f;
    TestEqual(TEXT("A replacement player asks the minimum"), UPSContractNegotiation::ComputeDemand(Tune, Cap, Replacement).AnnualValue, Tune.MinimumSalary);
    FPSNegotiationContext Receiver = Elite;
    Receiver.Role = EPlayerRole::WideReceiver;
    TestTrue(TEXT("A receiver's market is smaller"), UPSContractNegotiation::ComputeDemand(Tune, Cap, Receiver).AnnualValue < EliteDemand.AnnualValue);

    // Age: at 32 fewer years, and the market pays less.
    FPSNegotiationContext Veteran = Elite;
    Veteran.Age = 32;
    const FPSContractDemand VeteranDemand = UPSContractNegotiation::ComputeDemand(Tune, Cap, Veteran);
    TestEqual(TEXT("Two years at 32"), VeteranDemand.Years, 2);
    TestEqual(TEXT("24% less at 32"), VeteranDemand.AnnualValue, 38760);

    // The market: cap space everywhere raises asks, within the limit; a tight league lowers them.
    FPSNegotiationContext Flush = Elite;
    Flush.MarketCapSpaceFraction = 1.f;
    FPSNegotiationContext Tight = Elite;
    Tight.MarketCapSpaceFraction = 0.f;
    TestEqual(TEXT("A flush market: +15%"), UPSContractNegotiation::ComputeDemand(Tune, Cap, Flush).AnnualValue, FMath::RoundToInt(51000 * 1.15f));
    TestTrue(TEXT("A tight market: less"), UPSContractNegotiation::ComputeDemand(Tune, Cap, Tight).AnnualValue < EliteDemand.AnnualValue);

    // Answers: at his ask he accepts, a little under he counters with it, far under he walks.
    const auto Answer = [&](const FPSNegotiationContext& Context, const TCHAR* TeamId, int32 Years, int32 AnnualValue, float Guaranteed)
    {
        return UPSContractNegotiation::EvaluateOffer(Tune, EliteDemand, Context, MakeOffer(TeamId, Years, AnnualValue, 0, Guaranteed));
    };
    const float AskedGuarantee = EliteDemand.GuaranteedFraction;
    TestEqual(TEXT("At his ask: accepted"), Answer(Elite, TEXT("Hawks"), 5, 51000, AskedGuarantee).Response, EPSNegotiationResponse::Accept);
    const FPSNegotiationResult Counter = Answer(Elite, TEXT("Hawks"), 5, 45900, AskedGuarantee);
    TestEqual(TEXT("At 90%: countered"), Counter.Response, EPSNegotiationResponse::Counter);
    TestEqual(TEXT("...with his ask"), Counter.Demand.AnnualValue, 51000);
    TestTrue(TEXT("...and says what he wants"), Counter.Reason.Contains(TEXT("5 years")));
    TestEqual(TEXT("At half: rejected"), Answer(Elite, TEXT("Hawks"), 5, 25500, AskedGuarantee).Response, EPSNegotiationResponse::Reject);
    TestEqual(TEXT("95% with far more guaranteed: accepted"), Answer(Elite, TEXT("Hawks"), 5, 48450, AskedGuarantee + 0.3f).Response, EPSNegotiationResponse::Accept);
    TestEqual(TEXT("His ask over 3 years instead of 5: countered"), Answer(Elite, TEXT("Hawks"), 3, 51000, AskedGuarantee).Response, EPSNegotiationResponse::Counter);

    // Morale: his own team's offer is worth more to a happy player, less to an unhappy one.
    FPSNegotiationContext Happy = Elite;
    Happy.CurrentTeamId = FName(TEXT("Hawks"));
    Happy.Morale = 1.f;
    FPSNegotiationContext Unhappy = Happy;
    Unhappy.Morale = 0.f;
    TestEqual(TEXT("Happy: 90% from his own team is enough"), Answer(Happy, TEXT("Hawks"), 5, 45900, AskedGuarantee).Response, EPSNegotiationResponse::Accept);
    TestEqual(TEXT("...not from another team"), Answer(Happy, TEXT("Wolves"), 5, 45900, AskedGuarantee).Response, EPSNegotiationResponse::Counter);
    TestTrue(TEXT("Unhappy: his own team's full ask is worth less"), Answer(Unhappy, TEXT("Hawks"), 5, 51000, AskedGuarantee).ValueRatio < 1.f);

    // An extension he counters changes nothing; one he accepts is made.
    const FName Vet(TEXT("HAW_Vet"));
    TestTrue(TEXT("Two years left"), Sign(Manager, TEXT("HAW_Vet"), MakeOffer(TEXT("Hawks"), 2, 10000)));
    const FPSNegotiationContext VetContext = Manager->MakeNegotiationContext(MakeContractPlayer(TEXT("HAW_Vet"), EPlayerRole::WideReceiver, 90.f), 28, 0.5f, FName(TEXT("Hawks")));
    const FPSContractDemand VetDemand = Manager->GetDemand(VetContext);
    TestEqual(TEXT("At 28 he wants four years"), VetDemand.Years, 4);
    FPSCapPreview Preview;
    const FPSNegotiationResult Countered = Manager->ProposeExtension(Vet, VetContext, MakeOffer(TEXT("Hawks"), 2, VetDemand.AnnualValue, 0, VetDemand.GuaranteedFraction), Preview);
    TestEqual(TEXT("Two more years at his ask: countered (he wants four)"), Countered.Response, EPSNegotiationResponse::Counter);
    TestTrue(TEXT("...with a valid preview"), Preview.bValid);
    FPSContract Unchanged;
    Manager->GetContract(Vet, Unchanged);
    TestEqual(TEXT("...and his deal unchanged"), Unchanged.Years.Num(), 2);
    const FPSNegotiationResult Accepted = Manager->ProposeExtension(Vet, VetContext, MakeOffer(TEXT("Hawks"), 2, FMath::RoundToInt(VetDemand.AnnualValue * 1.2f), 0, VetDemand.GuaranteedFraction), Preview);
    TestEqual(TEXT("20% over his ask: accepted"), Accepted.Response, EPSNegotiationResponse::Accept);
    FPSContract Extended;
    Manager->GetContract(Vet, Extended);
    TestEqual(TEXT("...and extended to four years"), Extended.Years.Num(), 4);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- Free agency
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFreeAgencyTest,
    "PlaySports.Contracts.FreeAgencyPeriod",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFreeAgencyTest::RunTest(const FString& Parameters)
{
    using namespace PSContractTests;

    FFreeAgencyRun Run = StartFreeAgency();
    UPSFreeAgency* Agency = Run.FreeAgency;
    const FName Falcons(TEXT("Falcons"));
    const FName Hawks(TEXT("Hawks"));
    TestTrue(TEXT("Free agency is open"), Agency->IsOpen() && Agency->GetDay() == 1);
    TestEqual(TEXT("Four free agents, the Falcons' unsigned veteran among them"), Agency->GetPool().Num(), 4);
    TestNull(TEXT("He left the Falcons' roster"), Run.Falcons->FindPlayerPtr(FName(TEXT("FAL_Vet"))));
    FPSFreeAgent Vet;
    TestTrue(TEXT("...for the pool"), Agency->GetFreeAgent(FName(TEXT("FAL_Vet")), Vet) && Vet.Age == 31 && Vet.PreviousTeamId == Falcons);
    TestTrue(TEXT("Every ask is set"), Vet.Demand.AnnualValue >= Run.Manager->GetTuning().MinimumSalary && Vet.OpeningDemand.AnnualValue == Vet.Demand.AnnualValue);
    TestTrue(TEXT("The CPU teams need every role"), Agency->GetTeamNeeds(Hawks).Num() == 8);

    // The user's offers: over the cap, too low, far above the ask, at the ask.
    TestEqual(TEXT("An offer the cap can't hold"), Agency->SubmitOffer(FName(TEXT("FA_OL")), MakeOffer(TEXT("Falcons"), 1, 300000)).Result, EPSFreeAgencyOfferResult::OverCap);
    TestEqual(TEXT("Half his ask"), Agency->SubmitOffer(FName(TEXT("FA_OL")), OfferAtDemand(Run, TEXT("Falcons"), TEXT("FA_OL"), 0.5f)).Result, EPSFreeAgencyOfferResult::Rejected);
    TestEqual(TEXT("A team not in free agency"), Agency->SubmitOffer(FName(TEXT("FA_OL")), OfferAtDemand(Run, TEXT("Sharks"), TEXT("FA_OL"), 1.f)).Result, EPSFreeAgencyOfferResult::Unavailable);
    const FPSFreeAgencyOfferResponse Star = Agency->SubmitOffer(FName(TEXT("FA_QB")), OfferAtDemand(Run, TEXT("Falcons"), TEXT("FA_QB"), 1.2f));
    TestEqual(TEXT("20% over his ask: signed on the spot"), Star.Result, EPSFreeAgencyOfferResult::Signed);
    TestNotNull(TEXT("He is on the Falcons"), Run.Falcons->FindPlayerPtr(FName(TEXT("FA_QB"))));
    FPSContract StarDeal;
    TestTrue(TEXT("...under contract to them"), Run.Manager->GetContract(FName(TEXT("FA_QB")), StarDeal) && StarDeal.TeamId == Falcons);
    TestEqual(TEXT("At his ask: weighed"), Agency->SubmitOffer(FName(TEXT("FA_WR")), OfferAtDemand(Run, TEXT("Falcons"), TEXT("FA_WR"), 1.f)).Result, EPSFreeAgencyOfferResult::Pending);

    // Day 1: the CPU teams bid for the receiver over the user's offer; he weighs them.
    TestEqual(TEXT("Nobody signs on day 1"), Agency->AdvanceDay().Num(), 0);
    FPSFreeAgent Receiver;
    Agency->GetFreeAgent(FName(TEXT("FA_WR")), Receiver);
    TestEqual(TEXT("Three offers for the receiver"), Receiver.Offers.Num(), 3);
    FPSFreeAgent CooledVet;
    Agency->GetFreeAgent(FName(TEXT("FAL_Vet")), CooledVet);
    const FPSContractTuning& Tune = Run.Manager->GetTuning();
    TestEqual(TEXT("An unsigned player's ask cools"), CooledVet.Demand.AnnualValue,
        FMath::Max(FMath::Max(Tune.MinimumSalary, FMath::RoundToInt(Vet.OpeningDemand.AnnualValue * Tune.DemandFloorFraction)), FMath::RoundToInt(Vet.OpeningDemand.AnnualValue * (1.f - Tune.DemandDecayPerDay))));

    // Day 2, his decision day: the best offer, a CPU team's above the user's.
    const TArray<FPSFreeAgentSigning> DayTwo = Agency->AdvanceDay();
    TestTrue(TEXT("The receiver signs on day 2 with the Hawks"), DayTwo.Num() == 1 && DayTwo[0].PlayerId == FName(TEXT("FA_WR")) && DayTwo[0].TeamId == Hawks && DayTwo[0].Day == 2);
    TestNotNull(TEXT("He is on the Hawks"), Run.Hawks->FindPlayerPtr(FName(TEXT("FA_WR"))));
    TestFalse(TEXT("He has left the pool"), Agency->GetFreeAgent(FName(TEXT("FA_WR")), Receiver));

    // The rest of the period: the linemen sign, and it closes.
    Agency->RunToEnd();
    TestFalse(TEXT("Free agency closes"), Agency->IsOpen());
    TestFalse(TEXT("The lineman signed somewhere"), Agency->GetFreeAgent(FName(TEXT("FA_OL")), Receiver));
    TestEqual(TEXT("Offers after it closes go nowhere"), Agency->SubmitOffer(FName(TEXT("FAL_Vet")), MakeOffer(TEXT("Falcons"), 1, 5000)).Result, EPSFreeAgencyOfferResult::Unavailable);
    for (const FPSFreeAgentSigning& Signing : Agency->GetSignings())
    {
        TestTrue(*FString::Printf(TEXT("%s's team is under the cap"), *Signing.PlayerId.ToString()), Run.Manager->IsCompliant(Signing.TeamId));
        TestNotNull(*FString::Printf(TEXT("%s is under contract"), *Signing.PlayerId.ToString()), Run.Manager->FindContract(Signing.PlayerId));
    }

    // The same pool and offers sign the same way.
    FFreeAgencyRun Again = StartFreeAgency();
    Again.FreeAgency->SubmitOffer(FName(TEXT("FA_QB")), OfferAtDemand(Again, TEXT("Falcons"), TEXT("FA_QB"), 1.2f));
    Again.FreeAgency->SubmitOffer(FName(TEXT("FA_WR")), OfferAtDemand(Again, TEXT("Falcons"), TEXT("FA_WR"), 1.f));
    Again.FreeAgency->RunToEnd();
    AddInfo(DescribeSignings(Agency->GetSignings()));
    TestEqual(TEXT("Free agency is deterministic"), DescribeSignings(Again.FreeAgency->GetSignings()), DescribeSignings(Agency->GetSignings()));
    return true;
}

// ---------------------------------------------------------------------------
// Test 7 -- The franchise: contracts through a season, the rollover and the save
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSContractFranchiseTest,
    "PlaySports.Contracts.FranchiseSeasonRolloverAndSave",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSContractFranchiseTest::RunTest(const FString& Parameters)
{
    using namespace PSContractTests;

    const FName Falcons(TEXT("Falcons"));
    UPSScheduleEngine* Schedule = NewObject<UPSScheduleEngine>();
    UPSFranchiseSeason* Season = NewObject<UPSFranchiseSeason>();
    const TArray<FName> TeamIds = { Falcons, FName(TEXT("Hawks")), FName(TEXT("Wolves")), FName(TEXT("Bears")) };
    Season->InitializeSeason(TeamIds, Schedule->GenerateSeasonSchedule(FDateTime(2026, 9, 1), 1, TArray<int32>()));
    UPSStaffManager* Staffs = NewObject<UPSStaffManager>();
    Staffs->LoadFromJson(UPSStaffManager::GetDefaultDataPath());

    UPSFranchiseFlow* Flow = NewObject<UPSFranchiseFlow>();
    Flow->Initialize(Season, Staffs, Falcons);
    TestEqual(TEXT("The shipped league's rosters"), Flow->LoadLeagueRosters(UPSUITeamCatalog::GetDefaultTeamsPath()), 4);
    UPSContractManager* Manager = MakeManager();
    Flow->SetContracts(Manager);

    // A new franchise: every rostered player signed at his demand, staggered lengths, under the cap.
    int32 Rostered = 0;
    for (const FName& TeamId : TeamIds)
    {
        Rostered += Flow->GetTeamRoster(TeamId)->GetFullRoster().Num();
    }
    TestEqual(TEXT("Every rostered player is signed"), Flow->SignLeagueContracts(), Rostered);
    int32 ExpiringNextYear = 0;
    TMap<FName, int32> ExpiringByTeam;
    for (const FPSContract& Contract : Manager->GetLedger().Contracts)
    {
        if (Contract.GetLastYear() == 2026)
        {
            ++ExpiringNextYear;
            ExpiringByTeam.FindOrAdd(Contract.TeamId) += 1;
        }
    }
    TestTrue(TEXT("Some deals run one year, not all"), ExpiringNextYear > 0 && ExpiringNextYear < Rostered);
    for (const FName& TeamId : TeamIds)
    {
        TestTrue(*FString::Printf(TEXT("The %s are under the cap"), *TeamId.ToString()), Manager->IsCompliant(TeamId));
    }
    const int32 FalconsBefore = Flow->GetTeamRoster(Falcons)->GetFullRoster().Num();

    // The season is played and ends: the league year turns over and free agency opens.
    TestEqual(TEXT("Week 1's two games"), Flow->SimulateWeek(true), 2);
    TestTrue(TEXT("The season ends"), Flow->AdvanceWeek());
    const FPSLeagueYearRollover& Rollover = Flow->GetLastRollover();
    TestEqual(TEXT("It is 2027"), Rollover.LeagueYear, 2027);
    TestEqual(TEXT("The one-year deals expired"), Rollover.Expired.Num(), ExpiringNextYear);
    UPSFreeAgency* Agency = Flow->GetFreeAgency();
    if (!TestNotNull(TEXT("Free agency opened"), Agency))
    {
        return false;
    }
    TestTrue(TEXT("...and is open"), Agency->IsOpen());
    TestEqual(TEXT("Every expired player is a free agent"), Agency->GetPool().Num(), ExpiringNextYear);
    TestEqual(TEXT("...off his old roster"), Flow->GetTeamRoster(Falcons)->GetFullRoster().Num(), FalconsBefore - ExpiringByTeam.FindRef(Falcons));

    // The CPU teams sign under the cap; every signing joins its roster.
    Agency->RunToEnd();
    TestTrue(TEXT("The CPU teams signed someone"), Agency->GetSignings().Num() > 0);
    for (const FPSFreeAgentSigning& Signing : Agency->GetSignings())
    {
        TestTrue(*FString::Printf(TEXT("%s is a CPU team's signing"), *Signing.PlayerId.ToString()), Signing.TeamId != Falcons);
        TestNotNull(*FString::Printf(TEXT("%s is on the %s"), *Signing.PlayerId.ToString(), *Signing.TeamId.ToString()), Flow->GetTeamRoster(Signing.TeamId)->FindPlayerPtr(Signing.PlayerId));
    }
    for (const FName& TeamId : TeamIds)
    {
        TestTrue(*FString::Printf(TEXT("The %s are still under the cap"), *TeamId.ToString()), Manager->IsCompliant(TeamId));
    }

    // The ledger round-trips through the franchise save.
    Manager->CutPlayer(Manager->GetTeamContracts(FName(TEXT("Hawks")))[0].PlayerId, true);
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    Manager->SaveTo(Save);
    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    const FString Slot = TEXT("Test_ContractLedger");
    TestTrue(TEXT("The franchise saves"), Saves->SaveToSlot(Save, Slot));
    const UPSFranchiseSaveGame* Loaded = Cast<UPSFranchiseSaveGame>(Saves->LoadFromSlot(Slot));
    UPSContractManager* Restored = MakeManager();
    if (TestNotNull(TEXT("...and loads as a franchise"), Loaded))
    {
        TestTrue(TEXT("The ledger loads"), Restored->LoadFrom(Loaded));
        TestEqual(TEXT("The league year"), Restored->GetLeagueYear(), 2027);
        TestEqual(TEXT("The cap"), Restored->GetSalaryCap(), Manager->GetSalaryCap());
        TestEqual(TEXT("Every contract"), Restored->GetLedger().Contracts.Num(), Manager->GetLedger().Contracts.Num());
        TestEqual(TEXT("The dead money"), Restored->GetLedger().DeadMoney.Num(), Manager->GetLedger().DeadMoney.Num());
        for (const FName& TeamId : TeamIds)
        {
            TestEqual(*FString::Printf(TEXT("The %s cap space"), *TeamId.ToString()), Restored->GetCapSpace(TeamId), Manager->GetCapSpace(TeamId));
        }
    }
    IFileManager::Get().Delete(*UPSSaveSubsystem::GetSlotPath(Slot), false, true, true);
    IFileManager::Get().Delete(*(UPSSaveSubsystem::GetSlotPath(Slot) + TEXT(".bak")), false, true, true);
    TestFalse(TEXT("A save from before contracts keeps the current ledger"), Restored->LoadFrom(NewObject<UPSFranchiseSaveGame>()));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
