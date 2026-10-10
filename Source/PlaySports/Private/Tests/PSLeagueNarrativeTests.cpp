// PSLeagueNarrativeTests.cpp -- Epic 93 (league narrative and storyline generator)
//
// A four-team league plays six weeks through UPSFranchiseFlow: each game's plays go into the
// statistics engine (Epic 92) and its result into the season, the stronger team always winning.
// The narrative is driven by the flow, as a franchise drives it.
//
// Tests covered:
//   1. Data/league_narrative.json loads and validates; bad tunings are refused; every Narrative.*
//      string the news uses is in the string table.
//   2. Storylines: win and losing streaks, revenge games, records the stats engine broke; in a
//      league's second season, a rookie among the best and a close MVP race.
//   3. Awards: the week's honors from its box scores; the season's by a repeatable vote (a clear
//      favourite takes every first-place vote, a close race splits them), no rookie award in a
//      league's first season; the award record Epic 94 reads.
//   4. The weekly news digest: template text from the string tables, the heaviest storyline
//      first, honors after; with Epic 82's bridge online a model is asked to write it up and its
//      answer kept; with none, nothing is asked.
//   5. The broadcast: a game's storylines become chyrons through UPSOverlayBroadcastSubsystem,
//      only the two teams' and at most MaxBroadcastStorylines.
//   6. The franchise: the flow closes every week and votes the awards at the season's end, before
//      the stats engine archives it; the narrative round-trips through the franchise save.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"
#include "PSDataIngestion.h"
#include "PSFranchiseFlow.h"
#include "PSFranchiseSaveGame.h"
#include "PSFranchiseSeason.h"
#include "PSGameIntelligenceSubsystem.h"
#include "PSLeagueNarrative.h"
#include "PSLocalization.h"
#include "PSOverlayBroadcastSubsystem.h"
#include "PSScheduleEngine.h"
#include "PSStatsEngine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSLeagueNarrativeTests
{
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

    /** What AgenticLink registers while its MCP server serves (Epic 82's gate). */
    struct FPSFakeNewsBridge : public IModularFeature
    {
    };

    struct FScopedNewsBridge
    {
        FPSFakeNewsBridge Feature;

        FScopedNewsBridge()
        {
            IModularFeatures::Get().RegisterModularFeature(UPSGameIntelligenceSubsystem::GetBridgeFeatureName(), &Feature);
        }

        ~FScopedNewsBridge()
        {
            IModularFeatures::Get().UnregisterModularFeature(UPSGameIntelligenceSubsystem::GetBridgeFeatureName(), &Feature);
        }
    };

    UPSFranchiseSeason* MakeSeason(const TArray<FName>& Teams, int32 Weeks)
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

    FName PlayerOf(FName TeamId, const TCHAR* Position)
    {
        return FName(*FString::Printf(TEXT("%s_%s"), *TeamId.ToString(), Position));
    }

    void RecordPlay(UPSStatsEngine* Stats, bool bHomeOffense, int32 Yards, bool bPass, FName Carrier, FName Receiver, FName Tackler, bool bSack = false)
    {
        FPSTelemetryPlayResultEvent Event;
        Event.bHomeOffense = bHomeOffense;
        Event.Result = TEXT("Tackle");
        Event.YardsGained = Yards;
        Event.bPass = bPass;
        Event.bComplete = bPass && !bSack;
        Event.bSack = bSack;
        Event.PasserId = bPass ? Carrier : NAME_None;
        Event.ReceiverId = bPass && !bSack ? Receiver : NAME_None;
        Event.RusherId = bPass ? NAME_None : Carrier;
        Event.TacklerId = Tackler;
        Stats->RecordPlay(Event);
    }

    /** Pass yards a team's quarterback throws in Week: AAA's grow every week (so its records fall). */
    int32 PassYards(FName TeamId, int32 Week)
    {
        const FString Team = TeamId.ToString();
        return Team == TEXT("AAA") ? 150 + 30 * Week : Team == TEXT("BBB") ? 120 + 10 * Week : Team == TEXT("CCC") ? 100 : 80;
    }

    /** A league of four (AAA strongest, DDD weakest) over six weeks, every pair twice, run by a
     *  franchise flow with a statistics engine and a narrative. */
    struct FLeague
    {
        UPSFranchiseSeason* Season = nullptr;
        UPSStatsEngine* Stats = nullptr;
        UPSLeagueNarrative* Narrative = nullptr;
        UPSFranchiseFlow* Flow = nullptr;

        void Start()
        {
            Season = MakeSeason({ TEXT("AAA"), TEXT("BBB"), TEXT("CCC"), TEXT("DDD") }, 6);
            Stats = NewObject<UPSStatsEngine>();
            Narrative = NewObject<UPSLeagueNarrative>();
            Flow = NewObject<UPSFranchiseFlow>();
            Flow->Initialize(Season, nullptr, NAME_None);
            Flow->SetStats(Stats);
            Flow->SetNarrative(Narrative);
        }

        /** The week's games: the stronger team wins 21-14; each offense throws and runs once, the
         *  winner's linebacker sacks the loser once. Then the flow closes the week. */
        void PlayWeek()
        {
            const int32 Week = Season->GetCurrentWeek();
            for (const FPSWeekMatchup& Matchup : Season->GetMatchupsForWeek(Week))
            {
                const bool bHomeWins = Matchup.HomeTeamId.LexicalLess(Matchup.AwayTeamId);
                const FName Winner = bHomeWins ? Matchup.HomeTeamId : Matchup.AwayTeamId;
                const FName Loser = bHomeWins ? Matchup.AwayTeamId : Matchup.HomeTeamId;
                Stats->BeginGame(Week, Matchup.HomeTeamId, Matchup.AwayTeamId);
                for (const FName Offense : { Matchup.HomeTeamId, Matchup.AwayTeamId })
                {
                    const bool bHome = Offense == Matchup.HomeTeamId;
                    const FName Defense = bHome ? Matchup.AwayTeamId : Matchup.HomeTeamId;
                    RecordPlay(Stats, bHome, PassYards(Offense, Week), true, PlayerOf(Offense, TEXT("QB")), PlayerOf(Offense, TEXT("WR")), PlayerOf(Defense, TEXT("LB")));
                    RecordPlay(Stats, bHome, 30, false, PlayerOf(Offense, TEXT("RB")), NAME_None, PlayerOf(Defense, TEXT("LB")));
                }
                RecordPlay(Stats, Loser == Matchup.HomeTeamId, -7, true, PlayerOf(Loser, TEXT("QB")), NAME_None, PlayerOf(Winner, TEXT("LB")), true);
                Stats->FinishGame();
                Season->RecordGameResult(Week, Matchup.HomeTeamId, Matchup.AwayTeamId, bHomeWins ? 21 : 14, bHomeWins ? 14 : 21);
            }
            Flow->AdvanceWeek();
        }

        void PlayWeeks(int32 Count)
        {
            for (int32 Index = 0; Index < Count; ++Index)
            {
                PlayWeek();
            }
        }
    };

    const FPSStoryline* Find(const TArray<FPSStoryline>& Storylines, EPSStorylineKind Kind, FName TeamId = NAME_None)
    {
        return Storylines.FindByPredicate([Kind, TeamId](const FPSStoryline& Storyline)
        {
            return Storyline.Kind == Kind && (TeamId.IsNone() || Storyline.TeamId == TeamId);
        });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The tuning and the string table
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSNarrativeTuningTest,
    "PlaySports.Narrative.Tuning",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSNarrativeTuningTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSNarrativeTuning Loaded;
    if (!TestTrue(TEXT("Data/league_narrative.json loads"), Ingestion->LoadNarrativeTuningFromJson(UPSLeagueNarrative::GetDefaultTuningPath(), Loaded)))
    {
        return false;
    }
    for (const FString& Problem : UPSLeagueNarrative::ValidateTuning(Loaded))
    {
        AddError(Problem);
    }
    const FPSNarrativeTuning Defaults;
    TestEqual(TEXT("The file's storyline kinds are the struct's (not added to them)"), Loaded.StorylineKinds.Num(), Defaults.StorylineKinds.Num());
    TestEqual(TEXT("...its offense scoring too"), Loaded.OffenseScoring.Num(), Defaults.OffenseScoring.Num());
    TestTrue(TEXT("...and its ballot"), Loaded.BallotPoints == Defaults.BallotPoints);
    TestEqual(TEXT("...and its streak"), Loaded.StreakMin, Defaults.StreakMin);
    TestEqual(TEXT("...and its digest task"), Loaded.DigestTask, Defaults.DigestTask);

    FPSNarrativeTuning Missing = Loaded;
    Missing.StorylineKinds.RemoveAt(0);
    TestEqual(TEXT("A storyline kind without a weight is refused"), UPSLeagueNarrative::ValidateTuning(Missing).Num(), 1);
    FPSNarrativeTuning Rising = Loaded;
    Rising.BallotPoints = { 1, 5 };
    TestEqual(TEXT("A ballot worth more for a lower place is refused"), UPSLeagueNarrative::ValidateTuning(Rising).Num(), 1);
    UPSLeagueNarrative* Narrative = NewObject<UPSLeagueNarrative>();
    TestFalse(TEXT("The narrative refuses a bad tuning"), Narrative->SetTuning(Rising));
    TestTrue(TEXT("...keeping the shipped one"), Narrative->GetTuning().BallotPoints == Loaded.BallotPoints);

    // Every string the news can name is in the table.
    for (const TCHAR* Kind : { TEXT("WinStreak"), TEXT("LosingStreak"), TEXT("RookieSurge"), TEXT("RevengeGame"), TEXT("RecordBroken"), TEXT("AwardRace") })
    {
        TestTrue(*FString::Printf(TEXT("%s has a headline and a body"), Kind),
            UPSLocalization::HasText(FString::Printf(TEXT("Narrative.%s.Headline"), Kind)) && UPSLocalization::HasText(FString::Printf(TEXT("Narrative.%s.Body"), Kind)));
    }
    const UEnum* Categories = StaticEnum<EPSStatCategory>();
    for (int32 Index = 0; Index < Categories->NumEnums() - 1; ++Index)
    {
        const FString Key = FString::Printf(TEXT("Narrative.Stat.%s"), *Categories->GetNameStringByIndex(Index));
        TestTrue(*FString::Printf(TEXT("%s is in the string table"), *Key), UPSLocalization::HasText(Key));
    }
    const UEnum* Scopes = StaticEnum<EPSStatScope>();
    for (int32 Index = 0; Index < Scopes->NumEnums() - 1; ++Index)
    {
        const FString Key = FString::Printf(TEXT("Narrative.Scope.%s"), *Scopes->GetNameStringByIndex(Index));
        TestTrue(*FString::Printf(TEXT("%s is in the string table"), *Key), UPSLocalization::HasText(Key));
    }
    const UEnum* Awards = StaticEnum<EPSAwardKind>();
    for (int32 Index = 0; Index < Awards->NumEnums() - 1; ++Index)
    {
        const FString Key = FString::Printf(TEXT("Narrative.Award.%s"), *Awards->GetNameStringByIndex(Index));
        TestTrue(*FString::Printf(TEXT("%s is in the string table"), *Key), UPSLocalization::HasText(Key));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Storylines
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSNarrativeStorylinesTest,
    "PlaySports.Narrative.Storylines",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSNarrativeStorylinesTest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueNarrativeTests;

    FLeague League;
    League.Start();

    // Week 2: AAA's quarterback beats his own single-game mark.
    League.PlayWeeks(2);
    const TArray<FPSStoryline> WeekTwo = League.Narrative->GetActiveStorylines();
    TestNull(TEXT("Two wins aren't a streak yet"), Find(WeekTwo, EPSStorylineKind::WinStreak));
    const FPSStoryline* Record = WeekTwo.FindByPredicate([](const FPSStoryline& Storyline)
    {
        return Storyline.Kind == EPSStorylineKind::RecordBroken && Storyline.Category == EPSStatCategory::PassingYards && Storyline.Scope == EPSStatScope::Game;
    });
    if (TestNotNull(TEXT("The single-game passing record falls in week 2"), Record))
    {
        TestEqual(TEXT("...to AAA's quarterback"), Record->PlayerId, FName(TEXT("AAA_QB")));
        TestEqual(TEXT("...with his 210 yards"), Record->Value, 210);
        TestEqual(TEXT("...told after week 2"), Record->Week, 2);
    }

    // Week 3: three in a row either way; the schedule turns over, so every pair meets again.
    League.PlayWeek();
    const TArray<FPSStoryline>& WeekThree = League.Narrative->GetActiveStorylines();
    const FPSStoryline* Hot = Find(WeekThree, EPSStorylineKind::WinStreak, TEXT("AAA"));
    const FPSStoryline* Cold = Find(WeekThree, EPSStorylineKind::LosingStreak, TEXT("DDD"));
    TestTrue(TEXT("AAA have won three straight"), Hot && Hot->Value == 3);
    TestTrue(TEXT("DDD have lost three straight"), Cold && Cold->Value == 3);
    TestNull(TEXT("BBB (two wins, one loss) has no streak"), Find(WeekThree, EPSStorylineKind::WinStreak, TEXT("BBB")));
    TestTrue(TEXT("Last week's records are not told again"), !WeekThree.ContainsByPredicate([](const FPSStoryline& Storyline) { return Storyline.Week != 3; }));
    int32 Rematches = 0;
    for (const FPSWeekMatchup& Next : League.Season->GetMatchupsForWeek(4))
    {
        const FName Beaten = Next.HomeTeamId.LexicalLess(Next.AwayTeamId) ? Next.AwayTeamId : Next.HomeTeamId;
        const FName Beat = Beaten == Next.HomeTeamId ? Next.AwayTeamId : Next.HomeTeamId;
        const FPSStoryline* Revenge = Find(WeekThree, EPSStorylineKind::RevengeGame, Beaten);
        Rematches += Revenge && Revenge->OtherTeamId == Beat && Revenge->Value == 7 ? 1 : 0;
    }
    TestEqual(TEXT("Each of week 4's rematches is the loser's revenge game, by the 7 it lost by"), Rematches, League.Season->GetMatchupsForWeek(4).Num());
    TestTrue(TEXT("The storylines are told heaviest first"), WeekThree.Num() >= 2 && WeekThree[0].Weight >= WeekThree.Last().Weight);
    TestTrue(TEXT("Each carries its kind's weight"), Hot && Hot->Weight == 3.f);

    // A league's second season: a rookie among the best, and a close MVP race.
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    Stats->BeginGame(1, TEXT("FFF"), TEXT("EEE"));
    RecordPlay(Stats, true, 40, false, TEXT("FFF_RB"), NAME_None, TEXT("EEE_LB"));
    Stats->FinishGame();
    Stats->EndSeason();
    UPSFranchiseSeason* Second = MakeSeason({ TEXT("EEE"), TEXT("FFF") }, 2);
    UPSLeagueNarrative* Narrative = NewObject<UPSLeagueNarrative>();
    FPSNarrativeTuning Early = Narrative->GetTuning();
    Early.AwardRaceFromWeek = 1;
    Early.MvpWinWeight = 0.f;
    Narrative->SetTuning(Early);
    Narrative->SetStats(Stats);
    // The two-team schedule has FFF at home in week 1.
    Stats->BeginGame(1, TEXT("FFF"), TEXT("EEE"));
    RecordPlay(Stats, false, 100, false, TEXT("EEE_RB"), NAME_None, TEXT("FFF_LB"));
    RecordPlay(Stats, true, 95, false, TEXT("FFF_RB"), NAME_None, TEXT("EEE_LB"));
    Stats->FinishGame();
    TestTrue(TEXT("Week 1's result is recorded"), Second->RecordGameResult(1, TEXT("FFF"), TEXT("EEE"), 7, 10));
    Narrative->CloseWeek(Second, 1);
    const TArray<FPSStoryline>& Seasoned = Narrative->GetActiveStorylines();
    TestTrue(TEXT("EEE's back is a rookie"), Narrative->IsRookie(TEXT("EEE_RB")));
    TestFalse(TEXT("FFF's back played last season"), Narrative->IsRookie(TEXT("FFF_RB")));
    const FPSStoryline* Surge = Find(Seasoned, EPSStorylineKind::RookieSurge);
    TestTrue(TEXT("The rookie leads the league in rushing: a surge"), Surge && Surge->PlayerId == TEXT("EEE_RB") && Surge->Category == EPSStatCategory::RushingYards && Surge->Rank == 1 && Surge->Value == 100);
    TestFalse(TEXT("...and the veteran isn't one"), Seasoned.ContainsByPredicate([](const FPSStoryline& Storyline) { return Storyline.Kind == EPSStorylineKind::RookieSurge && Storyline.PlayerId == TEXT("FFF_RB"); }));
    const FPSStoryline* Race = Find(Seasoned, EPSStorylineKind::AwardRace);
    TestTrue(TEXT("10 points to 9.5 is an MVP race"), Race && Race->PlayerId == TEXT("EEE_RB") && Race->OtherPlayerId == TEXT("FFF_RB"));
    UPSLeagueNarrative* FirstYear = NewObject<UPSLeagueNarrative>();
    FirstYear->SetStats(League.Stats);
    TestFalse(TEXT("Nobody is a rookie in a league's first season"), FirstYear->IsRookie(TEXT("AAA_QB")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Awards
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSNarrativeAwardsTest,
    "PlaySports.Narrative.Awards",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSNarrativeAwardsTest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueNarrativeTests;

    // The vote on its own.
    TArray<FPSAwardRecord> Field;
    for (const TPair<const TCHAR*, float> Entry : { TPair<const TCHAR*, float>(TEXT("STAR"), 100.f), TPair<const TCHAR*, float>(TEXT("GOOD"), 50.f), TPair<const TCHAR*, float>(TEXT("FAIR"), 20.f) })
    {
        FPSAwardRecord& Candidate = Field.AddDefaulted_GetRef();
        Candidate.PlayerId = Entry.Key;
        Candidate.Score = Entry.Value;
    }
    const TArray<int32> Ballot = { 10, 5, 3, 1 };
    FRandomStream Stream(7);
    const TArray<FPSAwardRecord> Clear = UPSLeagueNarrative::RunVote(Field, 50, Ballot, 0.15f, Stream);
    TestTrue(TEXT("A clear favourite takes every first-place vote"), Clear[0].PlayerId == TEXT("STAR") && Clear[0].FirstPlaceVotes == 50 && Clear[0].VotePoints == 500);
    TestTrue(TEXT("...and the rest are ranked behind"), Clear[1].PlayerId == TEXT("GOOD") && Clear[1].VotePoints == 250 && Clear[2].VotePoints == 150);
    Field[1].Score = 99.f;
    FRandomStream Close(7);
    const TArray<FPSAwardRecord> Split = UPSLeagueNarrative::RunVote(Field, 50, Ballot, 0.15f, Close);
    TestTrue(TEXT("A close race splits the first-place votes"), Split[0].FirstPlaceVotes > 0 && Split[1].FirstPlaceVotes > 0 && Split[0].FirstPlaceVotes + Split[1].FirstPlaceVotes == 50);
    FRandomStream Again(7);
    const TArray<FPSAwardRecord> Repeated = UPSLeagueNarrative::RunVote(Field, 50, Ballot, 0.15f, Again);
    TestTrue(TEXT("The same seed gives the same vote"), Repeated[0].PlayerId == Split[0].PlayerId && Repeated[0].VotePoints == Split[0].VotePoints);
    FRandomStream Exact(7);
    TestEqual(TEXT("With no noise the higher score takes every vote"), UPSLeagueNarrative::RunVote(Field, 50, Ballot, 0.f, Exact)[0].FirstPlaceVotes, 50);

    // A season's honors.
    FLeague League;
    League.Start();
    TArray<FPSAwardRecord> Heard;
    League.Narrative->OnAwardGiven.AddLambda([&Heard](const FPSAwardRecord& Award) { Heard.Add(Award); });
    League.PlayWeek();
    const TArray<FPSAwardRecord> WeekOne = League.Narrative->GetAwards();
    const FPSAwardRecord* Offense = WeekOne.FindByPredicate([](const FPSAwardRecord& Award) { return Award.Award == EPSAwardKind::OffensivePlayerOfWeek; });
    const FPSAwardRecord* Defense = WeekOne.FindByPredicate([](const FPSAwardRecord& Award) { return Award.Award == EPSAwardKind::DefensivePlayerOfWeek; });
    // AAA's receiver: 180 yards (18) and a catch (0.5) is the week's best offensive game.
    TestTrue(TEXT("The week's offensive player is its best offensive game"), Offense && Offense->PlayerId == TEXT("AAA_WR") && Offense->Week == 1 && FMath::IsNearlyEqual(Offense->Score, 18.5f));
    // Each winner's linebacker: two tackles and a sack that is also a tackle (7); AAA's wins the tie by id.
    TestTrue(TEXT("...and its defensive player the best defensive game"), Defense && Defense->PlayerId == TEXT("AAA_LB") && Defense->Score == 7.f && Defense->Week == 1);
    TestEqual(TEXT("Each award is announced"), Heard.Num(), 2);

    League.PlayWeeks(5);
    TestTrue(TEXT("The season is over"), League.Flow->HasSeasonEnded());
    const FPSAwardRecord* Mvp = League.Narrative->GetAwards().FindByPredicate([](const FPSAwardRecord& Award) { return Award.Award == EPSAwardKind::MostValuablePlayer; });
    if (TestNotNull(TEXT("An MVP is voted"), Mvp))
    {
        TestEqual(TEXT("...AAA's receiver, far ahead"), Mvp->PlayerId, FName(TEXT("AAA_WR")));
        TestTrue(TEXT("...with every first-place vote"), Mvp->FirstPlaceVotes == League.Narrative->GetTuning().VoterCount && Mvp->Week == 0 && Mvp->Season == 1);
    }
    TestTrue(TEXT("An offensive player of the year is voted"), League.Narrative->GetAwards().ContainsByPredicate([](const FPSAwardRecord& Award) { return Award.Award == EPSAwardKind::OffensivePlayerOfYear; }));
    TestTrue(TEXT("...and a defensive one"), League.Narrative->GetAwards().ContainsByPredicate([](const FPSAwardRecord& Award) { return Award.Award == EPSAwardKind::DefensivePlayerOfYear; }));
    TestFalse(TEXT("No rookie of the year in a league's first season"), League.Narrative->GetAwards().ContainsByPredicate([](const FPSAwardRecord& Award) { return Award.Award == EPSAwardKind::RookieOfYear; }));
    TestEqual(TEXT("Twelve week honors and three season awards"), League.Narrative->GetAwards().Num(), 15);
    TestEqual(TEXT("A season's awards are given once"), League.Narrative->AwardSeason(League.Season).Num(), 0);

    // What Epic 94's hall of fame reads.
    TestEqual(TEXT("AAA's receiver was offensive player of the week every week"), League.Narrative->CountAwards(TEXT("AAA_WR"), EPSAwardKind::OffensivePlayerOfWeek), 6);
    TestEqual(TEXT("...and won the MVP once"), League.Narrative->CountAwards(TEXT("AAA_WR"), EPSAwardKind::MostValuablePlayer), 1);
    TestTrue(TEXT("His awards, in order"), League.Narrative->GetAwardsForPlayer(TEXT("AAA_WR")).Num() >= 7
        && League.Narrative->GetAwardsForPlayer(TEXT("AAA_WR"))[0].Week == 1);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The weekly news digest
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSNarrativeDigestTest,
    "PlaySports.Narrative.NewsDigest",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSNarrativeDigestTest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueNarrativeTests;

    UWorld* World = CreateTestWorld();
    UPSGameIntelligenceSubsystem* Intelligence = World ? World->GetSubsystem<UPSGameIntelligenceSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Game intelligence"), Intelligence))
    {
        DestroyTestWorld(World);
        return false;
    }
    FLeague League;
    League.Start();
    League.Narrative->SetIntelligence(Intelligence);

    // With no bridge the templates stand alone.
    const bool bBridgeAlready = UPSGameIntelligenceSubsystem::IsBridgeOnline();
    League.PlayWeeks(3);
    const FPSNewsDigest WeekThree = League.Narrative->GetDigests().Last();
    TestEqual(TEXT("A digest per week"), League.Narrative->GetDigests().Num(), 3);
    TestEqual(TEXT("...this one week 3's"), WeekThree.Week, 3);
    if (!bBridgeAlready)
    {
        TestEqual(TEXT("With no bridge no model is asked"), WeekThree.ModelRequestId, 0);
    }
    if (TestTrue(TEXT("The digest has news"), WeekThree.Items.Num() >= 3))
    {
        const FPSNewsItem& Lead = WeekThree.Items[0];
        TestTrue(TEXT("...led by the heaviest storyline"), Lead.Weight >= WeekThree.Items[1].Weight && !Lead.StorylineId.IsNone());
        TestFalse(TEXT("...in words from the string table"), Lead.Headline.IsEmpty() || Lead.Headline.Contains(TEXT("<Narrative")) || Lead.Body.Contains(TEXT("<Narrative")));
        TestTrue(TEXT("...with the honors after the storylines"), WeekThree.Items.Last().StorylineId.IsNone());
    }
    TestTrue(TEXT("At most MaxDigestItems"), WeekThree.Items.Num() <= League.Narrative->GetTuning().MaxDigestItems);
    const FPSStoryline* Hot = Find(League.Narrative->GetActiveStorylines(), EPSStorylineKind::WinStreak, TEXT("AAA"));
    if (TestNotNull(TEXT("AAA's streak"), Hot))
    {
        FFormatNamedArguments Arguments;
        Arguments.Add(TEXT("Team"), UPSLocalization::Verbatim(TEXT("AAA")));
        Arguments.Add(TEXT("Count"), UPSLocalization::FormatNumber(3.f, 0));
        TestEqual(TEXT("A streak's headline is the template's"), League.Narrative->DescribeStoryline(*Hot).Headline,
            UPSLocalization::Format(TEXT("Narrative.WinStreak.Headline"), Arguments).ToString());
    }

    // With the bridge online a model is asked to write the week up, as a narration task.
    {
        FScopedNewsBridge Bridge;
        League.PlayWeek();
        const FPSNewsDigest WeekFour = League.Narrative->GetDigests().Last();
        FPSIntelRequest Request;
        if (TestTrue(TEXT("Week 4's digest is offered to a model"), WeekFour.ModelRequestId != 0 && Intelligence->FindRequest(WeekFour.ModelRequestId, Request)))
        {
            TestTrue(TEXT("...as a news digest"), Request.Kind == EPSIntelRequestKind::NewsDigest);
            TestEqual(TEXT("...routed as narration"), Request.Task, League.Narrative->GetTuning().DigestTask);
            TestTrue(TEXT("...with its items as facts"), Request.Context.Contains(TEXT("play-sports.league-news/1")) && Request.Context.Contains(WeekFour.Items[0].Headline));
            TestTrue(TEXT("...within its budget"), Request.Context.Len() <= League.Narrative->GetTuning().DigestContextChars);
            FString Reason;
            TestTrue(TEXT("The model's column is taken"), Intelligence->AnswerRequest(WeekFour.ModelRequestId, TEXT("AAA keep rolling."), Reason));
            TestEqual(TEXT("...and kept with the digest"), League.Narrative->GetDigests().Last().ModelText, FString(TEXT("AAA keep rolling.")));
        }
    }

    // The season's end: an awards digest.
    League.PlayWeeks(2);
    const FPSNewsDigest Final = League.Narrative->GetDigests().Last();
    TestTrue(TEXT("The season ends with an awards digest"), Final.bSeasonEnd && Final.Week == 7 && Final.Items.Num() == 3);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The broadcast
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSNarrativeBroadcastTest,
    "PlaySports.Narrative.Broadcast",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSNarrativeBroadcastTest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueNarrativeTests;

    UWorld* World = CreateTestWorld();
    UPSOverlayBroadcastSubsystem* Broadcast = World ? World->GetSubsystem<UPSOverlayBroadcastSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Broadcast overlay"), Broadcast))
    {
        DestroyTestWorld(World);
        return false;
    }
    FLeague League;
    League.Start();
    League.PlayWeeks(3);
    Broadcast->SetOverlayDetail(EPSOverlayDetail::Full);

    // Week 4: AAA (on a streak) against the team it beat, which wants revenge.
    const TArray<FPSNewsItem> Points = League.Narrative->GetTalkingPoints(TEXT("AAA"), TEXT("DDD"), 10);
    TestTrue(TEXT("The game's talking points are about its teams"), Points.Num() >= 2);
    const TArray<FPSNewsItem> Elsewhere = League.Narrative->GetTalkingPoints(TEXT("ZZZ"), TEXT("YYY"), 10);
    TestEqual(TEXT("A game between two other teams has none"), Elsewhere.Num(), 0);

    const int32 Fed = League.Narrative->FeedBroadcast(Broadcast, TEXT("AAA"), TEXT("DDD"));
    TestEqual(TEXT("The broadcast takes MaxBroadcastStorylines of them"), Fed, League.Narrative->GetTuning().MaxBroadcastStorylines);
    FPSChyron Showing;
    TestTrue(TEXT("The first is on screen as a chyron"), Broadcast->GetCurrentChyron(Showing) && Showing.Headline == Points[0].Headline && Showing.Kind == EPSChyronKind::Custom);
    TestEqual(TEXT("...the second waits its turn"), Broadcast->GetQueuedChyronCount(), Fed - 1);
    TestEqual(TEXT("Without a broadcast nothing is fed"), League.Narrative->FeedBroadcast(nullptr, TEXT("AAA"), TEXT("DDD")), 0);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- The franchise and its save
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSNarrativeFranchiseTest,
    "PlaySports.Narrative.FranchiseSave",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSNarrativeFranchiseTest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueNarrativeTests;

    FLeague League;
    League.Start();
    TestEqual(TEXT("The flow holds the narrative"), League.Flow->GetNarrative(), League.Narrative);
    League.PlayWeeks(6);
    TestTrue(TEXT("The season ended"), League.Flow->HasSeasonEnded());
    TestEqual(TEXT("Six weekly digests and the awards"), League.Narrative->GetDigests().Num(), 7);
    TestEqual(TEXT("The awards were voted on season 1, before the stats engine moved on"), League.Narrative->GetAwards().Last().Season, 1);
    TestEqual(TEXT("...which it then did"), League.Stats->GetSeason(), 2);

    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    League.Narrative->SaveTo(Save);
    UPSLeagueNarrative* Restored = NewObject<UPSLeagueNarrative>();
    TestFalse(TEXT("An empty save has no narrative"), Restored->LoadFrom(NewObject<UPSFranchiseSaveGame>()));
    TestTrue(TEXT("The save's narrative loads"), Restored->LoadFrom(Save));
    TestEqual(TEXT("...its awards"), Restored->GetAwards().Num(), League.Narrative->GetAwards().Num());
    TestEqual(TEXT("...its digests"), Restored->GetDigests().Num(), 7);
    TestEqual(TEXT("...its storylines"), Restored->GetActiveStorylines().Num(), League.Narrative->GetActiveStorylines().Num());
    TestEqual(TEXT("...and its award counts"), Restored->CountAwards(TEXT("AAA_WR"), EPSAwardKind::MostValuablePlayer), 1);
    Restored->SetStats(League.Stats);
    TestEqual(TEXT("A loaded season's awards aren't given twice"), Restored->AwardSeason(League.Season).Num(), 0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
