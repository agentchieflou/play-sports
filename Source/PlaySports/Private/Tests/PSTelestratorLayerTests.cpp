// PSTelestratorLayerTests.cpp -- Epic 44.1 (the telestrator's drawing layer and the way into analysis)
//
// Tests covered:
//   1. The layer's geometry, for a monitor and a phone held either way: screen points to and
//      from the frame's normalized space (kept on the screen), each tool's lines (a dot for a
//      tap, an arrow's head, a circle round on the screen, a player's ring), widths from the
//      shorter side with a floor, auto-annotation colors, the stroke in progress, the cursor.
//   2. The cursor: the shipped catalog's TelestratorCursor bindings (keys, both ways, the stick
//      past its dead zone) and its movement, kept on the screen.
//   3. Analysis mode: the Telestrator action of a player looking through the broadcast camera
//      toggles it over what the camera shows (nothing without a replay or the film view); the
//      catalog has it in a replay only, never on the field's keys; the layer's buttons (tool,
//      undo, clear, leave) through RunLayerAction and the catalog's keys for them; the stroke in
//      progress; the touch layer standing down while it is on; unbinding.
//   4. Data/telestrator.json's drawing-layer fields load with the defaults and validate.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "PSBroadcastCamera.h"
#include "PSCameraAll22Component.h"
#include "PSDataIngestion.h"
#include "PSInputConfig.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSTelestratorLayer.h"
#include "PSTelestratorSubsystem.h"
#include "PSTouchInputComponent.h"
#include "PSUITeamCatalog.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSTelestratorLayerTests
{
    UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    void DestroyTestWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    FPSTelestratorTuning LoadTuning()
    {
        FPSTelestratorTuning Tuning;
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        Ingestion->LoadTelestratorTuningFromJson(UPSTelestratorSubsystem::GetDefaultTuningPath(), Tuning);
        return Tuning;
    }

    int32 CountTagged(const TArray<FPSWidgetStroke>& Strokes, const TCHAR* Tag)
    {
        const FName Wanted(Tag);
        int32 Count = 0;
        for (const FPSWidgetStroke& Stroke : Strokes)
        {
            Count += Stroke.Tag == Wanted ? 1 : 0;
        }
        return Count;
    }

    const FPSWidgetStroke* FindTagged(const TArray<FPSWidgetStroke>& Strokes, const TCHAR* Tag)
    {
        const FName Wanted(Tag);
        return Strokes.FindByPredicate([Wanted](const FPSWidgetStroke& Stroke) { return Stroke.Tag == Wanted; });
    }

    /** True when every point of Stroke is Radius from Center, give or take Tolerance. */
    bool IsRing(const FPSWidgetStroke& Stroke, const FVector2D& Center, float Radius, float Tolerance)
    {
        for (const FVector2D& Point : Stroke.Points)
        {
            if (!FMath::IsNearlyEqual(static_cast<float>(FVector2D::Distance(Point, Center)), Radius, Tolerance))
            {
                return false;
            }
        }
        return Stroke.Points.Num() > 0 && Stroke.bClosed;
    }

    FPSTelestratorMark MakeMark(EPSTelestratorTool Tool, const TArray<FVector2D>& Points, bool bAuto = false)
    {
        FPSTelestratorMark Mark;
        Mark.Tool = Tool;
        Mark.ScreenPoints = Points;
        Mark.bAuto = bAuto;
        return Mark;
    }
}

