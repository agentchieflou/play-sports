// PSOverlayBadgeTests.cpp -- Epic 28 (player position badges)
//
// Tests covered:
//   1. The style loads and validates, and a bad one is caught. The screen arithmetic: a
//      perspective projection, scaling by distance, and the overlap rules -- pass buttons placed
//      first, a badge nudged up clear of another and of the ball, one with no room left out.
//   2. Who wears what: before the snap the human's QB's receivers wear the buttons that throw
//      to them, left to right ("X", "Y", "B", "RB" on a gamepad; the catalog's keys on a
//      keyboard), the defense its role labels, each group in its color, nobody on the human's
//      own player; nearer badges are bigger. During the play the buttons stay while the QB can
//      throw and go once he can't; the defense's go at the snap. A human on defense sees role
//      labels on the offense. Minimal keeps the pass buttons only; a Full tier fades badges in.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSDataIngestion.h"
#include "PSInputConfig.h"
#include "PSInputDeviceComponent.h"
#include "PSOverlayBadgeComponent.h"
#include "PSOverlayBadgeLayout.h"
#include "PSPassingComponent.h"
#include "PSPlayContextComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "PSUITeamCatalog.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSOverlayBadgeTests
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

    /** A pawn with no controller of its own. */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, double X, double Y)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector(X, Y, 100.0), FRotator::ZeroRotator, SpawnParams);
        if (Pawn)
        {
            FPlayerAttributes Attributes;
            Attributes.PlayerId = FName(PlayerId);
            Attributes.DisplayName = PlayerId;
            Attributes.Role = Role;
            Pawn->InitializePlayer(Attributes);
        }
        return Pawn;
    }

    static FLinearColor HexColor(const FString& Hex)
    {
        FLinearColor Color = FLinearColor::Black;
        UPSUITeamCatalog::ParseHexColor(Hex, Color);
        return Color;
    }

    /** A badge laid out by hand for the overlap rules. */
    static FPSPositionBadge MakeBadge(const TCHAR* PlayerId, int32 PassSlot, float Distance, const FVector2D& Bottom)
    {
        FPSPositionBadge Badge;
        Badge.PlayerId = FName(PlayerId);
        Badge.PassSlot = PassSlot;
        Badge.Distance = Distance;
        Badge.Scale = 1.f;
        Badge.Size = FVector2D(44.0, 32.0);
        Badge.ScreenPosition = Bottom;
        Badge.bOnScreen = true;
        Badge.bVisible = true;
        return Badge;
    }

    /** Behind the offense and above it, looking downfield (+X) and down 20 degrees. */
    static FPSBadgeView BroadcastView()
    {
        FPSBadgeView View;
        View.CameraLocation = FVector(-1500.0, 0.0, 800.0);
        View.CameraRotation = FRotator(-20.f, 0.f, 0.f);
        View.FOVDegrees = 90.f;
        View.ViewportSize = FVector2D(1920.0, 1080.0);
        return View;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Style and the screen arithmetic
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSBadgeLayoutTest,
    "PlaySports.Overlay.BadgeStyleAndLayout",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSBadgeLayoutTest::RunTest(const FString& Parameters)
{
    using namespace PSOverlayBadgeTests;

    FPSOverlayBadgeStyle Style;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    if (TestTrue(TEXT("overlay_badges.json loads"), Ingestion->LoadOverlayBadgeStyleFromJson(UPSOverlayBadgeComponent::GetDefaultStylePath(), Style)))
    {
        for (const FString& Problem : UPSOverlayBadgeComponent::ValidateStyle(Style))
        {
            AddError(FString::Printf(TEXT("overlay_badges.json: %s"), *Problem));
        }
        const FPSBadgeGroupStyle* Receivers = Style.FindGroup(EPSBadgeGroup::Receiver);
        if (TestNotNull(TEXT("Receivers are styled"), Receivers))
        {
            TestTrue(TEXT("Receivers' pass buttons are kept on every tier"), Receivers->bEssential);
            TestEqual(TEXT("...and show while the QB can throw"), Receivers->InPlay, EPSBadgeInPlay::WhilePassing);
        }
        TestEqual(TEXT("A tight end without a button is TE"), Style.LabelForRole(EPlayerRole::TightEnd), FString(TEXT("TE")));
    }

    FPSOverlayBadgeStyle Bad = Style;
    Bad.Groups.RemoveAll([](const FPSBadgeGroupStyle& Entry) { return Entry.Group == EPSBadgeGroup::Defense; });
    Bad.RoleLabels.RemoveAll([](const FPSBadgeRoleLabel& Entry) { return Entry.Role == EPlayerRole::Linebacker; });
    if (Bad.Groups.Num() > 0)
    {
        Bad.Groups[0].Color = TEXT("white");
    }
    Bad.MaxScale = Bad.MinScale * 0.5f;
    TestEqual(TEXT("A bad style is caught: a group missing, a role unlabelled, a color, the scale range"),
        UPSOverlayBadgeComponent::ValidateStyle(Bad).Num(), 4);

    // The projection: a camera at the origin looking down +X, 90 degrees across 1920 x 1080.
    FPSBadgeView Straight;
    Straight.FOVDegrees = 90.f;
    Straight.ViewportSize = FVector2D(1920.0, 1080.0);
    FVector2D Screen;
    float Depth = 0.f;
    TestTrue(TEXT("Ahead projects"), PSOverlayBadgeLayout::ProjectToScreen(Straight, FVector(1000.0, 0.0, 0.0), Screen, Depth));
    TestTrue(TEXT("...dead ahead to the center"), Screen.Equals(FVector2D(960.0, 540.0), 0.01));
    TestEqual(TEXT("...10 m deep"), Depth, 1000.f, 0.01f);
    PSOverlayBadgeLayout::ProjectToScreen(Straight, FVector(1000.0, 500.0, 250.0), Screen, Depth);
    TestTrue(TEXT("Right and up: right of and above the center"), Screen.Equals(FVector2D(1440.0, 300.0), 0.01));
    TestFalse(TEXT("Behind the camera doesn't project"), PSOverlayBadgeLayout::ProjectToScreen(Straight, FVector(-100.0, 0.0, 0.0), Screen, Depth));

    // Scale by distance.
    TestEqual(TEXT("At the reference distance: scale 1"), PSOverlayBadgeLayout::ScaleForDistance(Style, Style.ReferenceDistance), 1.f, 0.001f);
    TestEqual(TEXT("Far: never under MinScale"), PSOverlayBadgeLayout::ScaleForDistance(Style, Style.ReferenceDistance * 100.f), Style.MinScale, 0.001f);
    TestEqual(TEXT("Near: never over MaxScale"), PSOverlayBadgeLayout::ScaleForDistance(Style, 1.f), Style.MaxScale, 0.001f);

    // Overlaps, with round numbers.
    FPSOverlayBadgeStyle Rules = Style;
    Rules.NudgeStep = 14.f;
    Rules.MaxNudges = 4;
    TArray<FPSPositionBadge> Badges;
    Badges.Add(MakeBadge(TEXT("NEAR_NO_BUTTON"), -1, 900.f, FVector2D(500.0, 500.0)));
    Badges.Add(MakeBadge(TEXT("BUTTON"), 0, 1000.f, FVector2D(500.0, 500.0)));
    Badges.Add(MakeBadge(TEXT("FAR"), -1, 2000.f, FVector2D(500.0, 500.0)));
    Badges.Add(MakeBadge(TEXT("BY_THE_BALL"), 1, 1500.f, FVector2D(900.0, 500.0)));
    Badges.Add(MakeBadge(TEXT("OFF_THE_TOP"), -1, 1500.f, FVector2D(1500.0, 20.0)));
    FPSPositionBadge Hidden = MakeBadge(TEXT("HIDDEN"), -1, 100.f, FVector2D(500.0, 500.0));
    Hidden.bVisible = false;
    Badges.Add(Hidden);
    const FBox2D Ball(FVector2D(872.0, 452.0), FVector2D(928.0, 508.0));
    PSOverlayBadgeLayout::ResolveOverlaps(Badges, &Ball, Rules, FVector2D(1920.0, 1080.0));
    TestTrue(TEXT("The pass button keeps its place, though another is nearer"), Badges[1].bVisible && Badges[1].Nudges == 0);
    TestTrue(TEXT("The nearer one moves up three steps to clear it"), Badges[0].bVisible && Badges[0].Nudges == 3);
    TestTrue(TEXT("...42 px higher"), Badges[0].ScreenPosition.Equals(FVector2D(500.0, 458.0), 0.01));
    TestTrue(TEXT("The farthest finds no room in four steps and isn't drawn"), !Badges[2].bVisible && Badges[2].bCrowdedOut);
    TestTrue(TEXT("A badge over the ball moves up clear of it"), Badges[3].bVisible && Badges[3].Nudges == 4);
    TestTrue(TEXT("...and sits above it"), PSOverlayBadgeLayout::BadgeRect(Badges[3]).Max.Y <= Ball.Min.Y);
    TestTrue(TEXT("Off the top of the screen: not drawn"), !Badges[4].bVisible && Badges[4].bCrowdedOut);
    TestTrue(TEXT("A hidden badge stays hidden and takes no room"), !Badges[5].bVisible && !Badges[5].bCrowdedOut);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Who wears what
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSBadgeAssignmentTest,
    "PlaySports.Overlay.BadgesNamePassTargets",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSBadgeAssignmentTest::RunTest(const FString& Parameters)
{
    using namespace PSOverlayBadgeTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    APSPlayerController* Controller = nullptr;
    if (World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    }
    UPSOverlayBadgeComponent* Badges = Controller ? Controller->GetOverlayBadgeComponent() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Badge component on the controller"), Badges))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    Controller->GetPlayContextComponent()->BindToBus();
    Badges->SetOverlayDetail(EPSOverlayDetail::Simplified);
    const FPSOverlayBadgeStyle Style = Badges->GetStyle();
    const FPSBadgeView View = BroadcastView();

    // Left to right across the field: WR_1, RB_1, TE_1, WR_2 are the QB's slots 1-4.
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB_1"), -100.0, 0.0);
    APSPlayerPawn* WR1 = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_1"), 0.0, -900.0);
    APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB_1"), -500.0, -200.0);
    APSPlayerPawn* TE = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE_1"), 0.0, 450.0);
    APSPlayerPawn* WR2 = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_2"), 0.0, 900.0);
    APSPlayerPawn* OL = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL_1"), 0.0, 150.0);
    APSPlayerPawn* LB = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB_1"), 900.0, -300.0);
    APSPlayerPawn* DB = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_1"), 1500.0, 300.0);
    FActorSpawnParameters BallParams;
    BallParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSBall* Ball = QB ? World->SpawnActor<APSBall>(APSBall::StaticClass(), QB->GetActorLocation(), FRotator::ZeroRotator, BallParams) : nullptr;
    if (!QB || !WR1 || !RB || !TE || !WR2 || !OL || !LB || !DB || !TestNotNull(TEXT("Ball"), Ball))
    {
        AddError(TEXT("Could not spawn the players"));
        DestroyTestWorld(World);
        return false;
    }
    Ball->AttachToCarrier(QB, TEXT("HandSocket"));
    QB->GainPossession();

    auto IsShown = [Badges](const APSPlayerPawn* Pawn)
    {
        const FPSPositionBadge* Badge = Badges->FindBadge(Pawn);
        return Badge && Badge->bVisible;
    };
    auto OpacityOf = [Badges](const APSPlayerPawn* Pawn)
    {
        const FPSPositionBadge* Badge = Badges->FindBadge(Pawn);
        return Badge ? Badge->Opacity : -1.f;
    };
    auto SlotOf = [Badges](const APSPlayerPawn* Pawn)
    {
        const FPSPositionBadge* Badge = Badges->FindBadge(Pawn);
        return Badge ? Badge->PassSlot : -2;
    };

    // Nobody controlled: no badges.
    Badges->Refresh(View);
    TestEqual(TEXT("No control, no badges"), Badges->GetVisibleBadges().Num(), 0);

    TestTrue(TEXT("The human takes the QB"), Controller->TakeControlOf(QB));
    Badges->Refresh(View);

    const TArray<APSPlayerPawn*> Slots = { WR1, RB, TE, WR2 };
    const TArray<FName> SlotActions = Controller->GetPassingComponent()->GetTuning().SlotActions;
    const FName PassingContext = Controller->GetPlayContextComponent()->PassingContextId;
    auto ExpectButtons = [&](EPSInputDevice Device, const TCHAR* DeviceName)
    {
        for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
        {
            const FPSPositionBadge* Badge = Badges->FindBadge(Slots[Slot]);
            FPSInputGlyph Glyph;
            const bool bGlyph = SlotActions.IsValidIndex(Slot) && Controller->GetInputConfig()->GetGlyphForAction(SlotActions[Slot], PassingContext, Device, Glyph);
            if (TestNotNull(*FString::Printf(TEXT("%s: slot %d has a badge"), DeviceName, Slot + 1), Badge) && TestTrue(TEXT("...and a glyph"), bGlyph))
            {
                TestTrue(*FString::Printf(TEXT("%s: slot %d is shown"), DeviceName, Slot + 1), Badge->bVisible);
                TestEqual(*FString::Printf(TEXT("%s: slot %d wears its button"), DeviceName, Slot + 1), Badge->Label, Glyph.Label);
                TestEqual(*FString::Printf(TEXT("%s: slot %d is slot %d"), DeviceName, Slot + 1, Slot + 1), Badge->PassSlot, Slot);
            }
        }
    };
    ExpectButtons(EPSInputDevice::KeyboardMouse, TEXT("Keyboard"));

    Controller->GetInputDeviceComponent()->NotifyInput(EKeys::Gamepad_FaceButton_Bottom, 1.f);
    Badges->Refresh(View);
    ExpectButtons(EPSInputDevice::Gamepad, TEXT("Gamepad"));
    const FPSPositionBadge* Leftmost = Badges->FindBadge(WR1);
    const FPSPositionBadge* Rightmost = Badges->FindBadge(WR2);
    if (Leftmost && Rightmost)
    {
        TestEqual(TEXT("The broadcast frame's letters: the leftmost receiver is X"), Leftmost->Label, FString(TEXT("X")));
        TestEqual(TEXT("...and the fourth slot is RB"), Rightmost->Label, FString(TEXT("RB")));
    }

    // Colors, role labels, and who goes without.
    const FPSPositionBadge* Back = Badges->FindBadge(RB);
    const FPSPositionBadge* Tight = Badges->FindBadge(TE);
    const FPSPositionBadge* Safety = Badges->FindBadge(DB);
    const FPSPositionBadge* Backer = Badges->FindBadge(LB);
    const FPSPositionBadge* Lineman = Badges->FindBadge(OL);
    const FPSBadgeGroupStyle* ReceiverStyle = Style.FindGroup(EPSBadgeGroup::Receiver);
    const FPSBadgeGroupStyle* BackStyle = Style.FindGroup(EPSBadgeGroup::Back);
    const FPSBadgeGroupStyle* DefenseStyle = Style.FindGroup(EPSBadgeGroup::Defense);
    if (TestNotNull(TEXT("RB badge"), Back) && TestNotNull(TEXT("TE badge"), Tight) && TestNotNull(TEXT("DB badge"), Safety)
        && TestNotNull(TEXT("LB badge"), Backer) && TestNotNull(TEXT("OL badge"), Lineman)
        && ReceiverStyle && BackStyle && DefenseStyle)
    {
        TestTrue(TEXT("A receiver in the receivers' color"), Tight->Color.Equals(HexColor(ReceiverStyle->Color)));
        TestTrue(TEXT("The back in the backs' color, though he has a button"), Back->Color.Equals(HexColor(BackStyle->Color)) && Back->PassSlot == 1);
        TestTrue(TEXT("The defense in its color"), Safety->Color.Equals(HexColor(DefenseStyle->Color)));
        TestEqual(TEXT("The safety wears DB"), Safety->Label, Style.LabelForRole(EPlayerRole::DefensiveBack));
        TestTrue(TEXT("...shown before the snap"), Safety->bVisible && Backer->bVisible);
        TestFalse(TEXT("The line goes without before the snap"), Lineman->bVisible);
        TestTrue(TEXT("Nearer is bigger: the TE over the far safety"), Tight->Scale > Safety->Scale);
        TestTrue(TEXT("Every shown badge is on screen"), Tight->bOnScreen && Safety->bOnScreen && Back->bOnScreen);
    }
    TestNull(TEXT("Nothing over the human's own player"), Badges->FindBadge(QB));
    TestEqual(TEXT("Six shown: four buttons, two defenders"), Badges->GetVisibleBadges().Num(), 6);

    // The snap: the buttons stay while the QB holds the ball; the defense's go.
    FPSTelemetrySnapEvent Snap;
    Bus->PublishSnap(Snap);
    Badges->Refresh(View);
    TestTrue(TEXT("In play, the QB can throw"), Controller->GetPassingComponent()->CanPass());
    TestTrue(TEXT("...his receivers keep their buttons"), IsShown(WR1) && SlotOf(WR1) == 0);
    TestFalse(TEXT("...the defense's badges are gone"), IsShown(DB));

    // The ball is gone: so are the buttons.
    QB->LosePossession();
    Ball->DetachFromCarrier();
    Badges->Refresh(View);
    TestFalse(TEXT("Once he can't throw, no buttons"), IsShown(WR1));
    TestEqual(TEXT("Nothing shown"), Badges->GetVisibleBadges().Num(), 0);

    // The whistle; on defense, the offense wears its role labels.
    FPSTelemetryPhaseChangeEvent Whistle;
    Whistle.NewPhase = TEXT("PreSnap");
    Bus->PublishPhaseChange(Whistle);
    TestTrue(TEXT("The human takes the linebacker"), Controller->TakeControlOf(LB));
    Badges->Refresh(View);
    const FPSPositionBadge* Receiver = Badges->FindBadge(WR1);
    if (TestNotNull(TEXT("WR_1's badge"), Receiver))
    {
        TestEqual(TEXT("On defense the receiver wears WR"), Receiver->Label, Style.LabelForRole(EPlayerRole::WideReceiver));
        TestEqual(TEXT("...no button"), Receiver->PassSlot, -1);
    }
    TestNull(TEXT("Nothing over the human's linebacker"), Badges->FindBadge(LB));

    // Minimal keeps what play needs: the pass buttons.
    TestTrue(TEXT("Back to the QB"), Controller->TakeControlOf(QB));
    Badges->SetOverlayDetail(EPSOverlayDetail::Minimal);
    Badges->Refresh(View);
    TestTrue(TEXT("Minimal: buttons stay"), IsShown(WR1) && SlotOf(WR1) == 0);
    TestFalse(TEXT("Minimal: the defense's labels go"), IsShown(DB));

    // A Full tier fades a badge in: the defense's reappear, the buttons have been up a second.
    Badges->AdvanceTime(1.f);
    Badges->SetOverlayDetail(EPSOverlayDetail::Full);
    Badges->Refresh(View);
    TestEqual(TEXT("Full: a badge that has just appeared starts faded out"), OpacityOf(DB), 0.f, 0.001f);
    TestEqual(TEXT("...one already up stays up"), OpacityOf(WR1), 1.f, 0.001f);
    Badges->AdvanceTime(Style.FadeInSeconds);
    Badges->Refresh(View);
    TestEqual(TEXT("...and is in after FadeInSeconds"), OpacityOf(DB), 1.f, 0.001f);

    DestroyTestWorld(World);
    return true;
}

#endif
