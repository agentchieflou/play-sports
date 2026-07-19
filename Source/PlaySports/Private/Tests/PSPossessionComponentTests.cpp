// PSPossessionComponentTests.cpp -- Epic C3: De-God-Class & Orphan Wiring tests
//
// Tests covered:
//   1. Possession transfer: gain -> transfer -> old pawn loses it, new pawn gains it.
//   2. Formation spawn via FieldGrid: SpawnPlayersFromRoster returns the correct pawn
//      count and positions them relative to the scrimmage line.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSPlayerPawn.h"
#include "PSPossessionComponent.h"
#include "PSFieldGrid.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Possession transfer via UPSPossessionComponent
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSC3PossessionTransferTest,
    "PlaySports.C3.PossessionTransfer",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSC3PossessionTransferTest::RunTest(const FString& Parameters)
{
    APSPlayerPawn* Carrier = NewObject<APSPlayerPawn>();
    APSPlayerPawn* Target = NewObject<APSPlayerPawn>();
    TestNotNull(TEXT("Carrier pawn created"), Carrier);
    TestNotNull(TEXT("Target pawn created"), Target);
    if (!Carrier || !Target)
    {
        return false;
    }

    TestFalse(TEXT("Carrier starts without possession"), Carrier->HasPossession());
    TestFalse(TEXT("Target starts without possession"), Target->HasPossession());

    Carrier->GainPossession();
    TestTrue(TEXT("Carrier gained possession"), Carrier->HasPossession());

    bool bTransferred = Carrier->TransferPossessionTo(Target);
    TestTrue(TEXT("Transfer reported success"), bTransferred);
    TestFalse(TEXT("Carrier lost possession after transfer"), Carrier->HasPossession());
    TestTrue(TEXT("Target gained possession after transfer"), Target->HasPossession());

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- APSFieldGrid::SpawnPlayersFromRoster spawns the correct pawn count
//           and positions them relative to the scrimmage line.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSC3FieldGridFormationSpawnTest,
    "PlaySports.C3.FieldGridFormationSpawn",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSC3FieldGridFormationSpawnTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("Test world created"), World);
    if (!World)
    {
        return false;
    }

    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    FPlayerAttributes QB;
    QB.PlayerId = TEXT("QB_TEST");
    QB.DisplayName = TEXT("Test QB");
    QB.Role = EPlayerRole::Quarterback;

    FPlayerAttributes OL;
    OL.PlayerId = TEXT("OL_TEST");
    OL.DisplayName = TEXT("Test OL");
    OL.Role = EPlayerRole::OffensiveLineman;

    TArray<FPlayerAttributes*> Roster = { &QB, &OL };
    const float ScrimmageX = 2000.f;

    TArray<APSPlayerPawn*> SpawnedPawns = APSFieldGrid::SpawnPlayersFromRoster(Roster, ScrimmageX, World);

    TestEqual(TEXT("Spawned pawn count matches roster size"), SpawnedPawns.Num(), Roster.Num());

    for (APSPlayerPawn* Pawn : SpawnedPawns)
    {
        if (TestNotNull(TEXT("Spawned pawn is valid"), Pawn))
        {
            if (Pawn->GetAttributes().Role == EPlayerRole::Quarterback)
            {
                TestTrue(TEXT("QB is spawned behind the scrimmage line"),
                    Pawn->GetActorLocation().X < ScrimmageX);
            }
            else
            {
                TestEqual(TEXT("Non-QB pawn is spawned on the scrimmage line"),
                    static_cast<double>(Pawn->GetActorLocation().X), static_cast<double>(ScrimmageX));
            }
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
