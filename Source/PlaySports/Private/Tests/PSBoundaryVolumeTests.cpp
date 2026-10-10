// PSBoundaryVolumeTests.cpp -- the field's volumes report on the bus; the simulation rules
//
// Tests covered:
//   1. Out of bounds and the end zones go through the bus (rules 5 and 6): the volumes publish a
//      BoundaryCrossed event and no longer call the play simulation, which rules on it once.
//      A carrier out of bounds is down at the spot, his yards from the line of scrimmage; a
//      second crossing after the whistle changes nothing; a pawn without the ball and a carried
//      ball report nothing; the ball alone out of bounds is dead, an incompletion; a carrier in
//      his own end zone scores nothing, in the far one he scores; an interceptor carried into the
//      offense's end zone scores for the defense.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSEndZoneVolume.h"
#include "PSOutOfBoundsVolume.h"
#include "PSPlaySimulation.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSBoundaryVolumeTests
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
        Player.Speed = 80.f;
        Player.Agility = 80.f;
        Player.Strength = 80.f;
        Player.Acceleration = 80.f;
        Player.Awareness = 80.f;
        Player.Stamina = 100.f;
        return Player;
    }

    template <typename ActorType>
    static ActorType* Spawn(UWorld* World, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<ActorType>(ActorType::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
    }

    static APSPlayerPawn* SpawnCarrier(UWorld* World, const FPlayerAttributes& Player, float X)
    {
        APSPlayerPawn* Pawn = Spawn<APSPlayerPawn>(World, FVector(X, 0.f, 100.f));
        if (Pawn)
        {
            Pawn->InitializePlayer(Player);
            Pawn->GainPossession();
        }
        return Pawn;
    }

    /** A simulation on World's bus, snapped with no flag down. */
    static UPSPlaySimulation* SnapNewSimulation(UWorld* World, const TArray<FPlayerAttributes>& Offense, const TArray<FPlayerAttributes>& Defense)
    {
        UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
        Sim->InitializePlay(Offense, Defense);
        Sim->InitializeWithWorld(World);
        Sim->TriggerSnap();
        Sim->ActivePenalty = EPSPenaltyType::None;
        return Sim;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSBoundaryVolumesBusTest,
    "PlaySports.C3.BoundaryVolumesReportThroughTheBus",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSBoundaryVolumesBusTest::RunTest(const FString& Parameters)
{
    using namespace PSBoundaryVolumeTests;

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
    TArray<FPSTelemetryBoundaryCrossedEvent> Crossings;
    Bus->OnBoundaryCrossedMC.AddLambda([&Crossings](const FPSTelemetryBoundaryCrossedEvent& Event) { Crossings.Add(Event); });

    // The home offense at its 20 (the line of scrimmage at X = 2000 cm) against the away defense.
    const TArray<FPlayerAttributes> Offense = {
        MakePlayer(TEXT("HOME_QB"), TEXT("Home Passer"), EPlayerRole::Quarterback),
        MakePlayer(TEXT("HOME_RB"), TEXT("Home Runner"), EPlayerRole::RunningBack) };
    const TArray<FPlayerAttributes> Defense = {
        MakePlayer(TEXT("AWAY_DB"), TEXT("Away Corner"), EPlayerRole::DefensiveBack),
        MakePlayer(TEXT("AWAY_LB"), TEXT("Away Backer"), EPlayerRole::Linebacker) };
    APSOutOfBoundsVolume* Sideline = Spawn<APSOutOfBoundsVolume>(World, FVector(5000.f, 3700.f, 250.f));
    APSEndZoneVolume* EndZone = Spawn<APSEndZoneVolume>(World, FVector(10500.f, 0.f, 250.f));
    APSPlayerPawn* Runner = SpawnCarrier(World, Offense[1], 3200.f);
    APSPlayerPawn* Backer = Spawn<APSPlayerPawn>(World, FVector(3300.f, 0.f, 100.f));
    if (!TestTrue(TEXT("The volumes and the players"), Sideline && EndZone && Runner && Backer))
    {
        DestroyTestWorld(World);
        return false;
    }
    Backer->InitializePlayer(Defense[1]);

    // 1. The runner steps out at the 32: down there, 12 yards from the line.
    UPSPlaySimulation* Sim = SnapNewSimulation(World, Offense, Defense);
    TestFalse(TEXT("A player without the ball reports nothing"), Sideline->ReportCrossing(Backer));
    TestTrue(TEXT("The carrier steps out"), Sideline->ReportCrossing(Runner));
    if (TestEqual(TEXT("One crossing on the bus"), Crossings.Num(), 1))
    {
        TestEqual(TEXT("...naming the carrier"), Crossings[0].CarrierName, Offense[1].DisplayName);
        TestTrue(TEXT("...out of bounds at the 32"), !Crossings[0].bEndZone && Crossings[0].YardLine == 32);
    }
    TestEqual(TEXT("The simulation blows the whistle"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);
    TestTrue(TEXT("...down out of bounds"), Sim->GetPlayResult().ResultType == EPlayResultType::Tackle && Sim->GetPlayResult().bOutOfBounds);
    TestEqual(TEXT("...12 yards from the line of scrimmage"), Sim->GetPlayResult().YardsGained, 12);

    // Crossings after the whistle change nothing: he is down, even carried on into the end zone.
    Sideline->ReportCrossing(Runner);
    Runner->SetActorLocation(FVector(10050.f, 0.f, 100.f));
    EndZone->ReportCrossing(Runner);
    TestEqual(TEXT("The later crossings are on the bus"), Crossings.Num(), 3);
    TestTrue(TEXT("...but the play stands: 12 yards, no touchdown"),
        Sim->GetPlayResult().ResultType == EPlayResultType::Tackle && Sim->GetPlayResult().YardsGained == 12);
    Sim->EndPlayAndPrepareNext();
    TestTrue(TEXT("1st and 10 at the 32"), Sim->GetPlayState().YardLine == 32 && Sim->GetPlayState().Down == 1);

    // 2. A pass sails out of bounds: the ball alone is dead, an incompletion. A carried ball
    //    goes out with its carrier, who reports it.
    APSBall* Ball = Spawn<APSBall>(World, FVector(4500.f, 3600.f, 100.f));
    if (!TestNotNull(TEXT("The ball"), Ball))
    {
        DestroyTestWorld(World);
        return false;
    }
    Sim->TriggerSnap();
    Sim->ActivePenalty = EPSPenaltyType::None;
    const int32 Before = Crossings.Num();
    TestTrue(TEXT("The ball goes out"), Sideline->ReportCrossing(Ball));
    TestTrue(TEXT("...reported as the ball, with no carrier"), Crossings.Num() == Before + 1 && Crossings.Last().CarrierName.IsEmpty());
    TestEqual(TEXT("The ball is dead"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);
    TestEqual(TEXT("...an incompletion"), Sim->GetPlayResult().ResultType, EPlayResultType::Incomplete);
    Sim->EndPlayAndPrepareNext();
    TestTrue(TEXT("2nd and 10, still at the 32"), Sim->GetPlayState().YardLine == 32 && Sim->GetPlayState().Down == 2);
    Ball->AttachToCarrier(Runner);
    TestFalse(TEXT("A carried ball reports nothing"), Sideline->ReportCrossing(Ball));

    // 3. The runner in his own end zone scores nothing; in the far one he scores.
    Sim->TriggerSnap();
    Sim->ActivePenalty = EPSPenaltyType::None;
    Runner->SetActorLocation(FVector(-50.f, 0.f, 100.f));
    TestTrue(TEXT("Into his own end zone"), EndZone->ReportCrossing(Runner));
    TestTrue(TEXT("...at his own goal line"), Crossings.Last().bEndZone && Crossings.Last().YardLine == 0);
    TestEqual(TEXT("...the play goes on"), Sim->GetPlayState().Phase, EPlayPhase::Snap);
    Runner->SetActorLocation(FVector(10050.f, 0.f, 100.f));
    TestTrue(TEXT("Into the end zone he attacks"), EndZone->ReportCrossing(Runner));
    TestTrue(TEXT("...at the goal line, the 100"), Crossings.Last().bEndZone && Crossings.Last().YardLine == 100);
    TestEqual(TEXT("A touchdown"), Sim->GetPlayResult().ResultType, EPlayResultType::Touchdown);
    Sim->EndPlayAndPrepareNext();
    TestTrue(TEXT("Six for the home team, or seven with the try"), Sim->GetPlayState().HomeScore == 6 || Sim->GetPlayState().HomeScore == 7);

    DestroyTestWorld(World);

    // 4. A pick carried into the offense's end zone: the defense's touchdown (a game of its own).
    UWorld* PickWorld = CreateTestWorld();
    UPSTelemetryBus* PickBus = PickWorld ? PickWorld->GetSubsystem<UPSTelemetryBus>() : nullptr;
    APSEndZoneVolume* NearEndZone = PickWorld ? Spawn<APSEndZoneVolume>(PickWorld, FVector(-500.f, 0.f, 250.f)) : nullptr;
    if (!TestTrue(TEXT("A second game's bus and end zone"), PickBus && NearEndZone))
    {
        if (PickWorld)
        {
            DestroyTestWorld(PickWorld);
        }
        return false;
    }
    UPSPlaySimulation* PickSim = SnapNewSimulation(PickWorld, Offense, Defense);
    FPSTelemetryThrowEvent Throw;
    Throw.PasserName = Offense[0].DisplayName;
    Throw.TargetReceiverName = Offense[1].DisplayName;
    PickBus->PublishThrow(Throw);
    FPSTelemetryCatchEvent Pick;
    Pick.ReceiverName = Defense[0].DisplayName;
    Pick.CatchLocation = FVector(3000.f, 0.f, 100.f);
    Pick.bIsInterception = true;
    PickBus->PublishCatch(Pick);
    APSPlayerPawn* Interceptor = SpawnCarrier(PickWorld, Defense[0], -50.f);
    TestTrue(TEXT("The interceptor reaches the offense's end zone"), Interceptor && NearEndZone->ReportCrossing(Interceptor));
    TestEqual(TEXT("His return is over"), PickSim->GetPlayState().Phase, EPlayPhase::Scoring);
    PickSim->EndPlayAndPrepareNext();
    TestTrue(TEXT("Six for the away team, or seven with the try"), PickSim->GetPlayState().AwayScore == 6 || PickSim->GetPlayState().AwayScore == 7);
    TestEqual(TEXT("...and none for the home team"), PickSim->GetPlayState().HomeScore, 0);

    DestroyTestWorld(PickWorld);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
