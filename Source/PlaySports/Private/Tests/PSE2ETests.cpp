// PSE2ETests.cpp -- Epic 1 Story 5: End-to-end play workflow tests (headless automation)
#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "Misc/AutomationTest.h"
#include "PSPlaySimulation.h"
#include "PSPlayerAttributes.h"
#include "PSDataIngestion.h"
#include "Engine/DataTable.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSE2EPlayWorkflowTest,
    "PlaySports.Epic1.E2EPlayWorkflow",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSE2EPlayWorkflowTest::RunTest(const FString& Parameters)
{
    // 1. Ingest roster
    UDataTable* PlayerRosterTable = NewObject<UDataTable>();
    PlayerRosterTable->RowStruct = FPlayerAttributes::StaticStruct();

    FString FullJsonPath = FPaths::ProjectDir() + TEXT("Data/sample_players.json");
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    TestNotNull(TEXT("Ingestion object created"), Ingestion);
    if (!Ingestion) return false;

    bool bIngestSuccess = Ingestion->LoadPlayerAttributesFromJson(FullJsonPath, PlayerRosterTable);
    TestTrue(TEXT("Roster ingestion succeeded"), bIngestSuccess);

    TArray<FPlayerAttributes*> AllPlayers;
    PlayerRosterTable->GetAllRows<FPlayerAttributes>(TEXT("E2ETests"), AllPlayers);
    TestTrue(TEXT("Roster contains players"), AllPlayers.Num() >= 2);

    // Separate players
    TArray<FPlayerAttributes> OffenseRoster;
    TArray<FPlayerAttributes> DefenseRoster;
    for (FPlayerAttributes* Player : AllPlayers)
    {
        if (Player)
        {
            if (Player->Role == EPlayerRole::Quarterback ||
                Player->Role == EPlayerRole::RunningBack ||
                Player->Role == EPlayerRole::WideReceiver ||
                Player->Role == EPlayerRole::TightEnd ||
                Player->Role == EPlayerRole::OffensiveLineman)
            {
                OffenseRoster.Add(*Player);
            }
            else
            {
                DefenseRoster.Add(*Player);
            }
        }
    }

    // 2. Initialize Play Simulation
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    TestNotNull(TEXT("Sim created"), Sim);
    if (!Sim) return false;

    Sim->InitializePlay(OffenseRoster, DefenseRoster);
    TestEqual(TEXT("Initial phase is PreSnap"), Sim->GetPlayState().Phase, EPlayPhase::PreSnap);

    // 3. Drive simulation phases
    Sim->bQuickSimMode = true;
    Sim->TriggerSnap(); // PreSnap -> Snap

    TestEqual(TEXT("Snapped transitions to Snap phase"), Sim->GetPlayState().Phase, EPlayPhase::Snap);

    // Advance clock/tick
    Sim->AdvancePlay(1.0f); // Snap -> PassRush
    TestEqual(TEXT("Snap transitions to PassRush"), Sim->GetPlayState().Phase, EPlayPhase::PassRush);

    Sim->AdvancePlay(2.0f); // PassRush -> BallCarrierMovement
    TestEqual(TEXT("PassRush transitions to BallCarrierMovement"), Sim->GetPlayState().Phase, EPlayPhase::BallCarrierMovement);

    Sim->AdvancePlay(3.5f); // BallCarrierMovement -> Scoring (Quick-Sim resolves here)
    TestEqual(TEXT("BallCarrierMovement transitions to Scoring"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);

    // 4. Verify result resolved
    FPlayResult Result = Sim->GetPlayResult();
    TestTrue(TEXT("Result resolved and yards recorded"), Result.YardsGained >= -20 && Result.YardsGained <= 100);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
