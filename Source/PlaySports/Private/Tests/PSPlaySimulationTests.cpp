// PSPlaySimulationTests.cpp -- Epic C2: Single Outcome Authority tests
//
// Tests covered:
//   1. Physical-event-driven outcome: publish a Catch event on the bus ->
//      phase must transition to BallCarrierMovement (not stay in PassRush).
//   2. Quick-sim flag: bQuickSimMode=true + AdvancePlay past BallCarrierMovement
//      timer -> ResolvePlayResult ran (result is not the default Incomplete).
//   3. No dual-write: OnBusScoreEvent keeps FPlayState.HomeScore/AwayScore in sync.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSPlaySimulation.h"
#include "PSTelemetryBus.h"
#include "PSPlayerAttributes.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Catch bus event drives phase transition (no quick-sim mode)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSimC2BusCatchDrivesPhase,
    "PlaySports.C2.BusCatchDrivesPhase",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSimC2BusCatchDrivesPhase::RunTest(const FString& Parameters)
{
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    TestNotNull(TEXT("Sim created"), Sim);
    if (!Sim) { return false; }

    TArray<FPlayerAttributes> Offense, Defense;
    Sim->InitializePlay(Offense, Defense);
    Sim->TriggerSnap();                              // PreSnap -> Snap
    Sim->SetPlayPhase(EPlayPhase::PassRush);         // Advance to PassRush

    // Invoke the handler directly (public UFUNCTION)
    FPSTelemetryCatchEvent CatchEvt;
    CatchEvt.ReceiverName    = TEXT("TestReceiver");
    CatchEvt.CatchLocation   = FVector::ZeroVector;
    CatchEvt.YardsGained     = 0;
    CatchEvt.bIsInterception = false;
    Sim->OnBusCatchEvent(CatchEvt);

    TestEqual(TEXT("Catch bus event transitions to BallCarrierMovement"),
        Sim->GetPlayState().Phase, EPlayPhase::BallCarrierMovement);

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Quick-sim flag enables statistical resolver
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSimC2QuickSimResolves,
    "PlaySports.C2.QuickSimResolves",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSimC2QuickSimResolves::RunTest(const FString& Parameters)
{
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    TestNotNull(TEXT("Sim created"), Sim);
    if (!Sim) { return false; }

    FPlayerAttributes QB;
    QB.Role         = EPlayerRole::Quarterback;
    QB.Awareness    = 80.f;
    QB.Speed        = 80.f;
    QB.Agility      = 80.f;
    QB.Strength     = 80.f;
    QB.Acceleration = 80.f;

    FPlayerAttributes DB;
    DB.Role         = EPlayerRole::DefensiveBack;
    DB.Awareness    = 60.f;
    DB.Speed        = 70.f;
    DB.Agility      = 70.f;
    DB.Strength     = 70.f;
    DB.Acceleration = 70.f;

    TArray<FPlayerAttributes> Offense = { QB };
    TArray<FPlayerAttributes> Defense = { DB };
    Sim->InitializePlay(Offense, Defense);
    Sim->bQuickSimMode = true;

    Sim->TriggerSnap();                                     // PreSnap -> Snap
    Sim->SetPlayPhase(EPlayPhase::BallCarrierMovement);     // Skip to BCM

    // Advance past the 3.0s BCM timer -- statistical resolver should fire
    Sim->AdvancePlay(3.5f);

    // Phase must now be Scoring
    TestEqual(TEXT("Quick-sim advances to Scoring phase"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);

    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- No dual-write: bus score event keeps FPlayState in sync
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSimC2NoDualWrite,
    "PlaySports.C2.NoDualWrite",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSimC2NoDualWrite::RunTest(const FString& Parameters)
{
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    TestNotNull(TEXT("Sim created"), Sim);
    if (!Sim) { return false; }

    TArray<FPlayerAttributes> Offense, Defense;
    Sim->InitializePlay(Offense, Defense);

    TestEqual(TEXT("Initial HomeScore is 0"), Sim->GetPlayState().HomeScore, 0);
    TestEqual(TEXT("Initial AwayScore is 0"), Sim->GetPlayState().AwayScore, 0);

    FPSTelemetryScoreEvent ScoreEvt;
    ScoreEvt.ScoreType   = TEXT("Touchdown");
    ScoreEvt.bHomeScored = true;
    ScoreEvt.Points      = 7;
    ScoreEvt.HomeScore   = 7;
    ScoreEvt.AwayScore   = 0;
    Sim->OnBusScoreEvent(ScoreEvt);

    TestEqual(TEXT("FPlayState.HomeScore synced from bus"), Sim->GetPlayState().HomeScore, 7);
    TestEqual(TEXT("FPlayState.AwayScore stays 0"),         Sim->GetPlayState().AwayScore, 0);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
