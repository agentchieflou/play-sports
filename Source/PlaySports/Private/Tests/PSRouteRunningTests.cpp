// PSRouteRunningTests.cpp -- Epic 68 (route-running nuance model)
//
// Tests covered:
//   1. The release: ratings set the win chance and a roll splits win, delay and reroute; a
//      pressed receiver is held or pushed toward his sideline, an unpressed one just goes.
//   2. Breaks: a stiff receiver rounds his break (turns early), a sharp one cuts at the corner;
//      the separation a break makes and where a route's break and read time are.
//   3. Double moves: at the fake he sells it; the defender on him bites (and freezes) or not.
//   4. Option routes: at the read point a receiver with a man on him breaks away from the
//      man's leverage, one in zone sits down.
//   5. The quarterback's progression follows break timing: a receiver is read only in his
//      window, and nobody between windows unless the QB is out of time.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSPlayOrchestrator.h"
#include "PSPlayerPawn.h"
#include "PSRouteRunnerComponent.h"
#include "PSRouteRunning.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSRouteRunningTests
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
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location, float Agility = 0.f, float Awareness = 0.f)
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
        Attributes.Agility = Agility;
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
            AI->GetSkillAI()->BindToBus();
        }
        return Pawn;
    }

    static APSOffenseController* AIOf(const APSPlayerPawn* Pawn)
    {
        return Pawn ? Cast<APSOffenseController>(Pawn->GetController()) : nullptr;
    }

    static FPSRouteWaypoint Waypoint(float X, float Y, float Seconds, bool bFake = false)
    {
        FPSRouteWaypoint Point;
        Point.Offset = FVector(X, Y, 0.f);
        Point.TimingSeconds = Seconds;
        Point.bFake = bFake;
        return Point;
    }

    static FPSRoute MakeRoute(const TCHAR* RouteId, const TArray<FPSRouteWaypoint>& Waypoints)
    {
        FPSRoute Route;
        Route.RouteId = FName(RouteId);
        Route.Waypoints = Waypoints;
        return Route;
    }

    /** The routes these tests run, as Data/sample_routes.json has them. */
    static UDataTable* MakeRouteLibrary()
    {
        UDataTable* Routes = NewObject<UDataTable>();
        Routes->RowStruct = FPSRoute::StaticStruct();
        Routes->AddRow(TEXT("Go"), MakeRoute(TEXT("Go"), { Waypoint(1500.f, 0.f, 2.5f) }));
        Routes->AddRow(TEXT("Slant"), MakeRoute(TEXT("Slant"), { Waypoint(300.f, 0.f, 0.6f), Waypoint(500.f, -400.f, 1.2f) }));
        Routes->AddRow(TEXT("SlantGo"), MakeRoute(TEXT("SlantGo"), { Waypoint(300.f, 0.f, 0.5f), Waypoint(400.f, -200.f, 0.8f, true), Waypoint(1600.f, -200.f, 2.6f) }));
        FPSRoute Option = MakeRoute(TEXT("Option"), { Waypoint(600.f, 0.f, 1.f) });
        Option.OptionReadWaypoint = 0;
        Option.VsManBranch = TEXT("OptionBreak");
        Option.VsZoneBranch = TEXT("OptionSit");
        Routes->AddRow(TEXT("Option"), Option);
        Routes->AddRow(TEXT("OptionBreak"), MakeRoute(TEXT("OptionBreak"), { Waypoint(100.f, 400.f, 0.5f) }));
        Routes->AddRow(TEXT("OptionSit"), MakeRoute(TEXT("OptionSit"), { Waypoint(-100.f, 0.f, 0.3f) }));
        return Routes;
    }

    /** An offensive play whose receivers run RouteIds in order (one slot each). */
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
        return Play;
    }

    /**
     * The snap (on which the play-call subsystem hands out a CPU play), then a pass call, and
     * every offensive AI's route cleared so each test hands out exactly its own.
     */
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

    static bool PointsToward(const FVector& Direction, const FVector& From, const FVector& To)
    {
        FVector Expected = To - From;
        Expected.Z = 0.f;
        return Direction.Equals(Expected.GetSafeNormal(), 0.01f);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The release against press
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSRouteReleaseTest,
    "PlaySports.Routes.ReleaseContest",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRouteReleaseTest::RunTest(const FString& Parameters)
{
    using namespace PSRouteRunningTests;

    // The model: ratings set the chance, a roll splits the outcomes.
    const FRouteRunningTuningRow Defaults;
    FPlayerAttributes Strong;
    Strong.Agility = 90.f;
    Strong.Strength = 70.f;
    FPlayerAttributes Weak;
    Weak.Agility = 50.f;
    Weak.Strength = 50.f;
    const float StrongChance = PSRouteRunning::ReleaseWinChance(Strong, Weak, Defaults);
    const float WeakChance = PSRouteRunning::ReleaseWinChance(Weak, Strong, Defaults);
    TestTrue(TEXT("A better release rating wins more often"), StrongChance > Defaults.ReleaseBaseWinChance && WeakChance < Defaults.ReleaseBaseWinChance);
    TestTrue(TEXT("A low roll wins"), PSRouteRunning::ResolveRelease(Weak, Strong, 0.f, Defaults) == EPSReleaseOutcome::Win);
    const float DelayCut = WeakChance + (1.f - WeakChance) * Defaults.DelayShare;
    TestTrue(TEXT("Just past the win chance: a delay"), PSRouteRunning::ResolveRelease(Weak, Strong, WeakChance + 0.01f, Defaults) == EPSReleaseOutcome::Delay);
    TestTrue(TEXT("Past the delay share: a reroute"), PSRouteRunning::ResolveRelease(Weak, Strong, DelayCut + 0.01f, Defaults) == EPSReleaseOutcome::Reroute);

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
    APSPlayerPawn* Jammed = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_JAMMED"), FVector(0.f, 900.f, 100.f), 50.f);
    APSPlayerPawn* Pushed = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_PUSHED"), FVector(0.f, -900.f, 100.f), 50.f);
    APSPlayerPawn* Free = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_FREE"), FVector(0.f, 2000.f, 100.f), 50.f);
    SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_RIGHT"), FVector(150.f, 900.f, 100.f));
    SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_LEFT"), FVector(150.f, -900.f, 100.f));
    if (!TestNotNull(TEXT("Jammed"), Jammed) || !TestNotNull(TEXT("Pushed"), Pushed) || !TestNotNull(TEXT("Free"), Free))
    {
        DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryRouteEvent> Contests;
    const FDelegateHandle Handle = Bus->OnRouteRunningMC.AddLambda([&Contests](const FPSTelemetryRouteEvent& Event) { Contests.Add(Event); });

    // Rig the rolls: one receiver always loses to a delay, the other to a reroute.
    FRouteRunningTuningRow AlwaysDelay;
    AlwaysDelay.ReleaseMinWinChance = 0.f;
    AlwaysDelay.ReleaseMaxWinChance = 0.f;
    AlwaysDelay.DelayShare = 1.f;
    FRouteRunningTuningRow AlwaysReroute = AlwaysDelay;
    AlwaysReroute.DelayShare = 0.f;
    AIOf(Jammed)->GetRouteRunner()->SetTuning(AlwaysDelay);
    AIOf(Pushed)->GetRouteRunner()->SetTuning(AlwaysReroute);

    StartPlay(World, Bus);
    UPSPlayOrchestrator* Orchestrator = NewObject<UPSPlayOrchestrator>();
    Orchestrator->DistributePlayCall(MakePassPlay({ TEXT("Go") }), { Jammed, Pushed, Free }, MakeRouteLibrary(), FVector::ZeroVector);
    TestTrue(TEXT("The orchestrator hands each receiver his route's plan"), AIOf(Jammed)->GetRouteRunner()->HasPlan());

    UPSSkillPlayerAIComponent* JammedBrain = AIOf(Jammed)->GetSkillAI();
    JammedBrain->TickAI(0.1f);
    TestTrue(TEXT("The pressed receiver loses to a delay"), AIOf(Jammed)->GetRouteRunner()->GetReleaseOutcome() == EPSReleaseOutcome::Delay);
    TestTrue(TEXT("...and is held at the line"), JammedBrain->GetDesiredDirection().IsNearlyZero());
    JammedBrain->TickAI(AlwaysDelay.DelaySeconds);
    TestTrue(TEXT("Once the jam's time is up he runs his route"), PointsToward(JammedBrain->GetDesiredDirection(), Jammed->GetActorLocation(), FVector(1500.f, 900.f, 0.f)));

    UPSSkillPlayerAIComponent* PushedBrain = AIOf(Pushed)->GetSkillAI();
    PushedBrain->TickAI(0.1f);
    TestTrue(TEXT("The other loses to a reroute"), AIOf(Pushed)->GetRouteRunner()->GetReleaseOutcome() == EPSReleaseOutcome::Reroute);
    TestTrue(TEXT("...his route pushed toward his (left) sideline"),
        AIOf(Pushed)->GetCurrentTargetLocation().Equals(FVector(1500.f, -900.f - AlwaysReroute.RerouteOffset, 0.f)));

    UPSSkillPlayerAIComponent* FreeBrain = AIOf(Free)->GetSkillAI();
    FreeBrain->TickAI(0.1f);
    TestTrue(TEXT("Nobody on the free receiver: unpressed"), AIOf(Free)->GetRouteRunner()->GetReleaseOutcome() == EPSReleaseOutcome::Unpressed);
    TestFalse(TEXT("...and he goes at once"), FreeBrain->GetDesiredDirection().IsNearlyZero());

    if (TestEqual(TEXT("Both contests are announced"), Contests.Num(), 2))
    {
        TestTrue(TEXT("...as releases"), Contests[0].Kind == EPSRouteEventKind::Release && Contests[1].Kind == EPSRouteEventKind::Release);
        TestEqual(TEXT("...the delay, naming the presser"), Contests[0].DefenderName, FString(TEXT("CB_RIGHT")));
        TestEqual(TEXT("...its outcome"), Contests[0].Outcome, FName(TEXT("Delay")));
        TestEqual(TEXT("...then the reroute"), Contests[1].Outcome, FName(TEXT("Reroute")));
    }

    Bus->OnRouteRunningMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Break sharpness and the route's timing
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSRouteBreakTest,
    "PlaySports.Routes.BreakSharpness",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRouteBreakTest::RunTest(const FString& Parameters)
{
    using namespace PSRouteRunningTests;

    const FRouteRunningTuningRow Defaults;
    TestEqual(TEXT("A stiff receiver rounds his break by the most"), PSRouteRunning::BreakRounding(0.f, Defaults), Defaults.MaxBreakRounding);
    TestEqual(TEXT("A sharp one not at all"), PSRouteRunning::BreakRounding(100.f, Defaults), 0.f);
    TestTrue(TEXT("A square cut turns 90 degrees"), FMath::IsNearlyEqual(PSRouteRunning::TurnAngleDegrees(FVector::ZeroVector, FVector(300.f, 0.f, 0.f), FVector(300.f, 300.f, 0.f)), 90.f, 0.1f));
    TestEqual(TEXT("Agility on the defender makes separation at the break"), PSRouteRunning::BreakSeparationGain(90.f, 60.f, Defaults), Defaults.BreakSeparationBase + 30.f * Defaults.BreakSeparationPerAgility);
    TestEqual(TEXT("...and a slower-footed receiver makes none"), PSRouteRunning::BreakSeparationGain(40.f, 90.f, Defaults), 0.f);

    UDataTable* Routes = MakeRouteLibrary();
    const FPSRoute* Slant = Routes->FindRow<FPSRoute>(TEXT("Slant"), TEXT("Test"));
    const FPSRoute* Go = Routes->FindRow<FPSRoute>(TEXT("Go"), TEXT("Test"));
    const FPSRoute* SlantGo = Routes->FindRow<FPSRoute>(TEXT("SlantGo"), TEXT("Test"));
    const FPSRoute* Option = Routes->FindRow<FPSRoute>(TEXT("Option"), TEXT("Test"));
    if (TestTrue(TEXT("The routes are in the library"), Slant && Go && SlantGo && Option))
    {
        TestEqual(TEXT("A slant breaks at the end of its stem"), PSRouteRunning::FindBreakWaypoint(*Slant, Defaults), 0);
        TestEqual(TEXT("...and is read then"), PSRouteRunning::ReadTime(*Slant, Routes, Defaults), 0.6f);
        TestEqual(TEXT("A go has no break"), PSRouteRunning::FindBreakWaypoint(*Go, Defaults), static_cast<int32>(INDEX_NONE));
        TestEqual(TEXT("...and is read deep"), PSRouteRunning::ReadTime(*Go, Routes, Defaults), 2.5f);
        TestEqual(TEXT("A double move is read on the go after its fake"), PSRouteRunning::ReadTime(*SlantGo, Routes, Defaults), 2.6f);
        TestEqual(TEXT("An option is read after its quicker branch"), PSRouteRunning::ReadTime(*Option, Routes, Defaults), 1.3f);
    }

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
    APSPlayerPawn* Stiff = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_STIFF"), FVector(0.f, 900.f, 100.f), 0.f);
    APSPlayerPawn* Sharp = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_SHARP"), FVector(0.f, -900.f, 100.f), 100.f);
    if (!TestNotNull(TEXT("Stiff"), Stiff) || !TestNotNull(TEXT("Sharp"), Sharp))
    {
        DestroyTestWorld(World);
        return false;
    }
    TestTrue(TEXT("The route-running tuning loads"), AIOf(Stiff)->GetRouteRunner()->LoadTuningFromJson(UPSRouteRunnerComponent::GetDefaultTuningPath()));

    StartPlay(World, Bus);
    const FVector StiffCorner(300.f, 900.f, 100.f);
    const FVector StiffBreak(500.f, 500.f, 100.f);
    const FVector SharpCorner(300.f, -900.f, 100.f);
    const FVector SharpBreak(500.f, -500.f, 100.f);
    AIOf(Stiff)->SetAssignedRoute({ StiffCorner, StiffBreak });
    AIOf(Sharp)->SetAssignedRoute({ SharpCorner, SharpBreak });
    UPSSkillPlayerAIComponent* StiffBrain = AIOf(Stiff)->GetSkillAI();
    UPSSkillPlayerAIComponent* SharpBrain = AIOf(Sharp)->GetSkillAI();
    StiffBrain->TickAI(0.1f);
    SharpBrain->TickAI(0.1f);
    TestTrue(TEXT("Both run up the stem"), PointsToward(StiffBrain->GetDesiredDirection(), Stiff->GetActorLocation(), StiffCorner)
        && PointsToward(SharpBrain->GetDesiredDirection(), Sharp->GetActorLocation(), SharpCorner));

    // 120 cm short of the corner.
    Stiff->SetActorLocation(StiffCorner - FVector(120.f, 0.f, 0.f));
    Sharp->SetActorLocation(SharpCorner - FVector(120.f, 0.f, 0.f));
    StiffBrain->TickAI(0.1f);
    SharpBrain->TickAI(0.1f);
    TestTrue(TEXT("The stiff receiver rounds it: already turning for the break"), PointsToward(StiffBrain->GetDesiredDirection(), Stiff->GetActorLocation(), StiffBreak));
    TestTrue(TEXT("The sharp one still runs to the corner to cut there"), PointsToward(SharpBrain->GetDesiredDirection(), Sharp->GetActorLocation(), SharpCorner));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Double moves and the bite
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSRouteDoubleMoveTest,
    "PlaySports.Routes.DoubleMoveBite",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRouteDoubleMoveTest::RunTest(const FString& Parameters)
{
    using namespace PSRouteRunningTests;

    const FRouteRunningTuningRow Defaults;
    TestEqual(TEXT("A quick receiver fools a raw defender most"), PSRouteRunning::BiteChance(100.f, 0.f, Defaults), Defaults.BiteMaxChance);
    TestEqual(TEXT("...a sharp defender barely bites"), PSRouteRunning::BiteChance(0.f, 100.f, Defaults), Defaults.BiteMinChance);

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
    APSPlayerPawn* Fooler = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_FOOLS"), FVector(0.f, 900.f, 100.f), 80.f);
    APSPlayerPawn* Fooled = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_BITES"), FVector(600.f, 900.f, 100.f));
    APSPlayerPawn* Stopped = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_READ"), FVector(0.f, -900.f, 100.f), 80.f);
    APSPlayerPawn* Disciplined = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_STAYS"), FVector(600.f, -900.f, 100.f));
    if (!TestNotNull(TEXT("Fooler"), Fooler) || !TestNotNull(TEXT("Fooled"), Fooled) || !TestNotNull(TEXT("Stopped"), Stopped) || !TestNotNull(TEXT("Disciplined"), Disciplined))
    {
        DestroyTestWorld(World);
        return false;
    }

    // Rig the bites: always for one, never for the other.
    FRouteRunningTuningRow AlwaysBite;
    AlwaysBite.BiteMinChance = 1.f;
    AlwaysBite.BiteMaxChance = 1.f;
    FRouteRunningTuningRow NeverBite;
    NeverBite.BiteMinChance = 0.f;
    NeverBite.BiteMaxChance = 0.f;
    AIOf(Fooler)->GetRouteRunner()->SetTuning(AlwaysBite);
    AIOf(Stopped)->GetRouteRunner()->SetTuning(NeverBite);

    TArray<FPSTelemetryRouteEvent> Contests;
    const FDelegateHandle Handle = Bus->OnRouteRunningMC.AddLambda([&Contests](const FPSTelemetryRouteEvent& Event) { Contests.Add(Event); });

    StartPlay(World, Bus);
    UPSPlayOrchestrator* Orchestrator = NewObject<UPSPlayOrchestrator>();
    Orchestrator->DistributePlayCall(MakePassPlay({ TEXT("SlantGo") }), { Fooler, Stopped }, MakeRouteLibrary(), FVector::ZeroVector);

    // Up the stem, then to the fake (the slant), where he sells it.
    UPSSkillPlayerAIComponent* FoolerBrain = AIOf(Fooler)->GetSkillAI();
    FoolerBrain->TickAI(0.1f);
    Fooler->SetActorLocation(FVector(300.f, 900.f, 100.f));
    FoolerBrain->TickAI(0.1f);
    Fooler->SetActorLocation(FVector(400.f, 700.f, 100.f));
    FoolerBrain->TickAI(0.1f);
    TestTrue(TEXT("At the fake he sells it: planted"), FoolerBrain->GetDesiredDirection().IsNearlyZero());
    const UPSDefenderAIComponent* FooledBrain = Cast<APSDefenseController>(Fooled->GetController())->GetDefenderAI();
    TestTrue(TEXT("The corner on him bit, and is frozen"), FooledBrain->IsFrozen());
    if (TestEqual(TEXT("The fake is announced"), Contests.Num(), 1))
    {
        TestTrue(TEXT("...as a double move"), Contests[0].Kind == EPSRouteEventKind::DoubleMove);
        TestEqual(TEXT("...that the corner bit on"), Contests[0].DefenderName, FString(TEXT("CB_BITES")));
        TestEqual(TEXT("...bit"), Contests[0].Outcome, FName(TEXT("Bit")));
        TestEqual(TEXT("...freezing him"), Contests[0].Seconds, AlwaysBite.BiteFreezeSeconds);
    }
    FoolerBrain->TickAI(AlwaysBite.FakeSellSeconds + 0.05f);
    TestTrue(TEXT("Sold, he takes off up the field"), PointsToward(FoolerBrain->GetDesiredDirection(), Fooler->GetActorLocation(), FVector(1600.f, 700.f, 0.f)));

    UPSSkillPlayerAIComponent* StoppedBrain = AIOf(Stopped)->GetSkillAI();
    StoppedBrain->TickAI(0.1f);
    Stopped->SetActorLocation(FVector(300.f, -900.f, 100.f));
    StoppedBrain->TickAI(0.1f);
    Stopped->SetActorLocation(FVector(400.f, -700.f, 100.f));
    StoppedBrain->TickAI(0.1f);
    const UPSDefenderAIComponent* DisciplinedBrain = Cast<APSDefenseController>(Disciplined->GetController())->GetDefenderAI();
    TestFalse(TEXT("A corner who reads it doesn't freeze"), DisciplinedBrain->IsFrozen());
    TestTrue(TEXT("...and the fake says so"), Contests.Num() == 2 && Contests[1].Outcome == FName(TEXT("Stayed")) && Contests[1].Seconds == 0.f);

    Bus->OnRouteRunningMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Option routes read the coverage
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSRouteOptionTest,
    "PlaySports.Routes.OptionRead",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRouteOptionTest::RunTest(const FString& Parameters)
{
    using namespace PSRouteRunningTests;

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
    APSPlayerPawn* VsMan = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_MAN"), FVector(0.f, 900.f, 100.f), 50.f);
    APSPlayerPawn* VsZone = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_ZONE"), FVector(0.f, -900.f, 100.f), 50.f);
    // A corner on the man receiver with outside leverage at the read point.
    SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_OUTSIDE"), FVector(700.f, 1100.f, 100.f));
    if (!TestNotNull(TEXT("VsMan"), VsMan) || !TestNotNull(TEXT("VsZone"), VsZone))
    {
        DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryRouteEvent> Contests;
    const FDelegateHandle Handle = Bus->OnRouteRunningMC.AddLambda([&Contests](const FPSTelemetryRouteEvent& Event) { Contests.Add(Event); });

    StartPlay(World, Bus);
    UPSPlayOrchestrator* Orchestrator = NewObject<UPSPlayOrchestrator>();
    Orchestrator->DistributePlayCall(MakePassPlay({ TEXT("Option") }), { VsMan, VsZone }, MakeRouteLibrary(), FVector::ZeroVector);

    UPSSkillPlayerAIComponent* ManBrain = AIOf(VsMan)->GetSkillAI();
    ManBrain->TickAI(0.1f);
    VsMan->SetActorLocation(FVector(600.f, 900.f, 100.f));
    ManBrain->TickAI(0.1f);
    EPSCoverageRead Read = EPSCoverageRead::Zone;
    TestTrue(TEXT("At the read point he reads"), AIOf(VsMan)->GetRouteRunner()->GetCoverageRead(Read));
    TestTrue(TEXT("...man, with the corner on him"), Read == EPSCoverageRead::Man);
    TestTrue(TEXT("...and breaks away from his outside leverage: inside"), AIOf(VsMan)->GetCurrentTargetLocation().Equals(FVector(700.f, 500.f, 0.f)));
    TestTrue(TEXT("...running there now"), ManBrain->GetDesiredDirection().Y < 0.f);

    UPSSkillPlayerAIComponent* ZoneBrain = AIOf(VsZone)->GetSkillAI();
    ZoneBrain->TickAI(0.1f);
    VsZone->SetActorLocation(FVector(600.f, -900.f, 100.f));
    ZoneBrain->TickAI(0.1f);
    TestTrue(TEXT("With nobody on him he reads zone"), AIOf(VsZone)->GetRouteRunner()->GetCoverageRead(Read) && Read == EPSCoverageRead::Zone);
    TestTrue(TEXT("...and sits down in the hole"), AIOf(VsZone)->GetCurrentTargetLocation().Equals(FVector(500.f, -900.f, 0.f)));
    TestTrue(TEXT("Both reads count as their break"), AIOf(VsMan)->GetRouteRunner()->HasBroken() && AIOf(VsZone)->GetRouteRunner()->HasBroken());

    if (TestEqual(TEXT("Both reads are announced"), Contests.Num(), 2))
    {
        TestTrue(TEXT("...as option reads"), Contests[0].Kind == EPSRouteEventKind::OptionRead);
        TestEqual(TEXT("...man"), Contests[0].Outcome, FName(TEXT("Man")));
        TestEqual(TEXT("...naming the corner"), Contests[0].DefenderName, FString(TEXT("CB_OUTSIDE")));
        TestEqual(TEXT("...and zone"), Contests[1].Outcome, FName(TEXT("Zone")));
    }

    Bus->OnRouteRunningMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The quarterback's reads follow break timing
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSRouteTimingTest,
    "PlaySports.Routes.QuarterbackTiming",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRouteTimingTest::RunTest(const FString& Parameters)
{
    using namespace PSRouteRunningTests;

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
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-500.f, 0.f, 100.f), 0.f, 100.f);
    APSPlayerPawn* Quick = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_QUICK"), FVector(0.f, 900.f, 100.f));
    APSPlayerPawn* Deep = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_DEEP"), FVector(0.f, -900.f, 100.f));
    // The quick receiver is 350 cm clear, the deep one 600 (open, but his coverage isn't blown).
    SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB"), FVector(0.f, 1250.f, 100.f));
    SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_DEEP"), FVector(0.f, -1500.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Quick"), Quick) || !TestNotNull(TEXT("Deep"), Deep))
    {
        DestroyTestWorld(World);
        return false;
    }
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), QB->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);
    if (!TestNotNull(TEXT("Ball"), Ball))
    {
        DestroyTestWorld(World);
        return false;
    }
    Ball->AttachToCarrier(QB, TEXT("HandSocket"));
    QB->GainPossession();

    TArray<FPSTelemetryThrowEvent> Throws;
    const FDelegateHandle Handle = Bus->OnThrowMC.AddLambda([&Throws](const FPSTelemetryThrowEvent& Event) { Throws.Add(Event); });

    StartPlay(World, Bus);
    UPSPlayOrchestrator* Orchestrator = NewObject<UPSPlayOrchestrator>();
    Orchestrator->DistributePlayCall(MakePassPlay({ TEXT("Slant"), TEXT("Go") }), { Quick, Deep }, MakeRouteLibrary(), FVector::ZeroVector);
    TestEqual(TEXT("The slant is read at its break"), AIOf(Quick)->GetRouteRunner()->GetReadTime(), 0.6f);
    TestEqual(TEXT("The go deep"), AIOf(Deep)->GetRouteRunner()->GetReadTime(), 2.5f);

    // Just past the read time: only the slant's read is up, so the QB goes to him although the
    // go is more open.
    UPSSkillPlayerAIComponent* Passer = AIOf(QB)->GetSkillAI();
    bool bOpen = false;
    float Separation = 0.f;
    Passer->TickAI(0.7f);
    if (TestEqual(TEXT("The QB throws on the slant's timing"), Throws.Num(), 1))
    {
        TestEqual(TEXT("...to the slant, not the more open go"), Throws[0].TargetReceiverName, FString(TEXT("WR_QUICK")));
    }

    // Between the two windows nobody's read is up -- unless the QB is out of time.
    Passer->TickAI(0.8f);
    TestNull(TEXT("Between windows there is no read"), Passer->ChooseReceiver(bOpen, Separation));
    TestTrue(TEXT("...but out of time he sees the whole field"), Passer->ChooseReceiver(bOpen, Separation, true) == Deep);

    Passer->TickAI(0.9f);
    TestTrue(TEXT("At the go's break his read is up"), Passer->ChooseReceiver(bOpen, Separation) == Deep && bOpen);

    Bus->OnThrowMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
