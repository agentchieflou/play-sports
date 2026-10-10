// PSBlownCoverageTests.cpp -- Epic 17.4: the defense reacts to a blown coverage
//
// Tests covered:
//   1. A receiver running free past the line is spotted once; the nearest zone defender is
//      sent, announced on the bus, and takes him in man coverage. A covered receiver, and one
//      not yet past the line, are left alone, as are the man defender and the other zone player.
//   2. Who can't help and when nobody looks: a frozen zone defender is passed over for the next
//      one; with only man defenders left the spotting names no helper; once the ball is thrown,
//      or the quarterback is past the line, the defense stops looking.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSBlownCoverageSubsystem.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSBlownCoverageTests
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

    /** A pawn at Location under its side's AI controller; defense AIs are bound to the bus. */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.PlayerId = FName(PlayerId);
        Attributes.DisplayName = PlayerId;
        Attributes.Role = Role;
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
        }
        return Pawn;
    }

    static APSDefenseController* ControllerOf(APSPlayerPawn* Pawn)
    {
        return Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr;
    }

    static UPSDefenderAIComponent* AIOf(APSPlayerPawn* Pawn)
    {
        const APSDefenseController* Controller = ControllerOf(Pawn);
        return Controller ? Controller->GetDefenderAI() : nullptr;
    }

    static APSBall* GiveBall(UWorld* World, APSPlayerPawn* Carrier)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), Carrier->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);
        if (Ball)
        {
            Ball->AttachToCarrier(Carrier, TEXT("HandSocket"));
            Carrier->GainPossession();
        }
        return Ball;
    }

    /** The snap at the origin. The play-call subsystem hands out a CPU play on it, so tests
     *  assign defenders after it, then tick each defender once to take the assignment up. */
    static void Snap(UPSTelemetryBus* Bus)
    {
        FPSTelemetrySnapEvent Event;
        Event.LineOfScrimmage = FVector::ZeroVector;
        Bus->PublishSnap(Event);
    }

    static void StartAssignment(APSPlayerPawn* Defender, EPSDefensiveAssignmentType Assignment, APSPlayerPawn* Target = nullptr)
    {
        if (APSDefenseController* Controller = ControllerOf(Defender))
        {
            Controller->SetAssignment(Assignment, Target);
            Controller->GetDefenderAI()->TickAI(0.05f);
        }
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The rotation
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCoverageRotationTest,
    "PlaySports.BrokenPlay.CoverageRotation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCoverageRotationTest::RunTest(const FString& Parameters)
{
    using namespace PSBlownCoverageTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSBlownCoverageSubsystem* Coverage = World ? World->GetSubsystem<UPSBlownCoverageSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Blown-coverage subsystem"), Coverage))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    Coverage->SetTuning(FBlownCoverageTuningRow());

    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f));
    APSPlayerPawn* Free = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_FREE"), FVector(800.f, -900.f, 100.f));
    APSPlayerPawn* Covered = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_COVERED"), FVector(800.f, 900.f, 100.f));
    // Nobody near him either, but he hasn't got past the line yet.
    SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB_FLAT"), FVector(100.f, -2200.f, 100.f));
    APSPlayerPawn* Corner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_MAN"), FVector(850.f, 950.f, 100.f));
    APSPlayerPawn* Safety = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("S_NEAR"), FVector(1800.f, -200.f, 100.f));
    APSPlayerPawn* FarSafety = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("S_FAR"), FVector(2000.f, 1000.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Free"), Free) || !TestNotNull(TEXT("Covered"), Covered) || !TestNotNull(TEXT("Corner"), Corner)
        || !TestNotNull(TEXT("Safety"), Safety) || !TestNotNull(TEXT("FarSafety"), FarSafety) || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryBlownCoverageEvent> Spottings;
    const FDelegateHandle Handle = Bus->OnBlownCoverageMC.AddLambda([&Spottings](const FPSTelemetryBlownCoverageEvent& Event) { Spottings.Add(Event); });

    Snap(Bus);
    TestTrue(TEXT("From the snap the defense watches"), Coverage->IsWatching());
    StartAssignment(Corner, EPSDefensiveAssignmentType::ManCoverage, Covered);
    StartAssignment(Safety, EPSDefensiveAssignmentType::ZoneCoverage);
    StartAssignment(FarSafety, EPSDefensiveAssignmentType::ZoneCoverage);
    TestTrue(TEXT("The corner is on his man"), AIOf(Corner)->GetAction() == EPSDefenderAction::Cover && AIOf(Corner)->GetCoveredReceiver() == Covered);
    TestTrue(TEXT("The safeties play zones"), AIOf(Safety)->GetAction() == EPSDefenderAction::Zone && AIOf(FarSafety)->GetAction() == EPSDefenderAction::Zone);

    TestEqual(TEXT("One receiver is running free"), Coverage->CheckCoverage(), 1);
    if (TestEqual(TEXT("...announced"), Spottings.Num(), 1))
    {
        TestEqual(TEXT("...naming him"), Spottings[0].ReceiverName, FString(TEXT("WR_FREE")));
        TestEqual(TEXT("...and the nearest zone defender as the help"), Spottings[0].HelperName, FString(TEXT("S_NEAR")));
        TestTrue(TEXT("...with how free he is"), Spottings[0].Separation >= Coverage->GetTuning().UncoveredSeparation);
    }

    UPSDefenderAIComponent* Helper = AIOf(Safety);
    TestTrue(TEXT("The safety leaves his zone to cover him"), Helper->GetAction() == EPSDefenderAction::Cover && Helper->GetCoveredReceiver() == Free);
    TestTrue(TEXT("The other safety holds his zone"), AIOf(FarSafety)->GetAction() == EPSDefenderAction::Zone);
    TestTrue(TEXT("The corner stays on his man"), AIOf(Corner)->GetCoveredReceiver() == Covered);

    Helper->TickAI(0.05f);
    TestTrue(TEXT("...and heads over to him"), Helper->GetDesiredDirection().Y < 0.f);

    TestEqual(TEXT("A receiver is spotted once a play"), Coverage->CheckCoverage(), 0);
    TestEqual(TEXT("...announced once"), Spottings.Num(), 1);

    Bus->OnBlownCoverageMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Who can't help, and when nobody looks
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCoverageRotationLimitsTest,
    "PlaySports.BrokenPlay.CoverageRotationLimits",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCoverageRotationLimitsTest::RunTest(const FString& Parameters)
{
    using namespace PSBlownCoverageTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSBlownCoverageSubsystem* Coverage = World ? World->GetSubsystem<UPSBlownCoverageSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Blown-coverage subsystem"), Coverage))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    Coverage->SetTuning(FBlownCoverageTuningRow());

    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f));
    APSPlayerPawn* Free = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_FREE"), FVector(800.f, -900.f, 100.f));
    APSPlayerPawn* Other = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_OTHER"), FVector(800.f, 900.f, 100.f));
    APSPlayerPawn* Corner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_MAN"), FVector(850.f, 950.f, 100.f));
    APSPlayerPawn* Bitten = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("S_BIT"), FVector(1800.f, -200.f, 100.f));
    APSPlayerPawn* Backup = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("S_BACKUP"), FVector(2000.f, 1000.f, 100.f));
    APSBall* Ball = QB ? GiveBall(World, QB) : nullptr;
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Free"), Free) || !TestNotNull(TEXT("Other"), Other) || !TestNotNull(TEXT("Corner"), Corner)
        || !TestNotNull(TEXT("Bitten"), Bitten) || !TestNotNull(TEXT("Backup"), Backup) || !TestNotNull(TEXT("Ball"), Ball))
    {
        DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryBlownCoverageEvent> Spottings;
    const FDelegateHandle Handle = Bus->OnBlownCoverageMC.AddLambda([&Spottings](const FPSTelemetryBlownCoverageEvent& Event) { Spottings.Add(Event); });

    // The nearer safety bit on a double move (Epic 68): frozen, he can't help.
    Snap(Bus);
    StartAssignment(Corner, EPSDefensiveAssignmentType::ManCoverage, Other);
    StartAssignment(Bitten, EPSDefensiveAssignmentType::ZoneCoverage);
    StartAssignment(Backup, EPSDefensiveAssignmentType::ZoneCoverage);
    FPSTelemetryRouteEvent Bite;
    Bite.Kind = EPSRouteEventKind::DoubleMove;
    Bite.ReceiverName = TEXT("WR_FREE");
    Bite.DefenderName = TEXT("S_BIT");
    Bite.Outcome = TEXT("Bit");
    Bite.Seconds = 1.f;
    Bus->PublishRouteRunning(Bite);
    TestTrue(TEXT("The safety who bit is frozen"), AIOf(Bitten)->IsFrozen());
    Coverage->CheckCoverage();
    TestTrue(TEXT("The next zone defender is sent instead"), Spottings.Num() == 1 && Spottings[0].HelperName == TEXT("S_BACKUP"));
    TestTrue(TEXT("...and covers him"), AIOf(Backup)->GetCoveredReceiver() == Free);
    TestTrue(TEXT("The frozen one stays in his zone"), AIOf(Bitten)->GetAction() == EPSDefenderAction::Zone);

    // A new play, every defender in man: nobody can leave his man.
    Spottings.Reset();
    Snap(Bus);
    StartAssignment(Corner, EPSDefensiveAssignmentType::ManCoverage, Other);
    StartAssignment(Bitten, EPSDefensiveAssignmentType::ManCoverage, Other);
    StartAssignment(Backup, EPSDefensiveAssignmentType::ManCoverage, Other);
    TestEqual(TEXT("He is still spotted"), Coverage->CheckCoverage(), 1);
    TestTrue(TEXT("...with nobody free to help"), Spottings.Num() == 1 && Spottings[0].HelperName.IsEmpty());
    TestTrue(TEXT("...so nobody leaves his man"), AIOf(Backup)->GetCoveredReceiver() == Other && AIOf(Bitten)->GetCoveredReceiver() == Other);

    // Once the quarterback is past the line, or the ball is in the air, the defense stops looking.
    Spottings.Reset();
    Snap(Bus);
    StartAssignment(Bitten, EPSDefensiveAssignmentType::ZoneCoverage);
    QB->SetActorLocation(FVector(200.f, 0.f, 100.f));
    TestEqual(TEXT("A quarterback past the line: no spotting"), Coverage->CheckCoverage(), 0);
    QB->SetActorLocation(FVector(-300.f, 0.f, 100.f));
    FPSTelemetryThrowEvent Throw;
    Bus->PublishThrow(Throw);
    TestFalse(TEXT("The ball in the air: the defense stops watching"), Coverage->IsWatching());
    TestEqual(TEXT("...and spots nobody"), Coverage->CheckCoverage(), 0);
    TestEqual(TEXT("Nothing was announced"), Spottings.Num(), 0);

    Bus->OnBlownCoverageMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
