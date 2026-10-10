// PSCoverageMatchupTests.cpp -- Epic 69 (coverage matchup engine)
//
// Tests covered:
//   1. Press: before the snap a man back over a receiver he can jam walks up into press,
//      shaded to his leverage; one who can't plays off. At the snap the receiver's release is
//      contested against him: a won jam has him trailing tight, a beaten one out of phase.
//   2. Leverage: each man matchup plays its shell's side, lost when the receiver crosses the
//      defender's face and regained; a break away from it puts the defender out of phase, one
//      into it doesn't; a fake toward it bites more often.
//   3. Zone hand-offs: a zone defender picks up the receiver in his zone, passes him off
//      underneath, hands him to the deep zone he runs into, and carries him on vertically when
//      nobody deeper is free.
//   4. Safety help: a deep defender stays over the top of the deepest receiver in his area; when
//      the other deep defender leaves, he rotates to the middle; man defenders left over in a
//      man-free call take the deep middle and the robber, who jumps a crosser.
//   5. Pass interference: contact from behind the targeted receiver while the ball is in the air
//      draws a flag (contact while playing the ball doesn't); the play simulation enforces it as
//      a spot foul with a first down, and declines it when the play gained more.
//   6. The shipped tuning loads, is sound, and its shell rules resolve.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSCoverageMatchupSubsystem.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayOrchestrator.h"
#include "PSPlaySimulation.h"
#include "PSPlayerPawn.h"
#include "PSRouteRunnerComponent.h"
#include "PSRouteRunning.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSCoverageMatchupTests
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
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location, float Agility = 50.f, float Strength = 50.f)
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
        Attributes.Strength = Strength;
        Attributes.Awareness = 0.f;
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

    static UPSDefenderAIComponent* DefenseOf(const APSPlayerPawn* Pawn)
    {
        const APSDefenseController* Controller = Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr;
        return Controller ? Controller->GetDefenderAI() : nullptr;
    }

    static APSOffenseController* OffenseOf(const APSPlayerPawn* Pawn)
    {
        return Pawn ? Cast<APSOffenseController>(Pawn->GetController()) : nullptr;
    }

    /** The quarterback behind the line with the ball: coverage matters while he holds it. */
    static APSPlayerPawn* SpawnPasser(UWorld* World)
    {
        APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-100.f, 0.f, 100.f));
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSBall* Ball = QB ? World->SpawnActor<APSBall>(APSBall::StaticClass(), QB->GetActorLocation(), FRotator::ZeroRotator, SpawnParams) : nullptr;
        if (Ball)
        {
            Ball->AttachToCarrier(QB, TEXT("HandSocket"));
            QB->GainPossession();
        }
        return QB;
    }

    /** The defense calls PlayId in an open call window (the offense is left to the CPU). */
    static bool CallDefense(UWorld* World, const TCHAR* PlayId)
    {
        UPSPlayCallSubsystem* PlayCall = World->GetSubsystem<UPSPlayCallSubsystem>();
        if (!PlayCall)
        {
            return false;
        }
        PlayCall->OpenPlayCall(FPSSituationContext());
        return PlayCall->CallPlay(FName(PlayId), EPSPlayCaller::CPU);
    }

    /** The snap at the origin: the play-call subsystem hands both calls out on it. */
    static void Snap(UPSTelemetryBus* Bus)
    {
        FPSTelemetrySnapEvent Event;
        Event.LineOfScrimmage = FVector::ZeroVector;
        Bus->PublishSnap(Event);
    }

    static FPSRouteWaypoint Waypoint(float X, float Y, float Seconds)
    {
        FPSRouteWaypoint Point;
        Point.Offset = FVector(X, Y, 0.f);
        Point.TimingSeconds = Seconds;
        return Point;
    }

    static FPSRoute MakeRoute(const TCHAR* RouteId, const TArray<FPSRouteWaypoint>& Waypoints)
    {
        FPSRoute Route;
        Route.RouteId = FName(RouteId);
        Route.Waypoints = Waypoints;
        return Route;
    }

    static UDataTable* MakeRouteLibrary()
    {
        UDataTable* Routes = NewObject<UDataTable>();
        Routes->RowStruct = FPSRoute::StaticStruct();
        Routes->AddRow(TEXT("Go"), MakeRoute(TEXT("Go"), { Waypoint(1500.f, 0.f, 2.5f) }));
        Routes->AddRow(TEXT("Slant"), MakeRoute(TEXT("Slant"), { Waypoint(300.f, 0.f, 0.6f), Waypoint(500.f, -400.f, 1.2f) }));
        Routes->AddRow(TEXT("Out"), MakeRoute(TEXT("Out"), { Waypoint(500.f, 0.f, 1.f), Waypoint(500.f, 400.f, 1.5f) }));
        return Routes;
    }

    /** After the snap: the test's own pass routes (one slot each, in order) in place of the CPU's call. */
    static void RunRoutes(UPSTelemetryBus* Bus, const TArray<FName>& RouteIds, const TArray<APSPlayerPawn*>& Receivers)
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
        UPSPlayOrchestrator* Orchestrator = NewObject<UPSPlayOrchestrator>();
        Orchestrator->DistributePlayCall(Play, Receivers, MakeRouteLibrary(), FVector::ZeroVector);
        FPSTelemetryPlayCallEvent Call;
        Call.bOffense = true;
        Call.PlayCategory = TEXT("ShortPass");
        Bus->PublishPlayCall(Call);
    }

    static bool PointsToward(const FVector& Direction, const FVector& From, const FVector& To)
    {
        FVector Expected = To - From;
        Expected.Z = 0.f;
        FVector Actual = Direction;
        Actual.Z = 0.f;
        return Actual.GetSafeNormal().Equals(Expected.GetSafeNormal(), 0.01f);
    }

    static int32 CountKind(const TArray<FPSTelemetryCoverageEvent>& Events, EPSCoverageEventKind Kind)
    {
        int32 Count = 0;
        for (const FPSTelemetryCoverageEvent& Event : Events)
        {
            Count += Event.Kind == Kind ? 1 : 0;
        }
        return Count;
    }

    static const FPSTelemetryCoverageEvent* FindKind(const TArray<FPSTelemetryCoverageEvent>& Events, EPSCoverageEventKind Kind, const TCHAR* DefenderName)
    {
        for (const FPSTelemetryCoverageEvent& Event : Events)
        {
            if (Event.Kind == Kind && Event.DefenderName == DefenderName)
            {
                return &Event;
            }
        }
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Press alignment and the jam
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCoverageMatchupPressTest,
    "PlaySports.Coverage.PressAndJam",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCoverageMatchupPressTest::RunTest(const FString& Parameters)
{
    using namespace PSCoverageMatchupTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSCoverageMatchupSubsystem* Matchups = World ? World->GetSubsystem<UPSCoverageMatchupSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Coverage matchup engine"), Matchups))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    // A strong corner over a weak release, a weak corner over a strong one, and a strong slot
    // corner over a receiver who will beat him.
    SpawnPasser(World);
    APSPlayerPawn* Jammed = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_JAMMED"), FVector(-50.f, 900.f, 100.f), 40.f, 40.f);
    APSPlayerPawn* Strong = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_STRONG"), FVector(-50.f, -900.f, 100.f), 95.f, 95.f);
    APSPlayerPawn* Quick = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_QUICK"), FVector(-50.f, 1500.f, 100.f), 50.f, 50.f);
    APSPlayerPawn* Presser = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_PRESS"), FVector(900.f, 900.f, 100.f), 90.f, 90.f);
    APSPlayerPawn* OffCorner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_OFF"), FVector(900.f, -900.f, 100.f), 40.f, 40.f);
    APSPlayerPawn* Beaten = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_BEATEN"), FVector(900.f, 1500.f, 100.f), 90.f, 90.f);
    if (!TestNotNull(TEXT("Jammed"), Jammed) || !TestNotNull(TEXT("Strong"), Strong) || !TestNotNull(TEXT("Quick"), Quick)
        || !TestNotNull(TEXT("Presser"), Presser) || !TestNotNull(TEXT("Off corner"), OffCorner) || !TestNotNull(TEXT("Beaten"), Beaten))
    {
        DestroyTestWorld(World);
        return false;
    }
    TArray<FPSTelemetryCoverageEvent> Contests;
    const FDelegateHandle Handle = Bus->OnCoverageMC.AddLambda([&Contests](const FPSTelemetryCoverageEvent& Event) { Contests.Add(Event); });

    // Man-free presses with outside leverage.
    TestTrue(TEXT("The defense calls nickel man-free"), CallDefense(World, TEXT("Defense_NickelManFree")));
    const FPSCoverageMatchupTuning Settings = Matchups->GetTuning();
    Matchups->UpdateCoverage(0.1f);

    APSPlayerPawn* Planned = nullptr;
    FVector Spot = FVector::ZeroVector;
    TestTrue(TEXT("The strong corner walks up to press"), Matchups->GetPressPlan(Presser, Planned, Spot) && Planned == Jammed);
    const FVector ExpectedSpot(-50.f + Settings.PressDepth, 900.f + Settings.PressShade, 100.f);
    TestTrue(TEXT("...in front of his man, shaded outside"), Spot.Equals(ExpectedSpot, 0.1f));
    TestTrue(TEXT("...and is walking there"), PointsToward(Presser->GetPendingMovementInputVector(), Presser->GetActorLocation(), ExpectedSpot));
    TestNull(TEXT("A corner who would lose the jam plays off"), Matchups->FindPlannedPresser(Strong));
    TestTrue(TEXT("The slot corner presses too"), Matchups->FindPlannedPresser(Quick) == Beaten);

    // In press when the ball is snapped; the receivers run their routes.
    Presser->SetActorLocation(ExpectedSpot);
    Beaten->SetActorLocation(FVector(-50.f + Settings.PressDepth, 1500.f + Settings.PressShade, 100.f));
    FRouteRunningTuningRow AlwaysJammed;
    AlwaysJammed.ReleaseMinWinChance = 0.f;
    AlwaysJammed.ReleaseMaxWinChance = 0.f;
    AlwaysJammed.DelayShare = 1.f;
    FRouteRunningTuningRow AlwaysFree;
    AlwaysFree.ReleaseMinWinChance = 1.f;
    AlwaysFree.ReleaseMaxWinChance = 1.f;
    Snap(Bus);
    OffenseOf(Jammed)->GetRouteRunner()->SetTuning(AlwaysJammed);
    OffenseOf(Quick)->GetRouteRunner()->SetTuning(AlwaysFree);
    RunRoutes(Bus, { TEXT("Go") }, { Jammed, Strong, Quick });

    OffenseOf(Jammed)->GetSkillAI()->TickAI(0.1f);
    OffenseOf(Quick)->GetSkillAI()->TickAI(0.1f);
    TestTrue(TEXT("The pressed receiver is jammed"), OffenseOf(Jammed)->GetRouteRunner()->GetReleaseOutcome() == EPSReleaseOutcome::Delay);
    const FPSTelemetryCoverageEvent* Jam = FindKind(Contests, EPSCoverageEventKind::Press, TEXT("CB_PRESS"));
    TestTrue(TEXT("...by the corner who lined up on him, who won it"), Jam && Jam->ReceiverName == TEXT("WR_JAMMED") && Jam->Outcome == FName(TEXT("Jammed")));
    TestTrue(TEXT("The quick receiver beats his"), OffenseOf(Quick)->GetRouteRunner()->GetReleaseOutcome() == EPSReleaseOutcome::Win);
    const FPSTelemetryCoverageEvent* Whiff = FindKind(Contests, EPSCoverageEventKind::Press, TEXT("CB_BEATEN"));
    TestTrue(TEXT("...who is beaten"), Whiff && Whiff->Outcome == FName(TEXT("Beaten")) && FMath::IsNearlyEqual(Whiff->Seconds, Settings.PressBeatenSeconds));
    TestTrue(TEXT("...and out of phase"), DefenseOf(Beaten)->IsFrozen());

    // Each corner takes the man he lined up on; the leverage is the shell's.
    DefenseOf(Presser)->TickAI(0.1f);
    DefenseOf(OffCorner)->TickAI(0.1f);
    TestTrue(TEXT("The presser covers the man he pressed"), DefenseOf(Presser)->GetCoveredReceiver() == Jammed);
    TestTrue(TEXT("The off corner takes his"), DefenseOf(OffCorner)->GetCoveredReceiver() == Strong);
    Matchups->UpdateCoverage(0.1f);
    TestTrue(TEXT("The presser won his jam"), Matchups->WonJam(Presser, Jammed));

    // Past the jam, the presser trails him tight on his outside; the off corner sits at his
    // cushion, on the outside too.
    Jammed->SetActorLocation(FVector(300.f, 900.f, 100.f));
    DefenseOf(Presser)->TickAI(0.1f);
    TestTrue(TEXT("The presser trails tight, outside"),
        PointsToward(DefenseOf(Presser)->GetDesiredDirection(), Presser->GetActorLocation(), FVector(300.f + Settings.PressCushion, 900.f + Settings.LeverageShade, 100.f)));
    DefenseOf(OffCorner)->TickAI(0.1f);
    const float OffCushion = DefenseOf(OffCorner)->GetTuning().ManCushion;
    TestTrue(TEXT("The off corner plays his cushion, outside"),
        PointsToward(DefenseOf(OffCorner)->GetDesiredDirection(), OffCorner->GetActorLocation(), FVector(-50.f + OffCushion, -900.f - Settings.LeverageShade, 100.f)));

    Bus->OnCoverageMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Leverage and breaks
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCoverageMatchupLeverageTest,
    "PlaySports.Coverage.LeverageAndBreaks",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCoverageMatchupLeverageTest::RunTest(const FString& Parameters)
{
    using namespace PSCoverageMatchupTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSCoverageMatchupSubsystem* Matchups = World ? World->GetSubsystem<UPSCoverageMatchupSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Coverage matchup engine"), Matchups))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    SpawnPasser(World);
    APSPlayerPawn* Right = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_R"), FVector(0.f, 900.f, 100.f), 80.f);
    APSPlayerPawn* Left = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_L"), FVector(0.f, -900.f, 100.f), 80.f);
    APSPlayerPawn* RightCorner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_R"), FVector(700.f, 900.f, 100.f), 40.f);
    APSPlayerPawn* LeftCorner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_L"), FVector(700.f, -900.f, 100.f), 40.f);
    if (!TestNotNull(TEXT("Right"), Right) || !TestNotNull(TEXT("Left"), Left) || !TestNotNull(TEXT("Right corner"), RightCorner) || !TestNotNull(TEXT("Left corner"), LeftCorner))
    {
        DestroyTestWorld(World);
        return false;
    }
    TArray<FPSTelemetryCoverageEvent> Contests;
    const FDelegateHandle Handle = Bus->OnCoverageMC.AddLambda([&Contests](const FPSTelemetryCoverageEvent& Event) { Contests.Add(Event); });

    TestTrue(TEXT("The defense calls nickel man-free"), CallDefense(World, TEXT("Defense_NickelManFree")));
    const FPSCoverageMatchupTuning Settings = Matchups->GetTuning();
    Snap(Bus);
    RunRoutes(Bus, { TEXT("Slant"), TEXT("Out") }, { Right, Left });
    DefenseOf(RightCorner)->TickAI(0.1f);
    DefenseOf(LeftCorner)->TickAI(0.1f);
    Matchups->UpdateCoverage(0.1f);

    EPSLeverage Leverage = EPSLeverage::Inside;
    float Side = 0.f;
    bool bHeld = false;
    TestTrue(TEXT("Each corner and his man are a matchup"), Matchups->GetLeverage(RightCorner, Right, Leverage, Side, bHeld));
    TestTrue(TEXT("...with man-free's outside leverage: right of the right receiver"), Leverage == EPSLeverage::Outside && Side > 0.f && bHeld);
    TestTrue(TEXT("...and left of the left one"), Matchups->GetLeverage(LeftCorner, Left, Leverage, Side, bHeld) && Side < 0.f);
    DefenseOf(RightCorner)->TickAI(0.1f);
    const float Cushion = DefenseOf(RightCorner)->GetTuning().ManCushion;
    TestTrue(TEXT("The corner plays his cushion on the outside shade"),
        PointsToward(DefenseOf(RightCorner)->GetDesiredDirection(), RightCorner->GetActorLocation(), FVector(Cushion, 900.f + Settings.LeverageShade, 100.f)));

    // The receiver crosses the corner's face to the outside: the leverage is his.
    Right->SetActorLocation(FVector(300.f, 900.f + Settings.LeverageLostMargin + 100.f, 100.f));
    Matchups->UpdateCoverage(0.1f);
    TestTrue(TEXT("Crossing his face takes the leverage"), Matchups->GetLeverage(RightCorner, Right, Leverage, Side, bHeld) && !bHeld);
    const FPSTelemetryCoverageEvent* Lost = FindKind(Contests, EPSCoverageEventKind::Leverage, TEXT("CB_R"));
    TestTrue(TEXT("...announced"), Lost && Lost->Outcome == FName(TEXT("Lost")));
    TestEqual(TEXT("A fake against lost leverage moves no bite"), Matchups->GetLeverageBiteBonus(RightCorner, Right, FVector(0.f, 1.f, 0.f)), 0.f);
    Right->SetActorLocation(FVector(0.f, 900.f - Settings.LeverageRegainMargin - 100.f, 100.f));
    Matchups->UpdateCoverage(0.1f);
    TestTrue(TEXT("Back inside him, the corner has it again"), Matchups->GetLeverage(RightCorner, Right, Leverage, Side, bHeld) && bHeld);
    TestEqual(TEXT("...announced"), CountKind(Contests, EPSCoverageEventKind::Leverage), 2);

    // A fake toward his leverage bites more often, one away from it less.
    TestEqual(TEXT("A fake outside, into his leverage, bites more"), Matchups->GetLeverageBiteBonus(RightCorner, Right, FVector(0.2f, 1.f, 0.f)), Settings.LeverageBiteBonus);
    TestEqual(TEXT("...one inside less"), Matchups->GetLeverageBiteBonus(RightCorner, Right, FVector(0.2f, -1.f, 0.f)), -Settings.LeverageBiteBonus);
    TestEqual(TEXT("...and a straight stem neither"), Matchups->GetLeverageBiteBonus(RightCorner, Right, FVector(1.f, 0.f, 0.f)), 0.f);

    // The right receiver's slant breaks inside, away from the corner's outside leverage: the
    // corner is out of phase while he makes up the separation.
    Right->SetActorLocation(FVector(0.f, 900.f, 100.f));
    UPSSkillPlayerAIComponent* RightBrain = OffenseOf(Right)->GetSkillAI();
    RightBrain->TickAI(0.1f);
    Right->SetActorLocation(FVector(300.f, 900.f, 100.f));
    RightBrain->TickAI(0.1f);
    const FPSTelemetryCoverageEvent* AwayBreak = FindKind(Contests, EPSCoverageEventKind::Break, TEXT("CB_R"));
    const float Gain = PSRouteRunning::BreakSeparationGain(80.f, 40.f, OffenseOf(Right)->GetRouteRunner()->GetTuning()) + Settings.AwayFromLeverageBonus;
    const float Expected = FMath::Min(Settings.MaxOutOfPhaseSeconds, Gain / Settings.SeparationRecoverySpeed);
    TestTrue(TEXT("The slant's break, away from the leverage, is resolved"), AwayBreak && AwayBreak->Outcome == FName(TEXT("AwayFromLeverage")));
    TestTrue(TEXT("...the corner out of phase for the separation it made"), AwayBreak && FMath::IsNearlyEqual(AwayBreak->Seconds, Expected, 0.001f));
    TestTrue(TEXT("...frozen while he makes it up"), DefenseOf(RightCorner)->IsFrozen());

    // The left receiver's out breaks into the corner's outside leverage: he is sitting on it.
    UPSSkillPlayerAIComponent* LeftBrain = OffenseOf(Left)->GetSkillAI();
    LeftBrain->TickAI(0.1f);
    Left->SetActorLocation(FVector(500.f, -900.f, 100.f));
    LeftBrain->TickAI(0.1f);
    const FPSTelemetryCoverageEvent* IntoBreak = FindKind(Contests, EPSCoverageEventKind::Break, TEXT("CB_L"));
    TestTrue(TEXT("The out breaks into the leverage"), IntoBreak && IntoBreak->Outcome == FName(TEXT("IntoLeverage")));
    TestTrue(TEXT("...and gains nothing"), IntoBreak && IntoBreak->Seconds == 0.f);
    TestFalse(TEXT("...the corner stays in phase"), DefenseOf(LeftCorner)->IsFrozen());

    Bus->OnCoverageMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Zone hand-offs
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCoverageMatchupZoneTest,
    "PlaySports.Coverage.ZoneHandoffs",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCoverageMatchupZoneTest::RunTest(const FString& Parameters)
{
    using namespace PSCoverageMatchupTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSCoverageMatchupSubsystem* Matchups = World ? World->GetSubsystem<UPSCoverageMatchupSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Coverage matchup engine"), Matchups))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    SpawnPasser(World);
    APSPlayerPawn* Receiver = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(-50.f, 300.f, 100.f));
    APSPlayerPawn* Second = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_2"), FVector(-50.f, -300.f, 100.f));
    APSPlayerPawn* Hook = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB"), FVector(450.f, 0.f, 100.f));
    APSPlayerPawn* Deep = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("FS"), FVector(900.f, 700.f, 100.f));
    if (!TestNotNull(TEXT("Receiver"), Receiver) || !TestNotNull(TEXT("Second"), Second) || !TestNotNull(TEXT("Hook"), Hook) || !TestNotNull(TEXT("Deep"), Deep))
    {
        DestroyTestWorld(World);
        return false;
    }
    TArray<FPSTelemetryCoverageEvent> Contests;
    const FDelegateHandle Handle = Bus->OnCoverageMC.AddLambda([&Contests](const FPSTelemetryCoverageEvent& Event) { Contests.Add(Event); });

    // Cover 3: the linebacker in the hook (800, 0), the back in the deep third on his side.
    TestTrue(TEXT("The defense calls 3-4 Cover 3"), CallDefense(World, TEXT("Defense_34Cover3")));
    const FPSCoverageMatchupTuning Settings = Matchups->GetTuning();
    Snap(Bus);
    DefenseOf(Hook)->TickAI(0.1f);
    DefenseOf(Deep)->TickAI(0.1f);
    const FVector HookSpot = DefenseOf(Hook)->GetZoneSpot();
    const FVector DeepSpot = DefenseOf(Deep)->GetZoneSpot();
    TestTrue(TEXT("The call's zones: the hook ..."), HookSpot.Equals(FVector(800.f, 0.f, 0.f), 0.1f));
    TestTrue(TEXT("...and the deep third on the back's side"), DeepSpot.Equals(FVector(1400.f, 700.f, 0.f), 0.1f));

    // Into the hook: the linebacker picks him up and plays him.
    Receiver->SetActorLocation(FVector(700.f, 100.f, 100.f));
    Matchups->UpdateCoverage(0.1f);
    TestTrue(TEXT("A receiver in the hook is the linebacker's"), Matchups->GetCarriedReceiver(Hook) == Receiver);
    const FPSTelemetryCoverageEvent* Pickup = FindKind(Contests, EPSCoverageEventKind::Carry, TEXT("LB"));
    TestTrue(TEXT("...announced"), Pickup && Pickup->ReceiverName == TEXT("WR") && Pickup->Outcome == FName(TEXT("Zone")));
    DefenseOf(Hook)->TickAI(0.1f);
    TestTrue(TEXT("...who plays on him"), PointsToward(DefenseOf(Hook)->GetDesiredDirection(), Hook->GetActorLocation(), FVector(700.f + Settings.ZoneCarryCushion, 100.f, 0.f)));
    TestTrue(TEXT("The deep defender is deep"), Matchups->IsDeepDefender(Deep));

    // Out of the hook underneath, to nobody's zone: passed off.
    Receiver->SetActorLocation(FVector(750.f, -1000.f, 100.f));
    Matchups->UpdateCoverage(0.1f);
    TestNull(TEXT("Leaving underneath he is passed off"), Matchups->GetCarriedReceiver(Hook));
    TestEqual(TEXT("...announced"), CountKind(Contests, EPSCoverageEventKind::PassOff), 1);

    // Back in, then vertical into the deep third: handed to the deep defender.
    Receiver->SetActorLocation(FVector(700.f, 100.f, 100.f));
    Matchups->UpdateCoverage(0.1f);
    TestTrue(TEXT("Back in the hook, picked up again"), Matchups->GetCarriedReceiver(Hook) == Receiver);
    Receiver->SetActorLocation(FVector(1600.f, 450.f, 100.f));
    Matchups->UpdateCoverage(0.1f);
    TestTrue(TEXT("Up the seam into the deep third: the deep defender has him"), Matchups->GetCarriedReceiver(Deep) == Receiver && !Matchups->GetCarriedReceiver(Hook));
    const FPSTelemetryCoverageEvent* HandOff = FindKind(Contests, EPSCoverageEventKind::HandOff, TEXT("LB"));
    TestTrue(TEXT("...handed off, naming who takes him"), HandOff && HandOff->ReceiverName == TEXT("WR") && HandOff->OtherDefenderName == TEXT("FS"));

    // A second receiver up the seam with the deep defender busy: the linebacker carries him.
    Second->SetActorLocation(FVector(700.f, -100.f, 100.f));
    Matchups->UpdateCoverage(0.1f);
    TestTrue(TEXT("The second receiver in the hook is the linebacker's"), Matchups->GetCarriedReceiver(Hook) == Second);
    Second->SetActorLocation(FVector(1700.f, -300.f, 100.f));
    Matchups->UpdateCoverage(0.1f);
    TestTrue(TEXT("Vertical with nobody free deeper, he is carried on"), Matchups->GetCarriedReceiver(Hook) == Second);
    bool bVertical = false;
    for (const FPSTelemetryCoverageEvent& Event : Contests)
    {
        bVertical |= Event.Kind == EPSCoverageEventKind::Carry && Event.DefenderName == TEXT("LB") && Event.Outcome == FName(TEXT("Vertical"));
    }
    TestTrue(TEXT("...announced as a vertical carry"), bVertical);
    FVector Target = FVector::ZeroVector;
    TestTrue(TEXT("...all the way, past his zone"), Matchups->GetZoneTarget(Hook, Target) && Target.Equals(FVector(1700.f + Settings.ZoneCarryCushion, -300.f, 0.f), 0.1f));

    Bus->OnCoverageMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Safety help: over the top, rotation, free roles
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCoverageMatchupSafetyTest,
    "PlaySports.Coverage.SafetyHelp",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCoverageMatchupSafetyTest::RunTest(const FString& Parameters)
{
    using namespace PSCoverageMatchupTests;

    // Cover 2: two deep halves.
    {
        UWorld* World = CreateTestWorld();
        UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
        UPSCoverageMatchupSubsystem* Matchups = World ? World->GetSubsystem<UPSCoverageMatchupSubsystem>() : nullptr;
        if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Coverage matchup engine"), Matchups))
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
            return false;
        }
        SpawnPasser(World);
        APSPlayerPawn* Receiver = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(1400.f, 500.f, 100.f));
        APSPlayerPawn* RightSafety = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("S_R"), FVector(900.f, 600.f, 100.f));
        APSPlayerPawn* LeftSafety = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("S_L"), FVector(900.f, -600.f, 100.f));
        if (!TestNotNull(TEXT("Receiver"), Receiver) || !TestNotNull(TEXT("Right safety"), RightSafety) || !TestNotNull(TEXT("Left safety"), LeftSafety))
        {
            DestroyTestWorld(World);
            return false;
        }
        TArray<FPSTelemetryCoverageEvent> Contests;
        const FDelegateHandle Handle = Bus->OnCoverageMC.AddLambda([&Contests](const FPSTelemetryCoverageEvent& Event) { Contests.Add(Event); });

        TestTrue(TEXT("The defense calls 4-3 Cover 2"), CallDefense(World, TEXT("Defense_43Cover2")));
        const FPSCoverageMatchupTuning Settings = Matchups->GetTuning();
        Snap(Bus);
        DefenseOf(RightSafety)->TickAI(0.1f);
        DefenseOf(LeftSafety)->TickAI(0.1f);
        Matchups->UpdateCoverage(0.1f);
        TestTrue(TEXT("Both halves are deep"), Matchups->IsDeepDefender(RightSafety) && Matchups->IsDeepDefender(LeftSafety));

        // The receiver past the right safety's spot: he stays over the top of him.
        FVector Target = FVector::ZeroVector;
        const FVector RightSpot = DefenseOf(RightSafety)->GetZoneSpot();
        const FVector OverTop(1400.f + Settings.OverTopCushion, FMath::Lerp(RightSpot.Y, 500.0, Settings.DeepShadeWeight), RightSpot.Z);
        TestTrue(TEXT("The safety stays over the top of the deepest receiver"), Matchups->GetZoneTarget(RightSafety, Target) && Target.Equals(OverTop, 0.1f));
        DefenseOf(RightSafety)->TickAI(0.1f);
        TestTrue(TEXT("...and plays there"), PointsToward(DefenseOf(RightSafety)->GetDesiredDirection(), RightSafety->GetActorLocation(), OverTop));

        // The left safety is sent to a free receiver: the right one rotates to the middle.
        FPSTelemetryBlownCoverageEvent Blown;
        Blown.ReceiverName = TEXT("WR");
        Blown.HelperName = TEXT("S_L");
        Bus->PublishBlownCoverage(Blown);
        TestTrue(TEXT("The left safety leaves for the receiver"), DefenseOf(LeftSafety)->GetAction() == EPSDefenderAction::Cover);
        Receiver->SetActorLocation(FVector(-50.f, 2000.f, 100.f));
        Matchups->UpdateCoverage(0.1f);
        TestTrue(TEXT("The right safety rotates"), Matchups->HasRotated(RightSafety));
        TestTrue(TEXT("...to the deep middle"), Matchups->GetZoneTarget(RightSafety, Target) && Target.Equals(FVector(RightSpot.X, 0.f, RightSpot.Z), 0.1f));
        const FPSTelemetryCoverageEvent* Rotation = FindKind(Contests, EPSCoverageEventKind::Rotate, TEXT("S_R"));
        TestTrue(TEXT("...announced"), Rotation != nullptr);

        Bus->OnCoverageMC.Remove(Handle);
        DestroyTestWorld(World);
    }

    // Nickel man-free: the backs left over take the free roles.
    {
        UWorld* World = CreateTestWorld();
        UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
        UPSCoverageMatchupSubsystem* Matchups = World ? World->GetSubsystem<UPSCoverageMatchupSubsystem>() : nullptr;
        if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Coverage matchup engine"), Matchups))
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
            return false;
        }
        SpawnPasser(World);
        APSPlayerPawn* Right = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_R"), FVector(0.f, 900.f, 100.f));
        APSPlayerPawn* Left = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_L"), FVector(0.f, -900.f, 100.f));
        APSPlayerPawn* RightCorner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_R"), FVector(700.f, 900.f, 100.f));
        APSPlayerPawn* LeftCorner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_L"), FVector(700.f, -900.f, 100.f));
        APSPlayerPawn* Free = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("FS"), FVector(1300.f, 200.f, 100.f));
        APSPlayerPawn* Robber = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("SS"), FVector(1100.f, -200.f, 100.f));
        if (!TestNotNull(TEXT("Right"), Right) || !TestNotNull(TEXT("Left"), Left) || !TestNotNull(TEXT("Right corner"), RightCorner)
            || !TestNotNull(TEXT("Left corner"), LeftCorner) || !TestNotNull(TEXT("Free"), Free) || !TestNotNull(TEXT("Robber"), Robber))
        {
            DestroyTestWorld(World);
            return false;
        }
        TArray<FPSTelemetryCoverageEvent> Contests;
        const FDelegateHandle Handle = Bus->OnCoverageMC.AddLambda([&Contests](const FPSTelemetryCoverageEvent& Event) { Contests.Add(Event); });

        TestTrue(TEXT("The defense calls nickel man-free"), CallDefense(World, TEXT("Defense_NickelManFree")));
        const FPSCoverageMatchupTuning Settings = Matchups->GetTuning();
        Snap(Bus);
        DefenseOf(RightCorner)->TickAI(0.1f);
        DefenseOf(LeftCorner)->TickAI(0.1f);
        DefenseOf(Free)->TickAI(0.1f);
        DefenseOf(Robber)->TickAI(0.1f);
        TestTrue(TEXT("Two backs have nobody left to cover"), !DefenseOf(Free)->GetCoveredReceiver() && !DefenseOf(Robber)->GetCoveredReceiver());
        Matchups->UpdateCoverage(0.1f);
        TestTrue(TEXT("The first takes the deep middle"), Matchups->GetFreeRole(Free) == EPSFreeRole::DeepMiddle);
        TestTrue(TEXT("...the second the robber"), Matchups->GetFreeRole(Robber) == EPSFreeRole::Robber);
        TestEqual(TEXT("...each announced"), CountKind(Contests, EPSCoverageEventKind::FreeRole), 2);

        FVector Target = FVector::ZeroVector;
        TestTrue(TEXT("The free safety plays the deep middle"), Matchups->GetZoneTarget(Free, Target) && Target.Equals(FVector(Settings.FreeDeepDepth, 0.f, 100.f), 0.1f));
        DefenseOf(Free)->TickAI(0.1f);
        TestTrue(TEXT("...and goes there"), PointsToward(DefenseOf(Free)->GetDesiredDirection(), Free->GetActorLocation(), FVector(Settings.FreeDeepDepth, 0.f, 100.f)));
        const FVector RobberSpot(Settings.RobberDepth, 0.f, 100.f);
        TestTrue(TEXT("The robber sits in the middle"), Matchups->GetZoneTarget(Robber, Target) && Target.Equals(RobberSpot, 0.1f));

        // A receiver crosses the robber's window: he jumps him.
        const FVector Crossing(Settings.RobberDepth + 100.f, 300.f, 100.f);
        Right->SetActorLocation(Crossing);
        TestTrue(TEXT("The robber jumps the crosser"), Matchups->GetZoneTarget(Robber, Target) && Target.Equals(FMath::Lerp(RobberSpot, Crossing, Settings.RobberJumpWeight), 0.1f));

        Bus->OnCoverageMC.Remove(Handle);
        DestroyTestWorld(World);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Pass interference
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCoverageMatchupInterferenceTest,
    "PlaySports.Coverage.PassInterference",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCoverageMatchupInterferenceTest::RunTest(const FString& Parameters)
{
    using namespace PSCoverageMatchupTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSCoverageMatchupSubsystem* Matchups = World ? World->GetSubsystem<UPSCoverageMatchupSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Coverage matchup engine"), Matchups))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    APSPlayerPawn* Receiver = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(1500.f, 900.f, 100.f));
    APSPlayerPawn* Corner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB"), FVector(1560.f, 900.f, 100.f));
    if (!TestNotNull(TEXT("Receiver"), Receiver) || !TestNotNull(TEXT("Corner"), Corner))
    {
        DestroyTestWorld(World);
        return false;
    }
    TArray<FPSTelemetryCoverageEvent> Flags;
    const FDelegateHandle Handle = Bus->OnCoverageMC.AddLambda([&Flags](const FPSTelemetryCoverageEvent& Event)
    {
        if (Event.Kind == EPSCoverageEventKind::PassInterference)
        {
            Flags.Add(Event);
        }
    });

    // The officials see every foul here.
    FPSCoverageMatchupTuning AlwaysFlagged = Matchups->GetTuning();
    AlwaysFlagged.FlagChance = 1.f;
    Matchups->SetTuning(AlwaysFlagged);

    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    Sim->InitializeWithWorld(World);
    Sim->InitializePlay(TArray<FPlayerAttributes>(), TArray<FPlayerAttributes>());
    const int32 StartYardLine = Sim->GetPlayState().YardLine;

    FPSTelemetryThrowEvent Throw;
    Throw.TargetReceiverName = TEXT("WR");
    Throw.TargetLocation = FVector(1800.f, 900.f, 0.f);
    Throw.LandingLocation = Throw.TargetLocation;

    Snap(Bus);
    Sim->SetPlayPhase(EPlayPhase::PassRush);
    Sim->ActivePenalty = EPSPenaltyType::None;
    Bus->PublishThrow(Throw);

    // In contact, but in front of him playing the ball: a clean play.
    Matchups->UpdateCoverage(0.1f);
    TestEqual(TEXT("Contact while playing the ball is clean"), Flags.Num(), 0);

    // Through his back as the ball comes: interference, flagged at the spot.
    Corner->SetActorLocation(FVector(1430.f, 900.f, 100.f));
    Matchups->UpdateCoverage(0.1f);
    if (TestEqual(TEXT("Playing through the receiver draws a flag"), Flags.Num(), 1))
    {
        TestEqual(TEXT("...on the corner"), Flags[0].DefenderName, FString(TEXT("CB")));
        TestEqual(TEXT("...at the spot, 15 yards past the line"), Flags[0].YardsPastLine, 15);
    }
    Matchups->UpdateCoverage(0.1f);
    TestEqual(TEXT("...once a play"), Flags.Num(), 1);
    TestEqual(TEXT("The play simulation flags it"), Sim->ActivePenalty, EPSPenaltyType::PassInterference);

    // Incomplete: the ball goes to the spot with a first down.
    Sim->EndPlayAndPrepareNext();
    TestEqual(TEXT("Enforced at the spot"), Sim->GetPlayState().YardLine, StartYardLine + 15);
    TestEqual(TEXT("...with a first down"), Sim->GetPlayState().Down, 1);
    TestEqual(TEXT("...the flag picked up"), Sim->ActivePenalty, EPSPenaltyType::None);

    // Next play the same foul, but the receiver makes the catch and runs on for 30: declined.
    const int32 SecondYardLine = Sim->GetPlayState().YardLine;
    Corner->SetActorLocation(FVector(1560.f, 900.f, 100.f));
    Snap(Bus);
    Sim->SetPlayPhase(EPlayPhase::PassRush);
    Sim->ActivePenalty = EPSPenaltyType::None;
    Bus->PublishThrow(Throw);
    Corner->SetActorLocation(FVector(1430.f, 900.f, 100.f));
    Matchups->UpdateCoverage(0.1f);
    TestEqual(TEXT("The second foul is flagged"), Sim->ActivePenalty, EPSPenaltyType::PassInterference);
    Sim->SetPlayPhase(EPlayPhase::BallCarrierMovement);
    Sim->RecordTackle(30);
    Sim->EndPlayAndPrepareNext();
    TestEqual(TEXT("A play that gained more declines it"), Sim->GetPlayState().YardLine, SecondYardLine + 30);

    Bus->OnCoverageMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- The shipped tuning
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCoverageMatchupTuningTest,
    "PlaySports.Coverage.TuningData",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCoverageMatchupTuningTest::RunTest(const FString& Parameters)
{
    using namespace PSCoverageMatchupTests;

    UWorld* World = CreateTestWorld();
    UPSCoverageMatchupSubsystem* Matchups = World ? World->GetSubsystem<UPSCoverageMatchupSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Coverage matchup engine"), Matchups))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    TestTrue(TEXT("Data/coverage_matchups.json loads and is sound"), Matchups->LoadTuningFromJson(UPSCoverageMatchupSubsystem::GetDefaultTuningPath()));
    const FPSCoverageMatchupTuning& Loaded = Matchups->GetTuning();
    const FPSCoverageMatchupTuning Defaults;
    TestEqual(TEXT("The struct's defaults are the file's: press depth"), Loaded.PressDepth, Defaults.PressDepth);
    TestEqual(TEXT("...leverage shade"), Loaded.LeverageShade, Defaults.LeverageShade);
    TestEqual(TEXT("...deep zone depth"), Loaded.DeepZoneDepth, Defaults.DeepZoneDepth);
    TestEqual(TEXT("...flag chance"), Loaded.FlagChance, Defaults.FlagChance);
    TestTrue(TEXT("Every shell the sample playbook calls has a rule"), Loaded.Shells.Num() >= 6);

    const FPSCoverageShellRule& Cover1 = Matchups->GetShellRule(TEXT("Cover1"));
    TestTrue(TEXT("Cover 1 presses with outside leverage"), Cover1.bPress && Cover1.Leverage == EPSLeverage::Outside);
    TestTrue(TEXT("...its extra cover men the deep middle, then the robber"),
        Cover1.FreeRoles.Num() == 2 && Cover1.FreeRoles[0] == EPSFreeRole::DeepMiddle && Cover1.FreeRoles[1] == EPSFreeRole::Robber);
    TestFalse(TEXT("Cover 2 plays off"), Matchups->GetShellRule(TEXT("Cover2")).bPress);
    const FPSCoverageShellRule& Unknown = Matchups->GetShellRule(TEXT("NoSuchShell"));
    TestTrue(TEXT("An unlisted shell takes the default rule"), Unknown.Shell.IsEmpty() && !Unknown.bPress && Unknown.FreeRoles.Num() == 0);

    FPSCoverageMatchupTuning Broken = Loaded;
    Broken.FlagChance = 1.5f;
    Broken.SeparationRecoverySpeed = 0.f;
    Broken.Shells.Add(Broken.Shells[0]);
    TestEqual(TEXT("Bad tuning is caught: a chance over 1, no recovery speed, a shell twice"), UPSCoverageMatchupSubsystem::ValidateTuning(Broken).Num(), 3);

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
