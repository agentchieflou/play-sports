// PSDefenderAITests.cpp -- defenders that play their assignment (Epic 15 made live)
//
// Tests covered:
//   1. Rushers go after the passer, a contain rusher aims outside him, a blocked rusher is
//      left to the engagement steering, and nothing moves once the play is over.
//   2. Man coverage: each defender takes the nearest receiver nobody else has, sits on him
//      from a cushion, and (with Awareness) reads where he is going.
//   3. Zone coverage: the defender goes to his zone and shades to a receiver who enters it.
//   4. Run fit: the defender holds, reads a drop-back as a pass and drops (in his pass read,
//      UPSPlayRecognitionSubsystem's, Epic 80), reads a hand-off as a run and pursues -- each as
//      fast as his Awareness lets him.
//   5. The throw: coverage near where it comes down breaks on the ball; after the catch
//      everyone pursues; a defender who takes the ball away returns it.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSPlayRecognitionSubsystem.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSDefenderAITests
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
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location, float Awareness = 0.f)
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
        Attributes.Awareness = Awareness;
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

    static void Assign(APSPlayerPawn* Pawn, EPSDefensiveAssignmentType Assignment, const FVector& Zone = FVector::ZeroVector)
    {
        if (APSDefenseController* Controller = ControllerOf(Pawn))
        {
            Controller->SetAssignment(Assignment, nullptr, Zone);
        }
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

    /** On a snap outside a call window the play-call subsystem calls and hands out CPU
     *  plays, so tests assign defenders after the snap, before their first tick (when the
     *  AI takes the assignment up). */
    static void Snap(UPSTelemetryBus* Bus)
    {
        FPSTelemetrySnapEvent Event;
        Event.LineOfScrimmage = FVector::ZeroVector;
        Bus->PublishSnap(Event);
    }

    static bool PointsToward(const FVector& Direction, const FVector& From, const FVector& To)
    {
        FVector Expected = To - From;
        Expected.Z = 0.f;
        return Direction.Equals(Expected.GetSafeNormal(), 0.01f);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The pass rush
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenderRushTest,
    "PlaySports.AI.Defense.RushAndContain",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenderRushTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenderAITests;

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

    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-200.f, 0.f, 100.f));
    APSPlayerPawn* Rusher = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL_RUSH"), FVector(100.f, 50.f, 100.f));
    APSPlayerPawn* Edge = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL_EDGE"), FVector(100.f, 400.f, 100.f));
    APSPlayerPawn* Blocked = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL_BLOCKED"), FVector(100.f, -300.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Rusher"), Rusher) || !TestNotNull(TEXT("Edge"), Edge) || !TestNotNull(TEXT("Blocked"), Blocked)
        || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }
    Blocked->bIsEngaged = true;

    UPSDefenderAIComponent* RushAI = AIOf(Rusher);
    UPSDefenderAIComponent* EdgeAI = AIOf(Edge);
    UPSDefenderAIComponent* BlockedAI = AIOf(Blocked);
    TestTrue(TEXT("Nobody moves before the snap"), RushAI->GetAction() == EPSDefenderAction::Idle);
    RushAI->TickAI(0.1f);
    TestTrue(TEXT("...not even a rusher"), RushAI->GetDesiredDirection().IsNearlyZero());

    Snap(Bus);
    Assign(Rusher, EPSDefensiveAssignmentType::PassRush);
    Assign(Edge, EPSDefensiveAssignmentType::Contain);
    Assign(Blocked, EPSDefensiveAssignmentType::PassRush);
    RushAI->TickAI(0.1f);
    EdgeAI->TickAI(0.1f);
    BlockedAI->TickAI(0.1f);
    TestTrue(TEXT("The rusher rushes"), RushAI->GetAction() == EPSDefenderAction::Rush);
    TestTrue(TEXT("...at the passer"), PointsToward(RushAI->GetDesiredDirection(), Rusher->GetActorLocation(), QB->GetActorLocation()));
    TestTrue(TEXT("The edge plays contain"), EdgeAI->GetAction() == EPSDefenderAction::Contain);
    TestTrue(TEXT("...aiming outside the passer on his side"),
        PointsToward(EdgeAI->GetDesiredDirection(), Edge->GetActorLocation(), QB->GetActorLocation() + FVector(0.f, EdgeAI->GetTuning().ContainWidth, 0.f)));
    TestTrue(TEXT("A blocked rusher is left to the engagement"), BlockedAI->GetDesiredDirection().IsNearlyZero());

    FPSTelemetryPhaseChangeEvent Over;
    Over.NewPhase = TEXT("Scoring");
    Bus->PublishPhaseChange(Over);
    RushAI->TickAI(0.1f);
    TestTrue(TEXT("Nothing moves once the play is over"), RushAI->GetDesiredDirection().IsNearlyZero() && RushAI->GetAction() == EPSDefenderAction::Idle);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Man coverage
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenderManCoverageTest,
    "PlaySports.AI.Defense.ManCoverage",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenderManCoverageTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenderAITests;

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

    APSPlayerPawn* Right = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_R"), FVector(0.f, 900.f, 100.f));
    APSPlayerPawn* Left = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_L"), FVector(0.f, -900.f, 100.f));
    APSPlayerPawn* TightEnd = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE"), FVector(0.f, 450.f, 100.f));
    APSPlayerPawn* Corner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_R"), FVector(900.f, 900.f, 100.f), 100.f);
    APSPlayerPawn* OtherCorner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_L"), FVector(900.f, -900.f, 100.f));
    APSPlayerPawn* Nickel = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("NB"), FVector(900.f, 880.f, 100.f));
    if (!TestNotNull(TEXT("Right WR"), Right) || !TestNotNull(TEXT("Left WR"), Left) || !TestNotNull(TEXT("TE"), TightEnd)
        || !TestNotNull(TEXT("Corner"), Corner) || !TestNotNull(TEXT("Other corner"), OtherCorner) || !TestNotNull(TEXT("Nickel"), Nickel))
    {
        DestroyTestWorld(World);
        return false;
    }

    Snap(Bus);
    Assign(Corner, EPSDefensiveAssignmentType::ManCoverage);
    Assign(OtherCorner, EPSDefensiveAssignmentType::ManCoverage);
    Assign(Nickel, EPSDefensiveAssignmentType::ManCoverage);
    UPSDefenderAIComponent* CornerAI = AIOf(Corner);
    UPSDefenderAIComponent* OtherCornerAI = AIOf(OtherCorner);
    UPSDefenderAIComponent* NickelAI = AIOf(Nickel);
    CornerAI->TickAI(0.1f);
    OtherCornerAI->TickAI(0.1f);
    NickelAI->TickAI(0.1f);

    TestTrue(TEXT("Each corner takes the receiver across from him"), CornerAI->GetCoveredReceiver() == Right && OtherCornerAI->GetCoveredReceiver() == Left);
    TestTrue(TEXT("The nickel doesn't double the right receiver: he takes the tight end"), NickelAI->GetCoveredReceiver() == TightEnd);
    TestTrue(TEXT("Man coverage"), CornerAI->GetAction() == EPSDefenderAction::Cover);

    const float Cushion = CornerAI->GetTuning().ManCushion;
    TestTrue(TEXT("The corner sits on his man from the cushion"),
        PointsToward(CornerAI->GetDesiredDirection(), Corner->GetActorLocation(), Right->GetActorLocation() + FVector(Cushion, 0.f, 0.f)));

    // The receiver breaks inside: the aware corner reads it, the raw one plays where he is.
    const FVector Break(0.f, -600.f, 0.f);
    Right->GetFloatingMovementComponent()->Velocity = Break;
    Left->GetFloatingMovementComponent()->Velocity = -Break;
    CornerAI->TickAI(0.1f);
    OtherCornerAI->TickAI(0.1f);
    const FVector Read = Break * CornerAI->GetTuning().ManAnticipationSeconds;
    TestTrue(TEXT("Awareness 100 reads the break"),
        PointsToward(CornerAI->GetDesiredDirection(), Corner->GetActorLocation(), Right->GetActorLocation() + Read + FVector(Cushion, 0.f, 0.f)));
    TestTrue(TEXT("Awareness 0 doesn't"),
        PointsToward(OtherCornerAI->GetDesiredDirection(), OtherCorner->GetActorLocation(), Left->GetActorLocation() + FVector(Cushion, 0.f, 0.f)));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Zone coverage
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenderZoneCoverageTest,
    "PlaySports.AI.Defense.ZoneCoverage",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenderZoneCoverageTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenderAITests;

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

    APSPlayerPawn* Receiver = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(0.f, 900.f, 100.f));
    APSPlayerPawn* Safety = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("S"), FVector(900.f, 900.f, 100.f));
    if (!TestNotNull(TEXT("WR"), Receiver) || !TestNotNull(TEXT("Safety"), Safety))
    {
        DestroyTestWorld(World);
        return false;
    }
    const FVector Zone(1200.f, 600.f, 100.f);

    Snap(Bus);
    Assign(Safety, EPSDefensiveAssignmentType::ZoneCoverage, Zone);
    UPSDefenderAIComponent* SafetyAI = AIOf(Safety);
    SafetyAI->TickAI(0.1f);
    TestTrue(TEXT("Zone coverage"), SafetyAI->GetAction() == EPSDefenderAction::Zone);
    TestTrue(TEXT("...the play's zone"), SafetyAI->GetZoneSpot().Equals(Zone));
    TestTrue(TEXT("With nobody in it, he goes to his spot"), PointsToward(SafetyAI->GetDesiredDirection(), Safety->GetActorLocation(), Zone));

    // A receiver comes into the zone: the safety shades toward him.
    const FVector InZone(1300.f, 300.f, 100.f);
    Receiver->SetActorLocation(InZone);
    SafetyAI->TickAI(0.1f);
    const FVector Shade = FMath::Lerp(Zone, InZone, SafetyAI->GetTuning().ZoneShadeWeight);
    TestTrue(TEXT("A receiver in his zone pulls him over"), PointsToward(SafetyAI->GetDesiredDirection(), Safety->GetActorLocation(), Shade));

    // Settled on his spot with the zone empty, he holds.
    Receiver->SetActorLocation(FVector(0.f, -2000.f, 100.f));
    Safety->SetActorLocation(Zone);
    SafetyAI->TickAI(0.1f);
    TestTrue(TEXT("On his spot with the zone empty, he holds"), SafetyAI->GetDesiredDirection().IsNearlyZero());

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Run fit: reading run or pass
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenderRunFitTest,
    "PlaySports.AI.Defense.RunFitReads",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenderRunFitTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenderAITests;

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

    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-100.f, 0.f, 100.f));
    APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-200.f, 0.f, 100.f));
    APSPlayerPawn* Sharp = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB_SHARP"), FVector(450.f, 0.f, 100.f), 100.f);
    APSPlayerPawn* Raw = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB_RAW"), FVector(450.f, 400.f, 100.f), 0.f);
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("RB"), RB) || !TestNotNull(TEXT("Sharp LB"), Sharp) || !TestNotNull(TEXT("Raw LB"), Raw)
        || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }
    UPSDefenderAIComponent* SharpAI = AIOf(Sharp);
    UPSDefenderAIComponent* RawAI = AIOf(Raw);
    const float RawReaction = RawAI->GetReactionSeconds();
    TestTrue(TEXT("Awareness sets the reaction: none at 100"), FMath::IsNearlyZero(SharpAI->GetReactionSeconds()));
    TestTrue(TEXT("...the full delay at 0"), FMath::IsNearlyEqual(RawReaction, RawAI->GetTuning().MaxReactionSeconds));

    // A drop-back: a pass read, so the linebackers drop.
    Snap(Bus);
    Assign(Sharp, EPSDefensiveAssignmentType::RunFit);
    Assign(Raw, EPSDefensiveAssignmentType::RunFit);
    SharpAI->TickAI(0.1f);
    RawAI->TickAI(0.1f);
    TestTrue(TEXT("At the snap the linebacker reads"), SharpAI->GetAction() == EPSDefenderAction::Read && SharpAI->GetDesiredDirection().IsNearlyZero());
    QB->SetActorLocation(FVector(-400.f, 0.f, 100.f));
    SharpAI->TickAI(0.1f);
    RawAI->TickAI(0.1f);
    TestTrue(TEXT("The sharp linebacker reads pass at once and drops"), SharpAI->GetAction() == EPSDefenderAction::Zone);
    TestTrue(TEXT("...to his drop depth"), PointsToward(SharpAI->GetDesiredDirection(), Sharp->GetActorLocation(),
        FVector(SharpAI->GetTuning().PassDropDepth, Sharp->GetActorLocation().Y, 100.f)));
    TestTrue(TEXT("The raw one is still reading"), RawAI->GetAction() == EPSDefenderAction::Read);
    // His pass read is his reaction, stretched by what the look made him expect (Epic 80).
    UPSPlayRecognitionSubsystem* Recognition = UPSPlayRecognitionSubsystem::Get(World);
    const float RawPassRead = Recognition ? Recognition->GetReadTimes(Raw).PassSeconds : RawReaction;
    TestTrue(TEXT("(His pass read is his reaction or longer)"), RawPassRead >= RawReaction);
    RawAI->TickAI(RawPassRead + 0.05f);
    TestTrue(TEXT("...until his pass read catches up"), RawAI->GetAction() == EPSDefenderAction::Zone);

    // Next play, a hand-off: a run read, so the linebackers pursue the back.
    QB->SetActorLocation(FVector(-100.f, 0.f, 100.f));
    Snap(Bus);
    Assign(Sharp, EPSDefensiveAssignmentType::RunFit);
    Assign(Raw, EPSDefensiveAssignmentType::RunFit);
    SharpAI->TickAI(0.1f);
    RawAI->TickAI(0.1f);
    TestTrue(TEXT("The QB hands off"), QB->ExecuteHandoff(RB));
    SharpAI->TickAI(0.1f);
    RawAI->TickAI(0.1f);
    TestTrue(TEXT("The sharp linebacker reads run at once and pursues"), SharpAI->GetAction() == EPSDefenderAction::Pursue);
    TestTrue(TEXT("...the back"), PointsToward(SharpAI->GetDesiredDirection(), Sharp->GetActorLocation(), RB->GetActorLocation()));
    TestTrue(TEXT("The raw one is still reading"), RawAI->GetAction() == EPSDefenderAction::Read);
    RawAI->TickAI(RawReaction + 0.05f);
    TestTrue(TEXT("...until his reaction catches up"), RawAI->GetAction() == EPSDefenderAction::Pursue);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The throw, the catch and the takeaway
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenderBallHawkTest,
    "PlaySports.AI.Defense.BallHawkAndPursuit",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenderBallHawkTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenderAITests;

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

    // An accurate passer (Awareness 100) so the ball comes down where it's thrown.
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f), 100.f);
    APSPlayerPawn* Receiver = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(1000.f, 600.f, 100.f));
    APSPlayerPawn* Near = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_NEAR"), FVector(1300.f, 700.f, 100.f), 100.f);
    APSPlayerPawn* Far = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_FAR"), FVector(1300.f, -2500.f, 100.f), 0.f);
    APSBall* Ball = QB ? GiveBall(World, QB) : nullptr;
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("WR"), Receiver) || !TestNotNull(TEXT("Near DB"), Near) || !TestNotNull(TEXT("Far DB"), Far)
        || !TestNotNull(TEXT("Ball"), Ball))
    {
        DestroyTestWorld(World);
        return false;
    }
    UPSDefenderAIComponent* NearAI = AIOf(Near);
    UPSDefenderAIComponent* FarAI = AIOf(Far);

    Snap(Bus);
    Assign(Near, EPSDefensiveAssignmentType::ZoneCoverage);
    Assign(Far, EPSDefensiveAssignmentType::ZoneCoverage);
    NearAI->TickAI(0.1f);
    FarAI->TickAI(0.1f);
    TestTrue(TEXT("A zone with no offset is the spot he lined up on"), FarAI->GetZoneSpot().Equals(Far->GetActorLocation()));

    TestTrue(TEXT("The QB throws"), QB->ThrowPass(Ball, Receiver->GetActorLocation(), false, Receiver));
    NearAI->TickAI(0.1f);
    FarAI->TickAI(0.1f);
    TestTrue(TEXT("The defender near where it comes down breaks on the ball"), NearAI->GetAction() == EPSDefenderAction::BallHawk);
    TestTrue(TEXT("...heading for it"), PointsToward(NearAI->GetDesiredDirection(), Near->GetActorLocation(), Receiver->GetActorLocation()));
    TestTrue(TEXT("The far one stays in his zone"), FarAI->GetAction() == EPSDefenderAction::Zone);

    // Caught: everyone chases, the far defender once he reacts.
    Receiver->GainPossession();
    FPSTelemetryCatchEvent Catch;
    Catch.ReceiverName = TEXT("WR");
    Catch.CatchLocation = Receiver->GetActorLocation();
    Bus->PublishCatch(Catch);
    NearAI->TickAI(0.1f);
    FarAI->TickAI(0.1f);
    TestTrue(TEXT("After the catch the near defender pursues"), NearAI->GetAction() == EPSDefenderAction::Pursue);
    TestTrue(TEXT("...the receiver"), PointsToward(NearAI->GetDesiredDirection(), Near->GetActorLocation(), Receiver->GetActorLocation()));
    FarAI->TickAI(FarAI->GetReactionSeconds() + 0.05f);
    TestTrue(TEXT("The far defender pursues too, once he reacts"), FarAI->GetAction() == EPSDefenderAction::Pursue);

    // A takeaway: the defender with the ball heads the other way.
    Receiver->LosePossession();
    Near->GainPossession();
    NearAI->TickAI(0.1f);
    TestTrue(TEXT("A defender with the ball returns it"), NearAI->GetAction() == EPSDefenderAction::Return && NearAI->GetDesiredDirection().X < 0.f);

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
