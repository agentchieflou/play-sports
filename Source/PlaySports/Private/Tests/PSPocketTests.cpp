// PSPocketTests.cpp -- Epic 71 (QB pocket play and scramble) and Epic 17.4 (broken plays)
//
// Tests covered:
//   1. The pocket's shape: edge pressure makes the QB climb (not onto the line), one-sided
//      inside pressure makes him slide away, a blocked rusher presses less; the AI QB moves so.
//   2. The escape and the scramble drill: a collapsed pocket with nobody open sends the QB out,
//      announced on the bus; the receivers on routes break to the drill's spots on his side
//      (deep ones further on), blockers keep blocking; he scrambles across and up.
//   3. Run or throw: scrambling behind the line he throws to a man who comes open.
//   4. Past the line he is a runner, and slides ahead of a closing tackler.
//   5. How a sack ends: a blind-side strip, a legal throwaway, a grounding risk an aware QB
//      takes and a sharper one won't, and a raw QB who just takes the sack.
//   6. Blown coverage: a receiver nobody is near is read whatever his route's timing.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSCarrierMoveComponent.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSPlayOrchestrator.h"
#include "PSPlayerPawn.h"
#include "PSPocketComponent.h"
#include "PSRouteRunnerComponent.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPocketTests
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
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location, float Awareness = 0.f, float Strength = 0.f)
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
        Attributes.Strength = Strength;
        Attributes.Stamina = 100.f;
        Pawn->InitializePlayer(Attributes);

        if (Pawn->TeamSide == EPSTeamSide::Defense)
        {
            if (APSDefenseController* AI = World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
            {
                AI->Possess(Pawn);
            }
        }
        else if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
            AI->GetSkillAI()->BindToBus();
        }
        return Pawn;
    }

    static APSOffenseController* AIOf(const APSPlayerPawn* Pawn)
    {
        return Pawn ? Cast<APSOffenseController>(Pawn->GetController()) : nullptr;
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

    static TArray<APSPlayerPawn*> FieldPawns(UWorld* World)
    {
        TArray<APSPlayerPawn*> Pawns;
        for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
        {
            Pawns.Add(*It);
        }
        return Pawns;
    }

    /** The snap at the origin (the play-call subsystem hands out a CPU play on it), a pass
     *  call, and every offensive AI's route cleared so each test hands out its own. */
    static void StartPlay(UWorld* World, UPSTelemetryBus* Bus)
    {
        FPSTelemetrySnapEvent Snap;
        Bus->PublishSnap(Snap);
        for (TActorIterator<APSOffenseController> It(World); It; ++It)
        {
            It->SetAssignedRoute(TArray<FVector>());
            It->GetRouteRunner()->ClearRoutePlan();
        }
        FPSTelemetryPlayCallEvent Call;
        Call.bOffense = true;
        Call.PlayCategory = TEXT("ShortPass");
        Bus->PublishPlayCall(Call);
    }

    static FPSRoute MakeRoute(const TCHAR* RouteId, const TArray<FVector>& Offsets, const TArray<float>& Seconds)
    {
        FPSRoute Route;
        Route.RouteId = FName(RouteId);
        for (int32 Index = 0; Index < Offsets.Num(); ++Index)
        {
            FPSRouteWaypoint Point;
            Point.Offset = Offsets[Index];
            Point.TimingSeconds = Seconds[Index];
            Route.Waypoints.Add(Point);
        }
        return Route;
    }

    static UDataTable* MakeRouteLibrary()
    {
        UDataTable* Routes = NewObject<UDataTable>();
        Routes->RowStruct = FPSRoute::StaticStruct();
        Routes->AddRow(TEXT("Go"), MakeRoute(TEXT("Go"), { FVector(1500.f, 0.f, 0.f) }, { 2.5f }));
        Routes->AddRow(TEXT("Slant"), MakeRoute(TEXT("Slant"), { FVector(300.f, 0.f, 0.f), FVector(500.f, -400.f, 0.f) }, { 0.6f, 1.2f }));
        return Routes;
    }

    /** Receivers running RouteIds in order, the tight end blocking. */
    static FPSPlayDefinition MakePassPlay(const TArray<FName>& RouteIds)
    {
        FPSPlayDefinition Play;
        Play.bIsOffensivePlay = true;
        Play.PlayCategory = TEXT("ShortPass");
        for (const FName& RouteId : RouteIds)
        {
            FPSPlayAssignment Slot;
            Slot.Role = EPlayerRole::WideReceiver;
            Slot.Kind = EPSAssignmentKind::Route;
            Slot.RouteId = RouteId;
            Play.Assignments.Add(Slot);
        }
        FPSPlayAssignment Block;
        Block.Role = EPlayerRole::TightEnd;
        Block.Kind = EPSAssignmentKind::PassBlock;
        Play.Assignments.Add(Block);
        return Play;
    }

    static bool PointsToward(const FVector& Direction, const FVector& From, const FVector& To)
    {
        FVector Expected = To - From;
        Expected.Z = 0.f;
        return Direction.Equals(Expected.GetSafeNormal(), 0.01f);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Climb and slide
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPocketShapeTest,
    "PlaySports.Pocket.ClimbAndSlide",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPocketShapeTest::RunTest(const FString& Parameters)
{
    using namespace PSPocketTests;

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
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f));
    APSPlayerPawn* LeftEdge = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DE_L"), FVector(-200.f, -280.f, 100.f));
    APSPlayerPawn* RightEdge = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DE_R"), FVector(-200.f, 280.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("DE_L"), LeftEdge) || !TestNotNull(TEXT("DE_R"), RightEdge) || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }
    UPSPocketComponent* Pocket = AIOf(QB)->GetPocket();
    const FVector LineOfScrimmage = FVector::ZeroVector;

    FPSPocketRead Read = Pocket->ReadPocket(QB, FieldPawns(World), LineOfScrimmage);
    TestTrue(TEXT("Rushers off both edges: he climbs"), Read.Move == EPSPocketMove::Climb && Read.Direction.Equals(FVector(1.f, 0.f, 0.f)));
    TestFalse(TEXT("...the pocket still holding"), Read.bCollapsed);

    // The AI QB, reading, steps up as the pocket asks.
    StartPlay(World, Bus);
    UPSSkillPlayerAIComponent* Passer = AIOf(QB)->GetSkillAI();
    Passer->TickAI(0.1f);
    TestTrue(TEXT("The QB reads"), Passer->GetAction() == EPSSkillPlayerAction::ReadDefense);
    TestTrue(TEXT("...stepping up in the pocket"), Passer->GetDesiredDirection().Equals(FVector(1.f, 0.f, 0.f)));

    // One rusher up the middle, from his left: he slides right.
    LeftEdge->SetActorLocation(FVector(-50.f, -150.f, 100.f));
    RightEdge->SetActorLocation(FVector(5000.f, 5000.f, 100.f));
    Read = Pocket->ReadPocket(QB, FieldPawns(World), LineOfScrimmage);
    TestTrue(TEXT("Pressure inside from the left: he slides right"), Read.Move == EPSPocketMove::SlideRight && Read.Direction.Equals(FVector(0.f, 1.f, 0.f)));

    LeftEdge->bIsEngaged = true;
    Read = Pocket->ReadPocket(QB, FieldPawns(World), LineOfScrimmage);
    TestTrue(TEXT("Held by a blocker, the same rusher presses too little to move him"), Read.Move == EPSPocketMove::Hold && Read.Direction.IsNearlyZero());

    // At the front of the pocket he climbs no further.
    LeftEdge->bIsEngaged = false;
    QB->SetActorLocation(FVector(-100.f, 0.f, 100.f));
    LeftEdge->SetActorLocation(FVector(0.f, -280.f, 100.f));
    RightEdge->SetActorLocation(FVector(0.f, 280.f, 100.f));
    Read = Pocket->ReadPocket(QB, FieldPawns(World), LineOfScrimmage);
    TestTrue(TEXT("Near the line he doesn't climb onto it"), Read.Move == EPSPocketMove::Hold);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The escape and the scramble drill (Epic 71 and 17.4)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSScrambleDrillTest,
    "PlaySports.BrokenPlay.ScrambleDrill",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSScrambleDrillTest::RunTest(const FString& Parameters)
{
    using namespace PSPocketTests;

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
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f));
    APSPlayerPawn* Short = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_SHORT"), FVector(-50.f, 900.f, 100.f));
    APSPlayerPawn* Deep = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_DEEP"), FVector(1200.f, -900.f, 100.f));
    // The tight end stays in to block, right by the rusher (too close to throw to).
    APSPlayerPawn* TE = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE"), FVector(-200.f, 250.f, 100.f));
    APSPlayerPawn* Rusher = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL"), FVector(-250.f, 150.f, 100.f));
    SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_SHORT"), FVector(-50.f, 960.f, 100.f));
    SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_DEEP"), FVector(1200.f, -960.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Short"), Short) || !TestNotNull(TEXT("Deep"), Deep) || !TestNotNull(TEXT("TE"), TE)
        || !TestNotNull(TEXT("Rusher"), Rusher) || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryPocketEvent> PocketPlays;
    const FDelegateHandle Handle = Bus->OnPocketMC.AddLambda([&PocketPlays](const FPSTelemetryPocketEvent& Event) { PocketPlays.Add(Event); });

    StartPlay(World, Bus);
    UPSPlayOrchestrator* Orchestrator = NewObject<UPSPlayOrchestrator>();
    Orchestrator->DistributePlayCall(MakePassPlay({ TEXT("Slant"), TEXT("Go") }), { Short, Deep, TE }, MakeRouteLibrary(), FVector::ZeroVector);
    TestEqual(TEXT("The tight end blocks"), AIOf(TE)->GetRouteWaypointCount(), 0);

    // A free rusher on him and both receivers covered: he gets out.
    UPSSkillPlayerAIComponent* Passer = AIOf(QB)->GetSkillAI();
    Passer->TickAI(0.1f);
    TestTrue(TEXT("The pocket collapses with nobody open: he escapes"), Passer->GetAction() == EPSSkillPlayerAction::CarryBall && AIOf(QB)->GetPocket()->IsScrambling());
    TestTrue(TEXT("...with the ball"), QB->HasPossession());
    if (TestEqual(TEXT("The escape is announced"), PocketPlays.Num(), 1))
    {
        TestTrue(TEXT("...as an escape"), PocketPlays[0].Kind == EPSPocketEventKind::Escape);
        TestEqual(TEXT("...away from the rusher (to his left)"), PocketPlays[0].ScrambleSide, -1.f);
        TestEqual(TEXT("...naming him"), PocketPlays[0].DefenderName, FString(TEXT("DL")));
    }

    // The scramble drill, from the orchestrator that handed out the play.
    const FPocketTuningRow& Drill = AIOf(QB)->GetPocket()->GetTuning();
    const FVector ShortSpot = AIOf(Short)->GetCurrentTargetLocation();
    const FVector DeepSpot = AIOf(Deep)->GetCurrentTargetLocation();
    TestEqual(TEXT("The short receiver has one spot"), AIOf(Short)->GetRouteWaypointCount(), 1);
    TestTrue(TEXT("...upfield of the QB"), FMath::Abs(ShortSpot.X - (QB->GetActorLocation().X + Drill.ScrambleDrillDepth)) <= Drill.ScrambleDrillJitter + 1.f);
    TestTrue(TEXT("...on the side he scrambles to"), ShortSpot.Y < QB->GetActorLocation().Y);
    TestTrue(TEXT("The deep receiver keeps going"), DeepSpot.X >= Deep->GetActorLocation().X + Drill.ScrambleDeepRunOn - Drill.ScrambleDrillJitter - 1.f);
    TestTrue(TEXT("...working to that side"), DeepSpot.Y < QB->GetActorLocation().Y);
    TestEqual(TEXT("The blocking tight end keeps blocking"), AIOf(TE)->GetRouteWaypointCount(), 0);

    UPSSkillPlayerAIComponent* ShortBrain = AIOf(Short)->GetSkillAI();
    ShortBrain->TickAI(0.1f);
    TestTrue(TEXT("The receiver runs to his drill spot"), ShortBrain->GetAction() == EPSSkillPlayerAction::RunRoute
        && PointsToward(ShortBrain->GetDesiredDirection(), Short->GetActorLocation(), ShortSpot));

    Passer->TickAI(0.1f);
    const FVector Scramble = Passer->GetDesiredDirection();
    TestTrue(TEXT("Scrambling, he runs across to his left"), Scramble.Y < 0.f);
    TestTrue(TEXT("...and a little upfield"), Scramble.X > 0.f);

    Bus->OnPocketMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Throwing on the run
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSScrambleThrowTest,
    "PlaySports.Pocket.ThrowOnTheRun",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSScrambleThrowTest::RunTest(const FString& Parameters)
{
    using namespace PSPocketTests;

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
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f), 100.f);
    APSPlayerPawn* WR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(500.f, -1500.f, 100.f));
    SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL"), FVector(-250.f, 150.f, 100.f));
    APSPlayerPawn* Corner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB"), FVector(500.f, -1560.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("WR"), WR) || !TestNotNull(TEXT("CB"), Corner) || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryThrowEvent> Throws;
    const FDelegateHandle Handle = Bus->OnThrowMC.AddLambda([&Throws](const FPSTelemetryThrowEvent& Event) { Throws.Add(Event); });

    StartPlay(World, Bus);
    UPSSkillPlayerAIComponent* Passer = AIOf(QB)->GetSkillAI();
    Passer->TickAI(0.1f);
    TestTrue(TEXT("Covered and collapsing: he scrambles"), AIOf(QB)->GetPocket()->IsScrambling() && QB->HasPossession());
    TestEqual(TEXT("...without throwing"), Throws.Num(), 0);

    // The corner loses him: still behind the line, the QB throws on the run.
    Corner->SetActorLocation(FVector(4000.f, 4000.f, 100.f));
    Passer->TickAI(0.1f);
    if (TestEqual(TEXT("Behind the line he throws on the run"), Throws.Num(), 1))
    {
        TestEqual(TEXT("...to the man who came open"), Throws[0].TargetReceiverName, FString(TEXT("WR")));
    }
    TestFalse(TEXT("The ball is gone"), QB->HasPossession());

    Bus->OnThrowMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Past the line: a runner who slides
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSQuarterbackSlideTest,
    "PlaySports.Pocket.SlidePastTheLine",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSQuarterbackSlideTest::RunTest(const FString& Parameters)
{
    using namespace PSPocketTests;

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
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(400.f, 0.f, 100.f));
    APSPlayerPawn* Safety = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("S"), FVector(600.f, 0.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Safety"), Safety) || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryPocketEvent> PocketPlays;
    const FDelegateHandle Handle = Bus->OnPocketMC.AddLambda([&PocketPlays](const FPSTelemetryPocketEvent& Event) { PocketPlays.Add(Event); });

    StartPlay(World, Bus);
    UPSSkillPlayerAIComponent* Passer = AIOf(QB)->GetSkillAI();
    Passer->TickAI(0.1f);
    TestTrue(TEXT("With nobody to throw to and a man on him, he runs"), Passer->GetAction() == EPSSkillPlayerAction::CarryBall);
    Passer->TickAI(0.1f);
    const UPSCarrierMoveComponent* Moves = QB->GetCarrierMoveComponent();
    TestTrue(TEXT("Four yards past the line with the safety closing, he slides"), Moves && Moves->HasGivenUp());
    TestTrue(TEXT("...announced"), PocketPlays.Num() > 0 && PocketPlays.Last().Kind == EPSPocketEventKind::Slide && PocketPlays.Last().DefenderName == TEXT("S"));
    TestTrue(TEXT("...still holding the ball"), QB->HasPossession());

    Bus->OnPocketMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- How a sack ends
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSackEndingsTest,
    "PlaySports.Pocket.SackEndings",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSackEndingsTest::RunTest(const FString& Parameters)
{
    using namespace PSPocketTests;

    const FPocketTuningRow Defaults;
    FPlayerAttributes StrongRusher;
    StrongRusher.Strength = 90.f;
    FPlayerAttributes WeakPasser;
    WeakPasser.Strength = 40.f;
    TestTrue(TEXT("A stronger rusher strips more often"), PSPocket::StripChance(StrongRusher, WeakPasser, Defaults) > Defaults.StripBaseChance);
    TestTrue(TEXT("Inside the tackle box behind the line"), PSPocket::IsInsideTackleBox(FVector(-300.f, 100.f, 0.f), FVector::ZeroVector, Defaults));
    TestFalse(TEXT("...not out wide"), PSPocket::IsInsideTackleBox(FVector(-300.f, 900.f, 0.f), FVector::ZeroVector, Defaults));

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
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f), 70.f);
    APSPlayerPawn* Rusher = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL"), FVector(-400.f, 50.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Rusher"), Rusher) || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryPocketEvent> PocketPlays;
    const FDelegateHandle Handle = Bus->OnPocketMC.AddLambda([&PocketPlays](const FPSTelemetryPocketEvent& Event) { PocketPlays.Add(Event); });
    StartPlay(World, Bus);
    UPSPocketComponent* Pocket = AIOf(QB)->GetPocket();
    const FVector LineOfScrimmage = FVector::ZeroVector;

    // From behind him: a strip (rigged to come out).
    FPocketTuningRow AlwaysStrip = Pocket->GetTuning();
    AlwaysStrip.StripBaseChance = 1.f;
    AlwaysStrip.StripStrengthWeight = 0.f;
    Pocket->SetTuning(AlwaysStrip);
    TestTrue(TEXT("A blind-side rusher strips the ball"), Pocket->ResolveImminentSack(QB, FieldPawns(World), LineOfScrimmage));
    TestFalse(TEXT("...it's out"), QB->HasPossession());
    TestTrue(TEXT("...announced as a strip that worked"), PocketPlays.Num() == 1 && PocketPlays[0].Kind == EPSPocketEventKind::StripAttempt && PocketPlays[0].bSuccess);

    // The rusher in front, in the tackle box, nobody to throw to: a grounding risk.
    Rusher->SetActorLocation(FVector(-200.f, 0.f, 100.f));
    GiveBall(World, QB);
    TestTrue(TEXT("Aware enough to throw it away, not to see the grounding: he throws it"), Pocket->ResolveImminentSack(QB, FieldPawns(World), LineOfScrimmage));
    TestFalse(TEXT("...the ball is gone"), QB->HasPossession());
    TestTrue(TEXT("...announced as grounding for the rules"), PocketPlays.Num() == 2 && PocketPlays[1].Kind == EPSPocketEventKind::IntentionalGrounding);

    FPlayerAttributes Sharp = QB->GetAttributes();
    Sharp.Awareness = 90.f;
    QB->InitializePlayer(Sharp);
    GiveBall(World, QB);
    TestFalse(TEXT("A sharper QB won't ground it: he takes the sack"), Pocket->ResolveImminentSack(QB, FieldPawns(World), LineOfScrimmage));
    TestTrue(TEXT("...ball in hand"), QB->HasPossession());

    // A receiver in range: the throwaway is legal, at his feet.
    APSPlayerPawn* Outlet = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-200.f, 800.f, 100.f));
    TestTrue(TEXT("With a receiver to throw at, he throws it away"), Outlet && Pocket->ResolveImminentSack(QB, FieldPawns(World), LineOfScrimmage));
    TestTrue(TEXT("...a legal throwaway"), PocketPlays.Num() == 3 && PocketPlays[2].Kind == EPSPocketEventKind::Throwaway);

    FPlayerAttributes Raw = QB->GetAttributes();
    Raw.Awareness = 10.f;
    QB->InitializePlayer(Raw);
    GiveBall(World, QB);
    TestFalse(TEXT("A raw QB just takes the sack"), Pocket->ResolveImminentSack(QB, FieldPawns(World), LineOfScrimmage));
    TestTrue(TEXT("...holding on"), QB->HasPossession());

    Bus->OnPocketMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- Blown coverage (17.4)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSBlownCoverageTest,
    "PlaySports.BrokenPlay.BlownCoverageRead",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSBlownCoverageTest::RunTest(const FString& Parameters)
{
    using namespace PSPocketTests;

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
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-500.f, 0.f, 100.f), 100.f);
    APSPlayerPawn* Quick = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_QUICK"), FVector(0.f, 900.f, 100.f));
    APSPlayerPawn* Deep = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_DEEP"), FVector(0.f, -900.f, 100.f));
    // The quick receiver has a man on him; nobody is anywhere near the deep one.
    SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB"), FVector(0.f, 1250.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Quick"), Quick) || !TestNotNull(TEXT("Deep"), Deep) || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryThrowEvent> Throws;
    const FDelegateHandle Handle = Bus->OnThrowMC.AddLambda([&Throws](const FPSTelemetryThrowEvent& Event) { Throws.Add(Event); });

    StartPlay(World, Bus);
    UPSPlayOrchestrator* Orchestrator = NewObject<UPSPlayOrchestrator>();
    Orchestrator->DistributePlayCall(MakePassPlay({ TEXT("Slant"), TEXT("Go") }), { Quick, Deep }, MakeRouteLibrary(), FVector::ZeroVector);

    // At the slant's read the go's read isn't up -- but his coverage is blown.
    UPSSkillPlayerAIComponent* Passer = AIOf(QB)->GetSkillAI();
    Passer->TickAI(0.7f);
    if (TestEqual(TEXT("The QB throws"), Throws.Num(), 1))
    {
        TestEqual(TEXT("...to the man whose coverage is blown, whatever the timing"), Throws[0].TargetReceiverName, FString(TEXT("WR_DEEP")));
    }

    Bus->OnThrowMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
