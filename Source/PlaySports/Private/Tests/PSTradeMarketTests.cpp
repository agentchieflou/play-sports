// PSTradeMarketTests.cpp -- Epic 88 (trade logic and the league market)
//
// A four-team league (AAA, BBB, CCC, DDD), each roster at the contract market's RosterTarget for
// every position (fillers rated 70, aged 26), trades through UPSTradeMarket with the contract
// manager, the draft and Epic 94's aging curves. After the first cycle of games AAA has won all
// three (a contender), DDD lost all three (a rebuilder), BBB and CCC are balanced.
//
// Tests covered:
//   1. Data/trades.json loads through UPSDataIngestion, validates clean and equals FPSTradeTuning's
//      defaults field by field; a broken tuning's problems are each reported.
//   2. The value model: rating, position, age, contract cost and control order players; a team's
//      need and a player's trade request move his value exactly as the tuning says; the pick chart,
//      a pick's projected place from its team's standing, later drafts discounted, and each
//      stance's taste for now (a rental, a veteran) and for picks.
//   3. Answers: a short offer rejected with its reason, a near miss countered with the asset that
//      closes it, the counter accepted and made (the player changes rosters, his contract follows,
//      the pick changes hands); the lopsided-trade guardrail, an asset not held, the cap, the
//      player's own team, and the deadline.
//   4. The deadline: urgency builds over the ramp; at the deadline the contender buys the
//      rebuilder's veteran for picks, the same way every run; past it the market is closed.
//   5. Guardrails and telemetry: cooldowns, the trade limit, a roster left too thin, too many or
//      duplicate assets, the lopsided guardrail and its minimum gap; every trade on the bus and
//      through OnTradeCompleted; the telemetry's counts; the market and the traded picks
//      round-trip through the franchise save.
//   6. A franchise season through UPSFranchiseFlow: the player's team trades, the CPU trades,
//      rosters, contracts, the cap and positions stay valid every week, the trade is news, the
//      deadline closes the market, the off-season opens it, the draft gives traded picks to their
//      holders, and everything round-trips through the save.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "PSContractData.h"
#include "PSContractManager.h"
#include "PSDataIngestion.h"
#include "PSDraft.h"
#include "PSDraftData.h"
#include "PSFranchiseFlow.h"
#include "PSFranchiseSaveGame.h"
#include "PSFranchiseSeason.h"
#include "PSLeagueData.h"
#include "PSLeagueGenerator.h"
#include "PSLeagueNarrative.h"
#include "PSLockerRoom.h"
#include "PSLockerRoomData.h"
#include "PSNarrativeTypes.h"
#include "PSPlayerAging.h"
#include "PSPlayerAttributes.h"
#include "PSRoster.h"
#include "PSSaveSubsystem.h"
#include "PSScheduleEngine.h"
#include "PSStaffManager.h"
#include "PSStatsEngine.h"
#include "PSTelemetryBus.h"
#include "PSTradeData.h"
#include "PSTradeMarket.h"
#include "PSUITeamCatalog.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSTradeMarketTests
{
    FString RoleName(EPlayerRole Role)
    {
        return StaticEnum<EPlayerRole>()->GetNameStringByValue(static_cast<int64>(Role));
    }

    FPlayerAttributes MakeTradePlayer(const FString& PlayerId, EPlayerRole Role, float Rating, int32 Age)
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

    UPSFranchiseSeason* MakeTradeSeason(const TArray<FName>& Teams, int32 Weeks)
    {
        TArray<FSeasonWeek> Schedule;
        for (int32 Week = 1; Week <= Weeks; ++Week)
        {
            FSeasonWeek& Entry = Schedule.AddDefaulted_GetRef();
            Entry.WeekNumber = Week;
        }
        UPSFranchiseSeason* Season = NewObject<UPSFranchiseSeason>();
        Season->InitializeSeason(Teams, Schedule);
        return Season;
    }

    /** Weeks 1-3, every pair once: AAA wins every game, DDD loses every one, BBB and CCC tie. */
    void PlayFirstCycle(UPSFranchiseSeason* Season)
    {
        const FName Winner(TEXT("AAA"));
        const FName Loser(TEXT("DDD"));
        for (int32 Week = 1; Week <= 3; ++Week)
        {
            for (const FPSWeekMatchup& Matchup : Season->GetMatchupsForWeek(Week))
            {
                int32 HomeScore = 17;
                int32 AwayScore = 17;
                if (Matchup.HomeTeamId == Winner || Matchup.AwayTeamId == Loser)
                {
                    HomeScore = 24;
                    AwayScore = 10;
                }
                else if (Matchup.AwayTeamId == Winner || Matchup.HomeTeamId == Loser)
                {
                    HomeScore = 10;
                    AwayScore = 24;
                }
                Season->RecordGameResult(Week, Matchup.HomeTeamId, Matchup.AwayTeamId, HomeScore, AwayScore);
            }
        }
    }

    /** A season's talent alone: no seasons ahead, no contract surplus, no need. */
    void Simplify(FPSTradeTuning& Tuning)
    {
        Tuning.ValueHorizonYears = 1;
        Tuning.SurplusValuePerCap = 0.f;
        Tuning.NeedValueWeight = 0.f;
    }

    void KeepShipped(FPSTradeTuning&)
    {
    }

    FPSTradeProposal MakeProposal(FName FromTeamId, FName ToTeamId, const TArray<FPSTradeAsset>& FromAssets, const TArray<FPSTradeAsset>& ToAssets)
    {
        FPSTradeProposal Proposal;
        Proposal.FromTeamId = FromTeamId;
        Proposal.ToTeamId = ToTeamId;
        Proposal.FromAssets = FromAssets;
        Proposal.ToAssets = ToAssets;
        return Proposal;
    }

    FPSTradeAsset PlayerAsset(const TCHAR* PlayerId)
    {
        return FPSTradeAsset::MakePlayer(FName(PlayerId));
    }

    FPSTradeAsset PickAsset(int32 DraftYear, int32 Round, const TCHAR* TeamId)
    {
        return FPSTradeAsset::MakePick(DraftYear, Round, FName(TeamId));
    }

    bool HasReason(const FPSTradeEvaluation& Evaluation, EPSTradeReason Reason)
    {
        return Evaluation.Reasons.Contains(Reason);
    }

    bool IsOn(const UPSRoster* Roster, const TCHAR* PlayerId)
    {
        return Roster && Roster->FindPlayerPtr(FName(PlayerId)) != nullptr;
    }

    UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    void DestroyTestWorld(UWorld* World)
    {
        if (World)
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    }

    /** The four teams, their rosters at RosterTarget, the contracts, the draft, the aging curves
     *  and the market, connected. */
    struct FTradeLeague
    {
        TArray<FName> TeamIds;
        UPSFranchiseSeason* Season = nullptr;
        UPSContractManager* Contracts = nullptr;
        UPSDraft* Draft = nullptr;
        UPSPlayerAging* Aging = nullptr;
        UPSTradeMarket* Market = nullptr;
        TMap<FName, UPSRoster*> Rosters;

        void Start(int32 Weeks, FName UserTeamId, void (*Adjust)(FPSTradeTuning&))
        {
            TeamIds = { FName(TEXT("AAA")), FName(TEXT("BBB")), FName(TEXT("CCC")), FName(TEXT("DDD")) };
            Season = MakeTradeSeason(TeamIds, Weeks);
            Contracts = NewObject<UPSContractManager>();
            Contracts->LoadTuningFromJson(UPSContractManager::GetDefaultTuningPath());
            Contracts->StartLeague();
            Draft = NewObject<UPSDraft>();
            Draft->LoadTuningFromJson(UPSDraft::GetDefaultTuningPath());
            Aging = NewObject<UPSPlayerAging>();
            Aging->LoadDefaults();
            Market = NewObject<UPSTradeMarket>();
            Market->LoadTuningFromJson(UPSTradeMarket::GetDefaultTuningPath());
            FPSTradeTuning Adjusted = Market->GetTuning();
            Adjust(Adjusted);
            Market->SetTuning(Adjusted);
            Market->Connect(Season, Contracts, Draft, UserTeamId);
            Market->SetPlayerAging(Aging);
            for (const FName& TeamId : TeamIds)
            {
                TArray<FPlayerAttributes> Players;
                for (const FPSPositionMarket& Position : Contracts->GetTuning().PositionMarkets)
                {
                    for (int32 Index = 0; Index < Position.RosterTarget; ++Index)
                    {
                        Players.Add(MakeTradePlayer(FString::Printf(TEXT("%s_%s_%d"), *TeamId.ToString(), *RoleName(Position.Role), Index), Position.Role, 70.f, 26));
                    }
                }
                UPSRoster* Roster = NewObject<UPSRoster>();
                Roster->InitializeRoster(Players);
                Roster->BuildDefaultDepthChart();
                Rosters.Add(TeamId, Roster);
                Market->RegisterTeam(TeamId, Roster);
                Contracts->RegisterTeam(TeamId);
            }
        }

        UPSRoster* RosterOf(const TCHAR* TeamId) const
        {
            return Rosters.FindRef(FName(TeamId));
        }

        void Add(const TCHAR* TeamId, const FPlayerAttributes& Player)
        {
            RosterOf(TeamId)->AddPlayer(Player);
        }

        bool Sign(const TCHAR* TeamId, const TCHAR* PlayerId, int32 Years, int32 AnnualValue)
        {
            FPSContractOffer Offer;
            Offer.TeamId = FName(TeamId);
            Offer.Years = Years;
            Offer.AnnualValue = AnnualValue;
            return Contracts->SignContract(Contracts->MakeContract(FName(PlayerId), Offer)) == EPSCapResult::Ok;
        }
    };
}

