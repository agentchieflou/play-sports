// PSSkillPlayerAITests.cpp -- Epic 14 (skill-position behavior that actually moves)
//
// Tests covered:
//   1. Receivers run their route waypoint by waypoint and settle at the end; a player with
//      no route blocks the rusher nearest the QB; linemen are left to the engagement system;
//      nothing moves once the play is over.
//   2. The QB reads: before MinReadSeconds he holds, then throws to the most open receiver,
//      who converges on the ball; Awareness changes how open a receiver must look.
//   3. Pressure with nobody open: the QB scrambles, upfield and away from the rusher.
//   4. A run play: the QB hands off to the back, who carries through the line's widest gap.
//   5. The lineup: offense behind the ball, defense across from it, each side with its own
//      AI controller; a route with no RouteId sends the QB to his drop.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSDefenseController.h"
#include "PSFieldGrid.h"
#include "PSOffenseController.h"
#include "PSPlayOrchestrator.h"
#include "PSPlayerPawn.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSSkillPlayerAITests
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

    /** A pawn at Location under its side's AI controller; offense AIs are bound to the bus. */
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
            }
        }
        else if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
            AI->GetSkillAI()->BindToBus();
        }
        return Pawn;
    }

    static UPSSkillPlayerAIComponent* AIOf(APSPlayerPawn* Pawn)
    {
        const APSOffenseController* Controller = Pawn ? Cast<APSOffenseController>(Pawn->GetController()) : nullptr;
        return Controller ? Controller->GetSkillAI() : nullptr;
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

    /**
     * The snap, then the offense's call. On a snap outside a call window the play-call
     * subsystem calls a CPU play and hands out its routes, so each test clears those and
     * announces its own call, then sets exactly the routes its scenario needs before the
     * first tick (when the AI takes up its assignment).
     */
    static void StartPlay(UWorld* World, UPSTelemetryBus* Bus, const TCHAR* Category, const FVector& LineOfScrimmage)
    {
        FPSTelemetrySnapEvent Snap;
        Snap.LineOfScrimmage = LineOfScrimmage;
        Bus->PublishSnap(Snap);

        for (TActorIterator<APSOffenseController> It(World); It; ++It)
        {
            It->SetAssignedRoute(TArray<FVector>());
        }

        FPSTelemetryPlayCallEvent Call;
        Call.bOffense = true;
        Call.PlayCategory = Category;
        Bus->PublishPlayCall(Call);
    }

    static bool PointsToward(const FVector& Direction, const FVector& From, const FVector& To)
    {
        FVector Expected = To - From;
        Expected.Z = 0.f;
        return Direction.Equals(Expected.GetSafeNormal(), 0.01f);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Route running and blocking
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSRouteRunningTest,
    "PlaySports.AI.RoutesAndBlocking",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRouteRunningTest::RunTest(const FString& Parameters)
{
    using namespace PSSkillPlayerAITests;

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
    APSPlayerPawn* WR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(0.f, 900.f, 100.f));
    APSPlayerPawn* TE = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE"), FVector(0.f, 450.f, 100.f));
    APSPlayerPawn* OL = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL"), FVector(-50.f, 0.f, 100.f));
    APSPlayerPawn* DL = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL"), FVector(250.f, 0.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("WR"), WR) || !TestNotNull(TEXT("TE"), TE) || !TestNotNull(TEXT("OL"), OL) || !TestNotNull(TEXT("DL"), DL))
    {
        DestroyTestWorld(World);
        return false;
    }

    const FVector FirstCut(300.f, 900.f, 100.f);
    const FVector Break(500.f, 500.f, 100.f);
    StartPlay(World, Bus, TEXT("ShortPass"), FVector::ZeroVector);
    Cast<APSOffenseController>(WR->GetController())->SetAssignedRoute({ FirstCut, Break });
    UPSSkillPlayerAIComponent* Receiver = AIOf(WR);
    Receiver->TickAI(0.1f);
    TestTrue(TEXT("The receiver runs his route"), Receiver->GetAction() == EPSSkillPlayerAction::RunRoute);
    TestTrue(TEXT("...toward the first waypoint"), PointsToward(Receiver->GetDesiredDirection(), WR->GetActorLocation(), FirstCut));

    WR->SetActorLocation(FirstCut);
    Receiver->TickAI(0.1f);
    TestTrue(TEXT("Reaching a waypoint turns him toward the next"), PointsToward(Receiver->GetDesiredDirection(), FirstCut, Break));

    WR->SetActorLocation(Break);
    Receiver->TickAI(0.1f);
    TestTrue(TEXT("At the end of the route he settles"), Receiver->GetAction() == EPSSkillPlayerAction::Idle && Receiver->GetDesiredDirection().IsNearlyZero());

    UPSSkillPlayerAIComponent* TightEnd = AIOf(TE);
    TightEnd->TickAI(0.1f);
    TestTrue(TEXT("With no route the tight end blocks"), TightEnd->GetAction() == EPSSkillPlayerAction::Block);
    TestTrue(TEXT("...taking on the rusher nearest the QB"), PointsToward(TightEnd->GetDesiredDirection(), TE->GetActorLocation(), DL->GetActorLocation()));

    UPSSkillPlayerAIComponent* Lineman = AIOf(OL);
    Lineman->TickAI(0.1f);
    TestTrue(TEXT("Linemen are left to the engagement system"), Lineman->GetDesiredDirection().IsNearlyZero());

    FPSTelemetryPhaseChangeEvent Over;
    Over.NewPhase = TEXT("Scoring");
    Bus->PublishPhaseChange(Over);
    TightEnd->TickAI(0.1f);
    TestTrue(TEXT("Nothing moves once the play is over"), TightEnd->GetDesiredDirection().IsNearlyZero() && TightEnd->GetAction() == EPSSkillPlayerAction::Idle);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The QB reads and throws to the open receiver
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSQuarterbackThrowTest,
    "PlaySports.AI.QuarterbackReadsAndThrows",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSQuarterbackThrowTest::RunTest(const FString& Parameters)
{
    using namespace PSSkillPlayerAITests;

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
    APSPlayerPawn* Covered = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_COVERED"), FVector(1000.f, 600.f, 100.f));
    APSPlayerPawn* Open = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_OPEN"), FVector(1000.f, -600.f, 100.f));
    APSPlayerPawn* DB = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB"), FVector(1030.f, 600.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Covered WR"), Covered) || !TestNotNull(TEXT("Open WR"), Open) || !TestNotNull(TEXT("DB"), DB)
        || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryThrowEvent> Throws;
    const FDelegateHandle Handle = Bus->OnThrowMC.AddLambda([&Throws](const FPSTelemetryThrowEvent& Event) { Throws.Add(Event); });

    StartPlay(World, Bus, TEXT("ShortPass"), FVector::ZeroVector);
    UPSSkillPlayerAIComponent* Passer = AIOf(QB);
    const float MinRead = Passer->GetTuning().MinReadSeconds;

    bool bOpen = false;
    float Separation = 0.f;
    TestTrue(TEXT("The QB's best read is the uncovered receiver"), Passer->ChooseReceiver(bOpen, Separation) == Open);
    TestTrue(TEXT("...and he sees him as open"), bOpen);

    Passer->TickAI(MinRead * 0.5f);
    TestEqual(TEXT("He holds the ball until the read time"), Throws.Num(), 0);
    TestTrue(TEXT("...still carrying it"), QB->HasPossession());

    Passer->TickAI(MinRead);
    if (TestEqual(TEXT("Then he throws"), Throws.Num(), 1))
    {
        TestEqual(TEXT("...to the open receiver"), Throws[0].TargetReceiverName, FString(TEXT("WR_OPEN")));
    }
    TestFalse(TEXT("The ball has left his hands"), QB->HasPossession());

    // The receiver drifted off his spot after the throw: he comes back to the ball.
    Open->SetActorLocation(Open->GetActorLocation() + FVector(-300.f, 200.f, 0.f));
    UPSSkillPlayerAIComponent* Target = AIOf(Open);
    Target->TickAI(0.1f);
    TestTrue(TEXT("The receiver tracks the ball"), Target->GetAction() == EPSSkillPlayerAction::TrackBall);
    if (Throws.Num() > 0)
    {
        // A raw passer (Awareness 0) misses his spot by up to 200 cm; the receiver adjusts.
        TestTrue(TEXT("The ball comes down near the spot thrown to"), FVector::Dist2D(Throws[0].LandingLocation, Throws[0].TargetLocation) <= 200.f + KINDA_SMALL_NUMBER);
        TestTrue(TEXT("...and the receiver heads for where it comes down"), PointsToward(Target->GetDesiredDirection(), Open->GetActorLocation(), Throws[0].LandingLocation));
    }

    // Awareness: a receiver 400 cm clear is open to a sharp QB, covered to a raw one.
    APSPlayerPawn* SharpQB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB_SHARP"), FVector(-300.f, 3000.f, 100.f), 100.f);
    APSPlayerPawn* RawQB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB_RAW"), FVector(-300.f, 3000.f, 100.f), 0.f);
    Open->SetActorLocation(FVector(1000.f, 1000.f, 100.f));
    Covered->SetActorLocation(FVector(1000.f, 200.f, 100.f));
    DB->SetActorLocation(FVector(1000.f, 600.f, 100.f));
    bool bSharpOpen = false;
    bool bRawOpen = false;
    AIOf(SharpQB)->ChooseReceiver(bSharpOpen, Separation);
    TestTrue(TEXT("Both receivers are 400 cm from the defender"), FMath::IsNearlyEqual(Separation, 400.f, 1.f));
    AIOf(RawQB)->ChooseReceiver(bRawOpen, Separation);
    TestTrue(TEXT("A sharp QB (Awareness 100) reads 400 cm as open"), bSharpOpen);
    TestFalse(TEXT("A raw QB (Awareness 0) doesn't"), bRawOpen);

    Bus->OnThrowMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Pressure and nobody open: scramble
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSScrambleTest,
    "PlaySports.AI.PressureAndScramble",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSScrambleTest::RunTest(const FString& Parameters)
{
    using namespace PSSkillPlayerAITests;

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
    APSPlayerPawn* WR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(1000.f, 600.f, 100.f));
    APSPlayerPawn* DB = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB"), FVector(1040.f, 600.f, 100.f));
    APSPlayerPawn* Rusher = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL"), FVector(-250.f, 80.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("WR"), WR) || !TestNotNull(TEXT("DB"), DB) || !TestNotNull(TEXT("Rusher"), Rusher)
        || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }

    StartPlay(World, Bus, TEXT("DeepPass"), FVector::ZeroVector);
    UPSSkillPlayerAIComponent* Passer = AIOf(QB);
    Passer->TickAI(0.05f);
    TestTrue(TEXT("Pressured with nobody open, the QB scrambles"), Passer->GetAction() == EPSSkillPlayerAction::CarryBall);
    TestTrue(TEXT("...keeping the ball"), QB->HasPossession());

    Passer->TickAI(0.05f);
    const FVector Direction = Passer->GetDesiredDirection();
    TestTrue(TEXT("He runs upfield"), Direction.X > 0.f);
    TestTrue(TEXT("...and away from the rusher (who is to his right)"), Direction.Y < 0.f);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Run play: hand-off and the run lane
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSHandoffTest,
    "PlaySports.AI.RunPlayHandoff",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSHandoffTest::RunTest(const FString& Parameters)
{
    using namespace PSSkillPlayerAITests;

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
    APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-220.f, 0.f, 100.f));
    // The line opens its widest gap to the right of centre.
    SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL_L"), FVector(-50.f, -150.f, 100.f));
    SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL_C"), FVector(-50.f, 0.f, 100.f));
    SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL_R"), FVector(-50.f, 450.f, 100.f));
    APSBall* Ball = (QB && RB) ? GiveBall(World, QB) : nullptr;
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("RB"), RB) || !TestNotNull(TEXT("Ball"), Ball))
    {
        DestroyTestWorld(World);
        return false;
    }

    StartPlay(World, Bus, TEXT("Run"), FVector::ZeroVector);
    UPSSkillPlayerAIComponent* Passer = AIOf(QB);
    TestTrue(TEXT("The offense knows it's a run"), Passer->IsRunPlay());

    Passer->TickAI(0.1f);
    TestTrue(TEXT("The QB hands off to the back beside him"), RB->HasPossession() && !QB->HasPossession());
    TestTrue(TEXT("...and the ball goes with him"), Ball->GetAttachParentActor() == RB);

    UPSSkillPlayerAIComponent* Back = AIOf(RB);
    Back->TickAI(0.1f);
    TestTrue(TEXT("The back carries"), Back->GetAction() == EPSSkillPlayerAction::CarryBall);
    const FVector Direction = Back->GetDesiredDirection();
    TestTrue(TEXT("...forward"), Direction.X > 0.f);
    TestTrue(TEXT("...into the widest gap, right of centre"), Direction.Y > 0.f);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The lineup and the QB's drop
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLineupTest,
    "PlaySports.AI.LineupBySide",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLineupTest::RunTest(const FString& Parameters)
{
    using namespace PSSkillPlayerAITests;

    const float ScrimmageX = 2500.f;
    const TArray<EPlayerRole> Roles = {
        EPlayerRole::Quarterback, EPlayerRole::RunningBack, EPlayerRole::WideReceiver, EPlayerRole::WideReceiver,
        EPlayerRole::TightEnd, EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman,
        EPlayerRole::DefensiveLineman, EPlayerRole::DefensiveLineman, EPlayerRole::Linebacker, EPlayerRole::DefensiveBack, EPlayerRole::DefensiveBack };
    const TArray<FVector> Lineup = APSFieldGrid::ComputeLineup(Roles, ScrimmageX);
    if (!TestEqual(TEXT("One spot per player"), Lineup.Num(), Roles.Num()))
    {
        return false;
    }

    float LineCentre = 0.f;
    for (int32 Index = 0; Index < Roles.Num(); ++Index)
    {
        const bool bDefense = APSFieldGrid::GetSideForRole(Roles[Index]) == EPSTeamSide::Defense;
        TestTrue(*FString::Printf(TEXT("%s lines up on his side of the ball"), *UEnum::GetValueAsString(Roles[Index])),
            bDefense ? Lineup[Index].X > ScrimmageX : Lineup[Index].X < ScrimmageX);
        if (Roles[Index] == EPlayerRole::OffensiveLineman)
        {
            LineCentre += Lineup[Index].Y;
        }
    }
    TestTrue(TEXT("The line is centred on the ball"), FMath::IsNearlyZero(LineCentre));
    TestTrue(TEXT("The receivers split to both sides"), Lineup[2].Y * Lineup[3].Y < 0.f);
    TestTrue(TEXT("The QB sets up between line and back"), Lineup[5].X > Lineup[0].X && Lineup[0].X > Lineup[1].X);

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world created"), World))
    {
        return false;
    }

    FPlayerAttributes QBRow;
    QBRow.PlayerId = TEXT("QB");
    QBRow.Role = EPlayerRole::Quarterback;
    FPlayerAttributes DBRow;
    DBRow.PlayerId = TEXT("DB");
    DBRow.Role = EPlayerRole::DefensiveBack;
    const TArray<const FPlayerAttributes*> Roster = { &QBRow, &DBRow };
    const TArray<APSPlayerPawn*> Spawned = APSFieldGrid::SpawnPlayersFromRoster(Roster, ScrimmageX, World);
    if (TestEqual(TEXT("Both spawn"), Spawned.Num(), 2))
    {
        TestTrue(TEXT("The offense gets an offense AI"), Spawned[0]->AIControllerClass == APSOffenseController::StaticClass());
        TestTrue(TEXT("The defense gets a defense AI, so defensive calls reach it"), Spawned[1]->AIControllerClass == APSDefenseController::StaticClass());
    }

    // A route with no RouteId is the player's spot: the QB's drop.
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB_DROP"), FVector(ScrimmageX - 100.f, 0.f, 100.f));
    FPSPlayDefinition Play;
    Play.bIsOffensivePlay = true;
    FPSPlayAssignment Drop;
    Drop.Role = EPlayerRole::Quarterback;
    Drop.Kind = EPSAssignmentKind::Route;
    Drop.FormationOffset = FVector(-700.f, 0.f, 0.f);
    Play.Assignments.Add(Drop);
    UDataTable* Routes = NewObject<UDataTable>();
    Routes->RowStruct = FPSRoute::StaticStruct();
    UPSPlayOrchestrator* Orchestrator = NewObject<UPSPlayOrchestrator>();
    const FVector LineOfScrimmage(ScrimmageX, 0.f, 0.f);
    Orchestrator->DistributePlayCall(Play, { QB }, Routes, LineOfScrimmage);
    const APSOffenseController* Controller = QB ? Cast<APSOffenseController>(QB->GetController()) : nullptr;
    if (TestNotNull(TEXT("The QB has an offense AI"), Controller))
    {
        TestEqual(TEXT("The drop is one waypoint"), Controller->GetRouteWaypointCount(), 1);
        TestTrue(TEXT("...seven yards behind the line"), Controller->GetCurrentTargetLocation().Equals(LineOfScrimmage + Drop.FormationOffset));
    }

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
