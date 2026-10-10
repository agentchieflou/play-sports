// PSLockerRoomTests.cpp -- Epic 91 (morale, chemistry and the locker room)
//
// Tests covered:
//   1. Data/morale.json loads through UPSDataIngestion, validates clean and equals FPSMoraleTuning's
//      defaults field by field; the starters per role come from the default personnel packages; a
//      broken tuning's problems are each reported.
//   2. Morale from its inputs: a starter, a backup, a backup rated above the starter, the team's
//      record, pay against his worth and a deal's last year, easing from last week; each factor
//      readable with its reason.
//   3. Chemistry: the same line game after game gels toward full cohesion and plays better, a
//      lineup change starts over, backups get no bonus, morale swings ratings too.
//   4. Events: a trade request after weeks of misery (once, withdrawn when he cheers up), a leader
//      emerging on a winning team and lifting his teammates, a holdout by an underpaid, unhappy
//      star at a new league year, ended once he is paid.
//   5. The franchise: a season through UPSFranchiseFlow evaluates every rostered player and gels
//      every unit; released players take their morale into free agency; the locker room
//      round-trips through the franchise save.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "PSContractManager.h"
#include "PSDataIngestion.h"
#include "PSFranchiseFlow.h"
#include "PSFranchiseSaveGame.h"
#include "PSFranchiseSeason.h"
#include "PSFreeAgency.h"
#include "PSLockerRoom.h"
#include "PSLockerRoomData.h"
#include "PSRoster.h"
#include "PSSaveSubsystem.h"
#include "PSScheduleEngine.h"
#include "PSStaffManager.h"
#include "PSUITeamCatalog.h"
#include "Engine/GameInstance.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSLockerRoomTests
{
    /** The shipped tuning and starters; with Inertia 0 morale follows its inputs at once. */
    static UPSLockerRoom* MakeLockerRoom(float Inertia = 0.f)
    {
        UPSLockerRoom* Room = NewObject<UPSLockerRoom>();
        Room->LoadDefaults();
        FPSMoraleTuning Tuning = Room->GetTuning();
        Tuning.MoraleInertia = Inertia;
        Room->SetTuning(Tuning);
        return Room;
    }

    static FPlayerAttributes MakeRoomPlayer(const TCHAR* PlayerId, EPlayerRole Role, float Rating, float Awareness = -1.f)
    {
        FPlayerAttributes Player;
        Player.PlayerId = FName(PlayerId);
        Player.DisplayName = PlayerId;
        Player.Role = Role;
        Player.WeightKg = 120.f;
        Player.HeightCm = 190.f;
        Player.Speed = Rating;
        Player.Agility = Rating;
        Player.Strength = Rating;
        Player.Acceleration = Rating;
        Player.Awareness = Awareness >= 0.f ? Awareness : Rating;
        Player.Stamina = 85.f;
        return Player;
    }

    static UPSRoster* MakeRoomRoster(const TArray<FPlayerAttributes>& Players)
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

    static int32 CountEvents(const TArray<FPSLockerRoomEvent>& Events, EPSLockerRoomEventKind Kind, const TCHAR* PlayerId)
    {
        const FName Id(PlayerId);
        return Events.FilterByPredicate([Kind, Id](const FPSLockerRoomEvent& Event) { return Event.Kind == Kind && Event.PlayerId == Id; }).Num();
    }

    /** Signs PlayerId to the Hawks for Years at Fraction of his worth now. */
    static void SignAtWorth(UPSContractManager* Contracts, const FPlayerAttributes& Player, float Fraction, int32 Years)
    {
        const FPSContractDemand Demand = Contracts->GetDemand(Contracts->MakeNegotiationContext(Player, Contracts->GetTuning().DefaultPlayerAge, 0.5f, FName(TEXT("Hawks"))));
        FPSContractOffer Offer;
        Offer.TeamId = FName(TEXT("Hawks"));
        Offer.Years = Years;
        Offer.AnnualValue = FMath::Max(Contracts->GetTuning().MinimumSalary, FMath::RoundToInt(Demand.AnnualValue * Fraction));
        Contracts->SignContract(Contracts->MakeContract(Player.PlayerId, Offer));
    }

    /** His pay against his worth as the locker room reads it. */
    static float PayRatio(const UPSContractManager* Contracts, const FPlayerAttributes& Player)
    {
        const int32 Worth = Contracts->GetDemand(Contracts->MakeNegotiationContext(Player, Contracts->GetTuning().DefaultPlayerAge, 0.5f, FName(TEXT("Hawks")))).AnnualValue;
        return static_cast<float>(Contracts->GetCapHit(Player.PlayerId, Contracts->GetLeagueYear())) / Worth;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The shipped tuning and the starters
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLockerRoomTuningTest,
    "PlaySports.LockerRoom.TuningLoadsAndValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLockerRoomTuningTest::RunTest(const FString& Parameters)
{
    using namespace PSLockerRoomTests;

    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSMoraleTuning FromFile;
    if (!TestTrue(TEXT("Data/morale.json loads"), Ingestion->LoadMoraleTuningFromJson(UPSLockerRoom::GetDefaultTuningPath(), FromFile)))
    {
        return false;
    }
    const TArray<FString> Problems = UPSLockerRoom::ValidateTuning(FromFile);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    TestEqual(TEXT("The shipped tuning is sound"), Problems.Num(), 0);
    TestEqual(TEXT("Two units gel: the line and the secondary"), FromFile.Units.Num(), 2);
    const FPSMoraleTuning Defaults;
    for (TFieldIterator<FProperty> It(FPSMoraleTuning::StaticStruct()); It; ++It)
    {
        if (It->GetFName() != GET_MEMBER_NAME_CHECKED(FPSMoraleTuning, Units))
        {
            TestTrue(*FString::Printf(TEXT("%s matches the default"), *It->GetName()), It->Identical_InContainer(&FromFile, &Defaults));
        }
    }

    // How many start at each role is the default personnel packages' (11 personnel, base 4-3).
    UPSLockerRoom* Room = NewObject<UPSLockerRoom>();
    TestTrue(TEXT("The defaults load"), Room->LoadDefaults());
    TestEqual(TEXT("One quarterback starts"), Room->GetStarterCount(EPlayerRole::Quarterback), 1);
    TestEqual(TEXT("Three receivers"), Room->GetStarterCount(EPlayerRole::WideReceiver), 3);
    TestEqual(TEXT("Five linemen"), Room->GetStarterCount(EPlayerRole::OffensiveLineman), 5);
    TestEqual(TEXT("Four defensive linemen"), Room->GetStarterCount(EPlayerRole::DefensiveLineman), 4);
    TestEqual(TEXT("Three linebackers"), Room->GetStarterCount(EPlayerRole::Linebacker), 3);
    TestEqual(TEXT("Four defensive backs"), Room->GetStarterCount(EPlayerRole::DefensiveBack), 4);

    FPSMoraleTuning Broken = FromFile;
    Broken.MoraleInertia = 1.f;
    Broken.TradeRequestWeeks = 0;
    Broken.Units[1].Unit = Broken.Units[0].Unit;
    Broken.Units[0].FullCohesionGames = 0;
    const TArray<FString> BrokenProblems = UPSLockerRoom::ValidateTuning(Broken);
    TestTrue(TEXT("Morale that never moves"), HasLine(BrokenProblems, TEXT("MoraleInertia must be below 1")));
    TestTrue(TEXT("No weeks to a trade request"), HasLine(BrokenProblems, TEXT("TradeRequestWeeks")));
    TestTrue(TEXT("A unit twice"), HasLine(BrokenProblems, TEXT("duplicate unit")));
    TestTrue(TEXT("A unit that never gels"), HasLine(BrokenProblems, TEXT("FullCohesionGames")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Morale from its inputs
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLockerRoomMoraleTest,
    "PlaySports.LockerRoom.MoraleFromItsInputs",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLockerRoomMoraleTest::RunTest(const FString& Parameters)
{
    using namespace PSLockerRoomTests;

    UPSLockerRoom* Room = MakeLockerRoom();
    const FName Hawks(TEXT("Hawks"));
    const FPlayerAttributes Starter = MakeRoomPlayer(TEXT("QB_Start"), EPlayerRole::Quarterback, 80.f);
    const FPlayerAttributes Better = MakeRoomPlayer(TEXT("QB_Better"), EPlayerRole::Quarterback, 90.f);
    const FPlayerAttributes Third = MakeRoomPlayer(TEXT("QB_Third"), EPlayerRole::Quarterback, 60.f);
    UPSRoster* Roster = MakeRoomRoster({ Starter, Better, Third });
    TestEqual(TEXT("Nobody's morale is neutral"), Room->GetMorale(FName(TEXT("Nobody"))), 0.5f);

    // A team winning three games in four: +0.05 for everyone.
    Room->EvaluateTeam(Hawks, Roster, 0.75f, nullptr, false);
    TestTrue(TEXT("The starter: +0.10 +0.05"), FMath::IsNearlyEqual(Room->GetMorale(Starter.PlayerId), 0.65f, 1e-4f));
    TestTrue(TEXT("The better backup: -0.05 -0.10 +0.05"), FMath::IsNearlyEqual(Room->GetMorale(Better.PlayerId), 0.40f, 1e-4f));
    TestTrue(TEXT("The third quarterback: -0.05 +0.05"), FMath::IsNearlyEqual(Room->GetMorale(Third.PlayerId), 0.50f, 1e-4f));

    // Every input is readable with its reason.
    FPSPlayerMorale Morale;
    if (TestTrue(TEXT("The better backup's breakdown"), Room->GetPlayerMorale(Better.PlayerId, Morale)))
    {
        TestEqual(TEXT("Three factors"), Morale.Factors.Num(), 3);
        TestTrue(TEXT("...playing time first"), Morale.Factors.Num() == 3 && Morale.Factors[0].Factor == FName(TEXT("PlayingTime")) && Morale.Factors[0].Description.Contains(TEXT("Backing up at Quarterback")));
        TestEqual(TEXT("...on his team"), Morale.TeamId, Hawks);
    }
    const TArray<FString> Described = Room->DescribePlayer(Better.PlayerId);
    TestTrue(TEXT("Described: his morale"), HasLine(Described, TEXT("morale 0.40")));
    TestTrue(TEXT("...that he should start"), HasLine(Described, TEXT("-0.10 Rated above a starter ahead of him")));
    TestTrue(TEXT("...the team's record"), HasLine(Described, TEXT("+0.05 Team winning 75%")));
    TestTrue(TEXT("...and what it costs him on the field"), HasLine(Described, TEXT("Plays -0.8% for his morale")));

    // Contracts: the starter underpaid in his deal's last year, the backup paid more than his worth.
    UPSContractManager* Contracts = NewObject<UPSContractManager>();
    Contracts->LoadTuningFromJson(UPSContractManager::GetDefaultTuningPath());
    Contracts->StartLeague();
    SignAtWorth(Contracts, Starter, 0.5f, 1);
    SignAtWorth(Contracts, Better, 1.2f, 2);
    const float StarterPay = PayRatio(Contracts, Starter);
    Room->EvaluateTeam(Hawks, Roster, 0.75f, Contracts, false);
    const float Underpaid = -0.15f * (0.8f - StarterPay) / 0.8f;
    TestTrue(TEXT("The starter is paid about half his worth"), StarterPay > 0.4f && StarterPay < 0.6f);
    TestTrue(TEXT("Underpaid in a last year: less happy"), FMath::IsNearlyEqual(Room->GetMorale(Starter.PlayerId), 0.65f + Underpaid - 0.05f, 1e-3f));
    TestTrue(TEXT("...and says why"), HasLine(Room->DescribePlayer(Starter.PlayerId), TEXT("In the last year of his deal")));
    TestTrue(TEXT("Paid his worth: happier"), PayRatio(Contracts, Better) >= 1.f && FMath::IsNearlyEqual(Room->GetMorale(Better.PlayerId), 0.45f, 1e-4f));

    // Morale eases toward its inputs: half of last week's at the shipped inertia.
    UPSLockerRoom* Easing = MakeLockerRoom(0.5f);
    Easing->EvaluateTeam(Hawks, Roster, 0.f, nullptr, false);
    TestTrue(TEXT("Halfway from neutral to 0.35"), FMath::IsNearlyEqual(Easing->GetMorale(Third.PlayerId), 0.425f, 1e-4f));
    FPSPlayerMorale Eased;
    TestTrue(TEXT("...heading for its target"), Easing->GetPlayerMorale(Third.PlayerId, Eased) && FMath::IsNearlyEqual(Eased.TargetMorale, 0.35f, 1e-4f));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Chemistry
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLockerRoomChemistryTest,
    "PlaySports.LockerRoom.ChemistryFromAStableLine",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLockerRoomChemistryTest::RunTest(const FString& Parameters)
{
    using namespace PSLockerRoomTests;

    UPSLockerRoom* Room = MakeLockerRoom();
    const FName Hawks(TEXT("Hawks"));
    const FName Line(TEXT("OffensiveLine"));
    TArray<FPlayerAttributes> Players;
    for (int32 Index = 1; Index <= 6; ++Index)
    {
        Players.Add(MakeRoomPlayer(*FString::Printf(TEXT("OL_%d"), Index), EPlayerRole::OffensiveLineman, 75.f));
    }
    for (int32 Index = 1; Index <= 5; ++Index)
    {
        Players.Add(MakeRoomPlayer(*FString::Printf(TEXT("DB_%d"), Index), EPlayerRole::DefensiveBack, 75.f));
    }
    UPSRoster* Roster = MakeRoomRoster(Players);

    // Three games with the same five linemen: halfway to full cohesion.
    for (int32 Game = 0; Game < 3; ++Game)
    {
        Room->RecordLineup(Hawks, Roster);
    }
    TestTrue(TEXT("Three games of six"), FMath::IsNearlyEqual(Room->GetUnitCohesion(Hawks, Line), 0.5f));
    TestTrue(TEXT("A starting lineman plays 2% better"), FMath::IsNearlyEqual(Room->GetEffectMultiplier(Hawks, Players[0]), 1.02f, 1e-4f));
    TestEqual(TEXT("The sixth lineman gets nothing"), Room->GetEffectMultiplier(Hawks, Players[5]), 1.f);
    TestTrue(TEXT("A starting defensive back: 1.5%"), FMath::IsNearlyEqual(Room->GetEffectMultiplier(Hawks, Players[6]), 1.015f, 1e-4f));
    TestEqual(TEXT("The fifth defensive back sits"), Room->GetEffectMultiplier(Hawks, Players[10]), 1.f);
    const FPlayerAttributes Gelled = Room->ApplyEffects(Hawks, Players[0]);
    TestTrue(TEXT("His ratings rise"), FMath::IsNearlyEqual(Gelled.Strength, 75.f * 1.02f, 1e-3f) && FMath::IsNearlyEqual(Gelled.Awareness, 75.f * 1.02f, 1e-3f));
    TestEqual(TEXT("...not his size"), Gelled.WeightKg, Players[0].WeightKg);
    TestTrue(TEXT("Readable"), HasLine(Room->DescribeTeamChemistry(Hawks), TEXT("OffensiveLine: 3 game(s) together, cohesion 50%")));

    // A new starter breaks it up; the same line long enough reaches full cohesion.
    Roster->SetDepthChartOrder(EPlayerRole::OffensiveLineman, { FName(TEXT("OL_6")), FName(TEXT("OL_1")), FName(TEXT("OL_2")), FName(TEXT("OL_3")), FName(TEXT("OL_4")), FName(TEXT("OL_5")) });
    Room->RecordLineup(Hawks, Roster);
    TestTrue(TEXT("A changed line starts over"), FMath::IsNearlyEqual(Room->GetUnitCohesion(Hawks, Line), 1.f / 6.f));
    TestEqual(TEXT("...and the benched lineman loses his bonus"), Room->GetEffectMultiplier(Hawks, Players[4]), 1.f);
    for (int32 Game = 0; Game < 10; ++Game)
    {
        Room->RecordLineup(Hawks, Roster);
    }
    TestTrue(TEXT("Full cohesion, no further"), FMath::IsNearlyEqual(Room->GetUnitCohesion(Hawks, Line), 1.f));
    TestTrue(TEXT("...worth 4%"), FMath::IsNearlyEqual(Room->GetEffectMultiplier(Hawks, Players[5]), 1.04f, 1e-4f));
    TestEqual(TEXT("Another team's line hasn't played"), Room->GetUnitCohesion(FName(TEXT("Wolves")), Line), 0.f);

    // Morale swings the ratings too: a happy starter on an unbeaten team.
    Room->EvaluateTeam(Hawks, Roster, 1.f, nullptr, false);
    const float Happy = Room->GetMorale(FName(TEXT("OL_6")));
    TestTrue(TEXT("Starter on an unbeaten team: 0.70"), FMath::IsNearlyEqual(Happy, 0.7f, 1e-4f));
    TestTrue(TEXT("...plays his morale and his line"), FMath::IsNearlyEqual(Room->GetEffectMultiplier(Hawks, Players[5]), (1.f + 0.04f * 0.4f) * 1.04f, 1e-4f));
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Trade requests, leaders and holdouts
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLockerRoomEventsTest,
    "PlaySports.LockerRoom.TradeRequestsHoldoutsAndLeaders",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLockerRoomEventsTest::RunTest(const FString& Parameters)
{
    using namespace PSLockerRoomTests;

    UPSLockerRoom* Room = MakeLockerRoom();
    const FName Hawks(TEXT("Hawks"));

    // A backup rated above the starter on a winless team: 0.25, under 0.3 three weeks running.
    UPSRoster* Unhappy = MakeRoomRoster({ MakeRoomPlayer(TEXT("WR_Start"), EPlayerRole::WideReceiver, 70.f), MakeRoomPlayer(TEXT("WR_2"), EPlayerRole::WideReceiver, 70.f),
        MakeRoomPlayer(TEXT("WR_3"), EPlayerRole::WideReceiver, 70.f), MakeRoomPlayer(TEXT("WR_Sulk"), EPlayerRole::WideReceiver, 88.f) });
    TArray<FPSLockerRoomEvent> Events;
    for (int32 Week = 1; Week <= 4; ++Week)
    {
        const TArray<FPSLockerRoomEvent> WeekEvents = Room->EvaluateTeam(Hawks, Unhappy, 0.f, nullptr, false);
        TestEqual(*FString::Printf(TEXT("Week %d: a trade request only in week 3"), Week), CountEvents(WeekEvents, EPSLockerRoomEventKind::TradeRequest, TEXT("WR_Sulk")), Week == 3 ? 1 : 0);
        Events.Append(WeekEvents);
    }
    TestTrue(TEXT("His morale: 0.25"), FMath::IsNearlyEqual(Room->GetMorale(FName(TEXT("WR_Sulk"))), 0.25f, 1e-4f));
    TestTrue(TEXT("He asked once"), CountEvents(Events, EPSLockerRoomEventKind::TradeRequest, TEXT("WR_Sulk")) == 1 && HasLine(Room->DescribePlayer(FName(TEXT("WR_Sulk"))), TEXT("asked to be traded")));
    Room->EvaluateTeam(Hawks, Unhappy, 1.f, nullptr, false);
    FPSPlayerMorale Cheered;
    TestTrue(TEXT("Winning cheers him; the request is withdrawn"), Room->GetPlayerMorale(FName(TEXT("WR_Sulk")), Cheered) && !Cheered.bTradeRequested && Cheered.LowMoraleWeeks == 0);

    // A leader: an aware starter on a winning team, then his teammates feel it.
    UPSRoster* Winners = MakeRoomRoster({ MakeRoomPlayer(TEXT("QB_Captain"), EPlayerRole::Quarterback, 80.f, 92.f), MakeRoomPlayer(TEXT("RB_1"), EPlayerRole::RunningBack, 75.f) });
    const TArray<FPSLockerRoomEvent> Emerged = Room->EvaluateTeam(Hawks, Winners, 0.75f, nullptr, false);
    TestEqual(TEXT("The captain emerges as a leader"), CountEvents(Emerged, EPSLockerRoomEventKind::LeaderEmerged, TEXT("QB_Captain")), 1);
    TestEqual(TEXT("...the running back's awareness isn't enough"), CountEvents(Emerged, EPSLockerRoomEventKind::LeaderEmerged, TEXT("RB_1")), 0);
    Room->EvaluateTeam(Hawks, Winners, 0.75f, nullptr, false);
    TestTrue(TEXT("The running back feels him: +0.03"), FMath::IsNearlyEqual(Room->GetMorale(FName(TEXT("RB_1"))), 0.68f, 1e-4f));
    TestTrue(TEXT("...but the leader doesn't lift himself"), FMath::IsNearlyEqual(Room->GetMorale(FName(TEXT("QB_Captain"))), 0.65f, 1e-4f));
    TestTrue(TEXT("Readable"), HasLine(Room->DescribePlayer(FName(TEXT("RB_1"))), TEXT("1 leader(s) in the room")));

    // A holdout: an underpaid star on a losing team, at a new league year; over once he is paid.
    UPSContractManager* Contracts = NewObject<UPSContractManager>();
    Contracts->LoadTuningFromJson(UPSContractManager::GetDefaultTuningPath());
    Contracts->StartLeague();
    const FPlayerAttributes Star = MakeRoomPlayer(TEXT("WR_Star"), EPlayerRole::WideReceiver, 90.f);
    UPSRoster* Holdouts = MakeRoomRoster({ Star });
    SignAtWorth(Contracts, Star, 0.5f, 3);
    TestEqual(TEXT("Mid-season, no holdout"), CountEvents(Room->EvaluateTeam(Hawks, Holdouts, 0.f, Contracts, false), EPSLockerRoomEventKind::Holdout, TEXT("WR_Star")), 0);
    TestTrue(TEXT("He is unhappy enough"), Room->GetMorale(Star.PlayerId) < Room->GetTuning().HoldoutMorale);
    TestEqual(TEXT("A new league year: he holds out"), CountEvents(Room->EvaluateTeam(Hawks, Holdouts, 0.f, Contracts, true), EPSLockerRoomEventKind::Holdout, TEXT("WR_Star")), 1);
    TestTrue(TEXT("...and sits"), Room->IsHoldingOut(Star.PlayerId) && HasLine(Room->DescribePlayer(Star.PlayerId), TEXT("Holding out")));
    Contracts->CutPlayer(Star.PlayerId, true);
    SignAtWorth(Contracts, Star, 1.2f, 3);
    TestEqual(TEXT("Paid, he ends it"), CountEvents(Room->EvaluateTeam(Hawks, Holdouts, 0.f, Contracts, false), EPSLockerRoomEventKind::HoldoutEnded, TEXT("WR_Star")), 1);
    TestFalse(TEXT("...and plays"), Room->IsHoldingOut(Star.PlayerId));
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The franchise and the save
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLockerRoomFranchiseTest,
    "PlaySports.LockerRoom.FranchiseFlowAndSave",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLockerRoomFranchiseTest::RunTest(const FString& Parameters)
{
    using namespace PSLockerRoomTests;

    UPSScheduleEngine* Schedule = NewObject<UPSScheduleEngine>();
    UPSFranchiseSeason* Season = NewObject<UPSFranchiseSeason>();
    const TArray<FName> TeamIds = { FName(TEXT("Falcons")), FName(TEXT("Hawks")), FName(TEXT("Wolves")), FName(TEXT("Bears")) };
    Season->InitializeSeason(TeamIds, Schedule->GenerateSeasonSchedule(FDateTime(2026, 9, 1), 2, TArray<int32>()));
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
    UPSLockerRoom* Room = MakeLockerRoom(0.5f);
    Flow->SetLockerRoom(Room);

    int32 Rostered = 0;
    for (const FName& TeamId : TeamIds)
    {
        Rostered += Flow->GetTeamRoster(TeamId)->GetFullRoster().Num();
    }

    // Week 1 played and the week turns: every rostered player's morale is read, every unit gels.
    TestEqual(TEXT("Week 1"), Flow->SimulateWeek(true), 2);
    TestFalse(TEXT("On to week 2"), Flow->AdvanceWeek());
    TestEqual(TEXT("Every rostered player is in a locker room"), Room->GetState().Players.Num(), Rostered);
    TestEqual(TEXT("Every team's two units have a lineup"), Room->GetState().Units.Num(), TeamIds.Num() * Room->GetTuning().Units.Num());
    TestEqual(TEXT("Week 2"), Flow->SimulateWeek(true), 2);
    for (const FName& TeamId : TeamIds)
    {
        TestTrue(*FString::Printf(TEXT("%s's line has played two games together"), *TeamId.ToString()),
            HasLine(Room->DescribeTeamChemistry(TeamId), TEXT("OffensiveLine: 2 game(s) together")));
    }

    // The season ends: the released players take their morale into free agency.
    TestTrue(TEXT("The season ends"), Flow->AdvanceWeek());
    UPSFreeAgency* Agency = Flow->GetFreeAgency();
    if (TestNotNull(TEXT("Free agency opened"), Agency) && TestTrue(TEXT("...with players"), Agency->GetPool().Num() > 0))
    {
        for (const FPSFreeAgent& FreeAgent : Agency->GetPool())
        {
            TestTrue(*FString::Printf(TEXT("%s brings his morale"), *FreeAgent.Player.PlayerId.ToString()),
                FMath::IsNearlyEqual(FreeAgent.Morale, Room->GetMorale(FreeAgent.Player.PlayerId), 1e-4f));
        }
    }

    // The locker room round-trips through the franchise save.
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    Room->SaveTo(Save);
    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    const FString Slot = TEXT("Test_LockerRoom");
    TestTrue(TEXT("The franchise saves"), Saves->SaveToSlot(Save, Slot));
    const UPSFranchiseSaveGame* Loaded = Cast<UPSFranchiseSaveGame>(Saves->LoadFromSlot(Slot));
    UPSLockerRoom* Restored = MakeLockerRoom(0.5f);
    if (TestNotNull(TEXT("...and loads"), Loaded) && TestTrue(TEXT("The locker room loads"), Restored->LoadFrom(Loaded)))
    {
        TestEqual(TEXT("Every player"), Restored->GetState().Players.Num(), Room->GetState().Players.Num());
        TestEqual(TEXT("Every unit"), Restored->GetState().Units.Num(), Room->GetState().Units.Num());
        const FName First = Room->GetState().Players[0].PlayerId;
        TestTrue(TEXT("A player's morale"), FMath::IsNearlyEqual(Restored->GetMorale(First), Room->GetMorale(First), 1e-6f));
        TestTrue(TEXT("A line's cohesion"), FMath::IsNearlyEqual(Restored->GetUnitCohesion(FName(TEXT("Hawks")), FName(TEXT("OffensiveLine"))), Room->GetUnitCohesion(FName(TEXT("Hawks")), FName(TEXT("OffensiveLine"))), 1e-6f));
    }
    IFileManager::Get().Delete(*UPSSaveSubsystem::GetSlotPath(Slot), false, true, true);
    IFileManager::Get().Delete(*(UPSSaveSubsystem::GetSlotPath(Slot) + TEXT(".bak")), false, true, true);
    TestFalse(TEXT("A save from before the locker room keeps the current one"), Restored->LoadFrom(NewObject<UPSFranchiseSaveGame>()));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