// ---------------------------------------------------------------------------
// Test 1 -- The shipped tuning
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTradeTuningTest,
    "PlaySports.Trade.TuningLoadsAndValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTradeTuningTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSTradeTuning FromFile;
    if (!TestTrue(TEXT("Data/trades.json loads"), Ingestion->LoadTradeTuningFromJson(UPSTradeMarket::GetDefaultTuningPath(), FromFile)))
    {
        return false;
    }
    const TArray<FString> Problems = UPSTradeMarket::ValidateTuning(FromFile);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    TestEqual(TEXT("The shipped tuning is sound"), Problems.Num(), 0);
    TestEqual(TEXT("Three stances"), FromFile.Stances.Num(), 3);
    TestEqual(TEXT("A value for each of the draft's rounds"), FromFile.PickRoundValues.Num(), 7);
    const FPSTradeTuning Defaults;
    for (TFieldIterator<FProperty> It(FPSTradeTuning::StaticStruct()); It; ++It)
    {
        TestTrue(*FString::Printf(TEXT("%s matches the default"), *It->GetName()), It->Identical_InContainer(&FromFile, &Defaults));
    }

    FPSTradeTuning Broken = FromFile;
    Broken.CounterRatio = 2.f;
    Broken.Stances.RemoveAt(2);
    Broken.PickRoundValues = { 100.f, 200.f };
    Broken.RebuilderWinPercentage = 0.7f;
    Broken.MaxAssetsPerSide = 0;
    const TArray<FString> BrokenProblems = UPSTradeMarket::ValidateTuning(Broken);
    for (const TCHAR* Expected : { TEXT("CounterRatio"), TEXT("Rebuilder has 0 entries"), TEXT("PickRoundValues"), TEXT("RebuilderWinPercentage"), TEXT("MaxAssetsPerSide") })
    {
        TestTrue(*FString::Printf(TEXT("Reported: %s"), Expected),
            BrokenProblems.ContainsByPredicate([Expected](const FString& Line) { return Line.Contains(Expected); }));
    }
    UPSTradeMarket* Market = NewObject<UPSTradeMarket>();
    TestFalse(TEXT("A missing file is refused"), Market->LoadTuningFromJson(TEXT("Data/no_such_trades.json")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The value model
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTradeValueModelTest,
    "PlaySports.Trade.ValueModel",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTradeValueModelTest::RunTest(const FString& Parameters)
{
    using namespace PSTradeMarketTests;

    FTradeLeague League;
    League.Start(20, NAME_None, &KeepShipped);
    UPSTradeMarket* Market = League.Market;
    const FPSTradeTuning& T = Market->GetTuning();
    const FName AAA(TEXT("AAA"));
    const FName BBB(TEXT("BBB"));
    const FName CCC(TEXT("CCC"));
    const FName DDD(TEXT("DDD"));

    // BBB's players: one change at a time from a 26-year-old quarterback.
    League.Add(TEXT("BBB"), MakeTradePlayer(TEXT("BBB_QB_HI"), EPlayerRole::Quarterback, 85.f, 26));
    League.Add(TEXT("BBB"), MakeTradePlayer(TEXT("BBB_QB_LO"), EPlayerRole::Quarterback, 75.f, 26));
    League.Add(TEXT("BBB"), MakeTradePlayer(TEXT("BBB_WR_HI"), EPlayerRole::WideReceiver, 85.f, 26));
    League.Add(TEXT("BBB"), MakeTradePlayer(TEXT("BBB_QB_YOUNG"), EPlayerRole::Quarterback, 85.f, 24));
    League.Add(TEXT("BBB"), MakeTradePlayer(TEXT("BBB_QB_OLD"), EPlayerRole::Quarterback, 85.f, 34));
    League.Add(TEXT("BBB"), MakeTradePlayer(TEXT("BBB_QB_CHEAP"), EPlayerRole::Quarterback, 80.f, 26));
    League.Add(TEXT("BBB"), MakeTradePlayer(TEXT("BBB_QB_DEAR"), EPlayerRole::Quarterback, 80.f, 26));
    League.Add(TEXT("BBB"), MakeTradePlayer(TEXT("BBB_QB_RENTAL"), EPlayerRole::Quarterback, 80.f, 26));
    TestTrue(TEXT("A cheap three-year deal is signed"), League.Sign(TEXT("BBB"), TEXT("BBB_QB_CHEAP"), 3, 900));
    TestTrue(TEXT("An expensive one"), League.Sign(TEXT("BBB"), TEXT("BBB_QB_DEAR"), 3, 30000));
    TestTrue(TEXT("A one-year deal at the cheap price"), League.Sign(TEXT("BBB"), TEXT("BBB_QB_RENTAL"), 1, 900));

    // Stances from the standings.
    TestEqual(TEXT("Before any games every team is balanced"), Market->GetTeamStance(AAA), EPSTradeStance::Balanced);
    PlayFirstCycle(League.Season);
    TestEqual(TEXT("3-0 is a contender"), Market->GetTeamStance(AAA), EPSTradeStance::Contender);
    TestEqual(TEXT("1-1-1 is balanced"), Market->GetTeamStance(BBB), EPSTradeStance::Balanced);
    TestEqual(TEXT("...both of them"), Market->GetTeamStance(CCC), EPSTradeStance::Balanced);
    TestEqual(TEXT("0-3 is a rebuilder"), Market->GetTeamStance(DDD), EPSTradeStance::Rebuilder);
    TestEqual(TEXT("Week 1 of 20 is far from the deadline"), Market->GetDeadlineUrgency(), 0.f);

    // The league's neutral view orders players.
    auto Neutral = [Market](const TCHAR* PlayerId) { return Market->ValuePlayer(NAME_None, FName(PlayerId)); };
    TestTrue(TEXT("A better rating is worth more"), Neutral(TEXT("BBB_QB_HI")) > Neutral(TEXT("BBB_QB_LO")));
    TestTrue(TEXT("A quarterback is worth more than a receiver as good"), Neutral(TEXT("BBB_QB_HI")) > Neutral(TEXT("BBB_WR_HI")));
    TestTrue(TEXT("A rising 24-year-old is worth more than a declining 34-year-old"), Neutral(TEXT("BBB_QB_YOUNG")) > Neutral(TEXT("BBB_QB_OLD")));
    TestTrue(TEXT("A cheap contract is worth more than an expensive one"), Neutral(TEXT("BBB_QB_CHEAP")) > Neutral(TEXT("BBB_QB_DEAR")));
    TestTrue(TEXT("...and three years of him more than a rental"), Neutral(TEXT("BBB_QB_CHEAP")) > Neutral(TEXT("BBB_QB_RENTAL")));
    TestEqual(TEXT("A player on no roster is worth nothing"), Market->ValuePlayer(NAME_None, FName(TEXT("NOBODY"))), 0.f);

    // Stances: a contender lives for now, a rebuilder for later.
    auto RatioFor = [Market](FName TeamId, const TCHAR* Numerator, const TCHAR* Denominator)
    {
        return Market->ValuePlayer(TeamId, FName(Numerator)) / FMath::Max(1.f, Market->ValuePlayer(TeamId, FName(Denominator)));
    };
    TestTrue(TEXT("A rental keeps more of his value to a contender than to a rebuilder"),
        RatioFor(AAA, TEXT("BBB_QB_RENTAL"), TEXT("BBB_QB_CHEAP")) > RatioFor(DDD, TEXT("BBB_QB_RENTAL"), TEXT("BBB_QB_CHEAP")));
    TestTrue(TEXT("...and so does a veteran against a prospect"),
        RatioFor(AAA, TEXT("BBB_QB_OLD"), TEXT("BBB_QB_YOUNG")) > RatioFor(DDD, TEXT("BBB_QB_OLD"), TEXT("BBB_QB_YOUNG")));

    // Need: CCC down to two receivers of five values a receiver NeedValueWeight x 3/5 more.
    FPlayerAttributes Removed;
    for (const TCHAR* Receiver : { TEXT("CCC_WideReceiver_2"), TEXT("CCC_WideReceiver_3"), TEXT("CCC_WideReceiver_4") })
    {
        TestTrue(*FString::Printf(TEXT("%s leaves"), Receiver), League.RosterOf(TEXT("CCC"))->RemovePlayer(FName(Receiver), Removed));
    }
    const float ReceiverNeutral = Neutral(TEXT("BBB_WR_HI"));
    TestEqual(TEXT("A team short at his position values him for its need"),
        Market->ValuePlayer(CCC, FName(TEXT("BBB_WR_HI"))), ReceiverNeutral * (1.f + T.NeedValueWeight * 0.6f), 0.01f * ReceiverNeutral);

    // A trade request (Epic 91): his own team values him at TradeRequestDiscount.
    const float BeforeRequest = Market->ValuePlayer(BBB, FName(TEXT("BBB_QB_LO")));
    const float ToContender = Market->ValuePlayer(AAA, FName(TEXT("BBB_QB_LO")));
    UPSLockerRoom* Room = NewObject<UPSLockerRoom>();
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    FPSPlayerMorale& Unhappy = Save->LockerRoom.Players.AddDefaulted_GetRef();
    Unhappy.PlayerId = FName(TEXT("BBB_QB_LO"));
    Unhappy.TeamId = BBB;
    Unhappy.Morale = 0.1f;
    Unhappy.bTradeRequested = true;
    TestTrue(TEXT("The locker room loads his request"), Room->LoadFrom(Save));
    Market->SetLockerRoom(Room);
    TestEqual(TEXT("His team values a player who asked out less"), Market->ValuePlayer(BBB, FName(TEXT("BBB_QB_LO"))), BeforeRequest * T.TradeRequestDiscount, 0.01f * BeforeRequest);
    TestEqual(TEXT("...other teams don't"), Market->ValuePlayer(AAA, FName(TEXT("BBB_QB_LO"))), ToContender, 0.01f * ToContender);

    // Picks: the chart, the team's standing, the year, the stance.
    TestEqual(TEXT("The first overall pick is the chart's top"), Market->GetPickChartValue(1, 0.f), T.PickRoundValues[0], 0.01f);
    TestEqual(TEXT("The second round starts at its value"), Market->GetPickChartValue(2, 0.f), T.PickRoundValues[1], 0.01f);
    TestEqual(TEXT("The last pick is worth LastPickValue"), Market->GetPickChartValue(7, 1.f), T.LastPickValue, 0.01f);
    TestEqual(TEXT("The coming draft is next league year's"), Market->GetNextDraftYear(), League.Contracts->GetLeagueYear() + 1);
    const int32 Year = Market->GetNextDraftYear();
    TestTrue(TEXT("The worst team's pick projects earlier than the best team's"),
        Market->GetProjectedRoundPosition(Year, 1, DDD) < Market->GetProjectedRoundPosition(Year, 1, AAA));
    TestEqual(TEXT("A later draft's pick projects to the middle"), Market->GetProjectedRoundPosition(Year + 1, 1, DDD), 3.f / 8.f, 0.001f);
    TestTrue(TEXT("...so the worst team's pick is worth more"), Market->ValuePick(NAME_None, Year, 1, DDD) > Market->ValuePick(NAME_None, Year, 1, AAA));
    TestTrue(TEXT("A first-rounder is worth more than a second"), Market->ValuePick(NAME_None, Year, 1, DDD) > Market->ValuePick(NAME_None, Year, 2, DDD));
    TestTrue(TEXT("This draft's pick is worth more than next year's"), Market->ValuePick(NAME_None, Year, 1, DDD) > Market->ValuePick(NAME_None, Year + 1, 1, DDD));
    TestEqual(TEXT("A rebuilder values a pick PickMultiplier more than a contender"),
        Market->ValuePick(DDD, Year, 1, BBB) / Market->ValuePick(AAA, Year, 1, BBB),
        T.FindStance(EPSTradeStance::Rebuilder)->PickMultiplier / T.FindStance(EPSTradeStance::Contender)->PickMultiplier, 0.001f);

    // What a team can trade: its roster and its picks in the tradable drafts.
    const TArray<FPSTradeAsset> Assets = Market->GetTeamAssets(AAA);
    const int32 PickCount = Assets.FilterByPredicate([](const FPSTradeAsset& Asset) { return Asset.Kind == EPSTradeAssetKind::DraftPick; }).Num();
    TestEqual(TEXT("AAA's players"), Assets.Num() - PickCount, League.RosterOf(TEXT("AAA"))->GetFullRoster().Num());
    TestEqual(TEXT("AAA's picks: every round of two drafts"), PickCount, League.Draft->GetTuning().NumRounds * T.TradablePickYears);
    const FPSTradeAssetValue Valued = Market->ValueAsset(PickAsset(Year, 1, TEXT("AAA")), AAA, DDD);
    TestTrue(TEXT("An asset's label names it"), Valued.Label.Contains(TEXT("AAA")) && Valued.Label.Contains(FString::FromInt(Year)));
    TestTrue(TEXT("...and its values are each side's"), Valued.ValueToReceiver > Valued.ValueToGiver);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Accept, counter, reject
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTradeAnswersTest,
    "PlaySports.Trade.AcceptCounterReject",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTradeAnswersTest::RunTest(const FString& Parameters)
{
    using namespace PSTradeMarketTests;

    // A season's talent alone (a quarterback rated 82 is worth 375), no games played: every team
    // balanced, every pick at the middle of its round. AAA is the player's team.
    FTradeLeague League;
    League.Start(20, FName(TEXT("AAA")), &Simplify);
    UPSTradeMarket* Market = League.Market;
    UPSContractManager* Contracts = League.Contracts;
    const FName AAA(TEXT("AAA"));
    const FName BBB(TEXT("BBB"));
    const int32 Year = Market->GetNextDraftYear();
    League.Add(TEXT("BBB"), MakeTradePlayer(TEXT("BBB_QB_STAR"), EPlayerRole::Quarterback, 82.f, 26));
    League.Add(TEXT("AAA"), MakeTradePlayer(TEXT("AAA_QB_81"), EPlayerRole::Quarterback, 81.f, 26));
    League.Add(TEXT("AAA"), MakeTradePlayer(TEXT("AAA_QB_80"), EPlayerRole::Quarterback, 80.f, 26));
    TestTrue(TEXT("The star is signed"), League.Sign(TEXT("BBB"), TEXT("BBB_QB_STAR"), 1, 5000));
    TestEqual(TEXT("A quarterback rated 82 is worth 375 for a season"), Market->ValuePlayer(BBB, FName(TEXT("BBB_QB_STAR"))), 375.f, 0.5f);

    // Short: rejected, with its reason.
    const FPSTradeEvaluation Short = Market->EvaluateTrade(MakeProposal(AAA, BBB, { PlayerAsset(TEXT("AAA_QB_80")) }, { PlayerAsset(TEXT("BBB_QB_STAR")) }));
    TestEqual(TEXT("80 for 82 is rejected"), Short.Response, EPSTradeResponse::Reject);
    TestTrue(TEXT("...for its value"), HasReason(Short, EPSTradeReason::NotEnoughValue));
    TestTrue(TEXT("...said in words"), Short.ReasonTexts.Num() == 1 && Short.ReasonTexts[0].Contains(TEXT("BBB")));
    TestTrue(TEXT("...not as lopsided"), !HasReason(Short, EPSTradeReason::Lopsided));
    TestEqual(TEXT("What BBB would get"), Short.ValueIn, Market->ValuePlayer(BBB, FName(TEXT("AAA_QB_80"))), 0.5f);
    TestEqual(TEXT("What BBB would give"), Short.ValueOut, 375.f, 0.5f);

    // A near miss: countered with the smallest asset that closes it, AAA's third-rounder.
    const FPSTradeProposal NearMiss = MakeProposal(AAA, BBB, { PlayerAsset(TEXT("AAA_QB_81")) }, { PlayerAsset(TEXT("BBB_QB_STAR")) });
    const FPSTradeEvaluation Countered = Market->ProposeTrade(NearMiss);
    TestEqual(TEXT("81 for 82 is countered"), Countered.Response, EPSTradeResponse::Counter);
    TestTrue(TEXT("...asking for one more asset"), HasReason(Countered, EPSTradeReason::CounterAddAsset));
    TestTrue(TEXT("...AAA's third-rounder"), Countered.bHasCounter && Countered.Counter.FromAssets.ContainsByPredicate([Year](const FPSTradeAsset& Asset)
    {
        return Asset.IsSameAs(PickAsset(Year, 3, TEXT("AAA")));
    }));
    TestEqual(TEXT("...and nothing else"), Countered.Counter.FromAssets.Num(), 2);
    TestEqual(TEXT("No trade is made on a counter"), Market->GetHistory().Num(), 0);

    // The counter, proposed, is accepted and made.
    const int32 AAASpace = Contracts->GetCapSpace(AAA);
    const int32 BBBSpace = Contracts->GetCapSpace(BBB);
    const FPSTradeEvaluation Accepted = Market->ProposeTrade(Countered.Counter);
    TestEqual(TEXT("The counter is accepted"), Accepted.Response, EPSTradeResponse::Accept);
    TestTrue(TEXT("...for its value"), HasReason(Accepted, EPSTradeReason::GoodValue));
    TestEqual(TEXT("...and made"), Accepted.TradeId, 1);
    TestTrue(TEXT("The star is AAA's"), IsOn(League.RosterOf(TEXT("AAA")), TEXT("BBB_QB_STAR")) && !IsOn(League.RosterOf(TEXT("BBB")), TEXT("BBB_QB_STAR")));
    TestTrue(TEXT("AAA's quarterback is BBB's"), IsOn(League.RosterOf(TEXT("BBB")), TEXT("AAA_QB_81")) && !IsOn(League.RosterOf(TEXT("AAA")), TEXT("AAA_QB_81")));
    const FPSContract* StarDeal = Contracts->FindContract(FName(TEXT("BBB_QB_STAR")));
    TestTrue(TEXT("His contract followed him"), StarDeal && StarDeal->TeamId == AAA);
    TestEqual(TEXT("...off BBB's cap"), Contracts->GetCapSpace(BBB), BBBSpace + 5000);
    TestEqual(TEXT("...onto AAA's"), Contracts->GetCapSpace(AAA), AAASpace - 5000);
    TestEqual(TEXT("The third-rounder is BBB's"), League.Draft->GetPickOwner(Year, 3, AAA), BBB);
    TestEqual(TEXT("A CPU team plays him where his rating puts him"), League.RosterOf(TEXT("BBB"))->GetStarterId(EPlayerRole::Quarterback), FName(TEXT("AAA_QB_81")));
    TestEqual(TEXT("The player's team sets its own depth chart"), League.RosterOf(TEXT("AAA"))->GetDepthChartForRole(EPlayerRole::Quarterback).Last(), FName(TEXT("BBB_QB_STAR")));
    TestEqual(TEXT("The trade is in the history"), Market->GetHistory().Num(), 1);
    TestTrue(TEXT("...described"), Market->GetHistory().Last().Description.Contains(TEXT("BBB_QB_STAR")));

    // The guardrail: BBB would love this, the league won't have it.
    const FPSTradeEvaluation Lopsided = Market->EvaluateTrade(MakeProposal(AAA, BBB,
        { PlayerAsset(TEXT("AAA_QB_80")), PickAsset(Year, 1, TEXT("AAA")) }, { PlayerAsset(TEXT("BBB_Quarterback_1")) }));
    TestEqual(TEXT("A star and a first for a backup is refused"), Lopsided.Response, EPSTradeResponse::Reject);
    TestTrue(TEXT("...as lopsided"), HasReason(Lopsided, EPSTradeReason::Lopsided));
    TestTrue(TEXT("...though BBB would gain"), Lopsided.ValueIn > Lopsided.ValueOut);
    TestTrue(TEXT("...far past MaxValueImbalance"), Lopsided.Imbalance > Market->GetTuning().MaxValueImbalance);

    // Assets a side doesn't hold.
    const FPSTradeEvaluation NotHeld = Market->EvaluateTrade(MakeProposal(AAA, BBB, { PickAsset(Year, 1, TEXT("BBB")) }, { PlayerAsset(TEXT("BBB_Quarterback_1")) }));
    TestTrue(TEXT("AAA can't give BBB's own pick"), HasReason(NotHeld, EPSTradeReason::AssetNotOwned));
    const FPSTradeEvaluation Gone = Market->EvaluateTrade(MakeProposal(AAA, BBB, { PickAsset(Year, 3, TEXT("AAA")) }, { PlayerAsset(TEXT("BBB_Quarterback_1")) }));
    TestTrue(TEXT("...nor the pick it traded away"), HasReason(Gone, EPSTradeReason::AssetNotOwned));

    // The player's team answers for itself.
    const FPSTradeEvaluation ToUser = Market->ProposeTrade(MakeProposal(BBB, AAA, { PlayerAsset(TEXT("BBB_Quarterback_1")) }, { PlayerAsset(TEXT("AAA_Quarterback_1")) }));
    TestTrue(TEXT("A CPU proposal to the player's team waits for the player"), ToUser.Response == EPSTradeResponse::Reject && HasReason(ToUser, EPSTradeReason::UserTeamDecides));

    // The cap: BBB, nearly capped out, can't take on AAA's big contract.
    League.Add(TEXT("AAA"), MakeTradePlayer(TEXT("AAA_BIG"), EPlayerRole::OffensiveLineman, 75.f, 28));
    League.Add(TEXT("BBB"), MakeTradePlayer(TEXT("BBB_RICH"), EPlayerRole::OffensiveLineman, 75.f, 28));
    TestTrue(TEXT("AAA's big deal is signed"), League.Sign(TEXT("AAA"), TEXT("AAA_BIG"), 1, 100000));
    TestTrue(TEXT("BBB's bigger one"), League.Sign(TEXT("BBB"), TEXT("BBB_RICH"), 1, 200000));
    const FPSTradeEvaluation OverCap = Market->EvaluateTrade(MakeProposal(AAA, BBB, { PlayerAsset(TEXT("AAA_BIG")) }, { PlayerAsset(TEXT("BBB_OffensiveLineman_0")) }));
    TestTrue(TEXT("A trade over the cap is refused"), OverCap.Response == EPSTradeResponse::Reject && HasReason(OverCap, EPSTradeReason::OverCap));
    TestTrue(TEXT("...naming the team"), OverCap.ReasonTexts.Num() > 0 && OverCap.ReasonTexts[0].Contains(TEXT("BBB")));
    const FPSContract* BigDeal = Contracts->FindContract(FName(TEXT("AAA_BIG")));
    TestTrue(TEXT("...and nothing moved"), IsOn(League.RosterOf(TEXT("AAA")), TEXT("AAA_BIG")) && BigDeal && BigDeal->TeamId == AAA);

    // The deadline: week 12 of 20; past it trades close until the season is over.
    TestEqual(TEXT("The deadline week"), Market->GetDeadlineWeek(), 12);
    for (int32 Week = 1; Week <= 12; ++Week)
    {
        League.Season->AdvanceWeek();
    }
    TestFalse(TEXT("Week 13 is past the deadline"), Market->IsWindowOpen());
    const FPSTradeEvaluation Late = Market->ProposeTrade(MakeProposal(AAA, BBB, { PickAsset(Year + 1, 2, TEXT("AAA")) }, { PlayerAsset(TEXT("BBB_Quarterback_1")) }));
    TestTrue(TEXT("A trade after the deadline is refused"), HasReason(Late, EPSTradeReason::WindowClosed));
    Market->BeginOffseason();
    TestTrue(TEXT("The off-season opens the market"), Market->IsOffseason() && Market->IsWindowOpen());
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The deadline: contenders buy, rebuilders sell
// ---------------------------------------------------------------------------
namespace PSTradeMarketTests
{
    /** Every CPU team that isn't balanced looks for a trade in the deadline's ramp; balanced ones
     *  never do. */
    void DeadlineOnly(FPSTradeTuning& Tuning)
    {
        Tuning.BaseTradeChance = 0.f;
        Tuning.DeadlineTradeChance = 1.f;
    }

    /** The league with DDD's 31-year-old receiver (rated 86), at Week. */
    void StartDeadlineLeague(FTradeLeague& League, int32 Week)
    {
        League.Start(10, NAME_None, &DeadlineOnly);
        League.Add(TEXT("DDD"), MakeTradePlayer(TEXT("DDD_WR_VET"), EPlayerRole::WideReceiver, 86.f, 31));
        PlayFirstCycle(League.Season);
        while (League.Season->GetCurrentWeek() < Week)
        {
            League.Season->AdvanceWeek();
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTradeDeadlineTest,
    "PlaySports.Trade.DeadlineBuyersAndSellers",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTradeDeadlineTest::RunTest(const FString& Parameters)
{
    using namespace PSTradeMarketTests;

    FTradeLeague League;
    StartDeadlineLeague(League, 1);
    UPSTradeMarket* Market = League.Market;
    const FName AAA(TEXT("AAA"));
    const FName DDD(TEXT("DDD"));
    const FName Veteran(TEXT("DDD_WR_VET"));

    // Far from the deadline nobody looks.
    TestEqual(TEXT("The deadline: week 6 of 10"), Market->GetDeadlineWeek(), 6);
    TestEqual(TEXT("No urgency in week 1"), Market->GetDeadlineUrgency(), 0.f);
    const float RebuilderEarly = Market->ValuePlayer(DDD, Veteran);
    TestEqual(TEXT("No trades in week 1"), Market->RunWeek().Num(), 0);

    // The ramp.
    for (int32 Week = 1; Week < 4; ++Week)
    {
        League.Season->AdvanceWeek();
    }
    TestEqual(TEXT("A third of the urgency two weeks out"), Market->GetDeadlineUrgency(), 1.f / 3.f, 0.001f);
    League.Season->AdvanceWeek();
    League.Season->AdvanceWeek();
    TestEqual(TEXT("Full urgency in the deadline week"), Market->GetDeadlineUrgency(), 1.f, 0.001f);
    TestTrue(TEXT("...trades still open"), Market->IsWindowOpen());

    // The veteran: worth more to the contender, and less to his rebuilding team as the deadline nears.
    const float RebuilderLate = Market->ValuePlayer(DDD, Veteran);
    TestTrue(TEXT("A rebuilder weighs this season less at the deadline"), RebuilderLate < RebuilderEarly);
    TestTrue(TEXT("The contender values the veteran more than his team"), Market->ValuePlayer(AAA, Veteran) > RebuilderLate + Market->GetTuning().MinTargetGain);

    // The deadline: the contender buys him for picks.
    const TArray<FPSTradeRecord> Made = Market->RunWeek();
    const FPSTradeRecord* Deal = Made.FindByPredicate([&Veteran](const FPSTradeRecord& Trade) { return Trade.HeadlinePlayerId == Veteran; });
    if (!TestNotNull(TEXT("The veteran is traded at the deadline"), Deal))
    {
        return false;
    }
    TestEqual(TEXT("...to the contender"), Deal->HeadlineTeamId, AAA);
    TestEqual(TEXT("...who proposed it"), Deal->Proposal.FromTeamId, AAA);
    TestEqual(TEXT("The buyer was a contender"), Deal->FromStance, EPSTradeStance::Contender);
    TestEqual(TEXT("The seller a rebuilder"), Deal->ToStance, EPSTradeStance::Rebuilder);
    TestTrue(TEXT("The rebuilder got picks"), Deal->Proposal.FromAssets.Num() > 0 && !Deal->Proposal.FromAssets.ContainsByPredicate([](const FPSTradeAsset& Asset)
    {
        return Asset.Kind != EPSTradeAssetKind::DraftPick;
    }));
    for (const FPSTradeAsset& Pick : Deal->Proposal.FromAssets)
    {
        TestEqual(*FString::Printf(TEXT("%s is DDD's"), *Market->DescribeAsset(Pick)), League.Draft->GetPickOwner(Pick.DraftYear, Pick.Round, Pick.OriginalTeamId), DDD);
    }
    TestTrue(TEXT("The veteran is on the contender's roster"), IsOn(League.RosterOf(TEXT("AAA")), TEXT("DDD_WR_VET")) && !IsOn(League.RosterOf(TEXT("DDD")), TEXT("DDD_WR_VET")));
    TestTrue(TEXT("Both sides cleared the bar"), Deal->ToValueIn >= Deal->ToValueOut && Deal->FromValueIn >= Deal->FromValueOut * 0.9f);
    TestTrue(TEXT("The guardrail held"), Deal->Imbalance <= Market->GetTuning().MaxValueImbalance);
    const FPSTradeTelemetry& Counts = Market->GetTelemetry();
    TestEqual(TEXT("Telemetry: the trades"), Counts.Trades, Made.Num());
    TestTrue(TEXT("Telemetry: a contender bought"), Counts.ContenderBuys >= 1);
    TestTrue(TEXT("Telemetry: a rebuilder sold"), Counts.RebuilderSells >= 1);
    TestTrue(TEXT("Telemetry: at the deadline"), Counts.DeadlineTrades >= 1);

    // The same week trades the same way every time.
    FTradeLeague Again;
    StartDeadlineLeague(Again, 6);
    const TArray<FPSTradeRecord> Repeated = Again.Market->RunWeek();
    TestEqual(TEXT("A second run makes as many trades"), Repeated.Num(), Made.Num());
    if (Repeated.Num() > 0 && Made.Num() > 0)
    {
        TestEqual(TEXT("...the same ones"), Repeated[0].Description, Made[0].Description);
    }

    // Past the deadline the market is closed.
    League.Season->AdvanceWeek();
    TestFalse(TEXT("Week 7 is past the deadline"), Market->IsWindowOpen());
    TestEqual(TEXT("...no urgency"), Market->GetDeadlineUrgency(), 0.f);
    TestEqual(TEXT("...no trades"), Market->RunWeek().Num(), 0);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Guardrails and telemetry
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTradeGuardrailTest,
    "PlaySports.Trade.GuardrailsAndTelemetry",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTradeGuardrailTest::RunTest(const FString& Parameters)
{
    using namespace PSTradeMarketTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestNotNull(TEXT("The bus"), Bus))
    {
        DestroyTestWorld(World);
        return false;
    }

    FTradeLeague League;
    League.Start(20, NAME_None, &Simplify);
    UPSTradeMarket* Market = League.Market;
    Market->BindToBus(Bus);
    const FName AAA(TEXT("AAA"));
    const FName BBB(TEXT("BBB"));
    const FName CCC(TEXT("CCC"));
    const int32 Year = Market->GetNextDraftYear();
    League.Add(TEXT("BBB"), MakeTradePlayer(TEXT("BBB_QB_STAR"), EPlayerRole::Quarterback, 82.f, 26));
    League.Add(TEXT("AAA"), MakeTradePlayer(TEXT("AAA_QB_81"), EPlayerRole::Quarterback, 81.f, 26));

    TArray<FPSTelemetryTradeEvent> Published;
    Bus->OnTradeMC.AddLambda([&Published](const FPSTelemetryTradeEvent& Event) { Published.Add(Event); });
    TArray<FPSTradeRecord> Completed;
    Market->OnTradeCompleted.AddLambda([&Completed](const FPSTradeRecord& Trade) { Completed.Add(Trade); });

    // A fair trade, announced.
    const FPSTradeEvaluation Fair = Market->ProposeTrade(MakeProposal(AAA, BBB,
        { PlayerAsset(TEXT("AAA_QB_81")), PickAsset(Year, 3, TEXT("AAA")) }, { PlayerAsset(TEXT("BBB_QB_STAR")) }));
    TestEqual(TEXT("A fair trade is made"), Fair.Response, EPSTradeResponse::Accept);
    TestEqual(TEXT("It is announced once"), Completed.Num(), 1);
    if (TestEqual(TEXT("...and on the bus"), Published.Num(), 1))
    {
        TestEqual(TEXT("The bus event's trade"), Published[0].TradeId, Fair.TradeId);
        TestEqual(TEXT("...its teams"), Published[0].FromTeamId, AAA);
        TestEqual(TEXT("...its players"), Published[0].PlayersMoved, 2);
        TestEqual(TEXT("...its picks"), Published[0].PicksMoved, 1);
        TestEqual(TEXT("...the stances"), Published[0].ToStance, FString(TEXT("Balanced")));
    }
    const TArray<FPSTelemetryEvent> History = Bus->GetEventHistory();
    TestTrue(TEXT("The bus keeps it in its history"), History.Num() > 0 && History.Last().EventType == EPSTelemetryEventType::Trade);

    // A player just traded can't move again.
    const FPSTradeEvaluation Again = Market->ProposeTrade(MakeProposal(BBB, CCC, { PlayerAsset(TEXT("AAA_QB_81")) }, { PlayerAsset(TEXT("CCC_Quarterback_0")) }));
    TestTrue(TEXT("A player just traded is cooling down"), HasReason(Again, EPSTradeReason::Cooldown));

    // The trade limit.
    const FPSTradeTuning Shipped = Market->GetTuning();
    FPSTradeTuning OneTrade = Shipped;
    OneTrade.MaxTradesPerTeamPerSeason = 1;
    Market->SetTuning(OneTrade);
    const FPSTradeEvaluation Limited = Market->ProposeTrade(MakeProposal(AAA, CCC, { PickAsset(Year + 1, 7, TEXT("AAA")) }, { PlayerAsset(TEXT("CCC_RunningBack_0")) }));
    TestTrue(TEXT("A team at its trade limit can't trade"), HasReason(Limited, EPSTradeReason::TradeLimit));
    TestEqual(TEXT("AAA's trades this season"), Market->CountTeamTrades(AAA), 1);
    Market->SetTuning(Shipped);

    // No team is left without a player at a position.
    FPlayerAttributes Removed;
    League.RosterOf(TEXT("CCC"))->RemovePlayer(FName(TEXT("CCC_TightEnd_1")), Removed);
    League.RosterOf(TEXT("CCC"))->RemovePlayer(FName(TEXT("CCC_TightEnd_2")), Removed);
    const FPSTradeEvaluation Thin = Market->ProposeTrade(MakeProposal(AAA, CCC, { PickAsset(Year, 2, TEXT("AAA")) }, { PlayerAsset(TEXT("CCC_TightEnd_0")) }));
    TestTrue(TEXT("CCC's last tight end stays"), HasReason(Thin, EPSTradeReason::RosterTooThin));
    TestTrue(TEXT("...said so"), Thin.ReasonTexts.ContainsByPredicate([](const FString& Text) { return Text.Contains(TEXT("CCC")) && Text.Contains(TEXT("TE")); }));

    // The trade's shape.
    const FPSTradeEvaluation TooMany = Market->ProposeTrade(MakeProposal(AAA, CCC,
        { PickAsset(Year + 1, 4, TEXT("AAA")), PickAsset(Year + 1, 5, TEXT("AAA")), PickAsset(Year + 1, 6, TEXT("AAA")), PickAsset(Year + 1, 7, TEXT("AAA")) },
        { PlayerAsset(TEXT("CCC_RunningBack_0")) }));
    TestTrue(TEXT("At most MaxAssetsPerSide a side"), HasReason(TooMany, EPSTradeReason::TooManyAssets));
    const FPSTradeEvaluation Twice = Market->ProposeTrade(MakeProposal(AAA, CCC,
        { PickAsset(Year + 1, 5, TEXT("AAA")), PickAsset(Year + 1, 5, TEXT("AAA")) }, { PlayerAsset(TEXT("CCC_RunningBack_0")) }));
    TestTrue(TEXT("An asset listed twice"), HasReason(Twice, EPSTradeReason::DuplicateAsset));
    const FPSTradeEvaluation Empty = Market->ProposeTrade(MakeProposal(AAA, CCC, {}, { PlayerAsset(TEXT("CCC_RunningBack_0")) }));
    TestTrue(TEXT("Each side gives something"), HasReason(Empty, EPSTradeReason::EmptySide));
    const FPSTradeEvaluation Self = Market->ProposeTrade(MakeProposal(AAA, AAA, { PickAsset(Year + 1, 5, TEXT("AAA")) }, { PlayerAsset(TEXT("AAA_Quarterback_0")) }));
    TestTrue(TEXT("No trading with yourself"), HasReason(Self, EPSTradeReason::SameTeam));
    const FPSTradeEvaluation Stranger = Market->ProposeTrade(MakeProposal(AAA, FName(TEXT("ZZZ")), { PickAsset(Year + 1, 5, TEXT("AAA")) }, { PlayerAsset(TEXT("ZZZ_QB")) }));
    TestTrue(TEXT("Only the market's teams"), HasReason(Stranger, EPSTradeReason::UnknownTeam));

    // The lopsided guardrail, and the gap below which it doesn't count.
    const FPSTradeEvaluation Lopsided = Market->ProposeTrade(MakeProposal(AAA, CCC, { PickAsset(Year, 1, TEXT("AAA")) }, { PlayerAsset(TEXT("CCC_Quarterback_0")) }));
    TestTrue(TEXT("A first for a backup is blocked"), HasReason(Lopsided, EPSTradeReason::Lopsided));
    const FPSTradeEvaluation Small = Market->ProposeTrade(MakeProposal(AAA, CCC, { PickAsset(Year + 1, 7, TEXT("AAA")) }, { PlayerAsset(TEXT("CCC_RunningBack_0")) }));
    TestTrue(TEXT("A small gap isn't lopsided, whatever its ratio"), !HasReason(Small, EPSTradeReason::Lopsided) && Small.Imbalance > Market->GetTuning().MaxValueImbalance);
    TestTrue(TEXT("...it is just not enough"), HasReason(Small, EPSTradeReason::NotEnoughValue));

    // The telemetry counts every answer.
    const FPSTradeTelemetry& Counts = Market->GetTelemetry();
    TestEqual(TEXT("Proposals"), Counts.Proposals, 11);
    TestEqual(TEXT("Accepted"), Counts.Accepted, 1);
    TestEqual(TEXT("Rejected"), Counts.Rejected, 10);
    TestEqual(TEXT("Guardrail blocks"), Counts.GuardrailBlocks, 1);
    TestEqual(TEXT("Trades"), Counts.Trades, 1);
    TestEqual(TEXT("Players moved"), Counts.PlayersMoved, 2);
    TestEqual(TEXT("Picks moved"), Counts.PicksMoved, 1);
    const int32 CooldownIndex = static_cast<int32>(EPSTradeReason::Cooldown);
    TestTrue(TEXT("Rejections by reason"), Counts.RejectionsByReason.IsValidIndex(CooldownIndex) && Counts.RejectionsByReason[CooldownIndex] == 1);
    const TArray<FString> Lines = Market->DescribeTelemetry();
    TestTrue(TEXT("The telemetry reads out"), Lines.Num() >= 4 && Lines[0].Contains(TEXT("Trades: 1")));

    // The market and the traded picks round-trip through the franchise save.
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    Market->SaveTo(Save);
    League.Draft->SaveTo(Save);
    UPSTradeMarket* Restored = NewObject<UPSTradeMarket>();
    UPSDraft* RestoredDraft = NewObject<UPSDraft>();
    RestoredDraft->LoadTuningFromJson(UPSDraft::GetDefaultTuningPath());
    TestTrue(TEXT("The market loads"), Restored->LoadFrom(Save));
    TestEqual(TEXT("...its trades"), Restored->GetHistory().Num(), 1);
    TestEqual(TEXT("...its telemetry"), Restored->GetTelemetry().Proposals, Counts.Proposals);
    TestTrue(TEXT("The draft loads its traded picks without a class"), RestoredDraft->LoadFrom(Save));
    TestEqual(TEXT("...who holds the third-rounder"), RestoredDraft->GetPickOwner(Year, 3, AAA), BBB);
    TestFalse(TEXT("A save from before trades keeps the current market"), Restored->LoadFrom(NewObject<UPSFranchiseSaveGame>()));

    Market->UnbindFromBus();
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- A franchise season with trades
// ---------------------------------------------------------------------------
namespace PSTradeMarketTests
{
    /** Every CPU team looks for a trade every week. */
    void Busy(FPSTradeTuning& Tuning)
    {
        Tuning.BaseTradeChance = 1.f;
        Tuning.DeadlineTradeChance = 1.f;
    }

    /** Each player on exactly one roster, the count unchanged, each contract with his team, every
     *  team under the cap and with a player at each position it had one at. */
    void CheckLeague(FAutomationTestBase& Test, const UPSFranchiseFlow* Flow, const TArray<FName>& Teams, int32 ExpectedPlayers,
        const TMap<FName, TSet<EPlayerRole>>& Positions, const FString& When)
    {
        const UPSContractManager* Contracts = Flow->GetContracts();
        TSet<FName> Seen;
        int32 Count = 0;
        for (const FName& TeamId : Teams)
        {
            const UPSRoster* Roster = Flow->GetTeamRoster(TeamId);
            if (!Roster)
            {
                continue;
            }
            TSet<EPlayerRole> Held;
            for (const FPlayerAttributes& Player : Roster->GetFullRoster())
            {
                ++Count;
                Held.Add(Player.Role);
                if (Seen.Contains(Player.PlayerId))
                {
                    Test.AddError(FString::Printf(TEXT("%s: %s is on two rosters"), *When, *Player.PlayerId.ToString()));
                }
                Seen.Add(Player.PlayerId);
                const FPSContract* Contract = Contracts->FindContract(Player.PlayerId);
                if (Contract && Contract->TeamId != TeamId)
                {
                    Test.AddError(FString::Printf(TEXT("%s: %s plays for %s but is paid by %s"), *When, *Player.PlayerId.ToString(), *TeamId.ToString(), *Contract->TeamId.ToString()));
                }
            }
            if (!Contracts->IsCompliant(TeamId))
            {
                Test.AddError(FString::Printf(TEXT("%s: %s is over the cap"), *When, *TeamId.ToString()));
            }
            if (const TSet<EPlayerRole>* Before = Positions.Find(TeamId))
            {
                for (const EPlayerRole Role : *Before)
                {
                    if (!Held.Contains(Role))
                    {
                        Test.AddError(FString::Printf(TEXT("%s: %s has nobody at %s"), *When, *TeamId.ToString(), *RoleName(Role)));
                    }
                }
            }
        }
        Test.TestEqual(*FString::Printf(TEXT("%s: every player still on a roster"), *When), Count, ExpectedPlayers);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTradeFranchiseTest,
    "PlaySports.Trade.FranchiseSeason",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTradeFranchiseTest::RunTest(const FString& Parameters)
{
    using namespace PSTradeMarketTests;

    const FName Falcons(TEXT("Falcons"));
    const FName Hawks(TEXT("Hawks"));
    const TArray<FName> Teams = { Falcons, Hawks, FName(TEXT("Wolves")), FName(TEXT("Bears")) };
    UPSFranchiseSeason* Season = MakeTradeSeason(Teams, 6);
    UPSStaffManager* Staffs = NewObject<UPSStaffManager>();
    Staffs->LoadFromJson(UPSStaffManager::GetDefaultDataPath());
    UPSFranchiseFlow* Flow = NewObject<UPSFranchiseFlow>();
    Flow->Initialize(Season, Staffs, Falcons);
    if (!TestEqual(TEXT("The league's rosters load"), Flow->LoadLeagueRosters(UPSUITeamCatalog::GetDefaultTeamsPath()), 4))
    {
        return false;
    }
    // Ages from 22 to 34, so the stances see prospects and veterans.
    int32 Players = 0;
    TMap<FName, TSet<EPlayerRole>> Positions;
    for (const FName& TeamId : Teams)
    {
        TArray<FPlayerAttributes>& Roster = Flow->GetTeamRoster(TeamId)->GetMutableFullRoster();
        for (int32 Index = 0; Index < Roster.Num(); ++Index)
        {
            Roster[Index].Age = 22 + (Index * 5) % 13;
            Positions.FindOrAdd(TeamId).Add(Roster[Index].Role);
        }
        Players += Roster.Num();
    }
    UPSContractManager* Contracts = NewObject<UPSContractManager>();
    Contracts->LoadTuningFromJson(UPSContractManager::GetDefaultTuningPath());
    Contracts->StartLeague();
    Flow->SetContracts(Contracts);
    Flow->SignLeagueContracts();
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    Flow->SetStats(Stats);
    UPSLeagueNarrative* Narrative = NewObject<UPSLeagueNarrative>();
    Flow->SetNarrative(Narrative);
    UPSDraft* Draft = NewObject<UPSDraft>();
    Draft->LoadTuningFromJson(UPSDraft::GetDefaultTuningPath());
    Flow->SetDraft(Draft);
    TestTrue(TEXT("The coming class is prepared"), Flow->PrepareDraft(NewObject<UPSLeagueGenerator>(), 88));
    UPSPlayerAging* Aging = NewObject<UPSPlayerAging>();
    Aging->LoadDefaults();
    Flow->SetPlayerAging(Aging);
    UPSTradeMarket* Market = NewObject<UPSTradeMarket>();
    Market->LoadTuningFromJson(UPSTradeMarket::GetDefaultTuningPath());
    FPSTradeTuning Tuning = Market->GetTuning();
    Busy(Tuning);
    Market->SetTuning(Tuning);
    Flow->SetTradeMarket(Market);
    TestEqual(TEXT("The flow hands the market its season's player"), Market->GetUserTeamId(), Falcons);
    TestTrue(TEXT("...and its rosters"), Market->GetTeamAssets(Hawks).Num() > 0);
    CheckLeague(*this, Flow, Teams, Players, Positions, TEXT("Before the season"));

    // Week 1 is played; then the player trades one of his picks for a Hawk, the best deal the
    // Hawks accept (or would, countered), this draft's picks first.
    TestEqual(TEXT("Week 1's games"), Flow->SimulateWeek(true), 2);
    const int32 DraftYear = Market->GetNextDraftYear();
    TestEqual(TEXT("The coming draft is the prepared class's"), DraftYear, Draft->GetState().DraftYear);
    TArray<FPSTradeAsset> Picks = Market->GetTeamAssets(Falcons).FilterByPredicate([](const FPSTradeAsset& Asset) { return Asset.Kind == EPSTradeAssetKind::DraftPick; });
    Picks.StableSort([Market, DraftYear, Falcons](const FPSTradeAsset& A, const FPSTradeAsset& B)
    {
        if ((A.DraftYear == DraftYear) != (B.DraftYear == DraftYear))
        {
            return A.DraftYear == DraftYear;
        }
        return Market->ValuePick(Falcons, A.DraftYear, A.Round, A.OriginalTeamId) > Market->ValuePick(Falcons, B.DraftYear, B.Round, B.OriginalTeamId);
    });
    TArray<FPlayerAttributes> HawksPlayers = Flow->GetTeamRoster(Hawks)->GetFullRoster();
    HawksPlayers.StableSort([Market, Hawks](const FPlayerAttributes& A, const FPlayerAttributes& B)
    {
        return Market->ValuePlayer(Hawks, A.PlayerId) > Market->ValuePlayer(Hawks, B.PlayerId);
    });
    FPSTradeEvaluation UserTrade;
    FPSTradeAsset TradedPick;
    for (const FPSTradeAsset& Pick : Picks)
    {
        for (const FPlayerAttributes& Hawk : HawksPlayers)
        {
            const FPSTradeProposal Offer = MakeProposal(Falcons, Hawks, { Pick }, { FPSTradeAsset::MakePlayer(Hawk.PlayerId) });
            const FPSTradeEvaluation Answer = Market->EvaluateTrade(Offer);
            if (Answer.Response == EPSTradeResponse::Accept)
            {
                UserTrade = Market->ProposeTrade(Offer);
            }
            else if (Answer.Response == EPSTradeResponse::Counter && Answer.bHasCounter)
            {
                UserTrade = Market->ProposeTrade(Answer.Counter);
            }
            if (UserTrade.TradeId != 0)
            {
                TradedPick = Pick;
                break;
            }
        }
        if (UserTrade.TradeId != 0)
        {
            break;
        }
    }
    if (!TestTrue(TEXT("The player's team makes a trade"), UserTrade.TradeId != 0))
    {
        return false;
    }
    TestEqual(TEXT("...its pick is the Hawks'"), Draft->GetPickOwner(TradedPick.DraftYear, TradedPick.Round, TradedPick.OriginalTeamId), Hawks);
    CheckLeague(*this, Flow, Teams, Players, Positions, TEXT("After the player's trade"));

    // The week closes: the CPU trades, the news tells of the player's trade.
    Flow->AdvanceWeek();
    const FName StoryId(*FString::Printf(TEXT("Trade.%d"), UserTrade.TradeId));
    const FPSStoryline* Story = Narrative->GetActiveStorylines().FindByPredicate([&StoryId](const FPSStoryline& Storyline) { return Storyline.StorylineId == StoryId; });
    if (TestNotNull(TEXT("The trade is a storyline"), Story))
    {
        TestEqual(TEXT("...of its kind"), Story->Kind, EPSStorylineKind::Trade);
        TestTrue(TEXT("...between its teams"), Story->Involves(Falcons) && Story->Involves(Hawks));
        TestTrue(TEXT("...told from the string table"), Narrative->DescribeStoryline(*Story).Headline.Contains(TEXT("TRADE")));
    }
    CheckLeague(*this, Flow, Teams, Players, Positions, TEXT("Week 1"));

    // The rest of the season: every week the league stays valid; the deadline (week 4) closes it.
    TestEqual(TEXT("The deadline"), Market->GetDeadlineWeek(), 4);
    for (int32 Week = 2; Week <= 6; ++Week)
    {
        Flow->SimulateWeek(true);
        if (Week == 5)
        {
            TestFalse(TEXT("Past the deadline trades are closed"), Market->IsWindowOpen());
            const FPSTradeEvaluation Late = Market->ProposeTrade(MakeProposal(Falcons, Hawks, { Picks.Last() }, { FPSTradeAsset::MakePlayer(HawksPlayers.Last().PlayerId) }));
            TestTrue(TEXT("...said so"), HasReason(Late, EPSTradeReason::WindowClosed));
        }
        const bool bEnded = Flow->AdvanceWeek();
        TestTrue(*FString::Printf(TEXT("Week %d ends the season only at the end"), Week), bEnded == (Week == 6));
        if (!bEnded)
        {
            CheckLeague(*this, Flow, Teams, Players, Positions, FString::Printf(TEXT("Week %d"), Week));
        }
    }
    TestTrue(TEXT("The season is over"), Flow->HasSeasonEnded());
    TestTrue(TEXT("Trades open for the off-season"), Market->IsOffseason() && Market->IsWindowOpen());
    const FPSTradeTelemetry& Counts = Market->GetTelemetry();
    TestEqual(TEXT("The history holds every trade"), Market->GetHistory().Num(), Counts.Trades);
    TestTrue(TEXT("...the player's among them"), Counts.Trades >= 1);
    for (const FPSTradeRecord& Trade : Market->GetHistory())
    {
        TestTrue(*FString::Printf(TEXT("Trade %d: before the deadline"), Trade.TradeId), Trade.Week <= Market->GetDeadlineWeek());
        TestTrue(*FString::Printf(TEXT("Trade %d: within the guardrail"), Trade.TradeId),
            Trade.Imbalance <= Market->GetTuning().MaxValueImbalance || FMath::Abs(Trade.NeutralFrom - Trade.NeutralTo) < Market->GetTuning().LopsidedMinGap);
    }

    // The draft: each pick goes to the team holding it.
    if (TestTrue(TEXT("The draft opens"), Flow->BeginDraft()))
    {
        const int32 TradedSlot = Draft->GetPickSlot(TradedPick.DraftYear, TradedPick.Round, TradedPick.OriginalTeamId);
        if (TradedPick.DraftYear == Draft->GetState().DraftYear && TestTrue(TEXT("The traded pick is in the order"), TradedSlot > 0))
        {
            // The Hawks hold it, or a CPU team they traded it on to.
            const FName Holder = Draft->GetPickOwner(TradedPick.DraftYear, TradedPick.Round, TradedPick.OriginalTeamId);
            TestTrue(TEXT("...held away from the Falcons"), Holder != Falcons && !Holder.IsNone());
            TestEqual(TEXT("...on its holder's clock"), Draft->GetState().Order[TradedSlot - 1], Holder);
            TestEqual(TEXT("...as the Falcons' pick"), Draft->GetState().OriginalOrder[TradedSlot - 1], Falcons);
        }
        Draft->RunToEnd();
        const int32 ThisDraft = Draft->GetState().DraftYear;
        for (const FPSDraftPick& Made : Draft->GetPicks())
        {
            TestEqual(*FString::Printf(TEXT("Pick %d goes to its holder"), Made.Overall), Made.TeamId, Draft->GetPickOwner(ThisDraft, Made.Round, Made.OriginalTeamId));
        }
    }

    // The market and the picks round-trip through the franchise save.
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    Market->SaveTo(Save);
    Draft->SaveTo(Save);
    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    const FString SaveSlot = TEXT("Test_TradeMarket");
    TestTrue(TEXT("The franchise saves"), Saves->SaveToSlot(Save, SaveSlot));
    const UPSFranchiseSaveGame* Loaded = Cast<UPSFranchiseSaveGame>(Saves->LoadFromSlot(SaveSlot));
    UPSTradeMarket* Restored = NewObject<UPSTradeMarket>();
    if (TestNotNull(TEXT("...and loads"), Loaded) && TestTrue(TEXT("The market loads"), Restored->LoadFrom(Loaded)))
    {
        TestEqual(TEXT("...every trade"), Restored->GetHistory().Num(), Market->GetHistory().Num());
        TestEqual(TEXT("...the telemetry"), Restored->GetTelemetry().Trades, Counts.Trades);
        TestEqual(TEXT("The traded picks"), Loaded->Draft.TradedPicks.Num(), Draft->GetState().TradedPicks.Num());
    }
    IFileManager::Get().Delete(*UPSSaveSubsystem::GetSlotPath(SaveSlot), false, true, true);
    IFileManager::Get().Delete(*(UPSSaveSubsystem::GetSlotPath(SaveSlot) + TEXT(".bak")), false, true, true);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
