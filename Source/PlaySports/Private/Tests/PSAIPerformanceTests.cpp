// PSAIPerformanceTests.cpp -- the AI's work per frame (Epic 17.5)
//
// Tests covered:
//   1. Twenty-two AI players deciding in one frame -- every skill player, defender, pass rusher
//      and the gap accounting -- scan the field once between them, through the shared
//      UPSAIFieldSnapshot; a new frame or a pawn spawning scans again. The snapshot carries each
//      pawn's role, and possession and positions are read live within the frame.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSAIFieldSnapshot.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenderGapSubsystem.h"
#include "PSDefenseController.h"
#include "PSFieldGrid.h"
#include "PSOffenseController.h"
#include "PSPlayerPawn.h"
#include "PSRushMoveComponent.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSAIPerformanceTests
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

    /** A pawn at Location under its side's AI controller, bound to the bus. */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const FString& PlayerId, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.PlayerId = FName(*PlayerId);
        Attributes.DisplayName = PlayerId;
        Attributes.Role = Role;
        Attributes.Awareness = 100.f;
        Attributes.Stamina = 100.f;
        Pawn->InitializePlayer(Attributes);

        if (Pawn->TeamSide == EPSTeamSide::Defense)
        {
            if (APSDefenseController* AI = World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
            {
                AI->Possess(Pawn);
                AI->GetDefenderAI()->BindToBus();
            }
        }
        else if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
            AI->GetSkillAI()->BindToBus();
        }
        return Pawn;
    }

    /** One frame of every AI on the field deciding: skill players, defenders and their pass
     *  rush, and the gap accounting. */
    static void DecideAll(UWorld* World, const TArray<APSPlayerPawn*>& Players, float DeltaSeconds)
    {
        for (APSPlayerPawn* Player : Players)
        {
            if (const APSDefenseController* Defense = Cast<APSDefenseController>(Player->GetController()))
            {
                Defense->GetDefenderAI()->TickAI(DeltaSeconds);
                Defense->GetRushMoves()->TickRush(DeltaSeconds);
            }
            else if (const APSOffenseController* Offense = Cast<APSOffenseController>(Player->GetController()))
            {
                Offense->GetSkillAI()->TickAI(DeltaSeconds);
            }
        }
        if (UPSDefenderGapSubsystem* Gaps = World->GetSubsystem<UPSDefenderGapSubsystem>())
        {
            Gaps->UpdateFits(DeltaSeconds);
        }
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- One field scan per frame
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSAIOneScanPerFrameTest,
    "PlaySports.AI.Performance.OneFieldScanPerFrame",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSAIOneScanPerFrameTest::RunTest(const FString& Parameters)
{
    using namespace PSAIPerformanceTests;

    TestEqual(TEXT("No world, no field"), UPSAIFieldSnapshot::GetFieldPawns(nullptr).Num(), 0);

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSAIFieldSnapshot* Field = World ? World->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Field snapshot"), Field))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    // Two full elevens, lined up as the game lines them up.
    const TArray<EPlayerRole> Personnel = {
        EPlayerRole::OffensiveLineman, EPlayerRole::Quarterback, EPlayerRole::RunningBack, EPlayerRole::WideReceiver, EPlayerRole::TightEnd,
        EPlayerRole::DefensiveLineman, EPlayerRole::Linebacker, EPlayerRole::DefensiveBack };
    const TArray<int32> Counts = { 5, 1, 1, 3, 1, 4, 3, 4 };
    TArray<EPlayerRole> Roles;
    for (int32 Group = 0; Group < Personnel.Num(); ++Group)
    {
        for (int32 Count = 0; Count < Counts[Group]; ++Count)
        {
            Roles.Add(Personnel[Group]);
        }
    }
    const TArray<FVector> Lineup = APSFieldGrid::ComputeLineup(Roles, 0.f);
    TArray<APSPlayerPawn*> Players;
    APSPlayerPawn* RunningBack = nullptr;
    for (int32 Index = 0; Index < Roles.Num(); ++Index)
    {
        APSPlayerPawn* Player = SpawnPlayer(World, Roles[Index], FString::Printf(TEXT("P%02d"), Index), Lineup[Index]);
        if (!Player)
        {
            AddError(TEXT("A player didn't spawn"));
            DestroyTestWorld(World);
            return false;
        }
        Players.Add(Player);
        RunningBack = Roles[Index] == EPlayerRole::RunningBack ? Player : RunningBack;
    }

    // The snap hands every player his part of a CPU play.
    FPSTelemetrySnapEvent Snap;
    Bus->PublishSnap(Snap);

    // Frame 1: all twenty-two decide.
    Field->Invalidate();
    const int32 ScansBefore = Field->GetScanCount();
    const float Frame = 1.f / 60.f;
    DecideAll(World, Players, Frame);
    TestEqual(TEXT("Twenty-two decisions in a frame scan the field once between them"), Field->GetScanCount() - ScansBefore, 1);
    TestEqual(TEXT("...and the scan holds all twenty-two"), Field->GetPawns().Num(), 22);
    bool bRolesMatch = Field->GetRoles().Num() == Field->GetPawns().Num();
    for (int32 Index = 0; bRolesMatch && Index < Field->GetPawns().Num(); ++Index)
    {
        bRolesMatch = Field->GetRoles()[Index] == Field->GetPawns()[Index]->GetAttributes().Role;
    }
    TestTrue(TEXT("...with each pawn's role"), bRolesMatch);

    // Still frame 1: a hand-off and a move are read live, with no new scan.
    RunningBack->GainPossession();
    RunningBack->SetActorLocation(RunningBack->GetActorLocation() + FVector(0.f, 400.f, 0.f));
    TestTrue(TEXT("The new carrier is found at once"), Field->FindBallCarrier() == RunningBack);
    DecideAll(World, Players, Frame);
    TestEqual(TEXT("...and a second round of decisions in the same frame scans nothing"), Field->GetScanCount() - ScansBefore, 1);
    const APSDefenseController* Linebacker = nullptr;
    for (APSPlayerPawn* Player : Players)
    {
        if (Player->GetAttributes().Role == EPlayerRole::Linebacker)
        {
            Linebacker = Cast<APSDefenseController>(Player->GetController());
            break;
        }
    }
    const EPSDefenderAction LinebackerAction = Linebacker ? Linebacker->GetDefenderAI()->GetAction() : EPSDefenderAction::Idle;
    TestTrue(TEXT("...while the defense reacts to the hand-off"), LinebackerAction == EPSDefenderAction::Fit || LinebackerAction == EPSDefenderAction::Pursue);

    // Frame 2: one more scan.
    Field->Invalidate();
    DecideAll(World, Players, Frame);
    TestEqual(TEXT("The next frame scans once more"), Field->GetScanCount() - ScansBefore, 2);

    // A pawn spawning mid-frame is picked up on the next read.
    APSPlayerPawn* Extra = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("P22"), FVector(450.f, 800.f, 100.f));
    TestTrue(TEXT("A spawned pawn is on the field at the next read"), Extra && Field->GetPawns().Contains(Extra));
    TestEqual(TEXT("...after one more scan"), Field->GetScanCount() - ScansBefore, 3);
    const int32 ExtraIndex = Field->GetPawns().IndexOfByKey(Extra);
    TestTrue(TEXT("...as a linebacker"), ExtraIndex != INDEX_NONE && Field->GetRoles()[ExtraIndex] == EPlayerRole::Linebacker);

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
