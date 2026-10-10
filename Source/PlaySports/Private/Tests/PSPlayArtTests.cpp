// PSPlayArtTests.cpp -- Epic 27 (pre-snap route visualization)
//
// Tests covered:
//   1. One resolution: PSPlayResolution resolves a play for where the players stand (routes
//      from each split, mirrored on the left, a spot for a "go to your spot", nothing for a
//      blocker, a zone on the defender's side), and the orchestrator hands the AI exactly that.
//      The GameState event carries the line the snap will use.
//   2. The art: ribbons from the feet through the waypoints with their cuts and fakes, a ring at
//      each end, an option route's branches, colors and widths by read order; nothing for a
//      blocker or a spot.
//   3. What is drawn is what is run: the art of the human's call against the announced line,
//      a hot route redrawing it, the read orders from the playbook, the AI's routes at the snap
//      matching the ribbons, the fade, and the next down starting afresh.
//   4. Who sees it and when: the platform tier (fade, cut, none), the RouteArt setting, kick
//      plays, a defending player, and the head-to-head rules.
//   5. Data/play_art.json loads and validates; an unsound style is refused.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSDefenseController.h"
#include "PSGameStateEvents.h"
#include "PSOffenseController.h"
#include "PSOverlayPlayArtSubsystem.h"
#include "PSPlayArt.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayOrchestrator.h"
#include "PSPlayResolution.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSPreSnapSubsystem.h"
#include "PSRouteRunning.h"
#include "PSSettingsSubsystem.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "PSUITeamCatalog.h"
#include "PSVersusSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPlayArtTests
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
            }
        }
        else if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
            AI->GetSkillAI()->BindToBus();
        }
        return Pawn;
    }

    static FPSRouteWaypoint Waypoint(float X, float Y, bool bFake = false)
    {
        FPSRouteWaypoint Point;
        Point.Offset = FVector(X, Y, 0.f);
        Point.bFake = bFake;
        return Point;
    }

    static void AddRoute(UDataTable* Table, const TCHAR* RouteId, const TArray<FPSRouteWaypoint>& Points, int32 OptionRead = -1, const TCHAR* VsMan = nullptr, const TCHAR* VsZone = nullptr)
    {
        FPSRoute Route;
        Route.RouteId = FName(RouteId);
        Route.Waypoints = Points;
        Route.OptionReadWaypoint = OptionRead;
        Route.VsManBranch = VsMan ? FName(VsMan) : NAME_None;
        Route.VsZoneBranch = VsZone ? FName(VsZone) : NAME_None;
        Table->AddRow(Route.RouteId, Route);
    }

    /** The sample library's shapes: a slant, a go, a flat, a slant-and-go, an option. */
    static UDataTable* MakeRouteLibrary()
    {
        UDataTable* Table = NewObject<UDataTable>();
        Table->RowStruct = FPSRoute::StaticStruct();
        AddRoute(Table, TEXT("Slant"), { Waypoint(300.f, 0.f), Waypoint(500.f, -400.f) });
        AddRoute(Table, TEXT("Go"), { Waypoint(1500.f, 0.f) });
        AddRoute(Table, TEXT("Flat"), { Waypoint(100.f, -400.f) });
        AddRoute(Table, TEXT("SlantGo"), { Waypoint(300.f, 0.f), Waypoint(400.f, -200.f, true), Waypoint(1600.f, -200.f) });
        AddRoute(Table, TEXT("Option"), { Waypoint(600.f, 0.f) }, 0, TEXT("OptionBreak"), TEXT("OptionSit"));
        AddRoute(Table, TEXT("OptionBreak"), { Waypoint(100.f, 400.f) });
        AddRoute(Table, TEXT("OptionSit"), { Waypoint(-100.f, 0.f) });
        return Table;
    }

    static FPSPlayAssignment Slot(EPlayerRole Role, EPSAssignmentKind Kind, const TCHAR* RouteId = nullptr, int32 ReadOrder = 0)
    {
        FPSPlayAssignment Assignment;
        Assignment.Role = Role;
        Assignment.Kind = Kind;
        Assignment.RouteId = RouteId ? FName(RouteId) : NAME_None;
        Assignment.ReadOrder = ReadOrder;
        return Assignment;
    }

    static FPSPlayArtStyle LoadStyle()
    {
        FPSPlayArtStyle Style;
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        Ingestion->LoadPlayArtStyleFromJson(UPSOverlayPlayArtSubsystem::GetDefaultStylePath(), Style);
        return Style;
    }

    static FPSTelemetryGameStateEvent LineAt(const FVector& Line)
    {
        FPSTelemetryGameStateEvent Event;
        Event.Phase = TEXT("PreSnap");
        Event.LineOfScrimmage = Line;
        return Event;
    }

    static FPSSituationContext FirstAndTen()
    {
        FPSSituationContext Situation;
        Situation.Down = 1;
        Situation.Distance = 10;
        Situation.YardLine = 20;
        return Situation;
    }

    static void Snap(UPSTelemetryBus* Bus, const FVector& Line)
    {
        FPSTelemetrySnapEvent Event;
        Event.Down = 1;
        Event.Distance = 10;
        Event.YardLine = 20;
        Event.LineOfScrimmage = Line;
        Bus->PublishSnap(Event);
    }

    static void NewDown(UPSTelemetryBus* Bus)
    {
        FPSTelemetryPhaseChangeEvent Whistle;
        Whistle.OldPhase = TEXT("Scoring");
        Whistle.NewPhase = TEXT("PreSnap");
        Bus->PublishPhaseChange(Whistle);
    }

    /** The art of one shape belonging to Pawn (not an option branch's), or null. */
    static const FPSPlayArtPrimitive* FindArt(const TArray<FPSPlayArtPrimitive>& Art, const APSPlayerPawn* Pawn, EPSPlayArtShape Shape)
    {
        return Art.FindByPredicate([Pawn, Shape](const FPSPlayArtPrimitive& Piece) { return Piece.Pawn.Get() == Pawn && Piece.Shape == Shape && !Piece.bBranch; });
    }

    static bool SameOnGround(const FVector& A, const FVector& B)
    {
        return FVector::Dist2D(A, B) < 0.1f;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- One resolution, shared by the snap and the art
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayArtResolutionTest,
    "PlaySports.PlayArt.OneResolution",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayArtResolutionTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayArtTests;

    // The GameState event announces the line the snap will be from (Epic 33's event).
    FPlayState State;
    State.YardLine = 35;
    const FPSTelemetryGameStateEvent Announced = PSGameStateEvents::MakeEvent(State, FDriveSummary(), 0, 3);
    TestTrue(TEXT("The GameState event carries the line of scrimmage"), Announced.LineOfScrimmage.Equals(FVector(3500.f, 0.f, 0.f)));
    TestTrue(TEXT("...the same spot the snap is announced from"), Announced.LineOfScrimmage.Equals(PSGameStateEvents::LineOfScrimmageFor(35)));

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    const FVector Line(1000.f, 0.f, 0.f);
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(800.f, 0.f, 100.f));
    APSPlayerPawn* Right = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_R"), FVector(950.f, 900.f, 100.f));
    APSPlayerPawn* Left = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_L"), FVector(950.f, -900.f, 100.f));
    APSPlayerPawn* TE = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE"), FVector(950.f, 450.f, 100.f));
    APSPlayerPawn* CornerLeft = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_L"), FVector(1900.f, -900.f, 100.f));
    APSPlayerPawn* Backer = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB"), FVector(1500.f, 0.f, 100.f));
    if (!TestTrue(TEXT("Players spawned"), QB && Right && Left && TE && CornerLeft && Backer))
    {
        DestroyTestWorld(World);
        return false;
    }
    UDataTable* Routes = MakeRouteLibrary();

    FPSPlayDefinition Offense;
    Offense.bIsOffensivePlay = true;
    FPSPlayAssignment Drop = Slot(EPlayerRole::Quarterback, EPSAssignmentKind::Route);
    Drop.FormationOffset = FVector(-200.f, 0.f, 0.f);
    Offense.Assignments = { Drop, Slot(EPlayerRole::WideReceiver, EPSAssignmentKind::Route, TEXT("Slant"), 1), Slot(EPlayerRole::TightEnd, EPSAssignmentKind::PassBlock) };
    const TArray<APSPlayerPawn*> OffensePawns = { QB, Right, Left, TE };
    const TArray<FPSResolvedAssignment> Resolved = PSPlayResolution::ResolvePlay(Offense, OffensePawns, Routes, Line);
    if (TestEqual(TEXT("One entry per player, in order"), Resolved.Num(), 4))
    {
        TestTrue(TEXT("The QB goes to his spot: the line plus his formation offset"),
            Resolved[0].Waypoints.Num() == 1 && Resolved[0].Waypoints[0].Equals(FVector(800.f, 0.f, 0.f)));
        TestTrue(TEXT("The right receiver's slant starts from his own split"),
            Resolved[1].Waypoints.Num() == 2 && Resolved[1].Waypoints[0].Equals(FVector(1300.f, 900.f, 0.f)) && Resolved[1].Waypoints[1].Equals(FVector(1500.f, 500.f, 0.f)));
        TestTrue(TEXT("The left receiver repeats the role's slot, mirrored"),
            Resolved[2].Mirror < 0.f && Resolved[2].Waypoints.Num() == 2 && Resolved[2].Waypoints[1].Equals(FVector(1500.f, -500.f, 0.f)));
        TestEqual(TEXT("...with the slot's read order"), Resolved[2].Assignment.ReadOrder, 1);
        TestTrue(TEXT("The tight end blocks: a slot, no route"), Resolved[3].bHasSlot && !Resolved[3].RunsRoute() && Resolved[3].Waypoints.Num() == 0);
    }

    FPSPlayDefinition Defense;
    Defense.bIsOffensivePlay = false;
    FPSPlayAssignment DeepHalf = Slot(EPlayerRole::DefensiveBack, EPSAssignmentKind::ZoneCoverage);
    DeepHalf.ZoneOffset = FVector(1200.f, 600.f, 0.f);
    Defense.Assignments = { DeepHalf, Slot(EPlayerRole::Linebacker, EPSAssignmentKind::Blitz) };
    const TArray<FPSResolvedAssignment> Coverage = PSPlayResolution::ResolvePlay(Defense, { CornerLeft, Backer, QB }, nullptr, Line);
    if (TestEqual(TEXT("Defense resolved"), Coverage.Num(), 3))
    {
        TestTrue(TEXT("A zone is played on the defender's own side"), Coverage[0].ZoneSpot.Equals(FVector(2200.f, -600.f, 0.f)));
        TestTrue(TEXT("A blitz rushes"), Coverage[1].DefensiveType == EPSDefensiveAssignmentType::PassRush);
        TestFalse(TEXT("A role the play has no slot for gets none"), Coverage[2].bHasSlot);
    }

    // The snap hands the AI exactly what was resolved.
    UPSPlayOrchestrator* Orchestrator = NewObject<UPSPlayOrchestrator>();
    Orchestrator->DistributePlayCall(Offense, OffensePawns, Routes, Line);
    for (const FPSResolvedAssignment& Entry : Resolved)
    {
        const APSPlayerPawn* Player = Entry.Pawn.Get();
        const APSOffenseController* AI = Player ? Cast<APSOffenseController>(Player->GetController()) : nullptr;
        if (TestNotNull(TEXT("Offense AI"), AI))
        {
            TestTrue(FString::Printf(TEXT("%s runs the resolved waypoints"), *Player->GetAttributes().DisplayName), AI->GetRouteWaypoints() == Entry.Waypoints);
        }
    }

    // An option branch is placed at the read the same way.
    const FPSRoute* Branch = Routes->FindRow<FPSRoute>(TEXT("OptionBreak"), TEXT("Test"));
    const TArray<FVector> Placed = Branch ? PSPlayResolution::PlaceRoute(*Branch, FVector(1600.f, -900.f, 0.f), -1.f) : TArray<FVector>();
    TestTrue(TEXT("A route placed on the left mirrors its Y"), Placed.Num() == 1 && Placed[0].Equals(FVector(1700.f, -1300.f, 0.f)));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Compiling the route art
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayArtCompileTest,
    "PlaySports.PlayArt.CompileRouteArt",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayArtCompileTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayArtTests;

    const FPSPlayArtStyle Style = LoadStyle();
    if (!TestTrue(TEXT("The style has read colors"), Style.ReadColors.Num() >= 2))
    {
        return false;
    }
    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    const FVector Line(1000.f, 0.f, 0.f);
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(800.f, 0.f, 100.f));
    APSPlayerPawn* Slanter = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_SLANT"), FVector(950.f, 900.f, 100.f));
    APSPlayerPawn* Faker = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE_SLANTGO"), FVector(950.f, -450.f, 100.f));
    APSPlayerPawn* Reader = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB_OPTION"), FVector(600.f, 300.f, 100.f));
    APSPlayerPawn* Blocker = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL"), FVector(980.f, 0.f, 100.f));
    if (!TestTrue(TEXT("Players spawned"), QB && Slanter && Faker && Reader && Blocker))
    {
        DestroyTestWorld(World);
        return false;
    }
    UDataTable* Routes = MakeRouteLibrary();

    FPSPlayDefinition Play;
    Play.bIsOffensivePlay = true;
    Play.Assignments = {
        Slot(EPlayerRole::Quarterback, EPSAssignmentKind::Route),
        Slot(EPlayerRole::WideReceiver, EPSAssignmentKind::Route, TEXT("Slant"), 1),
        Slot(EPlayerRole::TightEnd, EPSAssignmentKind::Route, TEXT("SlantGo"), 2),
        Slot(EPlayerRole::RunningBack, EPSAssignmentKind::Route, TEXT("Option")),
        Slot(EPlayerRole::OffensiveLineman, EPSAssignmentKind::PassBlock) };
    const TArray<FPSResolvedAssignment> Resolved = PSPlayResolution::ResolvePlay(Play, { QB, Slanter, Faker, Reader, Blocker }, Routes, Line);
    const float GroundZ = Line.Z;
    const float BreakAngle = FRouteRunningTuningRow().BreakMinAngleDegrees;
    const TArray<FPSPlayArtPrimitive> Art = PSPlayArt::CompileRouteArt(Resolved, Routes, Style, BreakAngle, GroundZ);

    TestNull(TEXT("A spot (the QB's drop) is no route art"), FindArt(Art, QB, EPSPlayArtShape::Ribbon));
    TestNull(TEXT("A blocker has none"), FindArt(Art, Blocker, EPSPlayArtShape::Ribbon));

    const FPSPlayArtPrimitive* Slant = FindArt(Art, Slanter, EPSPlayArtShape::Ribbon);
    if (TestNotNull(TEXT("The slant is a ribbon"), Slant))
    {
        TestEqual(TEXT("...from his feet through both waypoints"), Slant->Points.Num(), 3);
        TestTrue(TEXT("...starting at his feet"), SameOnGround(Slant->Points[0], Slanter->GetActorLocation()));
        TestTrue(TEXT("...through the waypoints the AI is handed"), SameOnGround(Slant->Points[1], FVector(1300.f, 900.f, 0.f)) && SameOnGround(Slant->Points[2], FVector(1500.f, 500.f, 0.f)));
        TestTrue(TEXT("...lying on the turf"), FMath::IsNearlyEqual(Slant->Points[0].Z, GroundZ + Style.GroundOffset) && FMath::IsNearlyEqual(Slant->Points[2].Z, GroundZ + Style.GroundOffset));
        TestTrue(TEXT("...its break articulated where it cuts inside"), Slant->BreakIndices.Num() == 1 && Slant->BreakIndices[0] == 1);
        TestTrue(TEXT("...the primary read's color"), Slant->Color.Equals(PSPlayArt::ColorForRead(Style, 1)));
        TestTrue(TEXT("...and width"), FMath::IsNearlyEqual(Slant->Size, Style.RibbonWidth * Style.PrimaryWidthScale));
        TestEqual(TEXT("...naming its route"), Slant->Source, FName(TEXT("Slant")));
    }
    const FPSPlayArtPrimitive* SlantRing = FindArt(Art, Slanter, EPSPlayArtShape::Ring);
    if (TestNotNull(TEXT("The slant ends in a ring"), SlantRing))
    {
        TestTrue(TEXT("...at its terminus"), SameOnGround(SlantRing->Points[0], FVector(1500.f, 500.f, 0.f)));
        TestTrue(TEXT("...of the ring radius"), FMath::IsNearlyEqual(SlantRing->Size, Style.RingRadius));
    }

    const FPSPlayArtPrimitive* DoubleMove = FindArt(Art, Faker, EPSPlayArtShape::Ribbon);
    if (TestNotNull(TEXT("The slant-and-go is a ribbon"), DoubleMove))
    {
        TestTrue(TEXT("...its fake marked as a fake"), DoubleMove->FakeIndices.Num() == 1 && DoubleMove->FakeIndices[0] == 2);
        TestFalse(TEXT("...not as a cut"), DoubleMove->BreakIndices.Contains(2));
        TestTrue(TEXT("...a check-down's color"), DoubleMove->Color.Equals(PSPlayArt::ColorForRead(Style, 2)));
        TestTrue(TEXT("...and the plain width"), FMath::IsNearlyEqual(DoubleMove->Size, Style.RibbonWidth));
        TestTrue(TEXT("...mirrored on the left"), SameOnGround(DoubleMove->Points.Last(), FVector(2600.f, -250.f, 0.f)));
    }

    const FPSPlayArtPrimitive* Option = FindArt(Art, Reader, EPSPlayArtShape::Ribbon);
    if (TestNotNull(TEXT("The option route is a ribbon"), Option))
    {
        TestEqual(TEXT("...run to its read"), Option->Points.Num(), 2);
        TestTrue(TEXT("...where it splits"), Option->BreakIndices.Contains(1));
        TestTrue(TEXT("...unranked"), Option->Color.Equals(PSPlayArt::ColorForRead(Style, 0)));
    }
    TestNull(TEXT("An option route's own end has no ring: its branches do"), FindArt(Art, Reader, EPSPlayArtShape::Ring));
    TArray<const FPSPlayArtPrimitive*> Branches;
    for (const FPSPlayArtPrimitive& Piece : Art)
    {
        if (Piece.Pawn.Get() == Reader && Piece.bBranch && Piece.Shape == EPSPlayArtShape::Ribbon)
        {
            Branches.Add(&Piece);
        }
    }
    if (TestEqual(TEXT("Both branches are drawn"), Branches.Num(), 2))
    {
        TestEqual(TEXT("...the man branch first"), Branches[0]->Source, FName(TEXT("OptionBreak")));
        TestTrue(TEXT("...placed at the read, authored outside"), SameOnGround(Branches[0]->Points[0], FVector(1600.f, 300.f, 0.f)) && SameOnGround(Branches[0]->Points[1], FVector(1700.f, 700.f, 0.f)));
        TestTrue(TEXT("...lighter"), FMath::IsNearlyEqual(Branches[0]->Opacity, Style.BranchOpacity));
    }

    TestEqual(TEXT("Three ribbons and their rings, plus two branches and theirs"), Art.Num(), 2 + 2 + 1 + 4);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- What is drawn before the snap is what the AI runs
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayArtRoundTripTest,
    "PlaySports.PlayArt.DrawnIsRun",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayArtRoundTripTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayArtTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSPreSnapSubsystem* PreSnap = World ? World->GetSubsystem<UPSPreSnapSubsystem>() : nullptr;
    UPSOverlayPlayArtSubsystem* Overlay = World ? World->GetSubsystem<UPSOverlayPlayArtSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Pre-snap subsystem"), PreSnap)
        || !TestNotNull(TEXT("Game worlds have the play art"), Overlay))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    Overlay->SetOverlayDetail(EPSOverlayDetail::Full);
    Overlay->SetRefreshHz(0.f);
    const FPSPlayArtStyle& Style = Overlay->GetStyle();

    const FVector Line(2000.f, 0.f, 0.f);
    SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(1800.f, 0.f, 100.f));
    APSPlayerPawn* Wide = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_WIDE"), FVector(1950.f, 900.f, 100.f));
    APSPlayerPawn* SlotWR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_SLOT"), FVector(1950.f, -500.f, 100.f));
    APSPlayerPawn* TE = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE"), FVector(1950.f, 450.f, 100.f));
    APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(1500.f, 0.f, 100.f));
    if (!TestTrue(TEXT("Offense spawned"), Wide && SlotWR && TE && RB))
    {
        DestroyTestWorld(World);
        return false;
    }

    // The read orders are play data.
    FPSPlayDefinition SlantFlat;
    if (TestTrue(TEXT("Slant-Flat is in the playbook"), PlayCall->FindPlay(TEXT("Offense_SlantFlat"), SlantFlat)))
    {
        const FPSPlayAssignment* SlantSlot = PSPlayResolution::FindAssignmentSlot(SlantFlat, EPlayerRole::WideReceiver, 0);
        const FPSPlayAssignment* FlatSlot = PSPlayResolution::FindAssignmentSlot(SlantFlat, EPlayerRole::RunningBack, 0);
        TestTrue(TEXT("The slant is the primary read and the flat the next"), SlantSlot && FlatSlot && SlantSlot->ReadOrder == 1 && FlatSlot->ReadOrder == 2);
    }

    PlayCall->OpenPlayCall(FirstAndTen());
    TestTrue(TEXT("The offense calls Slant-Flat"), PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human));
    Overlay->AdvanceTime(0.f);
    TestEqual(TEXT("No art before the line is announced"), Overlay->GetRouteArt().Num(), 0);
    Bus->PublishGameState(LineAt(Line));
    TestEqual(TEXT("An event makes the art stale; the next tick rebuilds it"), Overlay->GetRouteArt().Num(), 0);
    Overlay->AdvanceTime(0.f);
    TArray<FPSPlayArtPrimitive> Art = Overlay->GetRouteArt();
    TestEqual(TEXT("Two slants and the flat, each with its ring"), Art.Num(), 6);
    TestEqual(TEXT("The art is of the offense's call"), Overlay->GetPlayId(), FName(TEXT("Offense_SlantFlat")));
    TestNull(TEXT("The tight end blocks: no art"), FindArt(Art, TE, EPSPlayArtShape::Ribbon));
    const FPSPlayArtPrimitive* Flat = FindArt(Art, RB, EPSPlayArtShape::Ribbon);
    if (TestNotNull(TEXT("The back's flat is drawn"), Flat))
    {
        TestTrue(TEXT("...in the second read's color"), Flat->Color.Equals(PSPlayArt::ColorForRead(Style, 2)));
        TestTrue(TEXT("...from the announced line"), SameOnGround(Flat->Points.Last(), FVector(2100.f, -400.f, 0.f)));
    }
    const FPSPlayArtPrimitive* SlotSlant = FindArt(Art, SlotWR, EPSPlayArtShape::Ribbon);
    TestTrue(TEXT("The slot repeats the slant, primary, mirrored"), SlotSlant && SlotSlant->ReadOrder == 1 && SameOnGround(SlotSlant->Points.Last(), FVector(2500.f, -100.f, 0.f)));

    // A hot route redraws it.
    TestTrue(TEXT("The wide receiver is hot-routed to a go"), PreSnap->HotRoute(Wide, TEXT("Go"), true));
    Overlay->AdvanceTime(0.f);
    Art = Overlay->GetRouteArt();
    const FPSPlayArtPrimitive* Go = FindArt(Art, Wide, EPSPlayArtShape::Ribbon);
    TestTrue(TEXT("The art shows the go"), Go && Go->Source == FName(TEXT("Go")) && SameOnGround(Go->Points.Last(), FVector(3500.f, 900.f, 0.f)));

    TestTrue(TEXT("The defense calls Cover 2"), PlayCall->CallPlay(TEXT("Defense_43Cover2"), EPSPlayCaller::CPU));
    Snap(Bus, Line);

    // Every ribbon is the route the AI was handed at the snap.
    for (const FPSPlayArtPrimitive& Piece : Art)
    {
        const APSPlayerPawn* Runner = Piece.Pawn.Get();
        if (Piece.Shape != EPSPlayArtShape::Ribbon || !Runner)
        {
            continue;
        }
        const APSOffenseController* AI = Cast<APSOffenseController>(Runner->GetController());
        const TArray<FVector> Run = AI ? AI->GetRouteWaypoints() : TArray<FVector>();
        bool bSame = Run.Num() == Piece.Points.Num() - 1;
        for (int32 Index = 0; bSame && Index < Run.Num(); ++Index)
        {
            bSame = SameOnGround(Run[Index], Piece.Points[Index + 1]);
        }
        TestTrue(FString::Printf(TEXT("%s runs the %s he was drawn"), *Runner->GetAttributes().DisplayName, *Piece.Source.ToString()), bSame);
    }

    // The fade, on a Full tier.
    TestTrue(TEXT("At the snap the art fades"), Overlay->IsFading() && FMath::IsNearlyEqual(Overlay->GetOpacity(), 1.f));
    Overlay->AdvanceTime(Style.SnapFadeSeconds * 0.5f);
    TestTrue(TEXT("...halfway through"), FMath::IsNearlyEqual(Overlay->GetOpacity(), 0.5f, 0.01f) && Overlay->GetRouteArt().Num() == 6);
    Overlay->AdvanceTime(Style.SnapFadeSeconds);
    TestEqual(TEXT("...then it's gone"), Overlay->GetRouteArt().Num(), 0);
    TestFalse(TEXT("...and done fading"), Overlay->IsFading());

    // The next down starts afresh: nothing until the offense calls again.
    NewDown(Bus);
    Overlay->AdvanceTime(0.f);
    TestEqual(TEXT("A new down has no art before the call"), Overlay->GetRouteArt().Num(), 0);
    PlayCall->OpenPlayCall(FirstAndTen());
    PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human);
    Overlay->AdvanceTime(0.f);
    TestEqual(TEXT("...and the art is back with it"), Overlay->GetRouteArt().Num(), 6);
    const TArray<FPSPlayArtPrimitive> NextDown = Overlay->GetRouteArt();
    const FPSPlayArtPrimitive* Again = FindArt(NextDown, Wide, EPSPlayArtShape::Ribbon);
    TestTrue(TEXT("...the hot route gone with the old down"), Again && Again->Source == FName(TEXT("Slant")));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Who sees the art, and when
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayArtPolicyTest,
    "PlaySports.PlayArt.ShowHidePolicy",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayArtPolicyTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayArtTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSOverlayPlayArtSubsystem* Overlay = World ? World->GetSubsystem<UPSOverlayPlayArtSubsystem>() : nullptr;
    UPSVersusSubsystem* Versus = World ? World->GetSubsystem<UPSVersusSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Play art"), Overlay) || !TestNotNull(TEXT("Versus"), Versus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    Overlay->SetOverlayDetail(EPSOverlayDetail::Full);
    Overlay->SetRefreshHz(0.f);
    const FVector Line(1000.f, 0.f, 0.f);
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB_P"), FVector(800.f, 0.f, 100.f));
    APSPlayerPawn* WR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_P"), FVector(950.f, 800.f, 100.f));
    APSPlayerPawn* LB = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB_P"), FVector(1500.f, 0.f, 100.f));
    APSPlayerPawn* FS = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("FS_P"), FVector(2500.f, 600.f, 100.f));
    if (!TestTrue(TEXT("Players spawned"), QB && WR && LB && FS))
    {
        DestroyTestWorld(World);
        return false;
    }
    Bus->PublishGameState(LineAt(Line));
    PlayCall->OpenPlayCall(FirstAndTen());
    PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human);
    Overlay->AdvanceTime(0.f);
    TestEqual(TEXT("The slant and its ring"), Overlay->GetRouteArt().Num(), 2);

    // The platform tier: no art on Minimal; Simplified takes it away at the snap at once.
    Overlay->SetOverlayDetail(EPSOverlayDetail::Minimal);
    TestEqual(TEXT("A Minimal tier draws no route art"), Overlay->GetRouteArt().Num(), 0);
    Overlay->SetOverlayDetail(EPSOverlayDetail::Simplified);
    TestEqual(TEXT("A Simplified tier does"), Overlay->GetRouteArt().Num(), 2);

    // The player's setting.
    UPSSettingsSubsystem* Settings = NewObject<UPSSettingsSubsystem>(NewObject<UGameInstance>());
    Overlay->SetSettings(Settings);
    TestNotNull(TEXT("RouteArt is a setting"), Settings->GetCatalog().FindSetting(UPSOverlayPlayArtSubsystem::RouteArtSettingId));
    TestTrue(TEXT("...on by default"), Overlay->IsSettingOn(UPSOverlayPlayArtSubsystem::RouteArtSettingId));
    Settings->SetValue(UPSOverlayPlayArtSubsystem::RouteArtSettingId, 0.f);
    Overlay->Refresh();
    TestEqual(TEXT("Turned off, nothing is drawn"), Overlay->GetRouteArt().Num(), 0);
    Settings->SetValue(UPSOverlayPlayArtSubsystem::RouteArtSettingId, 1.f);
    Overlay->Refresh();
    TestEqual(TEXT("Turned on again, it is"), Overlay->GetRouteArt().Num(), 2);

    // A kick has no route art.
    PlayCall->CallPlay(TEXT("Offense_Punt"), EPSPlayCaller::Human);
    Overlay->AdvanceTime(0.f);
    TestEqual(TEXT("A punt draws none"), Overlay->GetRouteArt().Num(), 0);
    PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human);
    Overlay->AdvanceTime(0.f);
    TestEqual(TEXT("Back to Slant-Flat, it's drawn"), Overlay->GetRouteArt().Num(), 2);

    // Who sees it: the offense's own art, not the defense's.
    TestTrue(TEXT("A spectator sees the art"), Overlay->IsVisibleTo(nullptr));
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* P1 = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    APSPlayerController* P2 = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (TestTrue(TEXT("Two humans"), P1 && P2 && P1->TakeControlOf(QB) && P2->TakeControlOf(LB)))
    {
        TestTrue(TEXT("The quarterback's player sees his routes"), Overlay->IsVisibleTo(P1));
        TestFalse(TEXT("The defending player doesn't see the offense's"), Overlay->IsVisibleTo(P2));

        // Head to head, the house rules decide (Epic 107).
        const bool bSession = Versus->ClaimSeat(P1, 0) == 0 && Versus->ClaimSeat(P2, 1) == 1
            && Versus->SelectTeam(0, EPSVersusTeam::Home) && Versus->SelectTeam(1, EPSVersusTeam::Away)
            && Versus->SetReady(0, true) && Versus->SetReady(1, true) && Versus->StartSession();
        if (TestTrue(TEXT("Head-to-head session"), bSession))
        {
            TestFalse(TEXT("One shared screen: the offense's player doesn't see it either"), Overlay->IsVisibleTo(P1));
            FPSVersusRules Rules = Versus->GetRules();
            Rules.Screen = EPSVersusScreen::Split;
            Versus->SetRules(Rules);
            TestTrue(TEXT("Split screen: the offense sees it"), Overlay->IsVisibleTo(P1));
            TestFalse(TEXT("...the defense does not"), Overlay->IsVisibleTo(P2));
            Rules.RouteArtAudience = EPSVersusAudience::Everyone;
            Versus->SetRules(Rules);
            TestTrue(TEXT("House rules showing it to everyone show the defense too"), Overlay->IsVisibleTo(P2));
        }
    }

    // On a Simplified tier the snap takes the art away at once.
    Snap(Bus, Line);
    TestEqual(TEXT("Simplified: gone at the snap"), Overlay->GetRouteArt().Num(), 0);
    TestFalse(TEXT("...without a fade"), Overlay->IsFading());

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The style
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayArtStyleTest,
    "PlaySports.PlayArt.StyleValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayArtStyleTest::RunTest(const FString& Parameters)
{
    FPSPlayArtStyle Style;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    if (!TestTrue(TEXT("Data/play_art.json loads"), Ingestion->LoadPlayArtStyleFromJson(UPSOverlayPlayArtSubsystem::GetDefaultStylePath(), Style)))
    {
        return false;
    }
    for (const FString& Problem : UPSOverlayPlayArtSubsystem::ValidateStyle(Style))
    {
        AddError(FString::Printf(TEXT("play_art.json: %s"), *Problem));
    }
    TestTrue(TEXT("Kicks draw no route art"), Style.NoRouteArtCategories.Contains(TEXT("Punt")) && Style.NoRouteArtCategories.Contains(TEXT("Kickoff")));

    // Colors by read: past the list, its last; unranked, its own.
    FLinearColor Last = FLinearColor::Black;
    UPSUITeamCatalog::ParseHexColor(Style.ReadColors.Last(), Last);
    TestTrue(TEXT("A read past the list takes its last color"), PSPlayArt::ColorForRead(Style, Style.ReadColors.Num() + 3).Equals(Last));
    FLinearColor Unranked = FLinearColor::Black;
    UPSUITeamCatalog::ParseHexColor(Style.UnrankedColor, Unranked);
    TestTrue(TEXT("An unranked route takes the unranked color"), PSPlayArt::ColorForRead(Style, 0).Equals(Unranked));

    FPSPlayArtStyle Broken = Style;
    Broken.RibbonWidth = 0.f;
    Broken.ReadColors = { TEXT("gold") };
    Broken.BranchOpacity = 2.f;
    Broken.SnapFadeSeconds = -1.f;
    TestTrue(TEXT("An unsound style is reported"), UPSOverlayPlayArtSubsystem::ValidateStyle(Broken).Num() >= 4);
    FPSPlayArtStyle NoColors = Style;
    NoColors.ReadColors.Reset();
    TestTrue(TEXT("A style needs the primary read's color"), UPSOverlayPlayArtSubsystem::ValidateStyle(NoColors).Num() >= 1);
    return true;
}

#endif