// ---------------------------------------------------------------------------
// 1. The layer's geometry
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSTelestratorLayerGeometryTest,
    "PlaySports.Telestrator.LayerGeometry",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelestratorLayerGeometryTest::RunTest(const FString& Parameters)
{
    using namespace PSTelestratorLayerTests;

    const FPSTelestratorTuning Tuning = LoadTuning();
    const FVector2D Monitor(1920.0, 1080.0);
    const FVector2D Portrait(390.0, 844.0);
    const FVector2D Landscape(844.0, 390.0);

    // Screen points and the frame's normalized space.
    TestTrue(TEXT("A point maps to the frame and back"), PSTelestratorLayer::ToWidget(PSTelestratorLayer::ToFrame(FVector2D(480.0, 270.0), Monitor), Monitor)
        .Equals(FVector2D(480.0, 270.0), 0.01));
    TestTrue(TEXT("The frame's middle is the screen's"), PSTelestratorLayer::ToFrame(FVector2D(195.0, 422.0), Portrait).Equals(FVector2D(0.5, 0.5), 0.0001));
    TestTrue(TEXT("A point off the screen is kept on it"), PSTelestratorLayer::ToFrame(FVector2D(-50.0, 2000.0), Landscape).Equals(FVector2D(0.0, 1.0), 0.0001));
    TestTrue(TEXT("An empty widget gives the middle"), PSTelestratorLayer::ToFrame(FVector2D(10.0, 10.0), FVector2D::ZeroVector).Equals(FVector2D(0.5, 0.5), 0.0001));
    TestEqual(TEXT("The shorter side of a portrait phone"), PSTelestratorLayer::ShortSide(Portrait), 390.f);

    // The tools cycle.
    TestEqual(TEXT("Freehand, then arrow"), PSTelestratorLayer::NextTool(EPSTelestratorTool::Freehand), EPSTelestratorTool::Arrow);
    TestEqual(TEXT("...circle"), PSTelestratorLayer::NextTool(EPSTelestratorTool::Arrow), EPSTelestratorTool::Circle);
    TestEqual(TEXT("...player"), PSTelestratorLayer::NextTool(EPSTelestratorTool::Circle), EPSTelestratorTool::Player);
    TestEqual(TEXT("...and round to freehand"), PSTelestratorLayer::NextTool(EPSTelestratorTool::Player), EPSTelestratorTool::Freehand);

    FLinearColor HandColor = FLinearColor::Black;
    FLinearColor AutoColor = FLinearColor::Black;
    UPSUITeamCatalog::ParseHexColor(Tuning.MarkColor, HandColor);
    UPSUITeamCatalog::ParseHexColor(Tuning.AutoMarkColor, AutoColor);

    // Freehand: a line through its points, sized from the shorter side.
    const TArray<FPSWidgetStroke> Freehand = PSTelestratorLayer::MarkStrokes(EPSTelestratorTool::Freehand,
        { FVector2D(0.1, 0.1), FVector2D(0.2, 0.15), FVector2D(0.3, 0.3) }, HandColor, Monitor, Tuning);
    if (TestEqual(TEXT("A freehand mark is one line"), Freehand.Num(), 1))
    {
        TestEqual(TEXT("...through its points"), Freehand[0].Points.Num(), 3);
        TestTrue(TEXT("...on the screen"), Freehand[0].Points[2].Equals(FVector2D(576.0, 324.0), 0.01));
        TestEqual(TEXT("...as wide as its share of the shorter side"), Freehand[0].Width, FMath::Max(Tuning.MarkWidth * 1080.f, Tuning.MinStrokeWidth), 0.001f);
    }
    const TArray<FPSWidgetStroke> Tiny = PSTelestratorLayer::MarkStrokes(EPSTelestratorTool::Freehand, { FVector2D(0.1, 0.1), FVector2D(0.2, 0.2) },
        HandColor, FVector2D(100.0, 100.0), Tuning);
    TestTrue(TEXT("On a tiny screen no line is thinner than the floor"), Tiny.Num() == 1 && FMath::IsNearlyEqual(Tiny[0].Width, Tuning.MinStrokeWidth));
    const TArray<FPSWidgetStroke> Tap = PSTelestratorLayer::MarkStrokes(EPSTelestratorTool::Freehand, { FVector2D(0.5, 0.5) }, HandColor, Portrait, Tuning);
    TestTrue(TEXT("A freehand tap is a dot"), Tap.Num() == 1 && Tap[0].bClosed);

    // Arrow: a line and its head.
    const TArray<FPSWidgetStroke> Arrow = PSTelestratorLayer::MarkStrokes(EPSTelestratorTool::Arrow, { FVector2D(0.2, 0.5), FVector2D(0.8, 0.5) },
        HandColor, Landscape, Tuning);
    TestEqual(TEXT("An arrow is a line and a head"), Arrow.Num(), 2);
    const FPSWidgetStroke* Head = FindTagged(Arrow, TEXT("Arrowhead"));
    if (TestNotNull(TEXT("The arrow has its head"), Head) && TestEqual(TEXT("...of a barb, the tip and a barb"), Head->Points.Num(), 3))
    {
        const FVector2D Tip = PSTelestratorLayer::ToWidget(FVector2D(0.8, 0.5), Landscape);
        TestTrue(TEXT("...at the arrow's head"), Head->Points[1].Equals(Tip, 0.01));
        TestEqual(TEXT("...its barbs ArrowheadLength of the shorter side back"), static_cast<float>(FVector2D::Distance(Head->Points[0], Tip)),
            Tuning.ArrowheadLength * 390.f, 0.01f);
    }

    // Circle: round on the screen, whatever the widget's shape.
    const TArray<FPSWidgetStroke> Circle = PSTelestratorLayer::MarkStrokes(EPSTelestratorTool::Circle, { FVector2D(0.5, 0.5), FVector2D(0.6, 0.5) },
        HandColor, Portrait, Tuning);
    if (TestEqual(TEXT("A circle is one ring"), Circle.Num(), 1))
    {
        TestTrue(TEXT("...round on a portrait phone, through the point it was dragged to"), IsRing(Circle[0], FVector2D(195.0, 422.0), 39.f, 0.05f));
        TestEqual(TEXT("...of its segments"), Circle[0].Points.Num(), Tuning.CircleSegments);
    }
    TestEqual(TEXT("A circle of no size draws nothing"), PSTelestratorLayer::MarkStrokes(EPSTelestratorTool::Circle, { FVector2D(0.5, 0.5), FVector2D(0.5, 0.5) },
        HandColor, Portrait, Tuning).Num(), 0);

    // Player: ringed where he stood.
    const TArray<FPSWidgetStroke> Player = PSTelestratorLayer::MarkStrokes(EPSTelestratorTool::Player, { FVector2D(0.25, 0.75) }, HandColor, Monitor, Tuning);
    TestTrue(TEXT("A highlighted player is ringed PlayerRingRadius of the shorter side out"),
        Player.Num() == 1 && IsRing(Player[0], FVector2D(480.0, 810.0), Tuning.PlayerRingRadius * 1080.f, 0.05f));
    TestEqual(TEXT("A mark with no points draws nothing"), PSTelestratorLayer::MarkStrokes(EPSTelestratorTool::Player, {}, HandColor, Monitor, Tuning).Num(), 0);

    // Everything on the frame: marks in their colors, then the stroke in progress.
    FPSTelestration Telestration;
    Telestration.Marks.Add(MakeMark(EPSTelestratorTool::Arrow, { FVector2D(0.1, 0.1), FVector2D(0.4, 0.4) }));
    Telestration.Marks.Add(MakeMark(EPSTelestratorTool::Circle, { FVector2D(0.6, 0.6), FVector2D(0.7, 0.6) }, true));
    const TArray<FPSWidgetStroke> Drawn = PSTelestratorLayer::BuildStrokes(Telestration, { FVector2D(0.2, 0.8), FVector2D(0.3, 0.7), FVector2D(0.4, 0.8) },
        EPSTelestratorTool::Freehand, Monitor, Tuning);
    const FPSWidgetStroke* HandArrow = FindTagged(Drawn, TEXT("Arrow"));
    const FPSWidgetStroke* AutoCircle = FindTagged(Drawn, TEXT("Circle"));
    TestTrue(TEXT("A hand-drawn mark is in the mark color"), HandArrow && HandArrow->Color.Equals(HandColor));
    TestTrue(TEXT("An auto-annotation's mark is in its own color"), AutoCircle && AutoCircle->Color.Equals(AutoColor));
    const FPSWidgetStroke* Live = FindTagged(Drawn, TEXT("Live"));
    TestTrue(TEXT("The freehand stroke in progress is drawn so far, over the marks"), Live && Live->Points.Num() == 3 && Live->Layer > 0);
    const TArray<FPSWidgetStroke> LiveArrow = PSTelestratorLayer::BuildStrokes(FPSTelestration(), { FVector2D(0.2, 0.2), FVector2D(0.3, 0.3), FVector2D(0.5, 0.2) },
        EPSTelestratorTool::Arrow, Monitor, Tuning);
    TestEqual(TEXT("An arrow in progress runs from where it started to where it is, with its head"), CountTagged(LiveArrow, TEXT("Live")), 2);
    const FPSWidgetStroke* LiveShaft = FindTagged(LiveArrow, TEXT("Live"));
    TestTrue(TEXT("...a straight line"), LiveShaft && LiveShaft->Points.Num() == 2 && LiveShaft->Points[1].Equals(PSTelestratorLayer::ToWidget(FVector2D(0.5, 0.2), Monitor), 0.01));
    TestEqual(TEXT("A player tap shows nothing until it lands"), PSTelestratorLayer::BuildStrokes(FPSTelestration(), { FVector2D(0.5, 0.5) },
        EPSTelestratorTool::Player, Monitor, Tuning).Num(), 0);

    const TArray<FPSWidgetStroke> CursorMarks = PSTelestratorLayer::CursorStrokes(FVector2D(100.0, 100.0), Portrait, Tuning);
    TestEqual(TEXT("The cursor is a ring and a cross"), CountTagged(CursorMarks, TEXT("Cursor")), 3);
    TestTrue(TEXT("...its ring CursorRadius of the shorter side round it"), CursorMarks.Num() > 0 && IsRing(CursorMarks[0], FVector2D(100.0, 100.0), Tuning.CursorRadius * 390.f, 0.05f));
    return true;
}

