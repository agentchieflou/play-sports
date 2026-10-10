// PSSpecialTeamsGameTests.cpp -- Epic 75 (special teams depth and trick plays): the game around the kicks
//
// Tests covered:
//   1. Scores and kickoffs: the scoring team kicks off (a safety's free kick from the 20), the
//      kickoff resolves through the special-teams model, and the ball changes hands once.
//   2. 4th down in a played game is a call: a punt call snaps into the punt with the receiving
//      team's return scheme, a field-goal call into the kick on any down, a fake converts at the
//      snap; a physical tackle during a kick changes nothing; quick sim still kicks by rule.
//   3. The play caller: the CPU punts and the CPU defense sets up for it, a kickoff offers only
//      kickoff calls and returns, a scrimmage down never offers kickoff calls, and the simulation
//      snaps into the called kick.
//   4. A human offense lining up to punt: the CPU defense re-calls its return or block, and its
//      regular defense once the offense comes out of the punt.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSMenuComponent.h"
#include "PSPlayCallComponent.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlaySimulation.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSSpecialTeamsModel.h"
#include "PSTelemetryBus.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSSpecialTeamsGameTests
{
    static UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    static void DestroyTestWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    /** A sim whose kicks have no random spread: touchbacks always, 45-yard punts with no
     *  return beyond the scheme's, field goals always good, fakes always converted. */
    static UPSPlaySimulation* MakeSim(bool bQuickSim)
    {
        UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
        Sim->bQuickSimMode = bQuickSim;
        TArray<FPlayerAttributes> Offense, Defense;
        Sim->InitializePlay(Offense, Defense);

        FPSSpecialTeamsTuning Tuning;
        Tuning.KickoffTouchbackChance = 1.f;
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
        Tuning.FieldGoalRanges.SetNum(1);
        Tuning.FieldGoalRanges[0].MaxYards = 99.f;
        Tuning.FieldGoalRanges[0].MakeChance = 1.f;
        Tuning.FakePuntSuccessChance = 1.f;
        Sim->GetSpecialTeams()->SetTuning(Tuning);
        Sim->GetSpecialTeams()->Seed(75);
        return Sim;
    }

    /** A play from the snap (or before it) to the whistle: the carrier down for Yards. */
    static void TackleAndNext(UPSPlaySimulation* Sim, int32 Yards)
    {
        Sim->RecordTackle(Yards);
        Sim->ActivePenalty = EPSPenaltyType::None;
        Sim->EndPlayAndPrepareNext();
    }

    static FPSTelemetryPlayCallEvent MakeCall(bool bOffense, const TCHAR* Category, const TCHAR* Formation)
    {
        FPSTelemetryPlayCallEvent Call;
        Call.bOffense = bOffense;
        Call.PlayCategory = Category;
        Call.Formation = Formation;
        return Call;
    }

    /** Snaps, with no offside flag, and runs the play to the whistle. */
    static void SnapAndRun(UPSPlaySimulation* Sim, float Seconds)
    {
        Sim->TriggerSnap();
        Sim->ActivePenalty = EPSPenaltyType::None;
        Sim->AdvancePlay(Seconds);
    }

    static FPSSituationContext MakeContext(int32 Down, int32 Distance, int32 YardLine)
    {
        FPSSituationContext Context;
        Context.Down = Down;
        Context.Distance = Distance;
        Context.YardLine = YardLine;
        Context.GameClockSeconds = 600.f;
        return Context;
    }

    static FString CategoryOf(UPSPlayCallSubsystem* PlayCall, bool bOffense)
    {
        FPSPlayDefinition Play;
        return PlayCall->FindPlay(PlayCall->GetCall(bOffense).PlayId, Play) ? Play.PlayCategory : FString();
    }

    static bool IsReturnOrBlock(const FString& Category)
    {
        return Category == TEXT("KickReturn") || Category == TEXT("KickBlock");
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Scores, kickoffs and possession
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSpecialTeamsKickoffFlowTest,
    "PlaySports.SpecialTeams.ScoresKickOffAndChangeHands",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSpecialTeamsKickoffFlowTest::RunTest(const FString& Parameters)
{
    using namespace PSSpecialTeamsGameTests;

    UPSPlaySimulation* Sim = MakeSim(false);
    const FPSSpecialTeamsTuning& Tuning = Sim->GetSpecialTeams()->GetTuning();
    const bool bHomeScores = Sim->GetPlayState().bHomeHasPossession;

    Sim->RecordTouchdown();
    Sim->ActivePenalty = EPSPenaltyType::None;
    Sim->EndPlayAndPrepareNext();
    TestTrue(TEXT("A touchdown counts"), (bHomeScores ? Sim->GetPlayState().HomeScore : Sim->GetPlayState().AwayScore) >= 6);
    TestTrue(TEXT("...and the scoring team kicks off"), Sim->GetPlayState().bKickoff && Sim->GetPlayState().bHomeHasPossession == bHomeScores);
    TestEqual(TEXT("...from its kickoff spot"), Sim->GetPlayState().YardLine, Tuning.KickoffYardLine);
    TestEqual(TEXT("...after a call like any down"), Sim->GetPlayState().Phase, EPlayPhase::PreSnap);

    SnapAndRun(Sim, 0.1f);
    TestEqual(TEXT("The snap is the kickoff"), Sim->GetPlayState().Phase, EPlayPhase::Kickoff);
    Sim->AdvancePlay(2.f);
    TestEqual(TEXT("The special-teams model resolves it"), Sim->GetLastSpecialTeamsOutcome().Result, EPSSpecialTeamsResult::Touchback);
    Sim->ActivePenalty = EPSPenaltyType::None;
    Sim->EndPlayAndPrepareNext();
    TestEqual(TEXT("The receivers have the ball"), Sim->GetPlayState().bHomeHasPossession, !bHomeScores);
    TestTrue(TEXT("...1st and 10 at the touchback line, no kickoff pending"),
        !Sim->GetPlayState().bKickoff && Sim->GetPlayState().YardLine == Tuning.TouchbackYardLine && Sim->GetPlayState().Down == 1);

    // A safety: the team scored on free-kicks from its 20.
    UPSPlaySimulation* Safety = MakeSim(false);
    const bool bHomeConcedes = Safety->GetPlayState().bHomeHasPossession;
    TackleAndNext(Safety, -Safety->GetPlayState().YardLine - 5);
    TestEqual(TEXT("A safety scores two for the defense"), bHomeConcedes ? Safety->GetPlayState().AwayScore : Safety->GetPlayState().HomeScore, 2);
    TestTrue(TEXT("...and the team scored on kicks"), Safety->GetPlayState().bKickoff && Safety->GetPlayState().bHomeHasPossession == bHomeConcedes);
    TestEqual(TEXT("...from its 20"), Safety->GetPlayState().YardLine, Tuning.SafetyKickYardLine);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Kicks and fakes on a called down
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSpecialTeamsCalledKickTest,
    "PlaySports.SpecialTeams.FourthDownIsACall",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSpecialTeamsCalledKickTest::RunTest(const FString& Parameters)
{
    using namespace PSSpecialTeamsGameTests;

    UPSPlaySimulation* Sim = MakeSim(false);
    const bool bHomeKicks = Sim->GetPlayState().bHomeHasPossession;
    const int32 Start = Sim->GetPlayState().YardLine;
    for (int32 Down = 1; Down <= 3; ++Down)
    {
        TackleAndNext(Sim, 0);
    }
    TestEqual(TEXT("4th down"), Sim->GetPlayState().Down, 4);
    TestEqual(TEXT("...is a call in a played game, not an automatic kick"), Sim->GetPlayState().Phase, EPlayPhase::PreSnap);

    Sim->OnBusPlayCallEvent(MakeCall(true, TEXT("Punt"), TEXT("Punt")));
    Sim->OnBusPlayCallEvent(MakeCall(false, TEXT("KickReturn"), TEXT("Return Wall")));
    SnapAndRun(Sim, 0.1f);
    TestEqual(TEXT("A punt call snaps into the punt"), Sim->GetPlayState().Phase, EPlayPhase::Punt);
    Sim->RecordTackle(7);
    TestEqual(TEXT("A tackle during the kick changes nothing"), Sim->GetPlayState().Phase, EPlayPhase::Punt);
    Sim->AdvancePlay(2.f);
    Sim->ActivePenalty = EPSPenaltyType::None;
    Sim->EndPlayAndPrepareNext();
    const FPSReturnSchemeDef* Wall = Sim->GetSpecialTeams()->FindReturnScheme(TEXT("Return Wall"));
    TestEqual(TEXT("The receivers have it"), Sim->GetPlayState().bHomeHasPossession, !bHomeKicks);
    TestEqual(TEXT("...where the punt landed, plus the wall's return"), Sim->GetPlayState().YardLine,
        100 - (Start + 45) + (Wall ? FMath::RoundToInt(Wall->ReturnYardsBonus) : 0));

    // A field goal on 1st down from the opponent's 20.
    UPSPlaySimulation* Kicker = MakeSim(false);
    const bool bHomeKicker = Kicker->GetPlayState().bHomeHasPossession;
    TackleAndNext(Kicker, 80 - Kicker->GetPlayState().YardLine);
    Kicker->OnBusPlayCallEvent(MakeCall(true, TEXT("FieldGoal"), TEXT("Field Goal")));
    SnapAndRun(Kicker, 0.1f);
    TestEqual(TEXT("A field-goal call snaps into the kick, on any down"), Kicker->GetPlayState().Phase, EPlayPhase::FieldGoal);
    Kicker->AdvancePlay(2.f);
    Kicker->ActivePenalty = EPSPenaltyType::None;
    Kicker->EndPlayAndPrepareNext();
    TestEqual(TEXT("Three points"), bHomeKicker ? Kicker->GetPlayState().HomeScore : Kicker->GetPlayState().AwayScore, 3);
    TestTrue(TEXT("...and the kickers kick off"), Kicker->GetPlayState().bKickoff && Kicker->GetPlayState().bHomeHasPossession == bHomeKicker);

    // A fake punt on 4th and 10, converted at the snap.
    UPSPlaySimulation* Faker = MakeSim(false);
    const bool bHomeFakes = Faker->GetPlayState().bHomeHasPossession;
    for (int32 Down = 1; Down <= 3; ++Down)
    {
        TackleAndNext(Faker, 0);
    }
    Faker->OnBusPlayCallEvent(MakeCall(true, TEXT("FakePunt"), TEXT("Punt")));
    SnapAndRun(Faker, 0.1f);
    TestEqual(TEXT("The fake is decided at the snap"), Faker->GetPlayState().Phase, EPlayPhase::Scoring);
    TestTrue(TEXT("...for the line to gain"), Faker->GetPlayResult().YardsGained >= 10);
    Faker->ActivePenalty = EPSPenaltyType::None;
    Faker->EndPlayAndPrepareNext();
    TestTrue(TEXT("A converted fake is a 1st down for the same team"), Faker->GetPlayState().Down == 1 && Faker->GetPlayState().bHomeHasPossession == bHomeFakes);

    // Quick sim has no calls: it still kicks by rule on 4th down.
    UPSPlaySimulation* QuickSim = MakeSim(true);
    for (int32 Down = 1; Down <= 3; ++Down)
    {
        TackleAndNext(QuickSim, 0);
    }
    TestEqual(TEXT("Quick sim punts on 4th down in its own end"), QuickSim->GetPlayState().Phase, EPlayPhase::Punt);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The play caller's special teams
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSpecialTeamsPlayCallerTest,
    "PlaySports.SpecialTeams.PlayCallerCallsSpecialTeams",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSpecialTeamsPlayCallerTest::RunTest(const FString& Parameters)
{
    using namespace PSSpecialTeamsGameTests;

    UWorld* World = CreateTestWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Play-call subsystem"), PlayCall))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSPlaySimulation* Sim = MakeSim(false);
    Sim->InitializeWithWorld(World);

    // A scrimmage down offers every play but the kickoff calls.
    PlayCall->OpenPlayCall(MakeContext(1, 10, 30));
    const TArray<FPSPlayDefinition> Scrimmage = PlayCall->GetPlays(true);
    TestTrue(TEXT("A scrimmage down offers the punt"), Scrimmage.ContainsByPredicate([](const FPSPlayDefinition& Play) { return Play.PlayId == FName(TEXT("Offense_Punt")); }));
    TestFalse(TEXT("...but not the kickoff"), Scrimmage.ContainsByPredicate([](const FPSPlayDefinition& Play) { return Play.PlayCategory == TEXT("Kickoff") || Play.PlayCategory == TEXT("OnsideKick"); }));

    // 4th and 8 at the own 30: the CPU punts, and the CPU defense sets up for it.
    PlayCall->OpenPlayCall(MakeContext(4, 8, 30));
    PlayCall->PollReadyToSnap(0.f);
    TestEqual(TEXT("The CPU punts"), PlayCall->GetCall(true).PlayId, FName(TEXT("Offense_Punt")));
    TestTrue(TEXT("...and the CPU defense returns or blocks it"), IsReturnOrBlock(CategoryOf(PlayCall, false)));
    TestEqual(TEXT("The defense saw the punt formation"), PlayCall->GetSituation().OffenseKick, EPSSpecialTeamsPlay::Punt);
    SnapAndRun(Sim, 0.1f);
    TestEqual(TEXT("The simulation snaps into the called punt"), Sim->GetPlayState().Phase, EPlayPhase::Punt);

    // A kickoff: kickoff calls and returns only.
    FPSSituationContext Kickoff = MakeContext(1, 10, 35);
    Kickoff.bKickoff = true;
    PlayCall->OpenPlayCall(Kickoff);
    const TArray<FPSPlayDefinition> Kicks = PlayCall->GetPlays(true);
    const TArray<FPSPlayDefinition> Returns = PlayCall->GetPlays(false);
    TestTrue(TEXT("The kicking team has kickoff calls"), Kicks.Num() > 0);
    TestFalse(TEXT("...and nothing else"), Kicks.ContainsByPredicate([](const FPSPlayDefinition& Play) { return Play.PlayCategory != TEXT("Kickoff") && Play.PlayCategory != TEXT("OnsideKick"); }));
    TestTrue(TEXT("The receiving team has returns"), Returns.ContainsByPredicate([](const FPSPlayDefinition& Play) { return Play.PlayCategory == TEXT("KickReturn"); }));
    TestFalse(TEXT("...and no regular defense or kick block"), Returns.ContainsByPredicate([](const FPSPlayDefinition& Play)
    {
        return Play.PlayCategory != TEXT("KickReturn") && Play.PlayCategory != TEXT("HandsTeam") && Play.PlayCategory != TEXT("ReturnLaterals");
    }));
    TestEqual(TEXT("The call screen says it's a kickoff"), UPSPlayCallSubsystem::DescribeSituation(Kickoff), FString(TEXT("Kickoff from own 35")));
    PlayCall->PollReadyToSnap(0.f);
    const FString KickCall = CategoryOf(PlayCall, true);
    TestTrue(TEXT("The CPU kicks off"), KickCall == TEXT("Kickoff") || KickCall == TEXT("OnsideKick"));
    TestEqual(TEXT("...and the receivers set up a return"), CategoryOf(PlayCall, false), FString(TEXT("KickReturn")));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The defense answers a human punt formation
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSpecialTeamsDefenseRecallTest,
    "PlaySports.SpecialTeams.DefenseAnswersThePuntFormation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSpecialTeamsDefenseRecallTest::RunTest(const FString& Parameters)
{
    using namespace PSSpecialTeamsGameTests;

    UWorld* World = CreateTestWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Play-call subsystem"), PlayCall))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    // A human quarterback, so the offense waits for a person's call.
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerPawn* QB = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Controller"), Controller))
    {
        DestroyTestWorld(World);
        return false;
    }
    FPlayerAttributes Attributes;
    Attributes.PlayerId = TEXT("QB_SPECIAL");
    Attributes.DisplayName = TEXT("QB_SPECIAL");
    Attributes.Role = EPlayerRole::Quarterback;
    QB->InitializePlayer(Attributes);
    Controller->GetPlayCallComponent()->BindToPlayCall();
    Controller->TakeControlOf(QB);

    PlayCall->OpenPlayCall(MakeContext(4, 8, 30));
    PlayCall->PollReadyToSnap(0.f);
    TestTrue(TEXT("The offense waits for its human"), PlayCall->IsWaitingForHuman(true));
    TestFalse(TEXT("The CPU defense calls its regular defense"), CategoryOf(PlayCall, false).IsEmpty() || IsReturnOrBlock(CategoryOf(PlayCall, false)));

    PlayCall->CallPlay(TEXT("Offense_Punt"), EPSPlayCaller::Human);
    TestTrue(TEXT("The offense lines up to punt: the CPU defense answers with a return or a block"), IsReturnOrBlock(CategoryOf(PlayCall, false)));
    PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human);
    TestFalse(TEXT("...and goes back to its defense when the offense comes out of the punt"), IsReturnOrBlock(CategoryOf(PlayCall, false)));

    Controller->GetPlayCallComponent()->UnbindFromPlayCall();
    Controller->ReleaseControl();
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
