// PSBallBusTests.cpp -- the ball reports through the bus; the simulation rules (rules 5 and 6)
//
// Tests covered:
//   1. With no game mode in the world, the ball's outcomes reach the play simulation only
//      through the bus. A caught pass publishes a Catch and the simulation makes the receiver a
//      ball carrier. A pass that comes down untouched publishes BallGrounded once per flight and
//      the simulation rules it incomplete; a landing after the whistle changes nothing. A
//      recovered fumble publishes a Fumble and the simulation makes the recoverer a carrier. An
//      interception publishes a Catch the simulation rules a turnover, and downs the pass's
//      intended receiver (Epic 140), found on the field as the AI reads it.
//   2. The game mode announces every change of phase since the last one (MakePhaseChange):
//      the ones the simulation's clock makes and the ones bus events make between ticks.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSGameStateEvents.h"
#include "PSHealthComponent.h"
#include "PSPlaySimulation.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSBallBusTests
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

    static FPlayerAttributes MakePlayer(const TCHAR* PlayerId, EPlayerRole Role)
    {
        FPlayerAttributes Player;
        Player.PlayerId = FName(PlayerId);
        Player.DisplayName = PlayerId;
        Player.Role = Role;
        Player.Speed = 80.f;
        Player.Agility = 80.f;
        Player.Strength = 80.f;
        Player.Acceleration = 80.f;
        Player.Awareness = 80.f;
        Player.Stamina = 100.f;
        return Player;
    }

    static APSPlayerPawn* SpawnPlayer(UWorld* World, const FPlayerAttributes& Player, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (Pawn)
        {
            Pawn->InitializePlayer(Player);
        }
        return Pawn;
    }

    /** The passer throws for Target: announced on the bus, the ball in the air. */
    static void Throw(UPSTelemetryBus* Bus, APSBall* Ball, const FPlayerAttributes& Passer, const FPlayerAttributes& Target)
    {
        FPSTelemetryThrowEvent Pass;
        Pass.PasserName = Passer.DisplayName;
        Pass.TargetReceiverName = Target.DisplayName;
        Bus->PublishThrow(Pass);
        Ball->Launch(FVector(1500.f, 0.f, 400.f));
    }

    /** The next down: the play resolved, snapped again with no flag down. */
    static void NextSnap(UPSPlaySimulation* Sim)
    {
        Sim->ActivePenalty = EPSPenaltyType::None;
        Sim->EndPlayAndPrepareNext();
        Sim->TriggerSnap();
        Sim->ActivePenalty = EPSPenaltyType::None;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The ball's outcomes go through the bus
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSBallReportsThroughBusTest,
    "PlaySports.C3.BallReportsThroughTheBus",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSBallReportsThroughBusTest::RunTest(const FString& Parameters)
{
    using namespace PSBallBusTests;

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

    // No game mode: the bus is the only way the ball can reach the simulation.
    const FPlayerAttributes Passer = MakePlayer(TEXT("BALL_QB"), EPlayerRole::Quarterback);
    const FPlayerAttributes Receiver = MakePlayer(TEXT("BALL_WR"), EPlayerRole::WideReceiver);
    const FPlayerAttributes Corner = MakePlayer(TEXT("BALL_CB"), EPlayerRole::DefensiveBack);
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    Sim->InitializePlay({ Passer, Receiver }, { Corner });
    Sim->InitializeWithWorld(World);
    Sim->TriggerSnap();
    Sim->ActivePenalty = EPSPenaltyType::None;
    TestNull(TEXT("No game mode in the world"), World->GetAuthGameMode());

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), FVector(2000.f, 0.f, 100.f), FRotator::ZeroRotator, SpawnParams);
    APSPlayerPawn* WR = SpawnPlayer(World, Receiver, FVector(2600.f, 300.f, 100.f));
    APSPlayerPawn* CB = SpawnPlayer(World, Corner, FVector(2700.f, 300.f, 100.f));
    if (!TestTrue(TEXT("The ball, the receiver and the corner"), Ball && WR && CB))
    {
        DestroyTestWorld(World);
        return false;
    }
    // Headless worlds have no BeginPlay: the ball hears the passes by hand. Every roll is sure,
    // so the test needs no luck.
    Ball->BindToBus();
    Ball->CatchTuningSettings.CatchChanceMin = 1.f;
    Ball->CatchTuningSettings.CatchChanceMax = 1.f;
    Ball->CatchTuningSettings.InterceptionChanceMin = 1.f;
    Ball->CatchTuningSettings.InterceptionChanceMax = 1.f;
    Ball->CatchTuningSettings.FumbleRecoveryChanceMin = 1.f;
    Ball->CatchTuningSettings.FumbleRecoveryChanceMax = 1.f;

    TArray<FPSTelemetryCatchEvent> Catches;
    Bus->OnCatchMC.AddLambda([&Catches](const FPSTelemetryCatchEvent& Event) { Catches.Add(Event); });
    int32 Landings = 0;
    Bus->OnBallGroundedMC.AddLambda([&Landings](const FPSTelemetryBallGroundedEvent&) { ++Landings; });
    TArray<FPSTelemetryFumbleEvent> Recoveries;
    Bus->OnFumbleMC.AddLambda([&Recoveries](const FPSTelemetryFumbleEvent& Event) { Recoveries.Add(Event); });
    TArray<FPSTelemetryDeathEvent> Deaths;
    Bus->OnDeathMC.AddLambda([&Deaths](const FPSTelemetryDeathEvent& Event) { Deaths.Add(Event); });

    // 1. A caught pass: the Catch goes out, and the simulation makes the receiver a carrier.
    Throw(Bus, Ball, Passer, Receiver);
    TestTrue(TEXT("The receiver takes the pass"), Ball->ResolveTouch(WR));
    TestTrue(TEXT("...and has the ball"), WR->HasPossession() && Ball->GetAttachParentActor() == WR);
    TestTrue(TEXT("The catch is on the bus"), Catches.Num() == 1 && Catches[0].ReceiverName == Receiver.DisplayName && !Catches[0].bIsInterception);
    TestEqual(TEXT("The simulation, hearing it, makes him a ball carrier"), Sim->GetPlayState().Phase, EPlayPhase::BallCarrierMovement);
    TestFalse(TEXT("A held ball can't be taken"), Ball->ResolveTouch(CB));
    TestFalse(TEXT("...nor land"), Ball->ReportGrounded());

    // 2. A pass that comes down untouched: reported once, ruled incomplete.
    NextSnap(Sim);
    WR->LosePossession();
    Throw(Bus, Ball, Passer, Receiver);
    TestTrue(TEXT("The pass comes down"), Ball->ReportGrounded());
    TestFalse(TEXT("...reported once per flight"), Ball->ReportGrounded());
    TestEqual(TEXT("One landing on the bus"), Landings, 1);
    TestEqual(TEXT("The simulation blows the whistle"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);
    TestEqual(TEXT("...on an incompletion"), Sim->GetPlayResult().ResultType, EPlayResultType::Incomplete);
    Ball->Launch(FVector(100.f, 0.f, 100.f));
    TestTrue(TEXT("A ball kicked loose after the whistle lands"), Ball->ReportGrounded());
    TestEqual(TEXT("...and changes nothing"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);

    // 3. A fumble recovered during the play: the simulation makes the recoverer a carrier.
    NextSnap(Sim);
    Ball->Fumble(FVector(0.f, 200.f, 200.f));
    TestFalse(TEXT("A fumble on the ground is live, not a landing"), Ball->ReportGrounded());
    TestTrue(TEXT("The receiver falls on the fumble"), Ball->ResolveTouch(WR));
    TestTrue(TEXT("The recovery is on the bus"), Recoveries.Num() == 1 && Recoveries[0].RecoveryName == Receiver.DisplayName);
    TestEqual(TEXT("The simulation makes him a ball carrier"), Sim->GetPlayState().Phase, EPlayPhase::BallCarrierMovement);

    // 4. An interception: a turnover the simulation rules, and the intended receiver goes down.
    NextSnap(Sim);
    WR->LosePossession();
    Throw(Bus, Ball, Passer, Receiver);
    TestTrue(TEXT("The corner picks it off"), Ball->ResolveTouch(CB));
    TestTrue(TEXT("The pick is on the bus"), Catches.Num() == 2 && Catches[1].bIsInterception && Catches[1].ReceiverName == Corner.DisplayName);
    TestEqual(TEXT("The simulation rules a turnover"), Sim->GetPlayResult().ResultType, EPlayResultType::Interception);
    TestTrue(TEXT("The intended receiver is downed (Epic 140)"), WR->GetHealthComponent() && WR->GetHealthComponent()->IsDowned());
    TestTrue(TEXT("...as the bus hears"), Deaths.ContainsByPredicate([&Receiver](const FPSTelemetryDeathEvent& Death)
    {
        return Death.PlayerName == Receiver.DisplayName && Death.Cause == EPSDeathCause::InterceptionPunishment;
    }));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Every change of phase is announced
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPhaseAnnouncementTest,
    "PlaySports.C3.EveryPhaseChangeIsAnnounced",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPhaseAnnouncementTest::RunTest(const FString& Parameters)
{
    FPlayState State;
    State.Phase = EPlayPhase::PreSnap;
    State.GameClockSeconds = 600.f;
    State.PlayClockSeconds = 20.f;
    FPSTelemetryPhaseChangeEvent Announcement;
    TestFalse(TEXT("Nothing changed: nothing to announce"), PSGameStateEvents::MakePhaseChange(EPlayPhase::PreSnap, State, Announcement));

    // A tackle between ticks took the play from its carrier phase straight to the whistle.
    State.Phase = EPlayPhase::Scoring;
    if (TestTrue(TEXT("The whistle a bus event blew is announced"), PSGameStateEvents::MakePhaseChange(EPlayPhase::BallCarrierMovement, State, Announcement)))
    {
        TestEqual(TEXT("...from the phase last announced"), Announcement.OldPhase, FString(TEXT("BallCarrierMovement")));
        TestEqual(TEXT("...to the simulation's"), Announcement.NewPhase, FString(TEXT("Scoring")));
        TestEqual(TEXT("...with the clocks"), Announcement.GameClockSeconds, 600.f);
        TestEqual(TEXT("...both of them"), Announcement.PlayClockSeconds, 20.f);
    }

    // The snap, which the game mode makes after the simulation's tick, is announced on the next.
    State.Phase = EPlayPhase::Snap;
    TestTrue(TEXT("The snap is announced"), PSGameStateEvents::MakePhaseChange(EPlayPhase::PreSnap, State, Announcement)
        && Announcement.OldPhase == TEXT("PreSnap") && Announcement.NewPhase == TEXT("Snap"));
    State.Phase = EPlayPhase::Kickoff;
    TestTrue(TEXT("...a kickoff by its name"), PSGameStateEvents::MakePhaseChange(EPlayPhase::PreSnap, State, Announcement) && Announcement.NewPhase == TEXT("Kickoff"));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