// ---------------------------------------------------------------------------
// 2. The cursor
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSTelestratorCursorTest,
    "PlaySports.Telestrator.LayerCursor",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelestratorCursorTest::RunTest(const FString& Parameters)
{
    using namespace PSTelestratorLayerTests;

    UPSInputConfig* Config = NewObject<UPSInputConfig>();
    if (!TestTrue(TEXT("The input catalog loads"), Config->LoadDefaults()))
    {
        return false;
    }
    const UPSTelestratorSubsystem* Defaults = GetDefault<UPSTelestratorSubsystem>();
    const FPSInputActionDef* CursorAction = Config->Catalog.Actions.FindByPredicate([Defaults](const FPSInputActionDef& Def) { return Def.ActionId == Defaults->CursorActionId; });
    if (!TestNotNull(TEXT("The catalog has the telestrator's cursor"), CursorAction))
    {
        return false;
    }
    const TArray<FPSInputKeyBinding>& Bindings = CursorAction->Bindings;
    const float DeadZone = 0.2f;
    auto Push = [&Bindings, DeadZone](const TSet<FName>& Held, const FVector2D& Stick)
    {
        return PSTelestratorLayer::CursorDirection(Bindings, Held, Stick, FVector2D::ZeroVector, DeadZone);
    };
    TestTrue(TEXT("Nothing held, nothing tilted: still"), Push({}, FVector2D::ZeroVector).IsNearlyZero());
    TestTrue(TEXT("W pushes up"), Push({ FName(TEXT("W")) }, FVector2D::ZeroVector).Equals(FVector2D(0.0, 1.0), 0.0001));
    TestTrue(TEXT("Down pushes down"), Push({ FName(TEXT("Down")) }, FVector2D::ZeroVector).Equals(FVector2D(0.0, -1.0), 0.0001));
    TestTrue(TEXT("A pushes left"), Push({ FName(TEXT("A")) }, FVector2D::ZeroVector).Equals(FVector2D(-1.0, 0.0), 0.0001));
    TestTrue(TEXT("W and D push up and right, no faster than one key"), Push({ FName(TEXT("W")), FName(TEXT("D")) }, FVector2D::ZeroVector)
        .Equals(FVector2D(UE_HALF_SQRT_2, UE_HALF_SQRT_2), 0.0001));
    TestTrue(TEXT("Left and Right cancel"), Push({ FName(TEXT("Left")), FName(TEXT("Right")) }, FVector2D::ZeroVector).IsNearlyZero());
    TestTrue(TEXT("A key the cursor doesn't use does nothing"), Push({ FName(TEXT("Q")) }, FVector2D::ZeroVector).IsNearlyZero());
    TestTrue(TEXT("The stick inside its dead zone does nothing"), Push({}, FVector2D(0.15, 0.0)).IsNearlyZero());
    TestTrue(TEXT("Past it, the tilt counts from its edge"), Push({}, FVector2D(0.6, 0.0)).Equals(FVector2D(0.5, 0.0), 0.0001));
    TestTrue(TEXT("Full tilt is full speed"), Push({}, FVector2D(0.0, 1.0)).Equals(FVector2D(0.0, 1.0), 0.0001));

    // Moving: up the screen is up, a share of the shorter side a second, and never off it.
    const FPSTelestratorTuning Tuning = LoadTuning();
    const FVector2D Portrait(390.0, 844.0);
    const FVector2D Moved = PSTelestratorLayer::MoveCursor(FVector2D(195.0, 422.0), FVector2D(0.0, 1.0), 0.5f, Portrait, Tuning);
    TestTrue(TEXT("Up the stick is up the screen, CursorSpeed shorter sides a second"), Moved.Equals(FVector2D(195.0, 422.0 - Tuning.CursorSpeed * 390.0 * 0.5), 0.01));
    const FVector2D Edge = PSTelestratorLayer::MoveCursor(FVector2D(380.0, 10.0), FVector2D(1.0, 1.0).GetSafeNormal(), 2.f, Portrait, Tuning);
    TestTrue(TEXT("The cursor stays on the screen"), Edge.X <= 390.0 && Edge.Y >= 0.0 && FMath::IsNearlyEqual(Edge.X, 390.0) && FMath::IsNearlyEqual(Edge.Y, 0.0));
    return true;
}

