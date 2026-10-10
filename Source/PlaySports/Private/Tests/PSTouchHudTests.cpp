// PSTouchHudTests.cpp -- Epic 146.4: the UI is built in code; the touch controls are drawn from the touch layer
//
// Tests covered:
//   1. Data/touch_hud.json loads through UPSDataIngestion, equals FPSTouchHudStyle's defaults and
//      validates; an unsound style is caught.
//   2. Every HUD, menu and touch widget the game shows by default is a C++ class that builds its
//      own tree: no Widget Blueprint is needed for the HUD and score bug, the front end and
//      play-call screens, or the touch controls.
//   3. The touch layer tells the HUD what to draw: the stick and every button the active contexts
//      bind, where GetControlPlacement puts them, with a touch glyph for each; which one a finger
//      holds; the held stick's touch-down point and finger. The drawing has a ring per button
//      with its label, the stick faint at rest, a held button darkened, the held stick's knob at
//      its deflection and clamped to the rim, and a swipe's arrow that fades (or, with reduced
//      motion, doesn't) and is gone after SwipeFlashSeconds.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSHUD.h"
#include "PSInputConfig.h"
#include "PSInputGlyphs.h"
#include "PSMenuComponent.h"
#include "PSMenuScreenWidget.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSTouchControls.h"
#include "PSTouchHudTypes.h"
#include "PSTouchHudWidget.h"
#include "PSTouchInputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSTouchHudTests
{
    /** A point given in the layout's safe-area coordinates, in viewport pixels. */
    static FVector2D SafePoint(UPSTouchInputComponent* Touch, double X, double Y)
    {
        const FPSTouchSafeZone& Safe = Touch->GetLayout().SafeZone;
        const FVector2D Viewport = Touch->GetViewportSize();
        const FVector2D Origin(Safe.Left * Viewport.X, Safe.Top * Viewport.Y);
        const FVector2D Size(Viewport.X * (1.0 - Safe.Left - Safe.Right), Viewport.Y * (1.0 - Safe.Top - Safe.Bottom));
        return Origin + FVector2D(X, Y) * Size;
    }

    static double SafeHeight(UPSTouchInputComponent* Touch)
    {
        const FPSTouchSafeZone& Safe = Touch->GetLayout().SafeZone;
        return Touch->GetViewportSize().Y * (1.0 - Safe.Top - Safe.Bottom);
    }

    static const FPSTouchControlView* FindView(const TArray<FPSTouchControlView>& Views, FName ControlId)
    {
        return Views.FindByPredicate([ControlId](const FPSTouchControlView& View) { return View.ControlId == ControlId; });
    }

    static TArray<FPSWidgetStroke> StrokesTagged(const FPSTouchHudDrawing& Drawing, const TCHAR* Tag)
    {
        const FName Wanted(Tag);
        return Drawing.Strokes.FilterByPredicate([Wanted](const FPSWidgetStroke& Stroke) { return Stroke.Tag == Wanted; });
    }

    /** The middle of a ring: the mean of its points. */
    static FVector2D Centroid(const FPSWidgetStroke& Stroke)
    {
        FVector2D Sum = FVector2D::ZeroVector;
        for (const FVector2D& Point : Stroke.Points)
        {
            Sum += Point;
        }
        return Stroke.Points.Num() > 0 ? Sum / Stroke.Points.Num() : Sum;
    }

    static bool IsCodeBuilt(const UClass* WidgetClass)
    {
        return WidgetClass && WidgetClass->HasAnyClassFlags(CLASS_Native);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The style's data file
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTouchHudStyleDataTest,
    "PlaySports.UI.TouchHudStyleData",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTouchHudStyleDataTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSTouchHudStyle Loaded;
    if (!TestTrue(TEXT("Data/touch_hud.json loads"), Ingestion->LoadTouchHudStyleFromJson(PSTouchHud::GetDefaultStylePath(), Loaded)))
    {
        return false;
    }
    TestEqual(TEXT("...and validates"), PSTouchHud::ValidateStyle(Loaded).Num(), 0);

    const FPSTouchHudStyle Defaults;
    TestEqual(TEXT("RestOpacity"), Loaded.RestOpacity, Defaults.RestOpacity);
    TestEqual(TEXT("PressedOpacity"), Loaded.PressedOpacity, Defaults.PressedOpacity);
    TestEqual(TEXT("ControlColor"), Loaded.ControlColor, Defaults.ControlColor);
    TestEqual(TEXT("PressedColor"), Loaded.PressedColor, Defaults.PressedColor);
    TestEqual(TEXT("LabelColor"), Loaded.LabelColor, Defaults.LabelColor);
    TestEqual(TEXT("RingWidth"), Loaded.RingWidth, Defaults.RingWidth);
    TestEqual(TEXT("LabelSize"), Loaded.LabelSize, Defaults.LabelSize);
    TestEqual(TEXT("StickIdleFade"), Loaded.StickIdleFade, Defaults.StickIdleFade);
    TestEqual(TEXT("KnobRadius"), Loaded.KnobRadius, Defaults.KnobRadius);
    TestEqual(TEXT("SwipeFlashSeconds"), Loaded.SwipeFlashSeconds, Defaults.SwipeFlashSeconds);
    TestEqual(TEXT("SwipeArrowLength"), Loaded.SwipeArrowLength, Defaults.SwipeArrowLength);
    TestEqual(TEXT("SwipeArrowWidth"), Loaded.SwipeArrowWidth, Defaults.SwipeArrowWidth);
    TestEqual(TEXT("SwipeArrowHead"), Loaded.SwipeArrowHead, Defaults.SwipeArrowHead);
    TestEqual(TEXT("CircleSegments"), Loaded.CircleSegments, Defaults.CircleSegments);

    FPSTouchHudStyle Broken;
    Broken.PressedOpacity = 1.5f;
    Broken.LabelColor = TEXT("white");
    Broken.CircleSegments = 2;
    TestEqual(TEXT("An unsound style is caught, one problem each"), PSTouchHud::ValidateStyle(Broken).Num(), 3);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Every default widget is code-built
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefaultWidgetsCodeBuiltTest,
    "PlaySports.UI.DefaultWidgetsAreCodeBuilt",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefaultWidgetsCodeBuiltTest::RunTest(const FString& Parameters)
{
    using namespace PSTouchHudTests;

    const APSHUD* HUD = GetDefault<APSHUD>();
    TestTrue(TEXT("The score bug is a C++ widget"), IsCodeBuilt(HUD->ScoreboardWidgetClass.Get()));
    TestTrue(TEXT("The chyron is a C++ widget"), IsCodeBuilt(HUD->ChyronWidgetClass.Get()));
    TestTrue(TEXT("The personnel panels are a C++ widget"), IsCodeBuilt(HUD->PersonnelWidgetClass.Get()));
    TestTrue(TEXT("The position badges are a C++ widget"), IsCodeBuilt(HUD->BadgeWidgetClass.Get()));
    TestTrue(TEXT("The telestrator is a C++ widget"), IsCodeBuilt(HUD->TelestratorWidgetClass.Get()));
    TestTrue(TEXT("The touch controls are drawn by UPSTouchHudWidget"), HUD->TouchHudWidgetClass.Get() == UPSTouchHudWidget::StaticClass());

    const UPSMenuComponent* Menus = GetDefault<UPSMenuComponent>();
    TestTrue(TEXT("Every menu screen, the front end and the play-call screens included, is UPSMenuScreenWidget"),
        Menus->ScreenWidgetClass.Get() == UPSMenuScreenWidget::StaticClass());
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The touch layer tells the HUD what to draw
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTouchHudDrawsTest,
    "PlaySports.UI.TouchHudDrawsTheTouchLayer",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTouchHudDrawsTest::RunTest(const FString& Parameters)
{
    using namespace PSTouchHudTests;

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Test world created"), World))
    {
        return false;
    }
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    UPSTouchInputComponent* Touch = Controller ? Controller->GetTouchInputComponent() : nullptr;
    const UPSInputConfig* Config = Controller ? Controller->GetInputConfig() : nullptr;
    if (!TestNotNull(TEXT("Pawn spawned"), Pawn) || !TestNotNull(TEXT("Controller has a touch layer"), Touch) || !TestNotNull(TEXT("...and an input config"), Config))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }
    Controller->Possess(Pawn);
    TestEqual(TEXT("Before the viewport is known there is nothing to place"), Touch->GetControlViews().Num(), 0);
    Touch->SetViewportSize(FVector2D(2400.0, 1100.0));

    // What to draw: the stick and every button the active contexts bind, placed by the layout.
    const TArray<FPSTouchControlView> Views = Touch->GetControlViews();
    int32 Drawable = 0;
    for (const FPSTouchBindingDef& Active : Touch->GetActiveControls())
    {
        const FPSTouchControlDef* Control = PSTouchControls::FindControl(Touch->GetLayout(), Active.ControlId);
        Drawable += Control && Control->Kind != EPSTouchControlKind::Swipe ? 1 : 0;
    }
    TestTrue(TEXT("Something to draw on the field"), Views.Num() > 1);
    TestEqual(TEXT("One view per active stick or button"), Views.Num(), Drawable);
    int32 Buttons = 0;
    for (const FPSTouchControlView& View : Views)
    {
        FVector2D Center;
        float Radius = 0.f;
        TestTrue(*FString::Printf(TEXT("%s is placed"), *View.ControlId.ToString()), Touch->GetControlPlacement(View.ControlId, Center, Radius));
        TestTrue(*FString::Printf(TEXT("%s is where the layout puts it"), *View.ControlId.ToString()), View.Center.Equals(Center, 0.01) && FMath::IsNearlyEqual(View.Radius, Radius, 0.01f));
        TestFalse(*FString::Printf(TEXT("%s is not held yet"), *View.ControlId.ToString()), View.bHeld);
        FPSInputGlyph Glyph;
        TestTrue(*FString::Printf(TEXT("%s's action has a touch glyph"), *View.ControlId.ToString()),
            Config->GetGlyphForAction(View.ActionId, View.ContextId, EPSInputDevice::Touch, Glyph));
        Buttons += View.Kind == EPSTouchControlKind::Button ? 1 : 0;
    }
    const FPSTouchControlView* StickAtRest = FindView(Views, TEXT("Stick"));
    if (!TestNotNull(TEXT("The stick is drawn on the field"), StickAtRest) || !TestNotNull(TEXT("...and the hike button"), FindView(Views, TEXT("ButtonBottom"))))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }

    const FPSTouchHudStyle Style;
    const float PixelsToLocal = 0.5f;
    const float ScreenHeight = 550.f;
    const FPSTouchHudDrawing AtRest = PSTouchHud::BuildDrawing(Views, FPSTouchSwipeView(), 100.0, PixelsToLocal, ScreenHeight, false, Style);
    TestEqual(TEXT("A ring per button"), StrokesTagged(AtRest, TEXT("Button")).Num(), Buttons);
    TestEqual(TEXT("...each labelled"), AtRest.Labels.Num(), Buttons);
    TestEqual(TEXT("The stick at rest: one faint ring, no knob"), StrokesTagged(AtRest, TEXT("Stick")).Num(), 1);
    TestEqual(TEXT("...no knob"), StrokesTagged(AtRest, TEXT("Knob")).Num(), 0);
    TestEqual(TEXT("No swipe yet, no arrow"), StrokesTagged(AtRest, TEXT("Swipe")).Num(), 0);
    if (StrokesTagged(AtRest, TEXT("Stick")).Num() == 1)
    {
        const FPSWidgetStroke Ring = StrokesTagged(AtRest, TEXT("Stick"))[0];
        TestTrue(TEXT("...at its rest position, in local units"), Centroid(Ring).Equals(StickAtRest->Center * PixelsToLocal, 0.5));
        TestEqual(TEXT("...fainter than a button"), Ring.Color.A, Style.RestOpacity * Style.StickIdleFade, 0.001f);
    }
    for (const FPSTouchHudLabel& Label : AtRest.Labels)
    {
        const FPSTouchControlView* View = FindView(Views, Label.ControlId);
        TestTrue(TEXT("A label sits in its button"), View && Label.Center.Equals(View->Center * PixelsToLocal, 0.01));
    }

    // A finger on the hike button, and one on the stick, pushed far past its rim.
    const double Clock = 50.0;
    const FPSTouchControlView* Hike = FindView(Views, TEXT("ButtonBottom"));
    Touch->TouchStarted(1, Hike->Center, Clock);
    const FVector2D StickDown = SafePoint(Touch, 0.2, 0.7);
    Touch->TouchStarted(2, StickDown, Clock);
    const FVector2D StickFinger = StickDown + FVector2D(3.0 * StickAtRest->Radius, 0.0);
    Touch->TouchMoved(2, StickFinger, Clock + 0.05);

    const TArray<FPSTouchControlView> Held = Touch->GetControlViews();
    const FPSTouchControlView* HeldHike = FindView(Held, TEXT("ButtonBottom"));
    const FPSTouchControlView* HeldStick = FindView(Held, TEXT("Stick"));
    TestTrue(TEXT("The hike button is held"), HeldHike && HeldHike->bHeld);
    TestTrue(TEXT("The stick is held"), HeldStick && HeldStick->bHeld);
    if (HeldStick)
    {
        TestTrue(TEXT("...from where the finger landed"), HeldStick->TouchOrigin.Equals(StickDown, 0.01));
        TestTrue(TEXT("...to where it is"), HeldStick->TouchCurrent.Equals(StickFinger, 0.01));
    }
    int32 HeldCount = 0;
    for (const FPSTouchControlView& View : Held)
    {
        HeldCount += View.bHeld ? 1 : 0;
    }
    TestEqual(TEXT("Nothing else is held"), HeldCount, 2);

    const FPSTouchHudDrawing Pressed = PSTouchHud::BuildDrawing(Held, FPSTouchSwipeView(), 100.0, PixelsToLocal, ScreenHeight, false, Style);
    int32 DarkButtons = 0;
    for (const FPSWidgetStroke& Ring : StrokesTagged(Pressed, TEXT("Button")))
    {
        DarkButtons += FMath::IsNearlyEqual(Ring.Color.A, Style.PressedOpacity, 0.001f) ? 1 : 0;
    }
    TestEqual(TEXT("Only the held button is drawn pressed"), DarkButtons, 1);
    const TArray<FPSWidgetStroke> Knobs = StrokesTagged(Pressed, TEXT("Knob"));
    const TArray<FPSWidgetStroke> Bases = StrokesTagged(Pressed, TEXT("Stick"));
    if (TestEqual(TEXT("The held stick has a knob"), Knobs.Num(), 1) && TestEqual(TEXT("...and its base"), Bases.Num(), 1) && HeldStick)
    {
        const FVector2D Base = StickDown * PixelsToLocal;
        TestTrue(TEXT("The base is where the finger landed"), Centroid(Bases[0]).Equals(Base, 0.5));
        TestTrue(TEXT("The knob is on the rim, toward the finger: a push past it is clamped"),
            Centroid(Knobs[0]).Equals(Base + FVector2D(HeldStick->Radius * PixelsToLocal, 0.0), 0.5));
    }

    // A swipe down the gesture zone: its arrow shows briefly, fading, then is gone.
    Touch->TouchEnded(1, Hike->Center, Clock + 0.2);
    Touch->TouchEnded(2, StickFinger, Clock + 0.2);
    const FVector2D SwipeStart = SafePoint(Touch, 0.55, 0.45);
    const FVector2D SwipeEnd = SwipeStart + FVector2D(0.0, 0.3 * SafeHeight(Touch));
    Touch->TouchStarted(3, SwipeStart, Clock + 1.0);
    Touch->TouchMoved(3, SwipeEnd, Clock + 1.05);
    Touch->TouchEnded(3, SwipeEnd, Clock + 1.1);
    const FPSTouchSwipeView& Swipe = Touch->GetLastSwipe();
    TestTrue(TEXT("The layer remembers the swipe down"), Swipe.Direction == EPSSwipeDirection::Down);
    TestTrue(TEXT("...midway along it"), Swipe.Position.Equals((SwipeStart + SwipeEnd) * 0.5, 0.01));

    const double Midway = Swipe.TimeSeconds + Style.SwipeFlashSeconds * 0.5;
    const TArray<FPSTouchControlView> Released = Touch->GetControlViews();
    const TArray<FPSWidgetStroke> Arrow = StrokesTagged(PSTouchHud::BuildDrawing(Released, Swipe, Midway, PixelsToLocal, ScreenHeight, false, Style), TEXT("Swipe"));
    if (TestEqual(TEXT("A shaft and a head"), Arrow.Num(), 2))
    {
        TestEqual(TEXT("...half faded halfway through"), Arrow[0].Color.A, Style.PressedOpacity * 0.5f, 0.01f);
        TestTrue(TEXT("...pointing down the screen"), Arrow[0].Points.Num() == 2 && Arrow[0].Points[1].Y > Arrow[0].Points[0].Y);
    }
    const TArray<FPSWidgetStroke> Still = StrokesTagged(PSTouchHud::BuildDrawing(Released, Swipe, Midway, PixelsToLocal, ScreenHeight, true, Style), TEXT("Swipe"));
    TestTrue(TEXT("With reduced motion it shows without fading"), Still.Num() == 2 && FMath::IsNearlyEqual(Still[0].Color.A, Style.PressedOpacity, 0.001f));
    TestEqual(TEXT("After SwipeFlashSeconds it is gone"),
        StrokesTagged(PSTouchHud::BuildDrawing(Released, Swipe, Swipe.TimeSeconds + Style.SwipeFlashSeconds + 0.1, PixelsToLocal, ScreenHeight, false, Style), TEXT("Swipe")).Num(), 0);

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
