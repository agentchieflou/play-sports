// PSSpecialTeamsTests.cpp -- Epic 75 (special teams depth and trick plays): the model and the calls
//
// Tests covered:
//   1. Data/special_teams.json loads through UPSDataIngestion and equals the defaults, field by field.
//   2. Blocks: a unit's ratings, the base chance against a return unit, the block unit's
//      multiplier, edge timing and interior push, the cap, and no block on a fake.
//   3. Returns: the scheme (wall, wedge) from the receiving play's formation, the block unit's and
//      hands team's short returns, lane discipline in coverage, touchbacks and big returns.
//   4. Onside kicks (by surprise and against the hands team) and desperation laterals.
//   5. Punts (return, touchback, block), field goals by distance, fakes, and applying an outcome:
//      the score, who kicks off next, who has the ball and where.
//   6. The coaching AI's calls: punt, field goal or go on 4th down, the fake risk model, the last
//      play of a half, onside kicks, and the receiving team's return, block, hands team or
//      laterals -- picked outright by the play caller when due, never otherwise.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSCoachingAI.h"
#include "PSDataIngestion.h"
#include "PSPlaySimulation.h"
#include "PSSpecialTeamsAI.h"
#include "PSSpecialTeamsModel.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSSpecialTeamsTests
{
    /** The defaults with every random spread removed: returns to the 20, 45-yard punts, no
     *  touchbacks, blocks or big returns unless a test turns them on. */
    static FPSSpecialTeamsTuning FlatTuning()
    {
        FPSSpecialTeamsTuning Tuning;
        Tuning.KickoffTouchbackChance = 0.f;
        Tuning.KickoffReturnMinYardLine = 20;
        Tuning.KickoffReturnMaxYardLine = 20;
        Tuning.PuntGrossYardsMin = 45;
        Tuning.PuntGrossYardsMax = 45;
        Tuning.PuntReturnYardsMin = 0;
        Tuning.PuntReturnYardsMax = 0;
        Tuning.DefaultBigReturnChance = 0.f;
        for (FPSReturnSchemeDef& Scheme : Tuning.ReturnSchemes)
        {
            Scheme.BigReturnChance = 0.f;
        }
        Tuning.PuntBlockChance = 0.f;
        Tuning.FieldGoalBlockChance = 0.f;
        Tuning.BlockedKickTouchdownChance = 0.f;
        return Tuning;
    }

    static UPSSpecialTeamsModel* MakeModel(const FPSSpecialTeamsTuning& Tuning)
    {
        UPSSpecialTeamsModel* Model = NewObject<UPSSpecialTeamsModel>();
        Model->SetTuning(Tuning);
        Model->Seed(75);
        return Model;
    }

    static FPSSpecialTeamsCall MakeCall(EPSSpecialTeamsPlay Kicking, EPSSpecialTeamsPlay Receiving, const TCHAR* ReturnFormation = TEXT(""))
    {
        FPSSpecialTeamsCall Call;
        Call.Kicking = Kicking;
        Call.Receiving = Receiving;
        Call.ReturnFormation = ReturnFormation;
        return Call;
    }

    static FPSSpecialTeamsUnitRatings MakeRatings(float EdgeSpeed, float InteriorStrength, float Awareness)
    {
        FPSSpecialTeamsUnitRatings Ratings;
        Ratings.EdgeSpeed = EdgeSpeed;
        Ratings.InteriorStrength = InteriorStrength;
        Ratings.Awareness = Awareness;
        return Ratings;
    }

    static FPSSituationContext MakeContext(int32 Quarter, float Clock, int32 ScoreDifferential, int32 Down, int32 Distance, int32 YardLine)
    {
        FPSSituationContext Context;
        Context.Quarter = Quarter;
        Context.GameClockSeconds = Clock;
        Context.ScoreDifferential = ScoreDifferential;
        Context.Down = Down;
        Context.Distance = Distance;
        Context.YardLine = YardLine;
        return Context;
    }

    static FPSPlayDefinition MakePlay(const TCHAR* PlayId, const TCHAR* Category, bool bOffense, const TCHAR* Formation = TEXT(""))
    {
        FPSPlayDefinition Play;
        Play.PlayId = FName(PlayId);
        Play.DisplayName = PlayId;
        Play.PlayCategory = Category;
        Play.Formation = Formation;
        Play.bIsOffensivePlay = bOffense;
        return Play;
    }

    static FPlayerAttributes MakePlayer(EPlayerRole Role, float Speed, float Strength, float Awareness)
    {
        FPlayerAttributes Player;
        Player.Role = Role;
        Player.Speed = Speed;
        Player.Strength = Strength;
        Player.Awareness = Awareness;
        return Player;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The tuning file equals the defaults
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSpecialTeamsTuningFileTest,
    "PlaySports.SpecialTeams.TuningFileMatchesDefaults",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSpecialTeamsTuningFileTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSSpecialTeamsTuning FromFile;
    FromFile.FieldGoalRanges.Reset();
    FromFile.ReturnSchemes.Reset();
    if (!TestTrue(TEXT("special_teams.json loads"), Ingestion->LoadSpecialTeamsTuningFromJson(UPSSpecialTeamsModel::GetDefaultTuningPath(), FromFile)))
    {
        return false;
    }

    const FPSSpecialTeamsTuning Defaults;
    int32 Compared = 0;
    for (TFieldIterator<FProperty> It(FPSSpecialTeamsTuning::StaticStruct()); It; ++It)
    {
        TestTrue(*FString::Printf(TEXT("%s matches the default"), *It->GetName()), It->Identical_InContainer(&FromFile, &Defaults));
        ++Compared;
    }
    TestTrue(TEXT("Every field was compared"), Compared > 50);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Block mechanics
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSpecialTeamsBlockTest,
    "PlaySports.SpecialTeams.BlockMechanics",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSpecialTeamsBlockTest::RunTest(const FString& Parameters)
{
    using namespace PSSpecialTeamsTests;

    // A unit's ratings: its fastest player, its linemen's strength, everyone's awareness.
    const TArray<FPlayerAttributes> Unit = {
        MakePlayer(EPlayerRole::OffensiveLineman, 40.f, 80.f, 50.f),
        MakePlayer(EPlayerRole::OffensiveLineman, 45.f, 60.f, 70.f),
        MakePlayer(EPlayerRole::WideReceiver, 95.f, 30.f, 60.f) };
    const FPSSpecialTeamsUnitRatings Rated = UPSSpecialTeamsModel::RateUnit(Unit);
    TestEqual(TEXT("Edge speed is the fastest player's"), Rated.EdgeSpeed, 95.f);
    TestEqual(TEXT("Interior strength is the linemen's average"), Rated.InteriorStrength, 70.f);
    TestEqual(TEXT("Awareness is everyone's average"), Rated.Awareness, 60.f);
    TestEqual(TEXT("An empty unit rates average"), UPSSpecialTeamsModel::RateUnit(TArray<FPlayerAttributes>()).EdgeSpeed, 50.f);

    UPSSpecialTeamsModel* Model = MakeModel(FPSSpecialTeamsTuning());
    const FPSSpecialTeamsTuning& Tuning = Model->GetTuning();
    const FPSSpecialTeamsUnitRatings Even = MakeRatings(50.f, 50.f, 50.f);
    const float VsReturn = Model->GetBlockChance(EPSSpecialTeamsPlay::Punt, EPSSpecialTeamsPlay::KickReturn, Even, Even);
    const float VsBlock = Model->GetBlockChance(EPSSpecialTeamsPlay::Punt, EPSSpecialTeamsPlay::KickBlock, Even, Even);
    TestEqual(TEXT("Even units: the base chance against a return unit"), VsReturn, Tuning.PuntBlockChance);
    TestEqual(TEXT("...multiplied when the defense calls the block"), VsBlock, Tuning.PuntBlockChance * Tuning.BlockUnitMultiplier);
    TestTrue(TEXT("Edge timing: faster rushers block more kicks"),
        Model->GetBlockChance(EPSSpecialTeamsPlay::Punt, EPSSpecialTeamsPlay::KickBlock, Even, MakeRatings(90.f, 50.f, 50.f)) > VsBlock);
    TestTrue(TEXT("Interior push: stronger rushers block more kicks"),
        Model->GetBlockChance(EPSSpecialTeamsPlay::FieldGoal, EPSSpecialTeamsPlay::KickBlock, Even, MakeRatings(50.f, 90.f, 50.f))
        > Model->GetBlockChance(EPSSpecialTeamsPlay::FieldGoal, EPSSpecialTeamsPlay::KickBlock, Even, Even));
    TestTrue(TEXT("Strong protection blocks fewer"),
        Model->GetBlockChance(EPSSpecialTeamsPlay::Punt, EPSSpecialTeamsPlay::KickBlock, MakeRatings(90.f, 90.f, 50.f), Even) < VsBlock);
    TestTrue(TEXT("The chance is capped"),
        Model->GetBlockChance(EPSSpecialTeamsPlay::Punt, EPSSpecialTeamsPlay::KickBlock, MakeRatings(0.f, 0.f, 50.f), MakeRatings(100.f, 100.f, 50.f)) <= Tuning.MaxBlockChance);
    TestEqual(TEXT("A fake isn't a kick to block"), Model->GetBlockChance(EPSSpecialTeamsPlay::FakePunt, EPSSpecialTeamsPlay::KickBlock, Even, Even), 0.f);

    // A sure block: the defense falls on it behind the line.
    FPSSpecialTeamsTuning Sure = FlatTuning();
    Sure.PuntBlockChance = 1.f;
    Sure.MaxBlockChance = 1.f;
    UPSSpecialTeamsModel* Blocker = MakeModel(Sure);
    const FPSSpecialTeamsOutcome Blocked = Blocker->ResolvePunt(MakeCall(EPSSpecialTeamsPlay::Punt, EPSSpecialTeamsPlay::KickBlock), 30, Even, Even);
    TestEqual(TEXT("The punt is blocked"), Blocked.Result, EPSSpecialTeamsResult::Blocked);
    TestTrue(TEXT("...the defense has it"), Blocked.bPossessionChanges);
    TestEqual(TEXT("...where it bounced, behind the line"), Blocked.NextYardLine, 100 - (30 - Sure.BlockedPuntRecoilYards));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Return schemes and coverage
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSpecialTeamsReturnTest,
    "PlaySports.SpecialTeams.ReturnSchemesAndCoverage",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSpecialTeamsReturnTest::RunTest(const FString& Parameters)
{
    using namespace PSSpecialTeamsTests;

    const FPSSpecialTeamsTuning Tuning = FlatTuning();
    UPSSpecialTeamsModel* Model = MakeModel(Tuning);
    const FPSSpecialTeamsUnitRatings Even = MakeRatings(50.f, 50.f, 50.f);
    const FPSReturnSchemeDef* Wall = Model->FindReturnScheme(TEXT("Return Wall"));
    const FPSReturnSchemeDef* Wedge = Model->FindReturnScheme(TEXT("Return Wedge"));
    if (!TestNotNull(TEXT("The wall is a scheme"), Wall) || !TestNotNull(TEXT("The wedge is a scheme"), Wedge))
    {
        return false;
    }
    TestNull(TEXT("A formation that isn't a scheme"), Model->FindReturnScheme(TEXT("Base 4-3")));

    auto ReturnTo = [Model, &Even](const FPSSpecialTeamsCall& Call, const FPSSpecialTeamsUnitRatings& Coverage)
    {
        return Model->ResolveKickoff(Call, 35, Coverage, Even).NextYardLine;
    };
    const int32 Plain = ReturnTo(MakeCall(EPSSpecialTeamsPlay::Kickoff, EPSSpecialTeamsPlay::KickReturn), Even);
    TestEqual(TEXT("A plain return to the 20"), Plain, 20);
    TestEqual(TEXT("The wall adds its yards"), ReturnTo(MakeCall(EPSSpecialTeamsPlay::Kickoff, EPSSpecialTeamsPlay::KickReturn, TEXT("Return Wall")), Even),
        20 + FMath::RoundToInt(Wall->ReturnYardsBonus));
    TestEqual(TEXT("...the wedge its own"), ReturnTo(MakeCall(EPSSpecialTeamsPlay::Kickoff, EPSSpecialTeamsPlay::KickReturn, TEXT("Return Wedge")), Even),
        20 + FMath::RoundToInt(Wedge->ReturnYardsBonus));
    TestEqual(TEXT("The hands team returns short"), ReturnTo(MakeCall(EPSSpecialTeamsPlay::Kickoff, EPSSpecialTeamsPlay::HandsTeam), Even),
        20 - FMath::RoundToInt(Tuning.HandsTeamReturnPenaltyYards));
    TestEqual(TEXT("Coverage that holds its lanes takes yards off"), ReturnTo(MakeCall(EPSSpecialTeamsPlay::Kickoff, EPSSpecialTeamsPlay::KickReturn), MakeRatings(50.f, 50.f, 100.f)),
        20 - FMath::RoundToInt(Tuning.LaneDisciplineYards));
    TestEqual(TEXT("...and undisciplined coverage gives them up"), ReturnTo(MakeCall(EPSSpecialTeamsPlay::Kickoff, EPSSpecialTeamsPlay::KickReturn), MakeRatings(50.f, 50.f, 0.f)),
        20 + FMath::RoundToInt(Tuning.LaneDisciplineYards));

    // A punt return: from the 30, a 45-yard punt lands at the receivers' 25.
    const FPSSpecialTeamsOutcome Punted = Model->ResolvePunt(MakeCall(EPSSpecialTeamsPlay::Punt, EPSSpecialTeamsPlay::KickReturn), 30, Even, Even);
    TestEqual(TEXT("A fair punt and no return"), Punted.NextYardLine, 25);
    TestEqual(TEXT("...the punt is the result"), Punted.Result, EPSSpecialTeamsResult::Punted);
    const FPSSpecialTeamsOutcome WallPunt = Model->ResolvePunt(MakeCall(EPSSpecialTeamsPlay::Punt, EPSSpecialTeamsPlay::KickReturn, TEXT("Return Wall")), 30, Even, Even);
    TestEqual(TEXT("The wall returns a punt too"), WallPunt.NextYardLine, 25 + FMath::RoundToInt(Wall->ReturnYardsBonus));
    const FPSSpecialTeamsOutcome BlockUnitPunt = Model->ResolvePunt(MakeCall(EPSSpecialTeamsPlay::Punt, EPSSpecialTeamsPlay::KickBlock, TEXT("Kick Block")), 30, Even, Even);
    TestEqual(TEXT("A block unit that doesn't get there has nobody to return it"), BlockUnitPunt.NextYardLine, 25);

    // Touchbacks, and a big return.
    FPSSpecialTeamsTuning Deep = Tuning;
    Deep.KickoffTouchbackChance = 1.f;
    UPSSpecialTeamsModel* DeepKicker = MakeModel(Deep);
    const FPSSpecialTeamsOutcome Touchback = DeepKicker->ResolveKickoff(MakeCall(EPSSpecialTeamsPlay::Kickoff, EPSSpecialTeamsPlay::KickReturn), 35, Even, Even);
    TestEqual(TEXT("A touchback"), Touchback.Result, EPSSpecialTeamsResult::Touchback);
    TestEqual(TEXT("...to the touchback line"), Touchback.NextYardLine, Deep.TouchbackYardLine);
    TestEqual(TEXT("A punt into the end zone is a touchback"), Model->ResolvePunt(MakeCall(EPSSpecialTeamsPlay::Punt, EPSSpecialTeamsPlay::KickReturn), 60, Even, Even).NextYardLine, Tuning.PuntTouchbackYardLine);

    FPSSpecialTeamsTuning Breakaway = Tuning;
    Breakaway.DefaultBigReturnChance = 1.f;
    Breakaway.BigReturnYards = 90;
    UPSSpecialTeamsModel* Returner = MakeModel(Breakaway);
    const FPSSpecialTeamsOutcome Gone = Returner->ResolveKickoff(MakeCall(EPSSpecialTeamsPlay::Kickoff, EPSSpecialTeamsPlay::KickReturn), 35, Even, Even);
    TestEqual(TEXT("A return that breaks all the way scores"), Gone.Result, EPSSpecialTeamsResult::ReturnTouchdown);
    TestTrue(TEXT("...for the receivers"), Gone.bTouchdown && Gone.bPossessionChanges);
    Breakaway.LaneDisciplineBigReturnScale = 1.f;
    Returner->SetTuning(Breakaway);
    TestEqual(TEXT("Perfect lane discipline never lets it break"),
        Returner->ResolveKickoff(MakeCall(EPSSpecialTeamsPlay::Kickoff, EPSSpecialTeamsPlay::KickReturn), 35, MakeRatings(50.f, 50.f, 100.f), Even).Result, EPSSpecialTeamsResult::Returned);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Onside kicks and laterals
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSpecialTeamsOnsideTest,
    "PlaySports.SpecialTeams.OnsideKicksAndLaterals",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSpecialTeamsOnsideTest::RunTest(const FString& Parameters)
{
    using namespace PSSpecialTeamsTests;

    FPSSpecialTeamsTuning Tuning = FlatTuning();
    Tuning.OnsideRecoveryChance = 1.f;
    Tuning.OnsideRecoveryVsHandsTeamChance = 0.f;
    UPSSpecialTeamsModel* Model = MakeModel(Tuning);
    const FPSSpecialTeamsUnitRatings Even = MakeRatings(50.f, 50.f, 50.f);

    const FPSSpecialTeamsOutcome Surprise = Model->ResolveKickoff(MakeCall(EPSSpecialTeamsPlay::OnsideKick, EPSSpecialTeamsPlay::KickReturn), 35, Even, Even);
    TestEqual(TEXT("A surprise onside kick is recovered"), Surprise.Result, EPSSpecialTeamsResult::OnsideRecovered);
    TestFalse(TEXT("...the kickers keep the ball"), Surprise.bPossessionChanges);
    TestEqual(TEXT("...ten yards on"), Surprise.NextYardLine, 35 + Tuning.OnsideKickYards);
    const FPSSpecialTeamsOutcome Expected = Model->ResolveKickoff(MakeCall(EPSSpecialTeamsPlay::OnsideKick, EPSSpecialTeamsPlay::HandsTeam), 35, Even, Even);
    TestEqual(TEXT("The hands team gets it"), Expected.Result, EPSSpecialTeamsResult::OnsideLost);
    TestTrue(TEXT("...their ball"), Expected.bPossessionChanges);
    TestEqual(TEXT("...at the spot, from their side"), Expected.NextYardLine, 100 - (35 + Tuning.OnsideKickYards));

    // Laterals: a touchdown, or a fumble the kickers fall on where the return was stopped.
    FPSSpecialTeamsTuning Scoring = Tuning;
    Scoring.KickoffTouchbackChance = 1.f;
    Scoring.LateralTouchdownChance = 1.f;
    Scoring.LateralFumbleLostChance = 0.f;
    UPSSpecialTeamsModel* Lateraler = MakeModel(Scoring);
    const FPSSpecialTeamsOutcome Miracle = Lateraler->ResolveKickoff(MakeCall(EPSSpecialTeamsPlay::Kickoff, EPSSpecialTeamsPlay::ReturnLaterals), 35, Even, Even);
    TestEqual(TEXT("Laterals always run it back, and this time score"), Miracle.Result, EPSSpecialTeamsResult::LateralTouchdown);
    TestTrue(TEXT("...a touchdown for the receivers"), Miracle.bTouchdown && Miracle.bPossessionChanges);

    Scoring.LateralTouchdownChance = 0.f;
    Scoring.LateralFumbleLostChance = 1.f;
    Lateraler->SetTuning(Scoring);
    const FPSSpecialTeamsOutcome Fumble = Lateraler->ResolveKickoff(MakeCall(EPSSpecialTeamsPlay::Kickoff, EPSSpecialTeamsPlay::ReturnLaterals), 35, Even, Even);
    TestEqual(TEXT("A lateral on the ground"), Fumble.Result, EPSSpecialTeamsResult::LateralFumbleLost);
    TestFalse(TEXT("...the kickers recover"), Fumble.bPossessionChanges);
    TestEqual(TEXT("...where the return was stopped, from their side"), Fumble.NextYardLine, 100 - 20);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Field goals, fakes, and applying an outcome
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSpecialTeamsFieldGoalTest,
    "PlaySports.SpecialTeams.FieldGoalsFakesAndOutcomes",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSpecialTeamsFieldGoalTest::RunTest(const FString& Parameters)
{
    using namespace PSSpecialTeamsTests;

    FPSSpecialTeamsTuning Tuning = FlatTuning();
    UPSSpecialTeamsModel* Model = MakeModel(Tuning);
    const FPSSpecialTeamsUnitRatings Even = MakeRatings(50.f, 50.f, 50.f);

    TestEqual(TEXT("From the opponent's 20 the kick is 37 yards"), Model->GetFieldGoalDistance(80), 37.f);
    TestEqual(TEXT("A chip shot"), Model->GetFieldGoalChance(25.f), Tuning.FieldGoalRanges[0].MakeChance);
    TestTrue(TEXT("Longer kicks miss more"), Model->GetFieldGoalChance(48.f) < Model->GetFieldGoalChance(35.f));
    TestEqual(TEXT("Out of range: no chance"), Model->GetFieldGoalChance(90.f), 0.f);

    // A kick's quality can be passed in (a human kicker's meter): it decides the kick.
    TestEqual(TEXT("A good kick from 37 yards is good"), Model->ResolveFieldGoal(MakeCall(EPSSpecialTeamsPlay::FieldGoal, EPSSpecialTeamsPlay::KickReturn), 80, Even, Even, 0.5f).Result, EPSSpecialTeamsResult::FieldGoalGood);
    TestEqual(TEXT("...a shanked one misses"), Model->ResolveFieldGoal(MakeCall(EPSSpecialTeamsPlay::FieldGoal, EPSSpecialTeamsPlay::KickReturn), 80, Even, Even, 0.95f).Result, EPSSpecialTeamsResult::FieldGoalMissed);
    FPSSpecialTeamsTuning Ranged = Tuning;
    Ranged.PuntGrossYardsMin = 40;
    Ranged.PuntGrossYardsMax = 50;
    UPSSpecialTeamsModel* Punter = MakeModel(Ranged);
    TestEqual(TEXT("A perfect punt goes the longest"), Punter->ResolvePunt(MakeCall(EPSSpecialTeamsPlay::Punt, EPSSpecialTeamsPlay::KickReturn), 20, Even, Even, 0.f).Yards, 50);
    TestEqual(TEXT("...a poor one the shortest"), Punter->ResolvePunt(MakeCall(EPSSpecialTeamsPlay::Punt, EPSSpecialTeamsPlay::KickReturn), 20, Even, Even, 0.999f).Yards, 40);

    Tuning.FieldGoalRanges = { Tuning.FieldGoalRanges.Last() };
    Tuning.FieldGoalRanges[0].MaxYards = 99.f;
    Tuning.FieldGoalRanges[0].MakeChance = 1.f;
    Model->SetTuning(Tuning);
    TestEqual(TEXT("A sure kick is good"), Model->ResolveFieldGoal(MakeCall(EPSSpecialTeamsPlay::FieldGoal, EPSSpecialTeamsPlay::KickReturn), 80, Even, Even).Result, EPSSpecialTeamsResult::FieldGoalGood);
    Tuning.FieldGoalRanges[0].MakeChance = 0.f;
    Model->SetTuning(Tuning);
    const FPSSpecialTeamsOutcome Missed = Model->ResolveFieldGoal(MakeCall(EPSSpecialTeamsPlay::FieldGoal, EPSSpecialTeamsPlay::KickReturn), 90, Even, Even);
    TestEqual(TEXT("A sure miss"), Missed.Result, EPSSpecialTeamsResult::FieldGoalMissed);
    TestTrue(TEXT("...the defense takes over"), Missed.bPossessionChanges);
    TestEqual(TEXT("...no closer to its goal than the clamp"), Missed.NextYardLine, Tuning.MissedFieldGoalMinYardLine);

    // Fakes: a block unit that sold out is easier to fool.
    TestTrue(TEXT("A fake beats a block unit more often"),
        Model->GetFakeSuccessChance(EPSSpecialTeamsPlay::FakePunt, EPSSpecialTeamsPlay::KickBlock) > Model->GetFakeSuccessChance(EPSSpecialTeamsPlay::FakePunt, EPSSpecialTeamsPlay::KickReturn));
    Tuning.FakePuntSuccessChance = 1.f;
    Tuning.FakeFieldGoalSuccessChance = 0.f;
    Tuning.FakeVsBlockUnitDelta = 0.f;
    Model->SetTuning(Tuning);
    TestTrue(TEXT("A converted fake makes the line to gain"), Model->ResolveFake(MakeCall(EPSSpecialTeamsPlay::FakePunt, EPSSpecialTeamsPlay::KickReturn), 4) >= 4);
    TestTrue(TEXT("A stopped fake falls short"), Model->ResolveFake(MakeCall(EPSSpecialTeamsPlay::FakeFieldGoal, EPSSpecialTeamsPlay::KickReturn), 4) < 4);

    // Applying outcomes to the game state.
    FPlayState State;
    State.bHomeHasPossession = true;
    FPSSpecialTeamsOutcome Good;
    Good.Result = EPSSpecialTeamsResult::FieldGoalGood;
    TestFalse(TEXT("A good field goal keeps the kickers on"), Model->ApplyOutcome(State, Good, 6, 3, 1.f));
    TestEqual(TEXT("...three points"), State.HomeScore, 3);
    TestTrue(TEXT("...and they kick off"), State.bKickoff && State.YardLine == Tuning.KickoffYardLine);

    FPSSpecialTeamsOutcome ReturnTouchdown;
    ReturnTouchdown.Result = EPSSpecialTeamsResult::ReturnTouchdown;
    ReturnTouchdown.bTouchdown = true;
    ReturnTouchdown.bPossessionChanges = true;
    TestTrue(TEXT("A return touchdown changes hands"), Model->ApplyOutcome(State, ReturnTouchdown, 6, 3, 1.f));
    TestEqual(TEXT("...seven for the receivers"), State.AwayScore, 7);
    TestTrue(TEXT("...who kick off next"), State.bKickoff);

    FPSSpecialTeamsOutcome Punt;
    Punt.Result = EPSSpecialTeamsResult::Punted;
    Punt.bPossessionChanges = true;
    Punt.NextYardLine = 28;
    TestTrue(TEXT("A punt changes hands"), Model->ApplyOutcome(State, Punt, 6, 3, 1.f));
    TestTrue(TEXT("...1st and 10 at the return spot, no kickoff"), !State.bKickoff && State.YardLine == 28 && State.Down == 1 && State.Distance == 10);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- The coaching AI's special-teams calls
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSpecialTeamsCallsTest,
    "PlaySports.SpecialTeams.CoachingCalls",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSpecialTeamsCallsTest::RunTest(const FString& Parameters)
{
    using namespace PSSpecialTeamsTests;

    UPSCoachingAI* CoachingAI = NewObject<UPSCoachingAI>();
    CoachingAI->SeedDeterminism(75);
    const UPSSpecialTeamsAI* SpecialTeams = CoachingAI->GetSpecialTeamsAI();
    if (!TestNotNull(TEXT("The coaching AI has special teams"), SpecialTeams))
    {
        return false;
    }
    FPSTendencyProfile Steady;
    Steady.AggressionScore = 0.5f;
    FPSTendencyProfile Gambler;
    Gambler.AggressionScore = 1.f;
    FPSTendencyProfile Cautious;
    Cautious.AggressionScore = 0.f;

    // The offense on a scrimmage down.
    TestEqual(TEXT("4th and 8 at the own 30: punt"), SpecialTeams->DecideOffense(MakeContext(1, 600.f, 0, 4, 8, 30), Steady, false, 1.f), EPSSpecialTeamsPlay::Punt);
    TestEqual(TEXT("4th and 8 at the opponent's 20: field goal"), SpecialTeams->DecideOffense(MakeContext(1, 600.f, 0, 4, 8, 80), Steady, false, 1.f), EPSSpecialTeamsPlay::FieldGoal);
    TestEqual(TEXT("Going for it: no kick"), SpecialTeams->DecideOffense(MakeContext(1, 600.f, 0, 4, 1, 60), Steady, true, 1.f), EPSSpecialTeamsPlay::None);
    TestEqual(TEXT("3rd down: no kick"), SpecialTeams->DecideOffense(MakeContext(1, 600.f, 0, 3, 8, 30), Steady, false, 1.f), EPSSpecialTeamsPlay::None);
    TestEqual(TEXT("A gambler on 4th and 2 with a low roll fakes it"), SpecialTeams->DecideOffense(MakeContext(1, 600.f, 0, 4, 2, 40), Gambler, false, 0.f), EPSSpecialTeamsPlay::FakePunt);
    TestEqual(TEXT("...and in range fakes the field goal"), SpecialTeams->DecideOffense(MakeContext(1, 600.f, 0, 4, 2, 75), Gambler, false, 0.f), EPSSpecialTeamsPlay::FakeFieldGoal);
    TestEqual(TEXT("...but not with a high roll"), SpecialTeams->DecideOffense(MakeContext(1, 600.f, 0, 4, 2, 40), Gambler, false, 0.99f), EPSSpecialTeamsPlay::Punt);
    TestEqual(TEXT("A steady coach never fakes"), SpecialTeams->DecideOffense(MakeContext(1, 600.f, 0, 4, 2, 40), Steady, false, 0.f), EPSSpecialTeamsPlay::Punt);
    TestEqual(TEXT("...nor on 4th and long"), SpecialTeams->DecideOffense(MakeContext(1, 600.f, 0, 4, 9, 40), Gambler, false, 0.f), EPSSpecialTeamsPlay::Punt);
    TestEqual(TEXT("Down 7 late: never punt it away"), SpecialTeams->DecideOffense(MakeContext(4, 90.f, -7, 4, 8, 30), Steady, false, 1.f), EPSSpecialTeamsPlay::None);
    TestEqual(TEXT("Down 2 late in range: kick it"), SpecialTeams->DecideOffense(MakeContext(4, 90.f, -2, 4, 8, 75), Steady, false, 1.f), EPSSpecialTeamsPlay::FieldGoal);
    TestEqual(TEXT("Down 7 late in range: a field goal won't do"), SpecialTeams->DecideOffense(MakeContext(4, 90.f, -7, 4, 8, 75), Steady, false, 1.f), EPSSpecialTeamsPlay::None);
    TestEqual(TEXT("The half's last snap in range: kick on 2nd down"), SpecialTeams->DecideOffense(MakeContext(2, 5.f, -10, 2, 6, 75), Steady, false, 1.f), EPSSpecialTeamsPlay::FieldGoal);
    TestEqual(TEXT("The game's last snap down 7: no field goal"), SpecialTeams->DecideOffense(MakeContext(4, 5.f, -7, 2, 6, 75), Steady, false, 1.f), EPSSpecialTeamsPlay::None);

    // The kicking team at a kickoff.
    FPSSituationContext Kickoff = MakeContext(1, 600.f, 0, 1, 10, 35);
    Kickoff.bKickoff = true;
    TestEqual(TEXT("A normal kickoff"), SpecialTeams->DecideKickoff(Kickoff, Steady, 1.f), EPSSpecialTeamsPlay::Kickoff);
    TestEqual(TEXT("A gambler's surprise onside"), SpecialTeams->DecideKickoff(Kickoff, Gambler, 0.f), EPSSpecialTeamsPlay::OnsideKick);
    FPSSituationContext NeedBall = MakeContext(4, 120.f, -7, 1, 10, 35);
    NeedBall.bKickoff = true;
    TestEqual(TEXT("Down 7 with 2:00 left: onside"), SpecialTeams->DecideKickoff(NeedBall, Steady, 1.f), EPSSpecialTeamsPlay::OnsideKick);
    FPSSituationContext Ahead = MakeContext(4, 120.f, 7, 1, 10, 35);
    Ahead.bKickoff = true;
    TestEqual(TEXT("Leading late: no surprise onside"), SpecialTeams->DecideKickoff(Ahead, Gambler, 0.f), EPSSpecialTeamsPlay::Kickoff);

    // The receiving team.
    TestEqual(TEXT("A normal kickoff: set up the return"), SpecialTeams->DecideReceiving(Kickoff, Steady, 1.f), EPSSpecialTeamsPlay::KickReturn);
    TestEqual(TEXT("They need the ball: hands team"), SpecialTeams->DecideReceiving(NeedBall, Steady, 1.f), EPSSpecialTeamsPlay::HandsTeam);
    FPSSituationContext LastGasp = MakeContext(4, 4.f, 6, 1, 10, 35);
    LastGasp.bKickoff = true;
    TestEqual(TEXT("Down 6 with 0:04 left: laterals"), SpecialTeams->DecideReceiving(LastGasp, Steady, 1.f), EPSSpecialTeamsPlay::ReturnLaterals);
    TestEqual(TEXT("No kick shown: no special teams"), SpecialTeams->DecideReceiving(MakeContext(1, 600.f, 0, 4, 8, 30), Steady, 1.f), EPSSpecialTeamsPlay::None);
    FPSSituationContext Punting = MakeContext(1, 600.f, 0, 4, 8, 30);
    Punting.OffenseKick = EPSSpecialTeamsPlay::Punt;
    TestEqual(TEXT("A punt: set up the return"), SpecialTeams->DecideReceiving(Punting, Steady, 1.f), EPSSpecialTeamsPlay::KickReturn);
    TestEqual(TEXT("...or now and then rush it"), SpecialTeams->DecideReceiving(Punting, Steady, 0.f), EPSSpecialTeamsPlay::KickBlock);
    FPSSituationContext TyingKick = MakeContext(4, 60.f, -2, 4, 6, 75);
    TyingKick.OffenseKick = EPSSpecialTeamsPlay::FieldGoal;
    TestEqual(TEXT("A field goal to win late: block it"), SpecialTeams->DecideReceiving(TyingKick, Steady, 1.f), EPSSpecialTeamsPlay::KickBlock);

    // The play caller picks the due call outright, and never one that isn't due.
    const TArray<FPSPlayDefinition> Offense = {
        MakePlay(TEXT("Run"), TEXT("Run"), true),
        MakePlay(TEXT("Pass"), TEXT("ShortPass"), true),
        MakePlay(TEXT("Punt"), TEXT("Punt"), true, TEXT("Punt")),
        MakePlay(TEXT("FieldGoal"), TEXT("FieldGoal"), true, TEXT("Field Goal")),
        MakePlay(TEXT("FakePunt"), TEXT("FakePunt"), true, TEXT("Punt")) };
    TestEqual(TEXT("The CPU punts on 4th and 8 at its 30"), CoachingAI->SelectOffensivePlay(MakeContext(1, 600.f, 0, 4, 8, 30), Steady, Offense), FName(TEXT("Punt")));
    const TArray<FPSPlaySuggestion> Ranked = CoachingAI->RankPlays(MakeContext(1, 600.f, 0, 4, 8, 30), Steady, Offense, true);
    TestTrue(TEXT("...and suggests it first, saying why"), Ranked.Num() > 0 && Ranked[0].PlayId == FName(TEXT("Punt")) && Ranked[0].Reasons.Num() > 0);
    bool bSpecialOnFirstDown = false;
    for (int32 Roll = 0; Roll < 30; ++Roll)
    {
        const FName Picked = CoachingAI->SelectOffensivePlay(MakeContext(1, 600.f, 0, 1, 10, 30), Gambler, Offense);
        bSpecialOnFirstDown |= Picked != FName(TEXT("Run")) && Picked != FName(TEXT("Pass"));
    }
    TestFalse(TEXT("No kick or fake on 1st and 10"), bSpecialOnFirstDown);

    const TArray<FPSPlayDefinition> Defense = {
        MakePlay(TEXT("Base"), TEXT("Base"), false),
        MakePlay(TEXT("Wall"), TEXT("KickReturn"), false, TEXT("Return Wall")),
        MakePlay(TEXT("Wedge"), TEXT("KickReturn"), false, TEXT("Return Wedge")),
        MakePlay(TEXT("Block"), TEXT("KickBlock"), false, TEXT("Kick Block")) };
    bool bDefenseSpecial = false;
    for (int32 Roll = 0; Roll < 30; ++Roll)
    {
        const FName Picked = CoachingAI->SelectDefensivePlay(MakeContext(1, 600.f, 0, 3, 8, 30), Steady, Defense);
        bDefenseSpecial |= Picked != FName(TEXT("Base"));
    }
    TestFalse(TEXT("The defense never lines up for a kick that isn't coming"), bDefenseSpecial);
    const FName AgainstPunt = CoachingAI->SelectDefensivePlay(Punting, Steady, Defense);
    TestTrue(TEXT("Against a punt the defense returns or blocks"), AgainstPunt == FName(TEXT("Wall")) || AgainstPunt == FName(TEXT("Wedge")) || AgainstPunt == FName(TEXT("Block")));
    const TArray<FPSPlayDefinition> KickoffCalls = {
        MakePlay(TEXT("Kickoff"), TEXT("Kickoff"), true, TEXT("Kickoff")),
        MakePlay(TEXT("Onside"), TEXT("OnsideKick"), true, TEXT("Kickoff")) };
    TestEqual(TEXT("At a kickoff a cautious CPU kicks deep"), CoachingAI->SelectOffensivePlay(Kickoff, Cautious, KickoffCalls), FName(TEXT("Kickoff")));
    TestEqual(TEXT("...or onside when it needs the ball"), CoachingAI->SelectOffensivePlay(NeedBall, Steady, KickoffCalls), FName(TEXT("Onside")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
