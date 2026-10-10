// PSWeeklyPreparationTests.cpp -- Epic 90 (training, gameplan and weekly preparation)
//
// Tests covered:
//   1. Data/training.json loads through UPSDataIngestion, validates clean against the opponent
//      model's tracked categories and equals FPSTrainingTuning's defaults field by field; a broken
//      tuning's problems are each reported.
//   2. The allocation's tradeoffs: development raises the roster's ratings (less near 100), practice
//      tires players (less with more Stamina), rest restores them, a tired player plays below his
//      ratings, a game tires the players who played; a week is prepared once; a chosen allocation
//      is checked and normalized.
//   3. The gameplan: a scouting report reads a team's calls from its coordinators' schemes, or from
//      what the opponent model saw it call once there are enough calls; focus areas that prepare for
//      what it calls most are worth the most; the bonus lifts the focus area's players against that
//      opponent only, split between focus areas; training funding (the owner economy) and the
//      coordinator's development scale the week.
//   4. Practice injuries through Core 19's injury model: a hard week hurts, rest doesn't, a tired
//      roster gets hurt more than a fresh one, injuries heal week by week, the rolls repeat.
//   5. Every team runs the same system: a team that chooses nothing runs the recommendation (rest
//      when tired, the gameplan late in the season), and a team that chooses the recommendation
//      gets exactly what the CPU team gets.
//   6. The franchise: every team prepares each week before its games, once; the injured sit;
//      games tire the players; the player's own allocation is kept; the season's end heals
//      everyone; the condition round-trips through the franchise save.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "PSDataIngestion.h"
#include "PSEconomyData.h"
#include "PSFranchiseFlow.h"
#include "PSFranchiseSaveGame.h"
#include "PSFranchiseSeason.h"
#include "PSOpponentModel.h"
#include "PSOpponentModelTypes.h"
#include "PSOwnerEconomy.h"
#include "PSRoster.h"
#include "PSSaveSubsystem.h"
#include "PSScheduleEngine.h"
#include "PSStaffData.h"
#include "PSStaffManager.h"
#include "PSStatsData.h"
#include "PSStatsEngine.h"
#include "PSTrainingData.h"
#include "PSUITeamCatalog.h"
#include "PSWeeklyPreparation.h"
#include "Engine/GameInstance.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSWeeklyPreparationTests
{
    /** The shipped tuning with the practice injury chance set (0: nobody gets hurt). */
    static UPSWeeklyPreparation* MakePreparation(float InjuryChance)
    {
        UPSWeeklyPreparation* Prep = NewObject<UPSWeeklyPreparation>();
        Prep->LoadDefaults();
        FPSTrainingTuning Tuning = Prep->GetTuning();
        Tuning.PracticeInjury.BaseInjuryChance = InjuryChance;
        Prep->SetTuning(Tuning);
        return Prep;
    }

    static FPlayerAttributes MakePrepPlayer(const FString& PlayerId, EPlayerRole Role, float Rating, float Stamina = 50.f)
    {
        FPlayerAttributes Player;
        Player.PlayerId = FName(*PlayerId);
        Player.DisplayName = PlayerId;
        Player.Role = Role;
        Player.WeightKg = 110.f;
        Player.HeightCm = 188.f;
        Player.Speed = Rating;
        Player.Agility = Rating;
        Player.Strength = Rating;
        Player.Acceleration = Rating;
        Player.Awareness = Rating;
        Player.Stamina = Stamina;
        return Player;
    }

    static UPSRoster* MakePrepRoster(const TArray<FPlayerAttributes>& Players)
    {
        UPSRoster* Roster = NewObject<UPSRoster>();
        Roster->InitializeRoster(Players);
        Roster->BuildDefaultDepthChart();
        return Roster;
    }

    /** Count players named Prefix0, Prefix1, ... all rated Rating. */
    static UPSRoster* MakeSquad(const FString& Prefix, int32 Count, float Rating, float Stamina)
    {
        TArray<FPlayerAttributes> Players;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Players.Add(MakePrepPlayer(FString::Printf(TEXT("%s%d"), *Prefix, Index), EPlayerRole::DefensiveBack, Rating, Stamina));
        }
        return MakePrepRoster(Players);
    }

    static FPSTrainingAllocation MakeAllocation(float Develop, float Gameplan, float Rest)
    {
        FPSTrainingAllocation Allocation;
        Allocation.Develop = Develop;
        Allocation.Gameplan = Gameplan;
        Allocation.Rest = Rest;
        return Allocation;
    }

    static FPlayerAttributes RowOf(const UPSRoster* Roster, const TCHAR* PlayerId)
    {
        FPlayerAttributes Row;
        Roster->FindPlayerById(FName(PlayerId), Row);
        return Row;
    }

    static FPSTeamTraining TeamOf(const UPSWeeklyPreparation* Prep, const TCHAR* TeamId)
    {
        FPSTeamTraining Team;
        Prep->GetTeamTraining(FName(TeamId), Team);
        return Team;
    }

    static int32 CountEvents(const TArray<FPSTrainingEvent>& Events, EPSTrainingEventKind Kind)
    {
        return Events.FilterByPredicate([Kind](const FPSTrainingEvent& Event) { return Event.Kind == Kind; }).Num();
    }

    static bool HasLine(const TArray<FString>& Lines, const TCHAR* Fragment)
    {
        return Lines.ContainsByPredicate([Fragment](const FString& Line) { return Line.Contains(Fragment); });
    }

    /** What a player plays at with Freshness and no gameplan. */
    static float Tired(const UPSWeeklyPreparation* Prep, float Rating, float Freshness)
    {
        return Rating * (1.f - Prep->GetTuning().FatiguePerformanceSwing * (1.f - Freshness));
    }

    static FPSTendencyCell MakeCall(bool bOffense, const TCHAR* Category, int32 Count)
    {
        FPSTendencyCell Cell;
        Cell.bOffense = bOffense;
        Cell.Down = 1;
        Cell.DistanceBucket = 2;
        Cell.Category = Category;
        Cell.Count = Count;
        return Cell;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The shipped tuning
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSWeeklyPrepTuningTest,
    "PlaySports.Training.TuningLoadsAndValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSWeeklyPrepTuningTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSTrainingTuning FromFile;
    if (!TestTrue(TEXT("Data/training.json loads"), Ingestion->LoadTrainingTuningFromJson(UPSWeeklyPreparation::GetDefaultTuningPath(), FromFile)))
    {
        return false;
    }
    FPSOpponentModelTuning OpponentTuning;
    TestTrue(TEXT("Data/opponent_model.json loads"), Ingestion->LoadOpponentModelTuningFromJson(UPSOpponentModel::GetDefaultTuningPath(), OpponentTuning));
    const TArray<FString> Problems = UPSWeeklyPreparation::ValidateTuning(FromFile, &OpponentTuning);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    TestEqual(TEXT("The shipped tuning is sound, every focus category tracked"), Problems.Num(), 0);
    TestEqual(TEXT("Six focus areas: three against an offense, three against a defense"), FromFile.FocusAreas.Num(), 6);
    TestEqual(TEXT("...three against an offense"), FromFile.FocusAreas.FilterByPredicate([](const FPSGameplanFocusDef& Focus) { return Focus.bVersusOffense; }).Num(), 3);
    const FPSTrainingTuning Defaults;
    for (TFieldIterator<FProperty> It(FPSTrainingTuning::StaticStruct()); It; ++It)
    {
        if (It->GetFName() != GET_MEMBER_NAME_CHECKED(FPSTrainingTuning, FocusAreas))
        {
            TestTrue(*FString::Printf(TEXT("%s matches the default"), *It->GetName()), It->Identical_InContainer(&FromFile, &Defaults));
        }
    }
    UPSWeeklyPreparation* Prep = NewObject<UPSWeeklyPreparation>();
    TestTrue(TEXT("LoadDefaults reads both files"), Prep->LoadDefaults());

    FPSTrainingTuning Broken = FromFile;
    Broken.DefaultAllocation = PSWeeklyPreparationTests::MakeAllocation(0.f, 0.f, 0.f);
    Broken.GameFatigue = 1.5f;
    Broken.MaxGameplanBonus = 1.f;
    Broken.PracticeInjury.MaxRecoveryWeeks = 0;
    Broken.FocusAreas[1].FocusId = Broken.FocusAreas[0].FocusId;
    Broken.FocusAreas[2].Categories = { FString(TEXT("Blitz")) };
    Broken.FocusAreas[3].Roles.Reset();
    const TArray<FString> BrokenProblems = UPSWeeklyPreparation::ValidateTuning(Broken, &OpponentTuning);
    for (const TCHAR* Expected : { TEXT("DefaultAllocation"), TEXT("GameFatigue"), TEXT("MaxGameplanBonus"), TEXT("PracticeInjury"),
        TEXT("duplicate focus 'RunDefense'"), TEXT("category Blitz isn't one the opponent model tracks on the offense"), TEXT("BlitzPickup needs categories and roles") })
    {
        TestTrue(*FString::Printf(TEXT("Reported: %s"), Expected), PSWeeklyPreparationTests::HasLine(BrokenProblems, Expected));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Develop, gameplan or rest
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSWeeklyPrepAllocationTest,
    "PlaySports.Training.AllocationTradeoffs",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSWeeklyPrepAllocationTest::RunTest(const FString& Parameters)
{
    using namespace PSWeeklyPreparationTests;

    UPSWeeklyPreparation* Prep = MakePreparation(0.f);
    const FPSTrainingTuning& T = Prep->GetTuning();
    const FName Devs(TEXT("Devs"));
    const FName Rests(TEXT("Rests"));
    UPSRoster* DevRoster = MakePrepRoster({ MakePrepPlayer(TEXT("DEV_LOW"), EPlayerRole::DefensiveBack, 50.f), MakePrepPlayer(TEXT("DEV_HIGH"), EPlayerRole::DefensiveBack, 90.f),
        MakePrepPlayer(TEXT("DEV_IRON"), EPlayerRole::DefensiveBack, 50.f, 100.f) });
    UPSRoster* RestRoster = MakePrepRoster({ MakePrepPlayer(TEXT("REST_LOW"), EPlayerRole::DefensiveBack, 50.f) });

    // A chosen allocation: no negative shares, not all zero, normalized.
    TestFalse(TEXT("A negative share is refused"), Prep->SetAllocation(Devs, MakeAllocation(1.f, -0.5f, 0.f)));
    TestFalse(TEXT("So is nothing at all"), Prep->SetAllocation(Devs, MakeAllocation(0.f, 0.f, 0.f)));
    TestTrue(TEXT("2:1:1 is allowed"), Prep->SetAllocation(Devs, MakeAllocation(2.f, 1.f, 1.f)));
    TestTrue(TEXT("...and normalized"), FMath::IsNearlyEqual(TeamOf(Prep, TEXT("Devs")).ChosenAllocation.Develop, 0.5f, 1e-5f));
    TestTrue(TEXT("All development"), Prep->SetAllocation(Devs, MakeAllocation(1.f, 0.f, 0.f)));
    TestTrue(TEXT("All rest"), Prep->SetAllocation(Rests, MakeAllocation(0.f, 0.f, 1.f)));

    Prep->PrepareTeam(Devs, DevRoster, 1, NAME_None, 0.f, nullptr, nullptr);
    Prep->PrepareTeam(Rests, RestRoster, 1, NAME_None, 0.f, nullptr, nullptr);

    // Development: a full week adds DevelopPointsPerWeek to a weight-1 rating with full headroom.
    const float Points = T.DevelopPointsPerWeek;
    TestTrue(TEXT("A 50 develops fully: Awareness"), FMath::IsNearlyEqual(RowOf(DevRoster, TEXT("DEV_LOW")).Awareness, 50.f + Points * T.DevelopRatings.Awareness, 1e-4f));
    TestTrue(TEXT("...and Strength at its weight"), FMath::IsNearlyEqual(RowOf(DevRoster, TEXT("DEV_LOW")).Strength, 50.f + Points * T.DevelopRatings.Strength, 1e-4f));
    TestTrue(TEXT("A 90 develops in proportion to his distance from 100"),
        FMath::IsNearlyEqual(RowOf(DevRoster, TEXT("DEV_HIGH")).Awareness, 90.f + Points * T.DevelopRatings.Awareness * (10.f / T.DevelopHeadroom), 1e-4f));
    TestEqual(TEXT("Resting develops nobody"), RowOf(RestRoster, TEXT("REST_LOW")).Awareness, 50.f);
    FPSPlayerCondition Low;
    Prep->GetCondition(FName(TEXT("DEV_LOW")), Low);
    const float WeightSum = T.DevelopRatings.Speed + T.DevelopRatings.Agility + T.DevelopRatings.Strength + T.DevelopRatings.Acceleration + T.DevelopRatings.Awareness;
    TestTrue(TEXT("His development this season is counted"), FMath::IsNearlyEqual(Low.DevelopmentGained, Points * WeightSum, 1e-4f));

    // Practice tires, by Stamina; rest doesn't.
    const float LowFatigue = T.DevelopIntensity * T.PracticeFatigue * (1.f - T.StaminaFatigueRelief * 0.5f);
    TestTrue(TEXT("A week of practice tires him"), FMath::IsNearlyEqual(Prep->GetFreshness(FName(TEXT("DEV_LOW"))), 1.f - LowFatigue, 1e-4f));
    TestTrue(TEXT("...less with Stamina 100"), FMath::IsNearlyEqual(Prep->GetFreshness(FName(TEXT("DEV_IRON"))),
        1.f - T.DevelopIntensity * T.PracticeFatigue * (1.f - T.StaminaFatigueRelief), 1e-4f));
    TestEqual(TEXT("A rest week leaves him fresh"), Prep->GetFreshness(FName(TEXT("REST_LOW"))), 1.f);

    // A tired player plays below his ratings; the roster keeps his own.
    const FPlayerAttributes LowRow = RowOf(DevRoster, TEXT("DEV_LOW"));
    const FPlayerAttributes Playing = Prep->ApplyPreparation(Devs, NAME_None, LowRow);
    TestTrue(TEXT("He plays tired"), FMath::IsNearlyEqual(Playing.Speed, Tired(Prep, LowRow.Speed, 1.f - LowFatigue), 1e-3f) && Playing.Speed < LowRow.Speed);
    TestEqual(TEXT("...his Stamina untouched"), Playing.Stamina, LowRow.Stamina);

    // A game tires those who played: less with more Stamina.
    Prep->RecordGame(Devs, DevRoster->GetFullRoster());
    Prep->RecordGame(Rests, RestRoster->GetFullRoster());
    TestTrue(TEXT("A game costs GameFatigue, less his Stamina's relief"),
        FMath::IsNearlyEqual(Prep->GetFreshness(FName(TEXT("REST_LOW"))), 1.f - T.GameFatigue * (1.f - T.StaminaFatigueRelief * 0.5f), 1e-4f));

    // The week is prepared once.
    TestEqual(TEXT("Week 1 again: nothing"), Prep->PrepareTeam(Devs, DevRoster, 1, NAME_None, 0.f, nullptr, nullptr).Num(), 0);
    TestTrue(TEXT("...and no more development"), FMath::IsNearlyEqual(RowOf(DevRoster, TEXT("DEV_LOW")).Awareness, LowRow.Awareness, 1e-5f));

    // Week 2: a rest week restores more than a development week does.
    const float DevBefore = Prep->GetFreshness(FName(TEXT("DEV_LOW")));
    const float RestBefore = Prep->GetFreshness(FName(TEXT("REST_LOW")));
    Prep->PrepareTeam(Devs, DevRoster, 2, NAME_None, 0.f, nullptr, nullptr);
    Prep->PrepareTeam(Rests, RestRoster, 2, NAME_None, 0.f, nullptr, nullptr);
    TestTrue(TEXT("Rest: the week's recovery and the rest's"), FMath::IsNearlyEqual(Prep->GetFreshness(FName(TEXT("REST_LOW"))),
        FMath::Min(RestBefore + T.WeeklyRecovery + T.RestRecovery, 1.f), 1e-4f));
    TestTrue(TEXT("Development: the week's recovery, less practice"), FMath::IsNearlyEqual(Prep->GetFreshness(FName(TEXT("DEV_LOW"))),
        FMath::Min(DevBefore + T.WeeklyRecovery, 1.f) - LowFatigue, 1e-4f));
    TestTrue(TEXT("The week's report reads it"), HasLine(Prep->DescribeTeamWeek(Devs), TEXT("Week 2 practice: develop 100%, gameplan 0%, rest 0% (chosen)")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The gameplan against a scouted opponent
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSWeeklyPrepGameplanTest,
    "PlaySports.Training.GameplanAgainstAScoutedOpponent",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSWeeklyPrepGameplanTest::RunTest(const FString& Parameters)
{
    using namespace PSWeeklyPreparationTests;

    UPSWeeklyPreparation* Prep = MakePreparation(0.f);
    const FPSTrainingTuning& T = Prep->GetTuning();
    UPSStaffManager* Staffs = NewObject<UPSStaffManager>();
    TestTrue(TEXT("The league's coaches load"), Staffs->LoadFromJson(UPSStaffManager::GetDefaultDataPath()));
    const FName Falcons(TEXT("Falcons"));
    const FName Hawks(TEXT("Hawks"));
    const FName Wolves(TEXT("Wolves"));
    const FName RunDefense(TEXT("RunDefense"));
    const FName ShortPassDefense(TEXT("ShortPassDefense"));
    const FName DeepPassDefense(TEXT("DeepPassDefense"));

    // Scouting from the schemes: the Wolves run the Power Run, the Hawks the Air Raid.
    const FPSOpponentScoutingReport WolvesReport = Prep->BuildScoutingReport(Wolves, Staffs);
    TestEqual(TEXT("The Wolves' offense is read from its scheme"), WolvesReport.Offense.Basis, EPSTendencyBasis::Overall);
    TestFalse(TEXT("...not from calls seen"), WolvesReport.bOffenseObserved);
    TestEqual(TEXT("The Power Run runs most"), WolvesReport.Offense.TopCategory, FString(TEXT("Run")));
    TestEqual(TEXT("The 3-4 Pressure blitzes most"), WolvesReport.Defense.TopCategory, FString(TEXT("Blitz")));
    const float WolvesRun = Prep->GetFocusRelevance(WolvesReport, RunDefense);
    TestTrue(TEXT("Run defense is worth more than an even mix against them"), WolvesRun > 1.f);
    TestTrue(TEXT("...and more than pass defense"), WolvesRun > Prep->GetFocusRelevance(WolvesReport, ShortPassDefense) && WolvesRun > Prep->GetFocusRelevance(WolvesReport, DeepPassDefense));
    const FPSOpponentScoutingReport HawksReport = Prep->BuildScoutingReport(Hawks, Staffs);
    TestTrue(TEXT("Against the Air Raid, run defense is worth less than an even mix"), Prep->GetFocusRelevance(HawksReport, RunDefense) < 1.f);
    TestTrue(TEXT("...and less than deep-pass defense"), Prep->GetFocusRelevance(HawksReport, RunDefense) < Prep->GetFocusRelevance(HawksReport, DeepPassDefense));

    // The recommendation is the most relevant focus area.
    const TArray<FName> Recommended = Prep->RecommendFocus(WolvesReport);
    float Best = 0.f;
    for (const FPSGameplanFocusDef& Focus : T.FocusAreas)
    {
        Best = FMath::Max(Best, Prep->GetFocusRelevance(WolvesReport, Focus.FocusId));
    }
    if (TestEqual(TEXT("One focus area recommended"), Recommended.Num(), T.AIFocusAreas))
    {
        TestTrue(TEXT("...the most relevant"), FMath::IsNearlyEqual(Prep->GetFocusRelevance(WolvesReport, Recommended[0]), Best, 1e-5f));
    }

    // Unscouted: no staff, no calls seen.
    const FPSOpponentScoutingReport Unscouted = Prep->BuildScoutingReport(FName(TEXT("Expansion")), nullptr);
    TestEqual(TEXT("Nothing known"), Unscouted.Offense.Basis, EPSTendencyBasis::None);
    TestTrue(TEXT("A gameplan against the unknown is worth UnscoutedRelevance"), FMath::IsNearlyEqual(Prep->GetFocusRelevance(Unscouted, RunDefense), T.UnscoutedRelevance, 1e-5f));

    // What the opponent model saw (the human's calls): enough of them make the read.
    Prep->SetObservedCalls(Falcons, { MakeCall(true, TEXT("DeepPass"), 20), MakeCall(true, TEXT("Run"), 2) });
    const FPSOpponentScoutingReport Seen = Prep->BuildScoutingReport(Falcons, Staffs);
    TestTrue(TEXT("His offense is read from the calls seen"), Seen.bOffenseObserved && Seen.Offense.TopCategory == TEXT("DeepPass"));
    TestTrue(TEXT("...his defense, never seen, from its scheme"), !Seen.bDefenseObserved && Seen.Defense.Basis == EPSTendencyBasis::Overall);
    TestTrue(TEXT("Deep-pass defense is the gameplan against him"), Prep->GetFocusRelevance(Seen, DeepPassDefense) > Prep->GetFocusRelevance(Seen, RunDefense));
    Prep->SetObservedCalls(Falcons, { MakeCall(true, TEXT("DeepPass"), 2) });
    const FPSOpponentScoutingReport TooFew = Prep->BuildScoutingReport(Falcons, Staffs);
    TestTrue(TEXT("Too few calls: back to the West Coast scheme"), !TooFew.bOffenseObserved && TooFew.Offense.TopCategory == TEXT("ShortPass"));

    // Choosing focus areas: known ones, no repeats, MaxFocusAreas at most.
    TestFalse(TEXT("An unknown focus area is refused"), Prep->SetGameplanFocus(Falcons, { FName(TEXT("Kicking")) }));
    TestFalse(TEXT("So is a repeat"), Prep->SetGameplanFocus(Falcons, { RunDefense, RunDefense }));
    TestFalse(TEXT("So are three"), Prep->SetGameplanFocus(Falcons, { RunDefense, ShortPassDefense, DeepPassDefense }));

    // The bonus: the gameplan share, split between the focus areas, times each one's relevance.
    UPSRoster* FalconsRoster = MakePrepRoster({ MakePrepPlayer(TEXT("FAL_DL"), EPlayerRole::DefensiveLineman, 70.f), MakePrepPlayer(TEXT("FAL_QB"), EPlayerRole::Quarterback, 70.f) });
    UPSRoster* BearsRoster = MakePrepRoster({ MakePrepPlayer(TEXT("BER_DL"), EPlayerRole::DefensiveLineman, 70.f) });
    const FPSTrainingAllocation Half = MakeAllocation(0.5f, 0.5f, 0.f);
    Prep->SetAllocation(Falcons, Half);
    Prep->SetAllocation(FName(TEXT("Bears")), Half);
    TestTrue(TEXT("The Falcons prepare for the run"), Prep->SetGameplanFocus(Falcons, { RunDefense }));
    TestTrue(TEXT("The Bears for the run and the short pass"), Prep->SetGameplanFocus(FName(TEXT("Bears")), { RunDefense, ShortPassDefense }));
    Prep->PrepareTeam(Falcons, FalconsRoster, 1, Wolves, 0.f, nullptr, Staffs);
    Prep->PrepareTeam(FName(TEXT("Bears")), BearsRoster, 1, Wolves, 0.f, nullptr, Staffs);
    const FPSTeamTraining FalconsWeek = TeamOf(Prep, TEXT("Falcons"));
    const float Total = T.GameplanBonusPerShare * 0.5f;
    if (TestEqual(TEXT("The Falcons' gameplan has one focus"), FalconsWeek.Gameplan.Focus.Num(), 1))
    {
        TestEqual(TEXT("...against the Wolves"), FalconsWeek.Gameplan.OpponentId, Wolves);
        TestTrue(TEXT("...worth its relevance"), FMath::IsNearlyEqual(FalconsWeek.Gameplan.Focus[0].Bonus, FMath::Min(Total * WolvesRun, T.MaxGameplanBonus), 1e-5f));
    }
    const FPSTeamTraining BearsWeek = TeamOf(Prep, TEXT("Bears"));
    if (TestEqual(TEXT("The Bears' has two"), BearsWeek.Gameplan.Focus.Num(), 2))
    {
        TestTrue(TEXT("...each half the bonus times its relevance"), FMath::IsNearlyEqual(BearsWeek.Gameplan.Focus[0].Bonus, FMath::Min(Total * 0.5f * WolvesRun, T.MaxGameplanBonus), 1e-5f)
            && FMath::IsNearlyEqual(BearsWeek.Gameplan.Focus[1].Bonus, FMath::Min(Total * 0.5f * Prep->GetFocusRelevance(WolvesReport, ShortPassDefense), T.MaxGameplanBonus), 1e-5f));
    }
    TestEqual(TEXT("A choice is for one week"), FalconsWeek.ChosenFocus.Num(), 0);

    // It lifts the focus area's players, against that opponent only.
    const FPlayerAttributes Lineman = RowOf(FalconsRoster, TEXT("FAL_DL"));
    const float Freshness = Prep->GetFreshness(Lineman.PlayerId);
    const float Bonus = FalconsWeek.Gameplan.Focus.Num() > 0 ? FalconsWeek.Gameplan.Focus[0].Bonus : 0.f;
    const FPlayerAttributes VsWolves = Prep->ApplyPreparation(Falcons, Wolves, Lineman);
    TestTrue(TEXT("Against the Wolves his Strength rises by the bonus"), FMath::IsNearlyEqual(VsWolves.Strength, Tired(Prep, Lineman.Strength, Freshness) * (1.f + Bonus), 1e-3f));
    TestTrue(TEXT("...his Speed, which run defense doesn't weigh, doesn't"), FMath::IsNearlyEqual(VsWolves.Speed, Tired(Prep, Lineman.Speed, Freshness), 1e-3f));
    TestTrue(TEXT("Against the Hawks it doesn't apply"), FMath::IsNearlyEqual(Prep->ApplyPreparation(Falcons, Hawks, Lineman).Strength, Tired(Prep, Lineman.Strength, Freshness), 1e-3f));
    const FPlayerAttributes Passer = RowOf(FalconsRoster, TEXT("FAL_QB"));
    TestTrue(TEXT("The quarterback isn't in run defense"), FMath::IsNearlyEqual(Prep->ApplyPreparation(Falcons, Wolves, Passer).Awareness,
        Tired(Prep, Passer.Awareness, Prep->GetFreshness(Passer.PlayerId)), 1e-3f));
    TestTrue(TEXT("The week's report reads the gameplan"), HasLine(Prep->DescribeTeamWeek(Falcons), TEXT("Gameplan vs Wolves: Run defense")));

    // Funding: the owner's training budget against the league's average.
    TestTrue(TEXT("Unfunded: FundingFloor"), FMath::IsNearlyEqual(Prep->GetFundingMultiplier(0.f), T.FundingFloor, 1e-5f));
    TestTrue(TEXT("The league's average: 1"), FMath::IsNearlyEqual(Prep->GetFundingMultiplier(1.f), 1.f, 1e-5f));
    TestTrue(TEXT("Lavish: capped"), FMath::IsNearlyEqual(Prep->GetFundingMultiplier(5.f), T.MaxFundingMultiplier, 1e-5f));
    UPSOwnerEconomy* Economy = NewObject<UPSOwnerEconomy>();
    Economy->LoadTuningFromJson(UPSOwnerEconomy::GetDefaultTuningPath());
    Economy->RegisterTeam(FName(TEXT("Rich")));
    Economy->RegisterTeam(FName(TEXT("Poor")));
    FPSTeamBudget Lavish = Economy->GetTuning().DefaultBudget;
    Lavish.TrainingFraction *= 2.f;
    TestTrue(TEXT("The Rich double their training budget"), Economy->SetBudget(FName(TEXT("Rich")), Lavish));
    UPSRoster* RichRoster = MakeSquad(TEXT("RICH_"), 1, 50.f, 50.f);
    UPSRoster* PoorRoster = MakeSquad(TEXT("POOR_"), 1, 50.f, 50.f);
    for (const TCHAR* TeamId : { TEXT("Rich"), TEXT("Poor") })
    {
        Prep->SetAllocation(FName(TeamId), MakeAllocation(1.f, 0.f, 0.f));
    }
    Prep->PrepareTeam(FName(TEXT("Rich")), RichRoster, 1, NAME_None, 0.f, Economy, nullptr);
    Prep->PrepareTeam(FName(TEXT("Poor")), PoorRoster, 1, NAME_None, 0.f, Economy, nullptr);
    const float RichIndex = Economy->GetFundingIndex(FName(TEXT("Rich")), EPSBudgetDepartment::Training);
    const float PoorIndex = Economy->GetFundingIndex(FName(TEXT("Poor")), EPSBudgetDepartment::Training);
    TestTrue(TEXT("The Rich out-fund the league"), RichIndex > 1.f && PoorIndex < 1.f);
    TestTrue(TEXT("The week reads the economy's index"), FMath::IsNearlyEqual(TeamOf(Prep, TEXT("Rich")).FundingIndex, RichIndex, 1e-5f));
    TestTrue(TEXT("...and develops in proportion to the funding"), FMath::IsNearlyEqual(TeamOf(Prep, TEXT("Rich")).DevelopmentPoints / TeamOf(Prep, TEXT("Poor")).DevelopmentPoints,
        Prep->GetFundingMultiplier(RichIndex) / Prep->GetFundingMultiplier(PoorIndex), 1e-3f));

    // The coordinator who develops players (Epic 89).
    const FPSCoachDef* Coordinator = Staffs->FindTeamCoach(Wolves, EPSCoachRole::DefensiveCoordinator);
    UPSRoster* WolvesRoster = MakeSquad(TEXT("WLV_"), 1, 50.f, 50.f);
    Prep->SetAllocation(Wolves, MakeAllocation(1.f, 0.f, 0.f));
    Prep->PrepareTeam(Wolves, WolvesRoster, 1, NAME_None, 0.f, nullptr, Staffs);
    if (TestNotNull(TEXT("The Wolves have a defensive coordinator"), Coordinator))
    {
        TestTrue(TEXT("His Development scales a defensive back's week"), FMath::IsNearlyEqual(RowOf(WolvesRoster, TEXT("WLV_0")).Awareness,
            50.f + T.DevelopPointsPerWeek * T.DevelopRatings.Awareness * FMath::Lerp(T.MinCoachDevelopment, T.MaxCoachDevelopment, Coordinator->Development / 100.f), 1e-4f));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Practice injuries and fatigue
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSWeeklyPrepInjuryTest,
    "PlaySports.Training.PracticeInjuriesAndFatigue",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSWeeklyPrepInjuryTest::RunTest(const FString& Parameters)
{
    using namespace PSWeeklyPreparationTests;

    // A certain injury in a full-intensity week: everyone who practises is hurt.
    UPSWeeklyPreparation* Prep = MakePreparation(1.f);
    const FPSTrainingTuning& T = Prep->GetTuning();
    const FName Camp(TEXT("Camp"));
    UPSRoster* Roster = MakeSquad(TEXT("CAMP_"), 6, 50.f, 50.f);
    Prep->SetAllocation(Camp, MakeAllocation(1.f, 0.f, 0.f));
    const TArray<FPSTrainingEvent> Hurt = Prep->PrepareTeam(Camp, Roster, 1, NAME_None, 0.f, nullptr, nullptr);
    TestEqual(TEXT("Everyone who practised is hurt"), CountEvents(Hurt, EPSTrainingEventKind::Injury), 6);
    TMap<FName, int32> OutFor;
    for (const FPSTrainingEvent& Event : Hurt)
    {
        TestTrue(*FString::Printf(TEXT("%s: out within the recovery range"), *Event.PlayerId.ToString()),
            Event.Weeks >= T.PracticeInjury.MinRecoveryWeeks && Event.Weeks <= T.PracticeInjury.MaxRecoveryWeeks && Prep->IsInjured(Event.PlayerId));
        OutFor.Add(Event.PlayerId, Event.Weeks);
    }
    TestEqual(TEXT("The injured don't develop"), RowOf(Roster, TEXT("CAMP_0")).Awareness, 50.f);
    TestEqual(TEXT("The week counts them"), TeamOf(Prep, TEXT("Camp")).Injuries, 6);

    // Rest weeks: no practice, no injury; each heals in his weeks.
    Prep->SetAllocation(Camp, MakeAllocation(0.f, 0.f, 1.f));
    for (int32 Week = 2; Week <= 1 + T.PracticeInjury.MaxRecoveryWeeks; ++Week)
    {
        const TArray<FPSTrainingEvent> Events = Prep->PrepareTeam(Camp, Roster, Week, NAME_None, 0.f, nullptr, nullptr);
        TestEqual(*FString::Printf(TEXT("Week %d: rest hurts nobody"), Week), CountEvents(Events, EPSTrainingEventKind::Injury), 0);
        for (const TPair<FName, int32>& Player : OutFor)
        {
            const bool bBackThisWeek = Events.ContainsByPredicate([&Player](const FPSTrainingEvent& Event) { return Event.Kind == EPSTrainingEventKind::Recovered && Event.PlayerId == Player.Key; });
            TestEqual(*FString::Printf(TEXT("Week %d: %s back after his %d week(s)"), Week, *Player.Key.ToString(), Player.Value), bBackThisWeek, Week == 1 + Player.Value);
            TestEqual(*FString::Printf(TEXT("Week %d: %s injured until then"), Week, *Player.Key.ToString()), Prep->IsInjured(Player.Key), Week < 1 + Player.Value);
        }
    }

    // Fatigue raises the risk (Core 19's model): a spent squad gets hurt more than a fresh one.
    UPSWeeklyPreparation* Risky = MakePreparation(0.1f);
    UPSRoster* Spent = MakeSquad(TEXT("SPENT_"), 60, 50.f, 0.f);
    UPSRoster* Fresh = MakeSquad(TEXT("FRESH_"), 60, 50.f, 0.f);
    for (int32 Game = 0; Game < 6; ++Game)
    {
        Risky->RecordGame(FName(TEXT("Spent")), Spent->GetFullRoster());
    }
    TestEqual(TEXT("Six games spend them"), Risky->GetFreshness(FName(TEXT("SPENT_0"))), 0.f);
    for (const TCHAR* TeamId : { TEXT("Spent"), TEXT("Fresh") })
    {
        Risky->SetAllocation(FName(TeamId), MakeAllocation(1.f, 0.f, 0.f));
    }
    const int32 SpentInjuries = CountEvents(Risky->PrepareTeam(FName(TEXT("Spent")), Spent, 1, NAME_None, 0.f, nullptr, nullptr), EPSTrainingEventKind::Injury);
    const int32 FreshInjuries = CountEvents(Risky->PrepareTeam(FName(TEXT("Fresh")), Fresh, 1, NAME_None, 0.f, nullptr, nullptr), EPSTrainingEventKind::Injury);
    AddInfo(FString::Printf(TEXT("Practice injuries: %d of 60 spent, %d of 60 fresh"), SpentInjuries, FreshInjuries));
    TestTrue(TEXT("The spent squad is hurt more"), SpentInjuries > FreshInjuries);

    // The rolls repeat: the same week of the same team rolls the same.
    UPSWeeklyPreparation* Again = MakePreparation(0.1f);
    UPSRoster* FreshAgain = MakeSquad(TEXT("FRESH_"), 60, 50.f, 0.f);
    Again->SetAllocation(FName(TEXT("Fresh")), MakeAllocation(1.f, 0.f, 0.f));
    Again->PrepareTeam(FName(TEXT("Fresh")), FreshAgain, 1, NAME_None, 0.f, nullptr, nullptr);
    bool bSame = true;
    for (const FPlayerAttributes& Player : Fresh->GetFullRoster())
    {
        bSame &= Risky->IsInjured(Player.PlayerId) == Again->IsInjured(Player.PlayerId);
    }
    TestTrue(TEXT("The same injuries"), bSame);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- No user-only advantages
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSWeeklyPrepSameSystemTest,
    "PlaySports.Training.EveryTeamRunsTheSameSystem",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSWeeklyPrepSameSystemTest::RunTest(const FString& Parameters)
{
    using namespace PSWeeklyPreparationTests;

    UPSWeeklyPreparation* Prep = MakePreparation(0.f);
    const FPSTrainingTuning& T = Prep->GetTuning();
    UPSStaffManager* Staffs = NewObject<UPSStaffManager>();
    Staffs->LoadFromJson(UPSStaffManager::GetDefaultDataPath());
    const FName Wolves(TEXT("Wolves"));

    // The recommendation: the default, rest when tired, the gameplan late in the season.
    const FPSTrainingAllocation Default = T.DefaultAllocation.Normalized();
    UPSRoster* Rested = MakeSquad(TEXT("RESTED_"), 4, 60.f, 50.f);
    UPSRoster* Weary = MakeSquad(TEXT("WEARY_"), 4, 60.f, 50.f);
    for (int32 Game = 0; Game < 3; ++Game)
    {
        Prep->RecordGame(FName(TEXT("Weary")), Weary->GetFullRoster());
    }
    const FPSTrainingAllocation Early = Prep->RecommendAllocation(Rested, 0.f);
    TestTrue(TEXT("Fresh, early: the default"), FMath::IsNearlyEqual(Early.Develop, Default.Develop, 1e-5f) && FMath::IsNearlyEqual(Early.Rest, Default.Rest, 1e-5f));
    const FPSTrainingAllocation WhenTired = Prep->RecommendAllocation(Weary, 0.f);
    TestTrue(TEXT("Tired: development gives way to rest"), FMath::IsNearlyEqual(WhenTired.Rest, Default.Rest + T.AIRestShift, 1e-5f)
        && FMath::IsNearlyEqual(WhenTired.Develop, Default.Develop - T.AIRestShift, 1e-5f));
    const FPSTrainingAllocation Late = Prep->RecommendAllocation(Rested, 0.9f);
    TestTrue(TEXT("Late: development gives way to the gameplan"), FMath::IsNearlyEqual(Late.Gameplan, Default.Gameplan + T.AILateGameplanShift, 1e-5f));

    // A team that chooses nothing (every CPU team) and one that chooses the same, against the Wolves.
    UPSRoster* Cpu = MakePrepRoster({ MakePrepPlayer(TEXT("CPU_DL"), EPlayerRole::DefensiveLineman, 60.f), MakePrepPlayer(TEXT("CPU_DB"), EPlayerRole::DefensiveBack, 70.f) });
    UPSRoster* Human = MakePrepRoster({ MakePrepPlayer(TEXT("HUM_DL"), EPlayerRole::DefensiveLineman, 60.f), MakePrepPlayer(TEXT("HUM_DB"), EPlayerRole::DefensiveBack, 70.f) });
    TestTrue(TEXT("The player chooses what the CPU would"), Prep->SetAllocation(FName(TEXT("Human")), Prep->RecommendAllocation(Human, 0.f)));
    TestTrue(TEXT("...and its focus"), Prep->SetGameplanFocus(FName(TEXT("Human")), Prep->RecommendFocus(Prep->BuildScoutingReport(Wolves, Staffs))));
    Prep->PrepareTeam(FName(TEXT("Cpu")), Cpu, 1, Wolves, 0.f, nullptr, Staffs);
    Prep->PrepareTeam(FName(TEXT("Human")), Human, 1, Wolves, 0.f, nullptr, Staffs);
    const FPSTeamTraining CpuWeek = TeamOf(Prep, TEXT("Cpu"));
    const FPSTeamTraining HumanWeek = TeamOf(Prep, TEXT("Human"));
    TestTrue(TEXT("The CPU ran the recommendation"), CpuWeek.bRecommendedAllocation && HasLine(Prep->DescribeTeamWeek(FName(TEXT("Cpu"))), TEXT("(recommended)")));
    TestTrue(TEXT("The player ran his choice"), !HumanWeek.bRecommendedAllocation && HasLine(Prep->DescribeTeamWeek(FName(TEXT("Human"))), TEXT("(chosen)")));
    TestTrue(TEXT("The same allocation"), FMath::IsNearlyEqual(CpuWeek.Allocation.Develop, HumanWeek.Allocation.Develop, 1e-5f)
        && FMath::IsNearlyEqual(CpuWeek.Allocation.Gameplan, HumanWeek.Allocation.Gameplan, 1e-5f));
    TestTrue(TEXT("The same development"), FMath::IsNearlyEqual(CpuWeek.DevelopmentPoints, HumanWeek.DevelopmentPoints, 1e-5f));
    if (TestEqual(TEXT("The same focus"), CpuWeek.Gameplan.Focus.Num(), HumanWeek.Gameplan.Focus.Num()) && CpuWeek.Gameplan.Focus.Num() > 0)
    {
        TestEqual(TEXT("...area"), CpuWeek.Gameplan.Focus[0].FocusId, HumanWeek.Gameplan.Focus[0].FocusId);
        TestTrue(TEXT("...and bonus"), FMath::IsNearlyEqual(CpuWeek.Gameplan.Focus[0].Bonus, HumanWeek.Gameplan.Focus[0].Bonus, 1e-6f));
    }
    TestTrue(TEXT("Their players play the same"), FMath::IsNearlyEqual(Prep->ApplyPreparation(FName(TEXT("Cpu")), Wolves, RowOf(Cpu, TEXT("CPU_DL"))).Strength,
        Prep->ApplyPreparation(FName(TEXT("Human")), Wolves, RowOf(Human, TEXT("HUM_DL"))).Strength, 1e-4f));

    // Clearing the choice hands the player back to the recommendation.
    Prep->ClearAllocation(FName(TEXT("Human")));
    Prep->PrepareTeam(FName(TEXT("Human")), Human, 2, Wolves, 0.f, nullptr, Staffs);
    TestTrue(TEXT("Back to the recommendation"), TeamOf(Prep, TEXT("Human")).bRecommendedAllocation);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- The franchise and the save
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSWeeklyPrepFranchiseTest,
    "PlaySports.Training.FranchiseFlowAndSave",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSWeeklyPrepFranchiseTest::RunTest(const FString& Parameters)
{
    using namespace PSWeeklyPreparationTests;

    UPSScheduleEngine* Schedule = NewObject<UPSScheduleEngine>();
    UPSFranchiseSeason* Season = NewObject<UPSFranchiseSeason>();
    const TArray<FName> TeamIds = { FName(TEXT("Falcons")), FName(TEXT("Hawks")), FName(TEXT("Wolves")), FName(TEXT("Bears")) };
    Season->InitializeSeason(TeamIds, Schedule->GenerateSeasonSchedule(FDateTime(2026, 9, 1), 2, TArray<int32>()));
    UPSStaffManager* Staffs = NewObject<UPSStaffManager>();
    Staffs->LoadFromJson(UPSStaffManager::GetDefaultDataPath());
    UPSFranchiseFlow* Flow = NewObject<UPSFranchiseFlow>();
    Flow->Initialize(Season, Staffs, FName(TEXT("Falcons")));
    TestEqual(TEXT("Every team has a roster"), Flow->LoadLeagueRosters(UPSUITeamCatalog::GetDefaultTeamsPath()), TeamIds.Num());
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    Flow->SetStats(Stats);
    UPSWeeklyPreparation* Prep = NewObject<UPSWeeklyPreparation>();
    TestTrue(TEXT("The shipped tuning"), Prep->LoadDefaults());
    Flow->SetPreparation(Prep);

    const FName HawksQB(TEXT("HAW_QB_001"));
    float BearsAwareness = 0.f;
    for (const FPlayerAttributes& Player : Flow->GetTeamRoster(FName(TEXT("Bears")))->GetFullRoster())
    {
        BearsAwareness += Player.Awareness;
    }

    // Week 1: every team practises before its game, then plays tired.
    TestEqual(TEXT("Week 1's two games"), Flow->SimulateWeek(true), 2);
    for (const FName& TeamId : TeamIds)
    {
        const FPSTeamTraining Week = TeamOf(Prep, *TeamId.ToString());
        TestEqual(*FString::Printf(TEXT("%s prepared week 1"), *TeamId.ToString()), Week.PreparedWeek, 1);
        const FPSWeekMatchup* Game = Season->GetMatchupsForWeek(1).FindByPredicate([&TeamId](const FPSWeekMatchup& Matchup) { return Matchup.HomeTeamId == TeamId || Matchup.AwayTeamId == TeamId; });
        if (TestNotNull(*FString::Printf(TEXT("%s plays in week 1"), *TeamId.ToString()), Game))
        {
            TestEqual(*FString::Printf(TEXT("%s's gameplan is for its opponent"), *TeamId.ToString()), Week.Gameplan.OpponentId, Game->HomeTeamId == TeamId ? Game->AwayTeamId : Game->HomeTeamId);
            TestEqual(*FString::Printf(TEXT("%s has the recommended focus"), *TeamId.ToString()), Week.Gameplan.Focus.Num(), Prep->GetTuning().AIFocusAreas);
        }
        for (const FPlayerAttributes& Player : Flow->GetTeamRoster(TeamId)->GetFullRoster())
        {
            FPSPlayerCondition Condition;
            TestTrue(*FString::Printf(TEXT("%s has a condition"), *Player.PlayerId.ToString()), Prep->GetCondition(Player.PlayerId, Condition));
            TestTrue(*FString::Printf(TEXT("%s is tired after the game (or hurt)"), *Player.PlayerId.ToString()), Condition.Freshness < 1.f || Condition.InjuryWeeks > 0);
        }
    }
    TestEqual(TEXT("Practice runs once a week"), Flow->PrepareWeek().Num(), 0);
    TestEqual(TEXT("...the Hawks have one week of practice"), TeamOf(Prep, TEXT("Hawks")).PracticeWeeks, 1);
    float BearsAwarenessAfter = 0.f;
    for (const FPlayerAttributes& Player : Flow->GetTeamRoster(FName(TEXT("Bears")))->GetFullRoster())
    {
        BearsAwarenessAfter += Player.Awareness;
    }
    TestTrue(TEXT("The Bears developed in practice"), BearsAwarenessAfter > BearsAwareness);
    FPSBoxScore HawksWeek1;
    TestTrue(TEXT("The Hawks' week 1 box score"), Stats->FindGame(1, FName(TEXT("Hawks")), HawksWeek1));
    const bool bQBPlayedWeek1 = HawksWeek1.FindPlayer(HawksQB) != nullptr;
    TestTrue(TEXT("The Hawks' quarterback played week 1"), bQBPlayedWeek1 || Prep->IsInjured(HawksQB));

    // Into week 2: the player picks all rest; the Hawks' quarterback comes back from a save hurt.
    TestFalse(TEXT("On to week 2"), Flow->AdvanceWeek());
    TestTrue(TEXT("The player rests his team"), Prep->SetAllocation(FName(TEXT("Falcons")), MakeAllocation(0.f, 0.f, 1.f)));
    UPSFranchiseSaveGame* Injured = NewObject<UPSFranchiseSaveGame>();
    Prep->SaveTo(Injured);
    FPSPlayerCondition* QBCondition = Injured->Training.Players.FindByPredicate([&HawksQB](const FPSPlayerCondition& Condition) { return Condition.PlayerId == HawksQB; });
    if (TestNotNull(TEXT("The Hawks' quarterback is in the save"), QBCondition))
    {
        QBCondition->InjuryWeeks = 3;
    }
    TestTrue(TEXT("The save loads"), Prep->LoadFrom(Injured));
    TestEqual(TEXT("Week 2's two games"), Flow->SimulateWeek(true), 2);
    const FPSTeamTraining FalconsWeek2 = TeamOf(Prep, TEXT("Falcons"));
    TestTrue(TEXT("The Falcons rested, by choice"), FMath::IsNearlyEqual(FalconsWeek2.Allocation.Rest, 1.f, 1e-5f) && !FalconsWeek2.bRecommendedAllocation);
    TestTrue(TEXT("The Hawks ran the recommendation"), TeamOf(Prep, TEXT("Hawks")).bRecommendedAllocation);
    TestTrue(TEXT("The quarterback is still hurt"), Prep->IsInjured(HawksQB));
    FPSBoxScore HawksWeek2;
    if (TestTrue(TEXT("The Hawks' week 2 box score"), Stats->FindGame(2, FName(TEXT("Hawks")), HawksWeek2)))
    {
        TestNull(TEXT("...without their hurt quarterback: he sat"), HawksWeek2.FindPlayer(HawksQB));
    }

    // The condition round-trips through the franchise save.
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    Prep->SaveTo(Save);
    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    const FString Slot = TEXT("Test_WeeklyPreparation");
    TestTrue(TEXT("The franchise saves"), Saves->SaveToSlot(Save, Slot));
    const UPSFranchiseSaveGame* Loaded = Cast<UPSFranchiseSaveGame>(Saves->LoadFromSlot(Slot));
    UPSWeeklyPreparation* Restored = NewObject<UPSWeeklyPreparation>();
    Restored->LoadDefaults();
    if (TestNotNull(TEXT("...and loads"), Loaded) && TestTrue(TEXT("The preparation loads"), Restored->LoadFrom(Loaded)))
    {
        TestEqual(TEXT("Every player"), Restored->GetState().Players.Num(), Prep->GetState().Players.Num());
        TestTrue(TEXT("The quarterback's injury"), Restored->IsInjured(HawksQB));
        const FName First = Prep->GetState().Players[0].PlayerId;
        TestTrue(TEXT("A player's freshness"), FMath::IsNearlyEqual(Restored->GetFreshness(First), Prep->GetFreshness(First), 1e-6f));
        TestTrue(TEXT("The player's chosen allocation"), TeamOf(Restored, TEXT("Falcons")).bAllocationChosen);
        TestEqual(TEXT("The Hawks' practice weeks"), TeamOf(Restored, TEXT("Hawks")).PracticeWeeks, 2);
    }
    IFileManager::Get().Delete(*UPSSaveSubsystem::GetSlotPath(Slot), false, true, true);
    IFileManager::Get().Delete(*(UPSSaveSubsystem::GetSlotPath(Slot) + TEXT(".bak")), false, true, true);
    TestFalse(TEXT("A save from before weekly preparation keeps the current one"), Restored->LoadFrom(NewObject<UPSFranchiseSaveGame>()));

    // The season ends: everyone heals and is fresh; the player's choice stays.
    TestTrue(TEXT("The season ends"), Flow->AdvanceWeek());
    TestFalse(TEXT("The quarterback is healed"), Prep->IsInjured(HawksQB));
    TestEqual(TEXT("...and fresh"), Prep->GetFreshness(HawksQB), 1.f);
    TestEqual(TEXT("No week prepared"), TeamOf(Prep, TEXT("Hawks")).PreparedWeek, 0);
    TestTrue(TEXT("The player's allocation stays"), TeamOf(Prep, TEXT("Falcons")).bAllocationChosen);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