// ---------------------------------------------------------------------------
// 3. Analysis mode: the way in, the layer's buttons, touch standing down
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSTelestratorAnalysisModeTest,
    "PlaySports.Telestrator.AnalysisMode",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelestratorAnalysisModeTest::RunTest(const FString& Parameters)
{
    using namespace PSTelestratorLayerTests;

    UWorld* World = CreateTestWorld();
    UPSTelestratorSubsystem* Telestrator = World ? World->GetSubsystem<UPSTelestratorSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Game worlds have the telestrator"), Telestrator))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerPawn* Receiver = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector(1500.0, 900.0, 90.0), FRotator::ZeroRotator, SpawnParams);
    APSBroadcastCamera* Camera = World->SpawnActor<APSBroadcastCamera>(APSBroadcastCamera::StaticClass(), FVector(0.0, -2800.0, 600.0), FRotator(-10.0, 90.0, 0.0), SpawnParams);
    APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    UPSTouchInputComponent* Touch = Controller ? Controller->GetTouchInputComponent() : nullptr;
    if (!TestTrue(TEXT("A player, the broadcast camera and a controller"), Receiver && Camera && Controller && Touch))
    {
        DestroyTestWorld(World);
        return false;
    }
    FPlayerAttributes Attributes;
    Attributes.PlayerId = TEXT("WR_01");
    Attributes.DisplayName = TEXT("WR_01");
    Attributes.Role = EPlayerRole::WideReceiver;
    Receiver->InitializePlayer(Attributes);
    Controller->Possess(Receiver);
    Touch->SetViewportSize(FVector2D(2400.0, 1100.0));

    // The catalog: the way in on the field and in a replay, and the layer's own keys.
    UPSInputConfig* Config = Controller->GetInputConfig();
    if (TestNotNull(TEXT("The controller has the input catalog"), Config))
    {
        const FName Replay(TEXT("Replay"));
        TestEqual(TEXT("D-pad Up enters analysis in a replay"), Config->FindActionForKey(EKeys::Gamepad_DPad_Up, Replay), Telestrator->AnalysisActionId);
        TestEqual(TEXT("...and so does a flick of the right stick up"), Config->FindActionForKey(EKeys::Gamepad_RightStick_Up, Replay), Telestrator->AnalysisActionId);
        TestEqual(TEXT("...and Y on the keyboard"), Config->FindActionForKey(EKeys::Y, Replay), Telestrator->AnalysisActionId);
        // On the field D-pad Up is both sides' audible before the snap, and must be theirs alone.
        TestTrue(TEXT("The telestrator takes no key on the field"), Config->FindActionForKey(EKeys::Gamepad_DPad_Up, TEXT("OnField")).IsNone()
            && Config->FindActionForKey(EKeys::Gamepad_RightStick_Up, TEXT("OnField")).IsNone());
        const FName Layer = Telestrator->LayerContextId;
        TestEqual(TEXT("A draws at the cursor"), Config->FindActionForKey(EKeys::Gamepad_FaceButton_Bottom, Layer), Telestrator->DrawActionId);
        TestEqual(TEXT("...and Space"), Config->FindActionForKey(EKeys::SpaceBar, Layer), Telestrator->DrawActionId);
        TestEqual(TEXT("X is the next tool"), Config->FindActionForKey(EKeys::Gamepad_FaceButton_Left, Layer), Telestrator->ToolActionId);
        TestEqual(TEXT("Y takes the last mark back"), Config->FindActionForKey(EKeys::Gamepad_FaceButton_Top, Layer), Telestrator->UndoActionId);
        TestEqual(TEXT("LB clears"), Config->FindActionForKey(EKeys::Gamepad_LeftShoulder, Layer), Telestrator->ClearActionId);
        TestEqual(TEXT("RB saves the still"), Config->FindActionForKey(EKeys::Gamepad_RightShoulder, Layer), Telestrator->SaveActionId);
        TestEqual(TEXT("B leaves"), Config->FindActionForKey(EKeys::Gamepad_FaceButton_Right, Layer), Telestrator->ExitActionId);
        TestEqual(TEXT("...and so does the button that came in"), Config->FindActionForKey(EKeys::Gamepad_DPad_Up, Layer), Telestrator->AnalysisActionId);
        TestEqual(TEXT("The arrows move the cursor"), Config->FindActionForKey(EKeys::Up, Layer), Telestrator->CursorActionId);
    }

    // Whoever looks through the broadcast camera has the way in; with nothing to draw on it
    // does nothing.
    Telestrator->BindController(Controller);
    Controller->OnCatalogActionStarted.Broadcast(Telestrator->AnalysisActionId);
    TestFalse(TEXT("No analysis without a replay or the film view"), Telestrator->IsAnalysisActive());
    TestFalse(TEXT("...and the layer's buttons do nothing outside it"), Telestrator->RunLayerAction(Telestrator->ToolActionId));

    const int32 ControlsBefore = Touch->GetActiveControls().Num();
    TestTrue(TEXT("Touch shows the field's controls"), ControlsBefore > 0);
    TestTrue(TEXT("The film view is on"), Camera->GetAll22Component()->SetFilmView(TEXT("Sideline")));
    Controller->OnCatalogActionStarted.Broadcast(Telestrator->AnalysisActionId);
    TestTrue(TEXT("The Telestrator action enters analysis over what the camera shows (here the film view, as over a replay)"), Telestrator->IsAnalysisActive());
    TestEqual(TEXT("The touch layer stands down: every finger is the drawing layer's"), Touch->GetActiveControls().Num(), 0);

    // The layer's buttons.
    TestEqual(TEXT("It starts with the pen"), Telestrator->GetTool(), EPSTelestratorTool::Freehand);
    TestTrue(TEXT("The tool button is the layer's"), Telestrator->RunLayerAction(Telestrator->ToolActionId));
    TestEqual(TEXT("...and picks the next tool"), Telestrator->GetTool(), EPSTelestratorTool::Arrow);
    Telestrator->BeginStroke(FVector2D(0.3, 0.6));
    Telestrator->ExtendStroke(FVector2D(0.35, 0.55));
    TestEqual(TEXT("The stroke in progress is there to show"), Telestrator->GetLiveStroke().Num(), 1);
    TestTrue(TEXT("An arrow lands"), Telestrator->EndStroke(FVector2D(0.5, 0.5)) != INDEX_NONE);
    TestEqual(TEXT("...and the stroke in progress is gone"), Telestrator->GetLiveStroke().Num(), 0);
    Telestrator->SetTool(EPSTelestratorTool::Freehand);
    Telestrator->BeginStroke(FVector2D(0.2, 0.2));
    Telestrator->ExtendStroke(FVector2D(0.3, 0.3));
    TestEqual(TEXT("A freehand stroke in progress shows its points so far"), Telestrator->GetLiveStroke().Num(), 2);
    Telestrator->EndStroke(FVector2D(0.4, 0.4));
    TestEqual(TEXT("Two marks"), Telestrator->GetTelestration().Marks.Num(), 2);
    TestTrue(TEXT("Undo is the layer's"), Telestrator->RunLayerAction(Telestrator->UndoActionId));
    TestEqual(TEXT("...and takes the last mark back"), Telestrator->GetTelestrationRef().Marks.Num(), 1);
    TestTrue(TEXT("Clear is the layer's"), Telestrator->RunLayerAction(Telestrator->ClearActionId));
    TestEqual(TEXT("...and takes them all"), Telestrator->GetTelestrationRef().Marks.Num(), 0);
    TestFalse(TEXT("An action that isn't the layer's is refused"), Telestrator->RunLayerAction(TEXT("ReplayPlayPause")));
    TestTrue(TEXT("Still drawing"), Telestrator->IsAnalysisActive());

    TestTrue(TEXT("Exit is the layer's"), Telestrator->RunLayerAction(Telestrator->ExitActionId));
    TestFalse(TEXT("...and leaves analysis"), Telestrator->IsAnalysisActive());
    TestEqual(TEXT("The touch layer comes back"), Touch->GetActiveControls().Num(), ControlsBefore);

    // The same button leaves as came in.
    Controller->OnCatalogActionStarted.Broadcast(Telestrator->AnalysisActionId);
    TestTrue(TEXT("In again"), Telestrator->IsAnalysisActive());
    TestTrue(TEXT("The Telestrator button on the layer leaves"), Telestrator->RunLayerAction(Telestrator->AnalysisActionId));
    TestFalse(TEXT("...out"), Telestrator->IsAnalysisActive());
    TestTrue(TEXT("ToggleAnalysis goes in"), Telestrator->ToggleAnalysis());
    TestFalse(TEXT("...and out"), Telestrator->ToggleAnalysis());

    // A controller no longer looking through the camera has no way in.
    Telestrator->UnbindController(Controller);
    Controller->OnCatalogActionStarted.Broadcast(Telestrator->AnalysisActionId);
    TestFalse(TEXT("Unbound, the button does nothing"), Telestrator->IsAnalysisActive());

    Telestrator->EndAnalysis();
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// 4. The drawing layer's tuning
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSTelestratorLayerTuningTest,
    "PlaySports.Telestrator.LayerTuning",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelestratorLayerTuningTest::RunTest(const FString& Parameters)
{
    using namespace PSTelestratorLayerTests;

    const FPSTelestratorTuning Loaded = LoadTuning();
    TestEqual(TEXT("The shipped tuning is sound"), UPSTelestratorSubsystem::ValidateTuning(Loaded).Num(), 0);
    const FPSTelestratorTuning Defaults;
    TestEqual(TEXT("Defaults equal the file: the mark color"), Loaded.MarkColor, Defaults.MarkColor);
    TestEqual(TEXT("... the mark width"), Loaded.MarkWidth, Defaults.MarkWidth);
    TestEqual(TEXT("... the floor"), Loaded.MinStrokeWidth, Defaults.MinStrokeWidth);
    TestEqual(TEXT("... the ring"), Loaded.PlayerRingRadius, Defaults.PlayerRingRadius);
    TestEqual(TEXT("... the segments"), Loaded.CircleSegments, Defaults.CircleSegments);
    TestEqual(TEXT("... the cursor speed"), Loaded.CursorSpeed, Defaults.CursorSpeed);
    TestEqual(TEXT("... the dead zone"), Loaded.CursorDeadZone, Defaults.CursorDeadZone);

    FPSTelestratorTuning Bad = Loaded;
    Bad.MarkColor = TEXT("yellow");
    Bad.MarkWidth = 0.f;
    Bad.ArrowheadAngleDegrees = 95.f;
    Bad.CircleSegments = 4;
    Bad.CursorDeadZone = 1.f;
    TestEqual(TEXT("Each drawing-layer problem is named"), UPSTelestratorSubsystem::ValidateTuning(Bad).Num(), 5);
    return true;
}

#endif
