// PSPlayDiagramTests.cpp -- Epic 102.1 (play-call screen: play-art previews)
//
// Tests covered:
//   1. The geometry: arrowheads, rings and Xs; the field-to-widget transform (upfield is up, the
//      offense's right is right, one scale both ways, centred); widths scaled with a floor.
//   2. An offensive play's diagram is its compiled art laid flat: one route per ribbon through
//      the same points with an arrowhead, no rings, a T for every blocker, the quarterback's drop,
//      a ring for each offensive player, the defense faint, the line across the view; it fits a
//      phone's thumbnail, its portrait and landscape screens, and a monitor.
//   3. A defensive play's diagram: a star at each zone landmark with the defender's drop to it,
//      each rusher's arrow with its head, an X for each defender.
//   4. The play-call screen: every option that calls a play gets a preview, every play in the
//      shipped playbook builds one for the players on the field (whatever the art settings),
//      and nobody on the field or an unknown play builds none.
//   5. Data/play_art.json's Diagram block loads with the defaults and validates; an unsound one is
//      refused, through the play art style's validation too.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSDefenseController.h"
#include "PSFieldGrid.h"
#include "PSMenuScreenWidget.h"
#include "PSOffenseController.h"
#include "PSOverlayPlayArtSubsystem.h"
#include "PSPlayArt.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayDiagram.h"
#include "PSPlayResolution.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "PSWidgetDrawing.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPlayDiagramTests
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

    /** A pawn at Location under its side's AI controller. */
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
        }
        return Pawn;
    }

    /** 11 personnel against a base 4-3, lined up at Line as the game mode lines them up. */
    static TArray<APSPlayerPawn*> LineUpElevens(UWorld* World, const FVector& Line)
    {
        const TArray<EPlayerRole> Roles = {
            EPlayerRole::Quarterback, EPlayerRole::RunningBack, EPlayerRole::TightEnd,
            EPlayerRole::WideReceiver, EPlayerRole::WideReceiver, EPlayerRole::WideReceiver,
            EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman,
            EPlayerRole::DefensiveLineman, EPlayerRole::DefensiveLineman, EPlayerRole::DefensiveLineman, EPlayerRole::DefensiveLineman,
            EPlayerRole::Linebacker, EPlayerRole::Linebacker, EPlayerRole::Linebacker,
            EPlayerRole::DefensiveBack, EPlayerRole::DefensiveBack, EPlayerRole::DefensiveBack, EPlayerRole::DefensiveBack };
        const TArray<FVector> Spots = APSFieldGrid::ComputeLineup(Roles, Line.X);
        TArray<APSPlayerPawn*> Pawns;
        for (int32 Index = 0; Index < Roles.Num() && Index < Spots.Num(); ++Index)
        {
            Pawns.Add(SpawnPlayer(World, Roles[Index], FString::Printf(TEXT("P_%d"), Index), Spots[Index]));
        }
        return Pawns;
    }

    static void AnnounceLine(UPSTelemetryBus* Bus, const FVector& Line)
    {
        FPSTelemetryGameStateEvent Event;
        Event.Phase = TEXT("PreSnap");
        Event.LineOfScrimmage = Line;
        Bus->PublishGameState(Event);
    }

    static int32 CountTagged(const TArray<FPSWidgetStroke>& Strokes, const TCHAR* Tag)
    {
        const FName Wanted(Tag);
        int32 Count = 0;
        for (const FPSWidgetStroke& Stroke : Strokes)
        {
            Count += Stroke.Tag == Wanted ? 1 : 0;
        }
        return Count;
    }

    /** True when every point of Strokes lies inside a widget of Size. */
    static bool AllInside(const TArray<FPSWidgetStroke>& Strokes, const FVector2D& Size)
    {
        for (const FPSWidgetStroke& Stroke : Strokes)
        {
            for (const FVector2D& Point : Stroke.Points)
            {
                if (Point.X < -0.01 || Point.Y < -0.01 || Point.X > Size.X + 0.01 || Point.Y > Size.Y + 0.01)
                {
                    return false;
                }
            }
        }
        return true;
    }

    /** The phone's thumbnail, portrait and landscape screens, and a monitor (Slate units). */
    static TArray<FVector2D> ScreenSizes(const FPSPlayDiagramStyle& Style)
    {
        return { FVector2D(Style.PreviewWidth, Style.PreviewHeight), FVector2D(390.0, 844.0), FVector2D(844.0, 390.0), FVector2D(1920.0, 1080.0) };
    }

    static bool SamePoints(const TArray<FVector2D>& Flat, const TArray<FVector>& World)
    {
        if (Flat.Num() != World.Num())
        {
            return false;
        }
        for (int32 Index = 0; Index < Flat.Num(); ++Index)
        {
            if (!Flat[Index].Equals(FVector2D(World[Index].X, World[Index].Y), 0.1))
            {
                return false;
            }
        }
        return true;
    }

    static FVector2D Centroid(const TArray<FVector2D>& Points)
    {
        FVector2D Sum = FVector2D::ZeroVector;
        for (const FVector2D& Point : Points)
        {
            Sum += Point;
        }
        return Points.Num() > 0 ? Sum / Points.Num() : Sum;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The geometry
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayDiagramGeometryTest,
    "PlaySports.PlayDiagram.Geometry",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayDiagramGeometryTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayDiagramTests;

    // An arrowhead: its tip where the line ends, its barbs Length back, either side of the line.
    const TArray<FVector2D> Head = PSWidgetDrawing::ArrowHead(FVector2D(0.0, 0.0), FVector2D(100.0, 0.0), 10.f, 30.f);
    if (TestEqual(TEXT("An arrowhead is a barb, the tip and a barb"), Head.Num(), 3))
    {
        TestTrue(TEXT("The tip is where the line ends"), Head[1].Equals(FVector2D(100.0, 0.0), 0.01));
        TestEqual(TEXT("The left barb is Length back"), static_cast<float>(FVector2D::Distance(Head[0], Head[1])), 10.f, 0.01f);
        TestEqual(TEXT("The right barb is Length back"), static_cast<float>(FVector2D::Distance(Head[2], Head[1])), 10.f, 0.01f);
        TestTrue(TEXT("Both barbs lie behind the tip"), Head[0].X < 100.0 && Head[2].X < 100.0);
        TestEqual(TEXT("The barbs are either side of the line, alike"), static_cast<float>(Head[0].Y), static_cast<float>(-Head[2].Y), 0.01f);
        TestEqual(TEXT("At the half angle"), static_cast<float>(FMath::Abs(Head[0].Y)), 10.f * FMath::Sin(FMath::DegreesToRadians(30.f)), 0.01f);
    }
    TestEqual(TEXT("A line of no length has no head"), PSWidgetDrawing::ArrowHead(FVector2D(5.0, 5.0), FVector2D(5.0, 5.0), 10.f, 30.f).Num(), 0);

    const TArray<FVector2D> Ring = PSWidgetDrawing::Circle(FVector2D(10.0, 20.0), 50.f, 16);
    TestEqual(TEXT("A ring has its segments"), Ring.Num(), 16);
    bool bRound = true;
    for (const FVector2D& Point : Ring)
    {
        bRound &= FMath::IsNearlyEqual(static_cast<float>(FVector2D::Distance(Point, FVector2D(10.0, 20.0))), 50.f, 0.01f);
    }
    TestTrue(TEXT("Every point of a ring is its radius out"), bRound);
    TestEqual(TEXT("A ring has three points at least"), PSWidgetDrawing::Circle(FVector2D::ZeroVector, 5.f, 1).Num(), 3);
    const TArray<FVector2D> Arm = PSWidgetDrawing::CrossArm(FVector2D::ZeroVector, 10.f, true);
    TestTrue(TEXT("An X's arm runs through its centre, its ends Radius out"), Arm.Num() == 2 && Arm[0].Equals(-Arm[1], 0.01)
        && FMath::IsNearlyEqual(static_cast<float>(Arm[0].Size()), 10.f, 0.01f));

    // The transform: a view 2400 deep and 3600 across, centred 2000 upfield on the ball's Y.
    FPSPlayDiagram Diagram;
    Diagram.ViewCenter = FVector2D(2000.0, 0.0);
    Diagram.ViewExtent = FVector2D(1200.0, 1800.0);
    Diagram.Strokes.Add(PSWidgetDrawing::MakeStroke({ FVector2D(2000.0, 0.0), FVector2D(2100.0, 0.0) }, FLinearColor::White, 30.f, TEXT("Upfield")));
    Diagram.Strokes.Add(PSWidgetDrawing::MakeStroke({ FVector2D(2000.0, 0.0), FVector2D(2000.0, 100.0) }, FLinearColor::White, 5.f, TEXT("Right")));
    const FPSPlayDiagramTransform Wide = PSPlayDiagram::MakeTransform(Diagram, FVector2D(360.0, 240.0));
    TestEqual(TEXT("The view fits exactly when the shapes match"), Wide.Scale, 0.1f, 0.0001f);
    TestTrue(TEXT("The view's centre is the widget's"), Wide.ToWidget(FVector2D(2000.0, 0.0)).Equals(FVector2D(180.0, 120.0), 0.01));
    TestTrue(TEXT("Upfield is up"), Wide.ToWidget(FVector2D(2100.0, 0.0)).Equals(FVector2D(180.0, 110.0), 0.01));
    TestTrue(TEXT("The offense's right is right"), Wide.ToWidget(FVector2D(2000.0, 100.0)).Equals(FVector2D(190.0, 120.0), 0.01));

    // One scale both ways: a tall phone screen keeps the field's shape and centres it.
    const FPSPlayDiagramTransform Tall = PSPlayDiagram::MakeTransform(Diagram, FVector2D(390.0, 844.0));
    TestEqual(TEXT("A portrait screen fits the view's width"), Tall.Scale, static_cast<float>(390.0 / 3600.0), 0.0001f);
    const FVector2D Corner = Tall.ToWidget(FVector2D(3200.0, -1800.0));
    TestTrue(TEXT("The view's far left corner is on the screen's left edge, centred down it"), FMath::IsNearlyEqual(Corner.X, 0.0, 0.01)
        && FMath::IsNearlyEqual(Corner.Y, 422.0 - 1200.0 * Tall.Scale, 0.01));

    const TArray<FPSWidgetStroke> Placed = PSPlayDiagram::ToWidget(Diagram, FVector2D(360.0, 240.0), 1.5f);
    if (TestEqual(TEXT("Every stroke is placed"), Placed.Num(), 2))
    {
        TestEqual(TEXT("A width scales with the field"), Placed[0].Width, 3.f, 0.001f);
        TestEqual(TEXT("...but no line is thinner than the floor"), Placed[1].Width, 1.5f, 0.001f);
        TestTrue(TEXT("Points are placed by the transform"), Placed[0].Points[1].Equals(FVector2D(180.0, 110.0), 0.01));
    }
    TestEqual(TEXT("Nothing is placed in a widget of no size"), PSPlayDiagram::ToWidget(Diagram, FVector2D::ZeroVector, 1.5f).Num(), 0);
    TestEqual(TEXT("...or for a diagram of no view"), PSPlayDiagram::ToWidget(FPSPlayDiagram(), FVector2D(100.0, 100.0), 1.5f).Num(), 0);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- An offensive play laid flat
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayDiagramOffenseTest,
    "PlaySports.PlayDiagram.OffenseFromArt",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayDiagramOffenseTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayDiagramTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSOverlayPlayArtSubsystem* Overlay = World ? World->GetSubsystem<UPSOverlayPlayArtSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Play art"), Overlay))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    const FVector Line(2000.f, 0.f, 0.f);
    const TArray<APSPlayerPawn*> Pawns = LineUpElevens(World, Line);
    AnnounceLine(Bus, Line);
    const FPSPlayArtStyle& Style = Overlay->GetStyle();

    FPSPlayDefinition Play;
    FPSPlayDiagram Diagram;
    if (!TestTrue(TEXT("Slant-Flat is in the playbook"), PlayCall->FindPlay(TEXT("Offense_SlantFlat"), Play))
        || !TestTrue(TEXT("Its preview builds"), Overlay->BuildPlayDiagram(Play.PlayId, Diagram)))
    {
        DestroyTestWorld(World);
        return false;
    }
    TestEqual(TEXT("The diagram is of the play"), Diagram.PlayId, Play.PlayId);
    TestTrue(TEXT("...an offensive one"), Diagram.bOffense);

    // The same art the field draws: resolved for the players where they stand, compiled.
    const TArray<FPSResolvedAssignment> Resolved = PSPlayResolution::ResolvePlay(Play, Pawns, PlayCall->GetRouteLibrary(), Line, false);
    const TArray<FPSPlayArtPrimitive> Art = PSPlayArt::CompilePlayArt(Play, Resolved, PlayCall->GetRouteLibrary(), Style, Overlay->GetBreakMinAngleDegrees(), Line);
    int32 Ribbons = 0;
    for (const FPSPlayArtPrimitive& Piece : Art)
    {
        if (Piece.Shape != EPSPlayArtShape::Ribbon)
        {
            continue;
        }
        ++Ribbons;
        const bool bDrawn = Diagram.Strokes.ContainsByPredicate([&Piece](const FPSWidgetStroke& Stroke)
        {
            return Stroke.Tag == FName(TEXT("Route")) && PSPlayDiagramTests::SamePoints(Stroke.Points, Piece.Points);
        });
        TestTrue(FString::Printf(TEXT("The %s ribbon is a route through its points"), *Piece.Source.ToString()), bDrawn);
    }
    TestEqual(TEXT("Three slants and the flat"), Ribbons, 4);
    TestEqual(TEXT("One route per ribbon"), CountTagged(Diagram.Strokes, TEXT("Route")), Ribbons);
    // Each route and the quarterback's drop end in an arrowhead.
    TestEqual(TEXT("Every route and the drop end in an arrowhead"), CountTagged(Diagram.Strokes, TEXT("Arrowhead")), Ribbons + 1);
    TestEqual(TEXT("The quarterback's drop is shown"), CountTagged(Diagram.Strokes, TEXT("Spot")), 1);
    TestEqual(TEXT("A T (stem and bar) for the tight end and the five linemen"), CountTagged(Diagram.Strokes, TEXT("Block")), 12);
    TestEqual(TEXT("A ring for each offensive player"), CountTagged(Diagram.Strokes, TEXT("Player")), 11);
    TestEqual(TEXT("Each defender's X, faint"), CountTagged(Diagram.Strokes, TEXT("Opponent")), 22);
    TestEqual(TEXT("The line across the view"), CountTagged(Diagram.Strokes, TEXT("Line")), 1);
    TestEqual(TEXT("No ring: the arrowhead marks a route's end"), CountTagged(Diagram.Strokes, TEXT("Ring")), 0);
    TestEqual(TEXT("The view is centred across on the ball"), static_cast<float>(Diagram.ViewCenter.Y), static_cast<float>(Line.Y), 0.01f);
    TestTrue(TEXT("...at least the minimum field"), Diagram.ViewExtent.Y * 2.0 >= Style.Diagram.MinFieldWidth - 0.01
        && Diagram.ViewExtent.X * 2.0 >= Style.Diagram.MinFieldDepth - 0.01);

    const FPSWidgetStroke* Faint = Diagram.Strokes.FindByPredicate([](const FPSWidgetStroke& Stroke) { return Stroke.Tag == FName(TEXT("Opponent")); });
    TestTrue(TEXT("The defense is drawn at the opponent opacity"), Faint && FMath::IsNearlyEqual(Faint->Color.A, Style.Diagram.OpponentOpacity, 0.001f));
    const FPSWidgetStroke* Primary = Diagram.Strokes.FindByPredicate([&Style](const FPSWidgetStroke& Stroke)
    {
        return Stroke.Tag == FName(TEXT("Route")) && Stroke.Color.Equals(PSPlayArt::ColorForRead(Style, 1));
    });
    if (TestNotNull(TEXT("The primary read keeps its read's color"), Primary))
    {
        TestEqual(TEXT("...and its width"), Primary->Width, Style.RibbonWidth * Style.PrimaryWidthScale * Style.Diagram.WidthScale, 0.01f);
    }

    // It fits any screen, upfield up.
    for (const FVector2D& Size : ScreenSizes(Style.Diagram))
    {
        const TArray<FPSWidgetStroke> Placed = PSPlayDiagram::ToWidget(Diagram, Size, Style.Diagram.MinStrokeWidth);
        TestEqual(*FString::Printf(TEXT("Every stroke is placed at %.0fx%.0f"), Size.X, Size.Y), Placed.Num(), Diagram.Strokes.Num());
        TestTrue(FString::Printf(TEXT("Everything lies inside %.0fx%.0f"), Size.X, Size.Y), AllInside(Placed, Size));
        const FPSWidgetStroke* Route = Placed.FindByPredicate([](const FPSWidgetStroke& Stroke) { return Stroke.Tag == FName(TEXT("Route")); });
        TestTrue(FString::Printf(TEXT("A route ends higher up the screen than it starts at %.0fx%.0f"), Size.X, Size.Y),
            Route && Route->Points.Last().Y < Route->Points[0].Y);
        bool bThick = true;
        for (const FPSWidgetStroke& Stroke : Placed)
        {
            bThick &= Stroke.Width >= Style.Diagram.MinStrokeWidth;
        }
        TestTrue(TEXT("No line is thinner than the floor"), bThick);
    }

    // The blockers' marks sit on the blockers: a pass block's stem goes back toward the passer.
    const FPSWidgetStroke* Stem = Diagram.Strokes.FindByPredicate([](const FPSWidgetStroke& Stroke)
    {
        return Stroke.Tag == FName(TEXT("Block")) && Stroke.Points.Num() == 2 && FMath::IsNearlyEqual(Stroke.Points[0].Y, Stroke.Points[1].Y, 0.01);
    });
    TestTrue(TEXT("A pass blocker's stem goes back from his spot"), Stem && FMath::IsNearlyEqual(Stem->Points[1].X - Stem->Points[0].X, static_cast<double>(-Style.Diagram.PassBlockStemLength), 0.01));
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- A defensive play laid flat
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayDiagramDefenseTest,
    "PlaySports.PlayDiagram.DefenseFromArt",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayDiagramDefenseTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayDiagramTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSOverlayPlayArtSubsystem* Overlay = World ? World->GetSubsystem<UPSOverlayPlayArtSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Play art"), Overlay))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    const FVector Line(2000.f, 0.f, 0.f);
    const TArray<APSPlayerPawn*> Pawns = LineUpElevens(World, Line);
    AnnounceLine(Bus, Line);
    const FPSPlayArtStyle& Style = Overlay->GetStyle();

    FPSPlayDefinition Play;
    FPSPlayDiagram Diagram;
    if (!TestTrue(TEXT("Cover 2 is in the playbook"), PlayCall->FindPlay(TEXT("Defense_43Cover2"), Play))
        || !TestTrue(TEXT("Its preview builds"), Overlay->BuildPlayDiagram(Play.PlayId, Diagram)))
    {
        DestroyTestWorld(World);
        return false;
    }
    TestFalse(TEXT("The diagram is a defensive one"), Diagram.bOffense);
    TestEqual(TEXT("A star at each of the four zone landmarks"), CountTagged(Diagram.Strokes, TEXT("Zone")), 4);
    TestEqual(TEXT("An arrow for each of the four rushers"), CountTagged(Diagram.Strokes, TEXT("Rush")), 4);
    TestEqual(TEXT("...each with its head"), CountTagged(Diagram.Strokes, TEXT("Arrowhead")), 4);
    TestEqual(TEXT("Two arms of an X for each defender"), CountTagged(Diagram.Strokes, TEXT("Player")), 22);
    TestEqual(TEXT("The offense's rings, faint"), CountTagged(Diagram.Strokes, TEXT("Opponent")), 11);
    TestEqual(TEXT("A run fit draws nothing; no routes or blocks for a defense"), CountTagged(Diagram.Strokes, TEXT("Route")) + CountTagged(Diagram.Strokes, TEXT("Block")), 0);

    // Each star sits on its defender's landmark, and his drop runs to it from where he stands.
    const TArray<FPSResolvedAssignment> Resolved = PSPlayResolution::ResolvePlay(Play, Pawns, PlayCall->GetRouteLibrary(), Line, false);
    int32 Drops = 0;
    for (const FPSResolvedAssignment& Entry : Resolved)
    {
        if (!Entry.bHasSlot || Entry.DefensiveType != EPSDefensiveAssignmentType::ZoneCoverage)
        {
            continue;
        }
        const FVector2D Landmark = PSPlayDiagram::ToField(Entry.GetZoneLandmark());
        const bool bStar = Diagram.Strokes.ContainsByPredicate([&Landmark](const FPSWidgetStroke& Stroke)
        {
            return Stroke.Tag == FName(TEXT("Zone")) && Stroke.bClosed && Stroke.Points.Num() == 10 && PSPlayDiagramTests::Centroid(Stroke.Points).Equals(Landmark, 1.0);
        });
        TestTrue(TEXT("A star on the zone defender's landmark"), bStar);
        const FVector2D Spot = PSPlayDiagram::ToField(Entry.PawnLocation);
        if (FVector2D::Distance(Spot, Landmark) > Style.Diagram.PlayerRadius + Style.ZoneStarRadius)
        {
            ++Drops;
            const bool bDrop = Diagram.Strokes.ContainsByPredicate([&Spot, &Landmark](const FPSWidgetStroke& Stroke)
            {
                return Stroke.Tag == FName(TEXT("Drop")) && Stroke.Points.Num() == 2 && Stroke.Points[0].Equals(Spot, 0.1)
                    && FVector2D::Distance(Stroke.Points[1], Landmark) < FVector2D::Distance(Spot, Landmark);
            });
            TestTrue(TEXT("His drop runs from his spot toward it"), bDrop);
        }
    }
    TestTrue(TEXT("The deep zones are dropped to"), Drops > 0);
    TestEqual(TEXT("One drop per zone defender away from his landmark"), CountTagged(Diagram.Strokes, TEXT("Drop")), Drops);

    // A rusher's arrow points into the backfield: down the screen.
    for (const FVector2D& Size : ScreenSizes(Style.Diagram))
    {
        const TArray<FPSWidgetStroke> Placed = PSPlayDiagram::ToWidget(Diagram, Size, Style.Diagram.MinStrokeWidth);
        TestTrue(FString::Printf(TEXT("Everything lies inside %.0fx%.0f"), Size.X, Size.Y), AllInside(Placed, Size));
        const FPSWidgetStroke* Rush = Placed.FindByPredicate([](const FPSWidgetStroke& Stroke) { return Stroke.Tag == FName(TEXT("Rush")); });
        TestTrue(TEXT("A rush goes down the screen, at the offense"), Rush && Rush->Points.Last().Y > Rush->Points[0].Y);
    }
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The play-call screen's previews
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayDiagramPreviewTest,
    "PlaySports.PlayDiagram.PlayCallPreviews",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayDiagramPreviewTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayDiagramTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSOverlayPlayArtSubsystem* Overlay = World ? World->GetSubsystem<UPSOverlayPlayArtSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Play art"), Overlay))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    // Nobody on the field: nothing to resolve a preview for.
    FPSPlayDiagram Diagram;
    TestFalse(TEXT("No preview with nobody on the field"), Overlay->BuildPlayDiagram(TEXT("Offense_SlantFlat"), Diagram));
    TestTrue(TEXT("...and the diagram is left empty"), Diagram.IsEmpty());

    const FVector Line(2000.f, 0.f, 0.f);
    LineUpElevens(World, Line);
    AnnounceLine(Bus, Line);
    TestFalse(TEXT("No preview of a play the playbook doesn't have"), Overlay->BuildPlayDiagram(TEXT("Offense_NoSuchPlay"), Diagram));

    // Even on a tier that draws no field art, and before any call, every play previews.
    Overlay->SetOverlayDetail(EPSOverlayDetail::Minimal);
    const FPSPlayDiagramStyle& Look = Overlay->GetStyle().Diagram;
    const FVector2D Thumbnail(Look.PreviewWidth, Look.PreviewHeight);
    const TArray<FPSPlayDefinition> Playbook = PlayCall->GetPlaybook();
    TestTrue(TEXT("The playbook loads"), Playbook.Num() > 10);
    for (const FPSPlayDefinition& Play : Playbook)
    {
        const FString Name = Play.PlayId.ToString();
        FPSPlayDiagram Preview;
        if (!TestTrue(FString::Printf(TEXT("%s: its preview builds"), *Name), Overlay->BuildPlayDiagram(Play.PlayId, Preview)))
        {
            continue;
        }
        TestEqual(*FString::Printf(TEXT("%s: the preview is of the play"), *Name), Preview.PlayId, Play.PlayId);
        TestTrue(FString::Printf(TEXT("%s: of its side"), *Name), Preview.bOffense == Play.bIsOffensivePlay);
        TestTrue(FString::Printf(TEXT("%s: the side's players are drawn"), *Name), CountTagged(Preview.Strokes, TEXT("Player")) > 0);
        TestTrue(FString::Printf(TEXT("%s: it fits its thumbnail"), *Name), AllInside(PSPlayDiagram::ToWidget(Preview, Thumbnail, Look.MinStrokeWidth), Thumbnail));
    }

    // Every option on the play lists calls a play, so the screen shows each one's preview.
    int32 PlayOptions = 0;
    for (const bool bOffense : { true, false })
    {
        for (const FString& FormationName : PlayCall->GetFormations(bOffense))
        {
            for (const FPSMenuOptionDef& Option : PlayCall->BuildPlayOptions(FormationName, bOffense))
            {
                ++PlayOptions;
                TestTrue(FString::Printf(TEXT("%s: its option gets a preview"), *Option.Payload.ToString()), UPSMenuScreenWidget::ShowsPlayPreview(Option));
            }
        }
    }
    TestTrue(TEXT("Both sides list plays"), PlayOptions > 0);

    // Options that call no play get none.
    FPSMenuOptionDef Formation;
    Formation.OptionId = TEXT("Formation");
    Formation.TargetScreen = TEXT("PlayCallPlays");
    Formation.Payload = TEXT("Trips Right");
    TestFalse(TEXT("A formation option gets no preview"), UPSMenuScreenWidget::ShowsPlayPreview(Formation));
    FPSMenuOptionDef Empty;
    Empty.Command = EPSMenuCommand::CallPlay;
    TestFalse(TEXT("A call with no play gets none"), UPSMenuScreenWidget::ShowsPlayPreview(Empty));
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The style
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayDiagramStyleTest,
    "PlaySports.PlayDiagram.StyleValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayDiagramStyleTest::RunTest(const FString& Parameters)
{
    FPSPlayArtStyle Style;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    if (!TestTrue(TEXT("Data/play_art.json loads"), Ingestion->LoadPlayArtStyleFromJson(UPSOverlayPlayArtSubsystem::GetDefaultStylePath(), Style)))
    {
        return false;
    }
    for (const FString& Problem : PSPlayDiagram::ValidateStyle(Style.Diagram))
    {
        AddError(FString::Printf(TEXT("play_art.json Diagram: %s"), *Problem));
    }
    // The struct's defaults are the file's.
    const FPSPlayDiagramStyle Defaults;
    TestEqual(TEXT("MinFieldWidth"), Style.Diagram.MinFieldWidth, Defaults.MinFieldWidth);
    TestEqual(TEXT("MinFieldDepth"), Style.Diagram.MinFieldDepth, Defaults.MinFieldDepth);
    TestEqual(TEXT("MinStrokeWidth"), Style.Diagram.MinStrokeWidth, Defaults.MinStrokeWidth);
    TestEqual(TEXT("ArrowheadLength"), Style.Diagram.ArrowheadLength, Defaults.ArrowheadLength);
    TestEqual(TEXT("OpponentOpacity"), Style.Diagram.OpponentOpacity, Defaults.OpponentOpacity);
    TestEqual(TEXT("CircleSegments"), Style.Diagram.CircleSegments, Defaults.CircleSegments);
    TestEqual(TEXT("DefenseColor"), Style.Diagram.DefenseColor, Defaults.DefenseColor);
    TestEqual(TEXT("PreviewWidth"), Style.Diagram.PreviewWidth, Defaults.PreviewWidth);
    TestEqual(TEXT("PreviewHeight"), Style.Diagram.PreviewHeight, Defaults.PreviewHeight);

    FPSPlayArtStyle Broken = Style;
    Broken.Diagram.MinFieldWidth = 0.f;
    Broken.Diagram.OpponentOpacity = 1.5f;
    Broken.Diagram.ArrowheadAngleDegrees = 90.f;
    Broken.Diagram.CircleSegments = 4;
    Broken.Diagram.BlockColor = TEXT("grey");
    TestEqual(TEXT("Each unsound value is reported"), PSPlayDiagram::ValidateStyle(Broken.Diagram).Num(), 5);
    const TArray<FString> ArtProblems = UPSOverlayPlayArtSubsystem::ValidateStyle(Broken);
    TestTrue(TEXT("...through the play art style's validation, so the file is refused"),
        ArtProblems.ContainsByPredicate([](const FString& Problem) { return Problem.StartsWith(TEXT("Diagram.")); }));
    return true;
}

#endif
