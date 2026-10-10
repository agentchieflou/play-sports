// PSBallActionComponentTests.cpp -- Epic C3: Ball action component and attributes reference tests
//
// Tests covered:
//   1. The pawn's attributes pointer and its ball-action component.
//   2. A live tackle goes out on the bus: UPSBallActionComponent::ResolveTackle publishes the
//      Tackle event (tackler, carrier, spot, yards, sack), the play simulation records the play
//      from it, and the statistics engine counts a rush with its tackler, then a sack.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSPlayerPawn.h"
#include "PSBallActionComponent.h"
#include "PSBall.h"
#include "PSCarrierMoveComponent.h"
#include "PSPlaySimulation.h"
#include "PSStatsData.h"
#include "PSStatsEngine.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSBallActionTests
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

    static FPlayerAttributes MakePlayer(const TCHAR* PlayerId, const TCHAR* DisplayName, EPlayerRole Role)
    {
        FPlayerAttributes Player;
        Player.PlayerId = FName(PlayerId);
        Player.DisplayName = DisplayName;
        Player.Role = Role;
        Player.WeightKg = 100.f;
        Player.HeightCm = 188.f;
        Player.Speed = 70.f;
        Player.Agility = 70.f;
        Player.Strength = 70.f;
        Player.Acceleration = 70.f;
        Player.Awareness = 70.f;
        Player.Stamina = 100.f;
        return Player;
    }

    /** A pawn for Player standing at Location, having lined up at LinedUpAt. */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, const FPlayerAttributes& Player, const FVector& Location, const FVector& LinedUpAt)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (Pawn)
        {
            Pawn->InitializePlayer(Player);
            Pawn->SetStartingLocation(LinedUpAt);
        }
        return Pawn;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSBallActionComponentTest,
    "PlaySports.C3.BallActionComponentAndAttributes",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSBallActionComponentTest::RunTest(const FString& Parameters)
{
    // Test pawn attributes pointer mapping
    APSPlayerPawn* Pawn = NewObject<APSPlayerPawn>();
    TestNotNull(TEXT("Pawn created"), Pawn);
    if (!Pawn)
    {
        return false;
    }

    FPlayerAttributes AttributesSource;
    AttributesSource.PlayerId = TEXT("QB_TEST");
    AttributesSource.DisplayName = TEXT("Test QB");
    AttributesSource.Strength = 90.f;
    AttributesSource.Awareness = 85.f;

    Pawn->InitializePlayerPointer(&AttributesSource);

    // Verify pointer access
    FPlayerAttributes RetrievedAttributes = Pawn->GetAttributes();
    TestEqual(TEXT("PlayerId matches source"), RetrievedAttributes.PlayerId, AttributesSource.PlayerId);
    TestEqual(TEXT("Strength matches source"), RetrievedAttributes.Strength, AttributesSource.Strength);

    // Verify component exists
    UPSBallActionComponent* ActionComp = Pawn->GetBallActionComponent();
    TestNotNull(TEXT("BallActionComponent exists on pawn"), ActionComp);

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- A live tackle reaches the bus, the simulation and the stats
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLiveTackleOnBusTest,
    "PlaySports.C3.LiveTackleReachesStatsThroughBus",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLiveTackleOnBusTest::RunTest(const FString& Parameters)
{
    using namespace PSBallActionTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    // The home offense against the away defense, 1st and 10 at the home 20 (the line of
    // scrimmage at X = 2000 cm). No game mode: the bus is the only way the tackle can travel.
    const TArray<FPlayerAttributes> Offense = {
        MakePlayer(TEXT("HOME_QB"), TEXT("Home Quarterback"), EPlayerRole::Quarterback),
        MakePlayer(TEXT("HOME_RB"), TEXT("Home Runner"), EPlayerRole::RunningBack) };
    const TArray<FPlayerAttributes> Defense = {
        MakePlayer(TEXT("AWAY_DL"), TEXT("Away Lineman"), EPlayerRole::DefensiveLineman),
        MakePlayer(TEXT("AWAY_LB"), TEXT("Away Linebacker"), EPlayerRole::Linebacker) };
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    Sim->InitializePlay(Offense, Defense);
    Sim->InitializeWithWorld(World);
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    Stats->BindToBus(Bus);
    Stats->BeginGame(1, FName(TEXT("Home")), FName(TEXT("Away")));

    TArray<FPSTelemetryTackleEvent> Tackles;
    Bus->OnTackleMC.AddLambda([&Tackles](const FPSTelemetryTackleEvent& Event) { Tackles.Add(Event); });
    TArray<FPSTelemetryPlayResultEvent> Plays;
    Bus->OnPlayResultMC.AddLambda([&Plays](const FPSTelemetryPlayResultEvent& Event) { Plays.Add(Event); });

    // A run: the back lined up on the line and is brought down at the 28. He slides into the
    // contact, so the tackle holds with no hit and no fumble: the test needs no luck.
    Sim->TriggerSnap();
    APSPlayerPawn* Runner = SpawnPlayer(World, Offense[1], FVector(2800.f, 0.f, 100.f), FVector(2000.f, 0.f, 100.f));
    APSPlayerPawn* Linebacker = SpawnPlayer(World, Defense[1], FVector(2900.f, 0.f, 100.f), FVector(2450.f, 0.f, 100.f));
    if (!TestTrue(TEXT("The runner and the linebacker"), Runner && Linebacker))
    {
        DestroyTestWorld(World);
        return false;
    }
    Runner->GainPossession();
    TestTrue(TEXT("The runner slides"), Runner->GetCarrierMoveComponent()->TryMove(EPSCarrierMove::Slide, FVector2D::ZeroVector));
    TestTrue(TEXT("The tackle holds"), Runner->GetBallActionComponent()->ResolveTackle(Linebacker));

    if (TestEqual(TEXT("One Tackle event on the bus"), Tackles.Num(), 1))
    {
        TestEqual(TEXT("...naming the tackler"), Tackles[0].TacklerName, Defense[1].DisplayName);
        TestEqual(TEXT("...and the carrier"), Tackles[0].BallCarrierName, Offense[1].DisplayName);
        TestEqual(TEXT("...at the spot: the 28"), Tackles[0].YardLine, 28);
        TestEqual(TEXT("...8 yards on"), Tackles[0].YardsGained, 8);
        TestFalse(TEXT("...a run, not a sack"), Tackles[0].bIsSack);
    }
    TestEqual(TEXT("The simulation took the tackle: the whistle has blown"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);
    TestEqual(TEXT("...on a tackle"), Sim->GetPlayResult().ResultType, EPlayResultType::Tackle);
    TestEqual(TEXT("...for 8 yards"), Sim->GetPlayResult().YardsGained, 8);

    // The whistle's wait is over (called directly: AdvancePlay's random flags would move the yards).
    Sim->EndPlayAndPrepareNext();
    if (TestEqual(TEXT("The simulation announced one play"), Plays.Num(), 1))
    {
        TestTrue(TEXT("...a run"), !Plays[0].bPass && Plays[0].Result == TEXT("Tackle"));
        TestEqual(TEXT("...by the runner"), Plays[0].RusherId, Offense[1].PlayerId);
        TestEqual(TEXT("...stopped by the linebacker"), Plays[0].TacklerId, Defense[1].PlayerId);
    }
    const FPSBoxScore& Game = Stats->GetCurrentGame();
    TestEqual(TEXT("The stats engine recorded the play"), Game.PlayCount, 1);
    const FPSPlayerStatLine* RunnerLine = Game.FindPlayer(Offense[1].PlayerId);
    TestTrue(TEXT("A rush for 8 yards for the runner, on the home team"),
        RunnerLine && RunnerLine->RushAttempts == 1 && RunnerLine->RushingYards == 8 && RunnerLine->TeamId == FName(TEXT("Home")));
    const FPSPlayerStatLine* LinebackerLine = Game.FindPlayer(Defense[1].PlayerId);
    TestTrue(TEXT("A tackle for the linebacker, on the away team"),
        LinebackerLine && LinebackerLine->Tackles == 1 && LinebackerLine->Sacks == 0 && LinebackerLine->TeamId == FName(TEXT("Away")));
    TestEqual(TEXT("The home team's rushing yards"), Game.Home.RushingYards, 8);

    // A sack: the quarterback, still holding the ball, goes down behind where he lined up (a
    // yard behind the new line at the 28).
    Sim->TriggerSnap();
    APSPlayerPawn* Quarterback = SpawnPlayer(World, Offense[0], FVector(2100.f, 0.f, 100.f), FVector(2700.f, 0.f, 100.f));
    APSPlayerPawn* Lineman = SpawnPlayer(World, Defense[0], FVector(2050.f, 0.f, 100.f), FVector(2900.f, 0.f, 100.f));
    if (!TestTrue(TEXT("The quarterback and the lineman"), Quarterback && Lineman))
    {
        DestroyTestWorld(World);
        return false;
    }
    Quarterback->GainPossession();
    TestTrue(TEXT("The quarterback slides"), Quarterback->GetCarrierMoveComponent()->TryMove(EPSCarrierMove::Slide, FVector2D::ZeroVector));
    TestTrue(TEXT("The sack holds"), Quarterback->GetBallActionComponent()->ResolveTackle(Lineman));
    if (TestEqual(TEXT("A second Tackle event"), Tackles.Num(), 2))
    {
        TestTrue(TEXT("...a sack, 6 yards back"), Tackles[1].bIsSack && Tackles[1].YardsGained == -6);
        TestEqual(TEXT("...naming the lineman"), Tackles[1].TacklerName, Defense[0].DisplayName);
    }
    Sim->EndPlayAndPrepareNext();
    const FPSPlayerStatLine* LinemanLine = Stats->GetCurrentGame().FindPlayer(Defense[0].PlayerId);
    TestTrue(TEXT("The lineman's sack, a tackle too"), LinemanLine && LinemanLine->Sacks == 1 && LinemanLine->Tackles == 1);
    const FPSPlayerStatLine* QuarterbackLine = Stats->GetCurrentGame().FindPlayer(Offense[0].PlayerId);
    TestTrue(TEXT("The quarterback sacked, not rushing"), QuarterbackLine && QuarterbackLine->TimesSacked == 1 && QuarterbackLine->RushAttempts == 0);

    Stats->UnbindFromBus();
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
