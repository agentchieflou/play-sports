// PSDraftTests.cpp -- Epic 86 (the draft, scouting and the combine)
//
// Tests covered:
//   1. Data/draft.json loads through UPSDataIngestion, validates clean and equals FPSDraftTuning's
//      defaults field by field; a broken tuning's problems are each reported.
//   2. The class (Epic 122's draft-class mode): each prospect's true grade, the combine's drills
//      reading his true ratings, a pro day's results seen only by a team that scouts him, a public
//      projection surer after the combine, the busts and booms, the same seed making the same class.
//   3. Scouting: a budget from the owner economy's funding index, each report narrowing the range
//      exactly as the weights say, scouting beating the projection across a class, a misleading
//      report, the CPU spending its points over the best-projected.
//   4. The draft: rounds in order, the CPU taking its board's best (its estimate plus its need), the
//      player's team on the clock, rookies on the rosters with their true ratings and rookie-scale
//      contracts, the undrafted into free agency.
//   5. The franchise: a class prepared during the season, the draft opened after it with the worst
//      team first, rookies signed; the draft round-trips through the franchise save.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "PSContractData.h"
#include "PSContractManager.h"
#include "PSContractNegotiation.h"
#include "PSDataIngestion.h"
#include "PSDraft.h"
#include "PSDraftData.h"
#include "PSEconomyData.h"
#include "PSFranchiseFlow.h"
#include "PSFranchiseSaveGame.h"
#include "PSFranchiseSeason.h"
#include "PSFreeAgency.h"
#include "PSLeagueGenerator.h"
#include "PSLeagueGeneratorData.h"
#include "PSOwnerEconomy.h"
#include "PSRoster.h"
#include "PSSaveSubsystem.h"
#include "PSScheduleEngine.h"
#include "PSStaffData.h"
#include "PSStaffManager.h"
#include "PSUITeamCatalog.h"
#include "Engine/GameInstance.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSDraftTests
{
    /** The shipped tuning, with Adjust applied. */
    template <typename AdjustType>
    static UPSDraft* MakeDraft(AdjustType&& Adjust)
    {
        UPSDraft* Draft = NewObject<UPSDraft>();
        Draft->LoadTuningFromJson(UPSDraft::GetDefaultTuningPath());
        FPSDraftTuning Tuning = Draft->GetTuning();
        Adjust(Tuning);
        Draft->SetTuning(Tuning);
        return Draft;
    }

    static UPSDraft* MakeShippedDraft()
    {
        return MakeDraft([](FPSDraftTuning&) {});
    }

    /** A class for NumTeams teams from the shipped generator. */
    static FPSDraftClass MakeClass(int32 Seed, int32 NumTeams)
    {
        return NewObject<UPSLeagueGenerator>()->GenerateDraftClass(Seed, 2027, NumTeams, TArray<FPlayerAttributes>());
    }

    static const FPSProspect* FindProspectOf(const UPSDraft* Draft, FName PlayerId)
    {
        return Draft->GetState().Prospects.FindByPredicate([PlayerId](const FPSProspect& Prospect) { return Prospect.Player.PlayerId == PlayerId; });
    }

    static FPlayerAttributes MakeDraftPlayer(const FString& PlayerId, EPlayerRole Role, float Rating)
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
        Player.Age = 26;
        return Player;
    }

    /** A roster at the contract market's RosterTarget for every role (but SkipRole's, when given). */
    static UPSRoster* MakeTargetRoster(const UPSContractManager* Contracts, const FString& Prefix, bool bSkipQuarterbacks)
    {
        TArray<FPlayerAttributes> Players;
        for (const FPSPositionMarket& Market : Contracts->GetTuning().PositionMarkets)
        {
            if (bSkipQuarterbacks && Market.Role == EPlayerRole::Quarterback)
            {
                continue;
            }
            for (int32 Index = 0; Index < Market.RosterTarget; ++Index)
            {
                Players.Add(MakeDraftPlayer(FString::Printf(TEXT("%s_%d_%d"), *Prefix, static_cast<int32>(Market.Role), Index), Market.Role, 70.f));
            }
        }
        UPSRoster* Roster = NewObject<UPSRoster>();
        Roster->InitializeRoster(Players);
        Roster->BuildDefaultDepthChart();
        return Roster;
    }

    static UPSContractManager* MakeContracts()
    {
        UPSContractManager* Contracts = NewObject<UPSContractManager>();
        Contracts->LoadTuningFromJson(UPSContractManager::GetDefaultTuningPath());
        Contracts->StartLeague();
        return Contracts;
    }

    static float Width(const FPSProspectView& View)
    {
        return View.RangeHigh - View.RangeLow;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The shipped tuning
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDraftTuningTest,
    "PlaySports.Draft.TuningLoadsAndValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDraftTuningTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSDraftTuning FromFile;
    if (!TestTrue(TEXT("Data/draft.json loads"), Ingestion->LoadDraftTuningFromJson(UPSDraft::GetDefaultTuningPath(), FromFile)))
    {
        return false;
    }
    const TArray<FString> Problems = UPSDraft::ValidateTuning(FromFile);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    TestEqual(TEXT("The shipped tuning is sound"), Problems.Num(), 0);
    TestEqual(TEXT("Five combine drills"), FromFile.CombineDrills.Num(), 5);
    const FPSDraftTuning Defaults;
    for (TFieldIterator<FProperty> It(FPSDraftTuning::StaticStruct()); It; ++It)
    {
        if (It->GetFName() != GET_MEMBER_NAME_CHECKED(FPSDraftTuning, CombineDrills))
        {
            TestTrue(*FString::Printf(TEXT("%s matches the default"), *It->GetName()), It->Identical_InContainer(&FromFile, &Defaults));
        }
    }

    FPSDraftTuning Broken = FromFile;
    Broken.ReportCost = 0.f;
    Broken.ProDayShare = 1.5f;
    Broken.NumRounds = 0;
    Broken.CombineDrills[0].Attribute = FName(TEXT("WeightKg"));
    Broken.CombineDrills[2].DrillId = Broken.CombineDrills[1].DrillId;
    const TArray<FString> BrokenProblems = UPSDraft::ValidateTuning(Broken);
    for (const TCHAR* Expected : { TEXT("ReportCost"), TEXT("ProDayShare"), TEXT("NumRounds"), TEXT("reads 'WeightKg'"), TEXT("duplicate drill 'TenYardSplit'") })
    {
        TestTrue(*FString::Printf(TEXT("Reported: %s"), Expected),
            BrokenProblems.ContainsByPredicate([Expected](const FString& Line) { return Line.Contains(Expected); }));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The class, the combine and the projections
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDraftClassTest,
    "PlaySports.Draft.ProspectsCombineAndProjections",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDraftClassTest::RunTest(const FString& Parameters)
{
    using namespace PSDraftTests;

    UPSDraft* Draft = MakeShippedDraft();
    const FPSDraftTuning& T = Draft->GetTuning();
    TestFalse(TEXT("An empty class is refused"), Draft->PrepareClass(FPSDraftClass(), 7));
    const FPSDraftClass Class = MakeClass(7, 32);
    if (!TestTrue(TEXT("A league's class is prepared"), Draft->PrepareClass(Class, 7)))
    {
        return false;
    }
    TestEqual(TEXT("Every prospect"), Draft->GetState().Prospects.Num(), Class.Prospects.Num());
    TestEqual(TEXT("The class's year"), Draft->GetState().DraftYear, 2027);

    // The same class without busts and booms shows the combine's certainty plainly.
    UPSDraft* AllSwing = MakeDraft([](FPSDraftTuning& Tuning) { Tuning.BoomBustChance = 1.f; });
    UPSDraft* NoSwing = MakeDraft([](FPSDraftTuning& Tuning) { Tuning.BoomBustChance = 0.f; });
    AllSwing->PrepareClass(Class, 7);
    NoSwing->PrepareClass(Class, 7);

    int32 Combine = 0;
    float CombineError = 0.f;
    float ProDayError = 0.f;
    bool bDrillsRead = true;
    for (const FPSProspect& Prospect : NoSwing->GetState().Prospects)
    {
        TestTrue(*FString::Printf(TEXT("%s: his true grade is his market rating"), *Prospect.Player.PlayerId.ToString()),
            FMath::IsNearlyEqual(Prospect.TrueGrade, UPSContractNegotiation::RatePlayer(Prospect.Player), 1e-4f));
        TestTrue(*FString::Printf(TEXT("%s: a rookie's age"), *Prospect.Player.PlayerId.ToString()), Prospect.Player.Age >= 21 && Prospect.Player.Age <= 23);
        TestTrue(*FString::Printf(TEXT("%s: the projection's uncertainty"), *Prospect.Player.PlayerId.ToString()),
            FMath::IsNearlyEqual(Prospect.PublicUncertainty, T.PublicUncertainty * (Prospect.bAttendedCombine ? T.CombineCertainty : 1.f), 1e-4f));
        for (int32 Index = 0; Index < T.CombineDrills.Num() && Index < Prospect.Measurables.Num(); ++Index)
        {
            const FPSCombineDrill& Drill = T.CombineDrills[Index];
            float Rating = 0.f;
            PSStaff::GetAttribute(Prospect.Player, Drill.Attribute, Rating);
            bDrillsRead &= Prospect.Measurables[Index].DrillId == Drill.DrillId && FMath::Abs(Prospect.Measurables[Index].Value - (Drill.Base + Drill.PerPoint * Rating)) <= 5.f * Drill.Noise + 1e-3f;
        }
        Combine += Prospect.bAttendedCombine ? 1 : 0;
        (Prospect.bAttendedCombine ? CombineError : ProDayError) += FMath::Abs(Prospect.Projection - Prospect.TrueGrade);
    }
    const int32 ProDay = NoSwing->GetState().Prospects.Num() - Combine;
    TestTrue(TEXT("Every drill reads his true rating"), bDrillsRead);
    AddInfo(FString::Printf(TEXT("%d at the combine (error %.2f), %d at pro days (error %.2f)"), Combine, Combine > 0 ? CombineError / Combine : 0.f, ProDay, ProDay > 0 ? ProDayError / ProDay : 0.f));
    TestTrue(TEXT("Most go to the combine; some hold pro days"), ProDay > 0 && Combine > 3 * ProDay);
    TestTrue(TEXT("The combine makes the projection surer"), ProDay > 0 && Combine > 0 && CombineError / Combine < ProDayError / ProDay);

    // A pro day's results are seen only by a team that scouts him.
    const FPSProspect* ProDayProspect = Draft->GetState().Prospects.FindByPredicate([](const FPSProspect& Prospect) { return !Prospect.bAttendedCombine; });
    const FPSProspect* CombineProspect = Draft->GetState().Prospects.FindByPredicate([](const FPSProspect& Prospect) { return Prospect.bAttendedCombine; });
    Draft->RegisterTeam(FName(TEXT("Hawks")), nullptr, false, 1.f);
    if (TestNotNull(TEXT("A pro-day prospect"), ProDayProspect) && TestNotNull(TEXT("A combine prospect"), CombineProspect))
    {
        const FName Hidden = ProDayProspect->Player.PlayerId;
        TestEqual(TEXT("The combine's results are public"), Draft->GetProspectView(FName(TEXT("Hawks")), CombineProspect->Player.PlayerId).Measurables.Num(), T.CombineDrills.Num());
        TestEqual(TEXT("A pro day's are not"), Draft->GetProspectView(FName(TEXT("Hawks")), Hidden).Measurables.Num(), 0);
        TestTrue(TEXT("The Hawks scout him"), Draft->Scout(FName(TEXT("Hawks")), Hidden));
        TestEqual(TEXT("...and see his pro day"), Draft->GetProspectView(FName(TEXT("Hawks")), Hidden).Measurables.Num(), T.CombineDrills.Num());
        TestEqual(TEXT("...the Wolves don't"), Draft->GetProspectView(FName(TEXT("Wolves")), Hidden).Measurables.Num(), 0);
        const FPSProspectView View = Draft->GetProspectView(FName(TEXT("Hawks")), Hidden);
        TestTrue(TEXT("A view shows his style and his projection"), View.DNA.Mobility == ProDayProspect->Player.DNA.Mobility && View.Projection == ProDayProspect->Projection);
    }

    // The busts and the booms: the same draws, a swing either way for everyone at a chance of 1.
    bool bSwung = true;
    for (int32 Index = 0; Index < Class.Prospects.Num(); ++Index)
    {
        bSwung &= FMath::IsNearlyEqual(FMath::Abs(AllSwing->GetState().Prospects[Index].Projection - NoSwing->GetState().Prospects[Index].Projection), T.BoomBustSwing, 1e-3f);
    }
    TestTrue(TEXT("Every bust or boom is off by BoomBustSwing more"), bSwung);

    // The same seed, the same class.
    UPSDraft* Again = MakeShippedDraft();
    Again->PrepareClass(MakeClass(7, 32), 7);
    bool bSame = Again->GetState().Prospects.Num() == Draft->GetState().Prospects.Num();
    for (int32 Index = 0; bSame && Index < Again->GetState().Prospects.Num(); ++Index)
    {
        bSame &= Again->GetState().Prospects[Index].Projection == Draft->GetState().Prospects[Index].Projection;
    }
    TestTrue(TEXT("The same seed projects the same"), bSame);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Scouting
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDraftScoutingTest,
    "PlaySports.Draft.ScoutingNarrowsTheRange",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDraftScoutingTest::RunTest(const FString& Parameters)
{
    using namespace PSDraftTests;

    UPSDraft* Draft = MakeShippedDraft();
    const FPSDraftTuning& T = Draft->GetTuning();
    Draft->PrepareClass(MakeClass(11, 4), 11);

    // The budget: the owner economy's scouting funding against the league's average.
    UPSOwnerEconomy* Economy = NewObject<UPSOwnerEconomy>();
    Economy->LoadTuningFromJson(UPSOwnerEconomy::GetDefaultTuningPath());
    Economy->RegisterTeam(FName(TEXT("Rich")));
    Economy->RegisterTeam(FName(TEXT("Poor")));
    FPSTeamBudget Lavish = Economy->GetTuning().DefaultBudget;
    Lavish.ScoutingFraction *= 3.f;
    TestTrue(TEXT("The Rich triple their scouting budget"), Economy->SetBudget(FName(TEXT("Rich")), Lavish));
    const float RichIndex = Economy->GetFundingIndex(FName(TEXT("Rich")), EPSBudgetDepartment::Scouting);
    const float PoorIndex = Economy->GetFundingIndex(FName(TEXT("Poor")), EPSBudgetDepartment::Scouting);
    Draft->RegisterTeam(FName(TEXT("Rich")), nullptr, false, RichIndex);
    Draft->RegisterTeam(FName(TEXT("Poor")), nullptr, false, PoorIndex);
    Draft->RegisterTeam(FName(TEXT("Hawks")), nullptr, true, 1.f);
    FPSTeamScouting Rich;
    FPSTeamScouting Poor;
    FPSTeamScouting Hawks;
    Draft->GetTeamScouting(FName(TEXT("Rich")), Rich);
    Draft->GetTeamScouting(FName(TEXT("Poor")), Poor);
    Draft->GetTeamScouting(FName(TEXT("Hawks")), Hawks);
    TestTrue(TEXT("Average funding: PointsPerSeason"), FMath::IsNearlyEqual(Hawks.Points, T.PointsPerSeason, 1e-3f));
    TestTrue(TEXT("The Rich buy more scouting"), FMath::IsNearlyEqual(Rich.Points, T.PointsPerSeason * Draft->GetFundingMultiplier(RichIndex), 1e-3f) && Rich.Points > Hawks.Points);
    TestTrue(TEXT("The Poor less"), Poor.Points < Hawks.Points);
    Draft->RegisterTeam(FName(TEXT("Hawks")), nullptr, true, 5.f);
    Draft->GetTeamScouting(FName(TEXT("Hawks")), Hawks);
    TestTrue(TEXT("Registering again keeps the scouting"), FMath::IsNearlyEqual(Hawks.Points, T.PointsPerSeason, 1e-3f));

    // Each report narrows the range by the weights.
    const FPSProspect& Target = Draft->GetState().Prospects[0];
    const FName TargetId = Target.Player.PlayerId;
    const float PriorVariance = FMath::Square(Target.PublicUncertainty);
    const float ReportVariance = FMath::Square(T.ReportNoise);
    float LastWidth = Width(Draft->GetProspectView(FName(TEXT("Hawks")), TargetId));
    TestTrue(TEXT("Unscouted: the public range"), FMath::IsNearlyEqual(LastWidth, 2.f * T.RangeSigmas * Target.PublicUncertainty, 1e-3f));
    for (int32 Reports = 1; Reports <= 4; ++Reports)
    {
        TestTrue(*FString::Printf(TEXT("Report %d is filed"), Reports), Draft->Scout(FName(TEXT("Hawks")), TargetId));
        const float Expected = 2.f * T.RangeSigmas / FMath::Sqrt(1.f / PriorVariance + Reports / ReportVariance);
        const float Now = Width(Draft->GetProspectView(FName(TEXT("Hawks")), TargetId));
        TestTrue(*FString::Printf(TEXT("After %d reports the range is %.2f wide"), Reports, Expected), FMath::IsNearlyEqual(Now, Expected, 1e-3f) && Now < LastWidth);
        LastWidth = Now;
    }
    TestEqual(TEXT("Four reports on file"), Draft->GetProspectView(FName(TEXT("Hawks")), TargetId).Reports, 4);
    TestEqual(TEXT("The Wolves know nothing of them"), Draft->GetProspectView(FName(TEXT("Wolves")), TargetId).Reports, 0);

    // The budget runs out.
    int32 Filed = 4;
    while (Draft->Scout(FName(TEXT("Hawks")), TargetId))
    {
        ++Filed;
    }
    TestEqual(TEXT("Sixty points, sixty reports"), Filed, FMath::FloorToInt(T.PointsPerSeason / T.ReportCost));
    TestFalse(TEXT("No points, no report"), Draft->Scout(FName(TEXT("Hawks")), Draft->GetState().Prospects[1].Player.PlayerId));
    TestFalse(TEXT("Nor on a prospect not in the class"), Draft->Scout(FName(TEXT("Rich")), FName(TEXT("Nobody"))));

    // The CPU spends its points over the best-projected.
    const int32 RichReports = Draft->AutoScout(FName(TEXT("Rich")));
    TestEqual(TEXT("The Rich spend everything"), RichReports, FMath::FloorToInt(Rich.Points / T.ReportCost));
    Draft->GetTeamScouting(FName(TEXT("Rich")), Rich);
    TestTrue(TEXT("...on AIScoutTargets prospects at most"), Rich.Records.Num() <= T.AIScoutTargets && Rich.Records.Num() > 0);

    // Scouting beats the projection: eight reports on everyone, with points to spare.
    UPSDraft* Thorough = MakeDraft([](FPSDraftTuning& Tuning) { Tuning.PointsPerSeason = 10000.f; });
    Thorough->PrepareClass(MakeClass(11, 4), 11);
    Thorough->RegisterTeam(FName(TEXT("Hawks")), nullptr, true, 1.f);
    float ProjectionError = 0.f;
    float EstimateError = 0.f;
    for (const FPSProspect& Prospect : Thorough->GetState().Prospects)
    {
        for (int32 Report = 0; Report < 8; ++Report)
        {
            Thorough->Scout(FName(TEXT("Hawks")), Prospect.Player.PlayerId);
        }
        ProjectionError += FMath::Abs(Prospect.Projection - Prospect.TrueGrade);
        EstimateError += FMath::Abs(Thorough->GetProspectView(FName(TEXT("Hawks")), Prospect.Player.PlayerId).Estimate - Prospect.TrueGrade);
    }
    AddInfo(FString::Printf(TEXT("Total error over the class: projections %.1f, scouted estimates %.1f"), ProjectionError, EstimateError));
    TestTrue(TEXT("Scouted estimates are nearer the truth than the projections"), EstimateError < ProjectionError);

    // A scout who has him all wrong.
    UPSDraft* Misled = MakeDraft([](FPSDraftTuning& Tuning) { Tuning.MisleadChance = 1.f; Tuning.ReportNoise = 0.01f; });
    Misled->PrepareClass(MakeClass(11, 4), 11);
    Misled->RegisterTeam(FName(TEXT("Hawks")), nullptr, true, 1.f);
    const FPSProspect& Misread = Misled->GetState().Prospects[0];
    Misled->Scout(FName(TEXT("Hawks")), Misread.Player.PlayerId);
    FPSTeamScouting Fooled;
    Misled->GetTeamScouting(FName(TEXT("Hawks")), Fooled);
    TestTrue(TEXT("A misleading report is off by MisleadSwing"), Fooled.Records.Num() == 1
        && FMath::IsNearlyEqual(FMath::Abs(Fooled.Records[0].SumReads - Misread.TrueGrade), Misled->GetTuning().MisleadSwing, 0.1f));
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The draft
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDraftRoundsTest,
    "PlaySports.Draft.RoundsNeedsAndRookies",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDraftRoundsTest::RunTest(const FString& Parameters)
{
    using namespace PSDraftTests;

    UPSDraft* Draft = MakeDraft([](FPSDraftTuning& Tuning) { Tuning.NeedWeight = 100.f; });
    const FPSDraftTuning& T = Draft->GetTuning();
    UPSContractManager* Contracts = MakeContracts();
    const FName Bare(TEXT("Bare"));
    const FName Hawks(TEXT("Hawks"));
    const FName Wolves(TEXT("Wolves"));
    const FName Bears(TEXT("Bears"));
    TMap<FName, UPSRoster*> Rosters;
    Rosters.Add(Bare, MakeTargetRoster(Contracts, TEXT("BARE"), true));
    Rosters.Add(Hawks, MakeTargetRoster(Contracts, TEXT("HAWK"), false));
    Rosters.Add(Wolves, MakeTargetRoster(Contracts, TEXT("WOLF"), false));
    Rosters.Add(Bears, MakeTargetRoster(Contracts, TEXT("BEAR"), false));
    UPSFreeAgency* Agency = NewObject<UPSFreeAgency>();
    Agency->Initialize(Contracts);
    Agency->BeginPeriod();

    const FPSDraftClass Class = MakeClass(21, 4);
    Draft->PrepareClass(Class, 21);
    for (const TPair<FName, UPSRoster*>& Team : Rosters)
    {
        Draft->RegisterTeam(Team.Key, Team.Value, Team.Key == Bears, 1.f);
    }
    TestFalse(TEXT("No draft before it opens"), Draft->AutoPick());
    if (!TestTrue(TEXT("The draft opens"), Draft->BeginDraft({ Bare, Hawks, Wolves, Bears }, Contracts, Agency)))
    {
        return false;
    }
    TestFalse(TEXT("...once"), Draft->BeginDraft({ Bare, Hawks, Wolves, Bears }, Contracts, Agency));
    TestEqual(TEXT("Bare needs quarterbacks"), Draft->GetNeed(Bare, EPlayerRole::Quarterback), 1.f);
    TestEqual(TEXT("The Hawks don't"), Draft->GetNeed(Hawks, EPlayerRole::Quarterback), 0.f);
    const int32 NumPicks = FMath::Min(T.NumRounds * 4, Class.Prospects.Num());
    TestEqual(TEXT("NumRounds rounds of four"), Draft->GetState().Order.Num(), NumPicks);
    FPSTeamScouting HawksScouting;
    FPSTeamScouting BearsScouting;
    Draft->GetTeamScouting(Hawks, HawksScouting);
    Draft->GetTeamScouting(Bears, BearsScouting);
    TestTrue(TEXT("The CPU spent its points"), HawksScouting.Points < T.ReportCost && HawksScouting.Records.Num() > 0);
    TestTrue(TEXT("The player's are still his"), FMath::IsNearlyEqual(BearsScouting.Points, T.PointsPerSeason, 1e-3f));

    // Bare is on the clock and takes its board's best: with no quarterback, a quarterback.
    TestEqual(TEXT("Bare picks first"), Draft->GetTeamOnTheClock(), Bare);
    FName Expected;
    float ExpectedValue = 0.f;
    for (const FPSProspect& Prospect : Draft->GetState().Prospects)
    {
        const float Value = Draft->GetPickValue(Bare, Prospect.Player.PlayerId);
        if (Expected.IsNone() || Value > ExpectedValue)
        {
            Expected = Prospect.Player.PlayerId;
            ExpectedValue = Value;
        }
    }
    const bool bClassHasPasser = Class.Prospects.ContainsByPredicate([](const FPlayerAttributes& Player) { return Player.Role == EPlayerRole::Quarterback; });
    const TArray<FPSDraftPick> Opening = Draft->AdvanceDraft();
    if (TestEqual(TEXT("The CPU picks until the player is on the clock"), Opening.Num(), 3))
    {
        TestEqual(TEXT("Bare's pick is its board's best"), Opening[0].PlayerId, Expected);
        const FPSProspect* First = FindProspectOf(Draft, Opening[0].PlayerId);
        TestTrue(TEXT("...a quarterback, its need"), !bClassHasPasser || (First && First->Player.Role == EPlayerRole::Quarterback));
    }
    TestEqual(TEXT("The Bears are on the clock"), Draft->GetTeamOnTheClock(), Bears);
    TestEqual(TEXT("...with the fourth pick"), Draft->GetCurrentPick(), 4);
    TestTrue(TEXT("The draft waits for him"), Draft->IsOpen() && Draft->AdvanceDraft().Num() == 0);

    // The player picks his own man.
    const TArray<FPSProspectView> Board = Draft->GetBoard(Bears);
    TestFalse(TEXT("A drafted prospect can't be picked"), Opening.Num() > 0 && Draft->MakePick(Opening[0].PlayerId));
    TestFalse(TEXT("...nor scouted"), Opening.Num() > 0 && Draft->Scout(Bears, Opening[0].PlayerId));
    if (TestTrue(TEXT("The Bears' board"), Board.Num() > 1))
    {
        TestTrue(TEXT("...best first"), Board[0].Estimate >= Board[1].Estimate);
        TestTrue(TEXT("The Bears take their second-best"), Draft->MakePick(Board[1].PlayerId));
        TestEqual(TEXT("...him"), Draft->GetPicks().Last().PlayerId, Board[1].PlayerId);
    }

    // The rest of the draft.
    Draft->RunToEnd();
    TestTrue(TEXT("The draft is over"), Draft->IsComplete() && !Draft->IsOpen());
    TestEqual(TEXT("Every pick made"), Draft->GetPicks().Num(), NumPicks);
    if (Draft->GetPicks().Num() == NumPicks && NumPicks > 4)
    {
        const FPSDraftPick& Fifth = Draft->GetPicks()[4];
        TestTrue(TEXT("Pick 5 opens round 2"), Fifth.Round == 2 && Fifth.PickInRound == 1 && Fifth.TeamId == Bare);
        TestEqual(TEXT("The first pick is paid FirstPickSalary"), Draft->GetPicks()[0].AnnualValue, T.FirstPickSalary);
        TestEqual(TEXT("The last the minimum"), Draft->GetPicks().Last().AnnualValue, Contracts->GetTuning().MinimumSalary);
    }
    int32 LastSalary = TNumericLimits<int32>::Max();
    for (const FPSDraftPick& Pick : Draft->GetPicks())
    {
        const FString Label = FString::Printf(TEXT("Pick %d (%s)"), Pick.Overall, *Pick.PlayerId.ToString());
        const FPSProspect* Prospect = FindProspectOf(Draft, Pick.PlayerId);
        FPlayerAttributes Rostered;
        TestTrue(*(Label + TEXT(": on his team's roster")), Rosters[Pick.TeamId]->FindPlayerById(Pick.PlayerId, Rostered));
        TestTrue(*(Label + TEXT(": with his true ratings")), Prospect && Rostered.Awareness == Prospect->Player.Awareness && Rostered.Speed == Prospect->Player.Speed);
        TestTrue(*(Label + TEXT(": signed to the rookie scale")), Pick.bSigned && Contracts->GetCapHit(Pick.PlayerId, Contracts->GetLeagueYear()) == Pick.AnnualValue);
        TestTrue(*(Label + TEXT(": paid no more than the pick before")), Pick.AnnualValue <= LastSalary);
        TestTrue(*(Label + TEXT(": his true grade revealed")), Prospect && Pick.TrueGrade == Prospect->TrueGrade);
        LastSalary = Pick.AnnualValue;
    }
    FPSContract RookieDeal;
    if (TestTrue(TEXT("The first pick's contract"), Contracts->GetContract(Draft->GetPicks()[0].PlayerId, RookieDeal)))
    {
        TestEqual(TEXT("...runs RookieYears"), RookieDeal.Years.Num(), T.RookieYears);
    }
    int32 Undrafted = 0;
    for (const FPSProspect& Prospect : Draft->GetState().Prospects)
    {
        if (Prospect.DraftedBy.IsNone())
        {
            ++Undrafted;
            FPSFreeAgent FreeAgent;
            TestTrue(*FString::Printf(TEXT("%s, undrafted, is a free agent"), *Prospect.Player.PlayerId.ToString()), Agency->GetFreeAgent(Prospect.Player.PlayerId, FreeAgent));
        }
    }
    TestEqual(TEXT("The rest of the class went undrafted"), Undrafted, Class.Prospects.Num() - NumPicks);
    TestEqual(TEXT("The draft reads a line a pick"), Draft->DescribeDraft().Num(), NumPicks + 1);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The franchise and the save
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDraftFranchiseTest,
    "PlaySports.Draft.FranchiseFlowAndSave",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDraftFranchiseTest::RunTest(const FString& Parameters)
{
    using namespace PSDraftTests;

    UPSScheduleEngine* Schedule = NewObject<UPSScheduleEngine>();
    UPSFranchiseSeason* Season = NewObject<UPSFranchiseSeason>();
    const TArray<FName> TeamIds = { FName(TEXT("Falcons")), FName(TEXT("Hawks")), FName(TEXT("Wolves")), FName(TEXT("Bears")) };
    Season->InitializeSeason(TeamIds, Schedule->GenerateSeasonSchedule(FDateTime(2026, 9, 1), 1, TArray<int32>()));
    UPSStaffManager* Staffs = NewObject<UPSStaffManager>();
    Staffs->LoadFromJson(UPSStaffManager::GetDefaultDataPath());
    UPSFranchiseFlow* Flow = NewObject<UPSFranchiseFlow>();
    Flow->Initialize(Season, Staffs, FName(TEXT("Falcons")));
    Flow->LoadLeagueRosters(UPSUITeamCatalog::GetDefaultTeamsPath());
    UPSContractManager* Contracts = MakeContracts();
    Flow->SetContracts(Contracts);
    Flow->SignLeagueContracts();
    UPSDraft* Draft = MakeShippedDraft();
    Flow->SetDraft(Draft);
    UPSLeagueGenerator* Generator = NewObject<UPSLeagueGenerator>();

    // During the season: the coming class, scouted.
    const int32 LeagueYear = Contracts->GetLeagueYear();
    if (!TestTrue(TEXT("The coming class is prepared"), Flow->PrepareDraft(Generator, 31)))
    {
        return false;
    }
    TestEqual(TEXT("...next league year's"), Draft->GetState().DraftYear, LeagueYear + 1);
    TestEqual(TEXT("...a class for four teams"), Draft->GetState().Prospects.Num(), 4 * Generator->GetTuning().DraftClass.ProspectsPerTeam);
    TestEqual(TEXT("Every team scouts it"), Draft->GetState().Teams.Num(), TeamIds.Num());
    FPSTeamScouting Falcons;
    Draft->GetTeamScouting(FName(TEXT("Falcons")), Falcons);
    TestTrue(TEXT("...the player's as his"), Falcons.bUserControlled);
    TestTrue(TEXT("The player scouts"), Draft->Scout(FName(TEXT("Falcons")), Draft->GetState().Prospects[0].Player.PlayerId));
    TestFalse(TEXT("No draft before the season ends"), Flow->BeginDraft());

    // The season ends; the draft opens with the worst team first.
    TestEqual(TEXT("The week's games"), Flow->SimulateWeek(true), 2);
    TestTrue(TEXT("The season ends"), Flow->AdvanceWeek());
    if (!TestTrue(TEXT("The draft opens"), Flow->BeginDraft()))
    {
        return false;
    }
    const TArray<FPSTeamStanding> Final = Season->GetSortedStandings();
    TestEqual(TEXT("The worst team picks first"), Draft->GetTeamOnTheClock(), Final.Last().TeamId);
    TestEqual(TEXT("...the best last in the round"), Draft->GetState().Order[TeamIds.Num() - 1], Final[0].TeamId);
    Draft->RunToEnd();
    TestTrue(TEXT("The draft is over"), Draft->IsComplete());
    int32 Signed = 0;
    for (const FPSDraftPick& Pick : Draft->GetPicks())
    {
        FPlayerAttributes Rookie;
        TestTrue(*FString::Printf(TEXT("%s joins %s"), *Pick.PlayerId.ToString(), *Pick.TeamId.ToString()), Flow->GetTeamRoster(Pick.TeamId)->FindPlayerById(Pick.PlayerId, Rookie));
        TestEqual(*FString::Printf(TEXT("%s: signed as the contracts say"), *Pick.PlayerId.ToString()), Pick.bSigned, Contracts->FindContract(Pick.PlayerId) != nullptr);
        Signed += Pick.bSigned ? 1 : 0;
    }
    TestTrue(TEXT("Rookies signed"), Signed > 0);
    UPSFreeAgency* Agency = Flow->GetFreeAgency();
    if (TestNotNull(TEXT("Free agency is open"), Agency))
    {
        for (const FPSProspect& Prospect : Draft->GetState().Prospects)
        {
            FPSFreeAgent FreeAgent;
            TestTrue(*FString::Printf(TEXT("%s: drafted or a free agent"), *Prospect.Player.PlayerId.ToString()),
                !Prospect.DraftedBy.IsNone() || Agency->GetFreeAgent(Prospect.Player.PlayerId, FreeAgent));
        }
    }

    // The draft round-trips through the franchise save.
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    Draft->SaveTo(Save);
    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    const FString Slot = TEXT("Test_Draft");
    TestTrue(TEXT("The franchise saves"), Saves->SaveToSlot(Save, Slot));
    const UPSFranchiseSaveGame* Loaded = Cast<UPSFranchiseSaveGame>(Saves->LoadFromSlot(Slot));
    UPSDraft* Restored = MakeShippedDraft();
    if (TestNotNull(TEXT("...and loads"), Loaded) && TestTrue(TEXT("The draft loads"), Restored->LoadFrom(Loaded)))
    {
        TestEqual(TEXT("The class"), Restored->GetState().Prospects.Num(), Draft->GetState().Prospects.Num());
        TestEqual(TEXT("The picks"), Restored->GetPicks().Num(), Draft->GetPicks().Num());
        TestTrue(TEXT("Complete"), Restored->IsComplete());
        const FName Scouted = Draft->GetState().Prospects[0].Player.PlayerId;
        TestEqual(TEXT("The player's report"), Restored->GetProspectView(FName(TEXT("Falcons")), Scouted).Reports, Draft->GetProspectView(FName(TEXT("Falcons")), Scouted).Reports);
    }
    IFileManager::Get().Delete(*UPSSaveSubsystem::GetSlotPath(Slot), false, true, true);
    IFileManager::Get().Delete(*(UPSSaveSubsystem::GetSlotPath(Slot) + TEXT(".bak")), false, true, true);
    TestFalse(TEXT("A save from before the draft keeps the current one"), Restored->LoadFrom(NewObject<UPSFranchiseSaveGame>()));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
