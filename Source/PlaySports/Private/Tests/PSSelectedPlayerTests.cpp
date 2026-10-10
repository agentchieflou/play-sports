// PSSelectedPlayerTests.cpp -- Epic 30 (selected-player indicator and control handoff)
//
// Tests covered:
//   1. The reticle and the switch tuning load from Data/ and validate, and each platform tier
//      says how much overlay it draws.
//   2. The reticle follows the controlled pawn in its team's color and changes look with the
//      moment of the play: pre-snap, in play, ball carrier (pulsing only on a Full tier), and
//      hides when the human lets go.
//   3. Switch presses cycle through teammates nearest the ball, skip the downed, come back to
//      the starting player, and always go to (and stay on) the side's ball carrier.
//   4. Before the snap, pick buttons move control across the field, and a named pick works;
//      during the play neither does.
//   5. A handoff keeps the pawn's velocity both ways, and the AI that takes a receiver back
//      mid-route carries on from where the human left him instead of running back.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSControlHandoffComponent.h"
#include "PSDataIngestion.h"
#include "PSHealthComponent.h"
#include "PSInputBufferComponent.h"
#include "PSInputConfig.h"
#include "PSOffenseController.h"
#include "PSOverlayReticle.h"
#include "PSOverlayReticleComponent.h"
#include "PSPlatformTiers.h"
#include "PSPlayContextComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "PSUITeamCatalog.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSSelectedPlayerTests
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

    /** A pawn possessed by an offense AI controller of its own (returned in OutAI). */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const FName PlayerId, EPSTeamSide Side, const FVector& Location, APSOffenseController** OutAI = nullptr)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.PlayerId = PlayerId;
        Attributes.DisplayName = PlayerId.ToString();
        Attributes.Role = Role;
        Pawn->InitializePlayer(Attributes);
        Pawn->TeamSide = Side;

        if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
            if (OutAI)
            {
                *OutAI = AI;
            }
        }
        return Pawn;
    }

    static APSPlayerController* SpawnPlayerController(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    }

    static void SetVelocity(APSPlayerPawn* Pawn, const FVector& Velocity)
    {
        if (UFloatingPawnMovement* Movement = Pawn ? Pawn->GetFloatingMovementComponent() : nullptr)
        {
            Movement->Velocity = Velocity;
        }
    }

    static FVector GetVelocity(const APSPlayerPawn* Pawn)
    {
        const UFloatingPawnMovement* Movement = Pawn ? Pawn->GetFloatingMovementComponent() : nullptr;
        return Movement ? Movement->Velocity : FVector::ZeroVector;
    }

    static FLinearColor HexColor(const FString& Hex)
    {
        FLinearColor Color = FLinearColor::Black;
        UPSUITeamCatalog::ParseHexColor(Hex, Color);
        return Color;
    }

    static FLinearColor Brightened(const FLinearColor& Color, float Brightness)
    {
        FLinearColor Out = Color * Brightness;
        Out.A = 1.f;
        return Out;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Data loads and validates; tiers name their overlay detail
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSelectedPlayerDataTest,
    "PlaySports.Overlay.SelectedPlayerDataValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSelectedPlayerDataTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();

    FPSOverlayReticleStyle Style;
    if (TestTrue(TEXT("overlay_reticle.json loads"), Ingestion->LoadOverlayReticleStyleFromJson(UPSOverlayReticleComponent::GetDefaultStylePath(), Style)))
    {
        for (const FString& Problem : UPSOverlayReticleComponent::ValidateStyle(Style))
        {
            AddError(FString::Printf(TEXT("overlay_reticle.json: %s"), *Problem));
        }
        const FPSOverlayReticleStateStyle* Carrier = Style.FindState(EPSReticleState::BallCarrier);
        const FPSOverlayReticleStateStyle* InPlay = Style.FindState(EPSReticleState::InPlay);
        if (TestNotNull(TEXT("Ball-carrier look"), Carrier) && TestNotNull(TEXT("In-play look"), InPlay))
        {
            TestTrue(TEXT("The ball carrier is emphasised: bigger"), Carrier->Radius > InPlay->Radius);
            TestTrue(TEXT("...and brighter"), Carrier->Brightness > InPlay->Brightness);
        }
    }

    FPSOverlayReticleStyle Bad = Style;
    Bad.OffenseColor = TEXT("blue");
    Bad.ReticleStates.Reset();
    FPSOverlayReticleStateStyle HiddenLook;
    HiddenLook.State = EPSReticleState::Hidden;
    Bad.ReticleStates.Add(HiddenLook);
    // Bad offense color, three missing looks, a look for Hidden.
    TestEqual(TEXT("A bad style is caught"), UPSOverlayReticleComponent::ValidateStyle(Bad).Num(), 5);

    FControlHandoffTuningRow Tuning;
    TestTrue(TEXT("control_handoff.json loads"), Ingestion->LoadControlHandoffTuningFromJson(UPSControlHandoffComponent::GetDefaultTuningPath(), Tuning));
    UPSInputConfig* Input = NewObject<UPSInputConfig>();
    TestTrue(TEXT("The input catalog loads"), Input->LoadDefaults());
    for (const FString& Problem : UPSControlHandoffComponent::ValidateTuning(Tuning, &Input->Catalog))
    {
        AddError(FString::Printf(TEXT("control_handoff.json: %s"), *Problem));
    }
    FControlHandoffTuningRow BadTuning = Tuning;
    BadTuning.CycleWindowSeconds = -1.f;
    BadTuning.PickLeftAction = TEXT("Juke");
    TestEqual(TEXT("A bad window and a pick action outside PreSnap are caught"), UPSControlHandoffComponent::ValidateTuning(BadTuning, &Input->Catalog).Num(), 2);

    FPSPlatformTierCatalog Tiers;
    if (TestTrue(TEXT("platform_tiers.json loads"), Ingestion->LoadPlatformTiersFromJson(PSPlatformTiers::GetDefaultCatalogPath(), Tiers)))
    {
        const FPSPlatformTier* Desktop = PSPlatformTiers::FindTier(Tiers, TEXT("DesktopHigh"));
        const FPSPlatformTier* Phone = PSPlatformTiers::FindTier(Tiers, TEXT("MobileBaseline"));
        const FPSPlatformTier* LowPhone = PSPlatformTiers::FindTier(Tiers, TEXT("MobileLow"));
        if (TestNotNull(TEXT("Desktop tier"), Desktop) && TestNotNull(TEXT("Phone tier"), Phone) && TestNotNull(TEXT("Low tier"), LowPhone))
        {
            TestEqual(TEXT("Desktop draws full overlays"), Desktop->OverlayDetail, EPSOverlayDetail::Full);
            TestEqual(TEXT("The phone draws them without animation"), Phone->OverlayDetail, EPSOverlayDetail::Simplified);
            TestEqual(TEXT("The low tier draws the minimum"), LowPhone->OverlayDetail, EPSOverlayDetail::Minimal);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The reticle follows control and the moment of the play
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSelectedPlayerReticleTest,
    "PlaySports.Overlay.SelectedPlayerReticle",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSelectedPlayerReticleTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSSelectedPlayerTests::CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    APSPlayerController* Controller = PSSelectedPlayerTests::SpawnPlayerController(World);
    APSPlayerPawn* Quarterback = PSSelectedPlayerTests::SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB_1"), EPSTeamSide::Offense, FVector(-300.f, 0.f, 100.f));
    APSPlayerPawn* Safety = PSSelectedPlayerTests::SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_1"), EPSTeamSide::Defense, FVector(1500.f, 400.f, 100.f));
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("Quarterback"), Quarterback) || !TestNotNull(TEXT("Safety"), Safety))
    {
        PSSelectedPlayerTests::DestroyTestWorld(World);
        return false;
    }

    UPSOverlayReticleComponent* ReticleComp = Controller->GetOverlayReticleComponent();
    UPSPlayContextComponent* PlayContext = Controller->GetPlayContextComponent();
    if (!TestNotNull(TEXT("Reticle component on the controller"), ReticleComp) || !TestNotNull(TEXT("Play context"), PlayContext))
    {
        PSSelectedPlayerTests::DestroyTestWorld(World);
        return false;
    }
    PlayContext->BindToBus();
    ReticleComp->SetOverlayDetail(EPSOverlayDetail::Full);
    const FPSOverlayReticleStyle Style = ReticleComp->GetStyle();
    const FPSOverlayReticleStateStyle* PreSnapLook = Style.FindState(EPSReticleState::PreSnap);
    const FPSOverlayReticleStateStyle* InPlayLook = Style.FindState(EPSReticleState::InPlay);
    const FPSOverlayReticleStateStyle* CarrierLook = Style.FindState(EPSReticleState::BallCarrier);
    if (!TestNotNull(TEXT("Pre-snap look"), PreSnapLook) || !TestNotNull(TEXT("In-play look"), InPlayLook) || !TestNotNull(TEXT("Carrier look"), CarrierLook))
    {
        PSSelectedPlayerTests::DestroyTestWorld(World);
        return false;
    }
    const FLinearColor OffenseColor = PSSelectedPlayerTests::HexColor(Style.OffenseColor);
    const FLinearColor DefenseColor = PSSelectedPlayerTests::HexColor(Style.DefenseColor);

    // Nobody controlled: nothing to mark.
    ReticleComp->Refresh();
    TestEqual(TEXT("No control: hidden"), ReticleComp->ComputeState(), EPSReticleState::Hidden);
    TestNull(TEXT("No reticle until someone is controlled"), ReticleComp->GetReticle());

    // Pre-snap, on the QB, in the offense's color.
    TestTrue(TEXT("Human takes the QB"), Controller->TakeControlOf(Quarterback));
    ReticleComp->Refresh();
    APSOverlayReticle* Reticle = ReticleComp->GetReticle();
    if (!TestNotNull(TEXT("Reticle made"), Reticle))
    {
        PSSelectedPlayerTests::DestroyTestWorld(World);
        return false;
    }
    TestEqual(TEXT("Pre-snap look"), Reticle->GetState(), EPSReticleState::PreSnap);
    TestTrue(TEXT("Marks the QB"), Reticle->GetFollowedPawn() == Quarterback);
    TestTrue(TEXT("Rides with the QB"), Reticle->GetAttachParentActor() == Quarterback);
    TestFalse(TEXT("Visible"), Reticle->IsHidden());
    TestEqual(TEXT("At the QB's feet"), Reticle->GetActorLocation().Z, Quarterback->GetActorLocation().Z - Quarterback->GetSimpleCollisionHalfHeight(), 0.5);
    TestEqual(TEXT("Under the QB"), FVector::Dist2D(Reticle->GetActorLocation(), Quarterback->GetActorLocation()), 0.0, 0.5);
    TestEqual(TEXT("Pre-snap radius"), Reticle->GetRadius(), PreSnapLook->Radius, 0.01f);
    TestTrue(TEXT("Offense color"), Reticle->GetColor().Equals(PSSelectedPlayerTests::Brightened(OffenseColor, PreSnapLook->Brightness), 0.001f));
    // GetStaticMesh may hand back a TObjectPtr; a raw pointer is what TestNotNull takes.
    const UStaticMesh* RingAsset = nullptr;
    if (Reticle->GetRingMesh())
    {
        RingAsset = Reticle->GetRingMesh()->GetStaticMesh();
    }
    TestNotNull(TEXT("Drawn with the style's mesh"), RingAsset);

    // The snap: in play, without the ball.
    FPSTelemetrySnapEvent Snap;
    Bus->PublishSnap(Snap);
    ReticleComp->Refresh();
    TestEqual(TEXT("After the snap: in-play look"), Reticle->GetState(), EPSReticleState::InPlay);
    TestEqual(TEXT("In-play radius"), Reticle->GetRadius(), InPlayLook->Radius, 0.01f);
    TestTrue(TEXT("In-play brightness"), Reticle->GetColor().Equals(PSSelectedPlayerTests::Brightened(OffenseColor, InPlayLook->Brightness), 0.001f));

    // With the ball: the emphasised look, pulsing on a Full tier.
    Quarterback->GainPossession();
    ReticleComp->Refresh();
    TestEqual(TEXT("With the ball: ball-carrier look"), Reticle->GetState(), EPSReticleState::BallCarrier);
    TestEqual(TEXT("A pulse starts at the base radius"), Reticle->GetRadius(), CarrierLook->Radius, 0.01f);
    if (CarrierLook->PulseHz > 0.f)
    {
        // Half a pulse later the ring is at its widest.
        ReticleComp->AdvanceAnimation(0.5f / CarrierLook->PulseHz);
        ReticleComp->Refresh();
        TestEqual(TEXT("Mid-pulse the ring swells by PulseAmount"), Reticle->GetRadius(), CarrierLook->Radius * (1.f + CarrierLook->PulseAmount), 0.05f);

        ReticleComp->SetOverlayDetail(EPSOverlayDetail::Simplified);
        ReticleComp->AdvanceAnimation(0.5f / CarrierLook->PulseHz);
        ReticleComp->Refresh();
        TestEqual(TEXT("Without animation the ring holds still"), Reticle->GetRadius(), CarrierLook->Radius, 0.01f);
        ReticleComp->SetOverlayDetail(EPSOverlayDetail::Full);
    }

    // The human's team color wins over the side color.
    const FLinearColor Teal(0.f, 0.5f, 0.5f, 1.f);
    ReticleComp->SetTeamColor(Teal);
    ReticleComp->Refresh();
    TestTrue(TEXT("Team color"), Reticle->GetColor().Equals(PSSelectedPlayerTests::Brightened(Teal, CarrierLook->Brightness), 0.001f));
    ReticleComp->ClearTeamColor();
    Quarterback->LosePossession();

    // Control moves to the safety: the reticle moves with it, in the defense's color.
    TestTrue(TEXT("Human takes the safety"), Controller->TakeControlOf(Safety));
    ReticleComp->Refresh();
    TestTrue(TEXT("Marks the safety"), Reticle->GetFollowedPawn() == Safety);
    TestTrue(TEXT("Rides with the safety"), Reticle->GetAttachParentActor() == Safety);
    TestEqual(TEXT("Still in play"), Reticle->GetState(), EPSReticleState::InPlay);
    TestTrue(TEXT("Defense color"), Reticle->GetColor().Equals(PSSelectedPlayerTests::Brightened(DefenseColor, InPlayLook->Brightness), 0.001f));

    // The whistle: pre-snap again.
    FPSTelemetryPhaseChangeEvent Whistle;
    Whistle.OldPhase = TEXT("BallCarrierMovement");
    Whistle.NewPhase = TEXT("PreSnap");
    Bus->PublishPhaseChange(Whistle);
    ReticleComp->Refresh();
    TestEqual(TEXT("After the whistle: pre-snap look"), Reticle->GetState(), EPSReticleState::PreSnap);

    // Let go: hidden and detached.
    Controller->ReleaseControl();
    ReticleComp->Refresh();
    TestEqual(TEXT("Released: hidden state"), Reticle->GetState(), EPSReticleState::Hidden);
    TestTrue(TEXT("Released: not drawn"), Reticle->IsHidden());
    TestNull(TEXT("Released: marks nobody"), Reticle->GetFollowedPawn());
    TestNull(TEXT("Released: attached to nobody"), Reticle->GetAttachParentActor());

    PSSelectedPlayerTests::DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Switch presses cycle nearest-to-the-ball
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSwitchCycleTest,
    "PlaySports.Control.SwitchCyclesNearestToBall",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSwitchCycleTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSSelectedPlayerTests::CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    APSPlayerController* Controller = PSSelectedPlayerTests::SpawnPlayerController(World);
    if (!TestNotNull(TEXT("Controller"), Controller))
    {
        PSSelectedPlayerTests::DestroyTestWorld(World);
        return false;
    }
    Controller->HumanSide = EPSTeamSide::Defense;
    UPSControlHandoffComponent* Handoff = Controller->GetControlHandoffComponent();
    const float Window = Handoff->GetTuning().CycleWindowSeconds;

    // Defenders 50, 100, 300 and 600 cm from the ball; the nearest is down.
    const FVector Ball(0.f, 0.f, 100.f);
    APSPlayerPawn* Downed = PSSelectedPlayerTests::SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB_Down"), EPSTeamSide::Defense, FVector(0.f, 50.f, 100.f));
    APSPlayerPawn* Near = PSSelectedPlayerTests::SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB_Near"), EPSTeamSide::Defense, FVector(100.f, -50.f, 100.f));
    APSPlayerPawn* Mid = PSSelectedPlayerTests::SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_Mid"), EPSTeamSide::Defense, FVector(-300.f, 0.f, 100.f));
    APSPlayerPawn* Far = PSSelectedPlayerTests::SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_Far"), EPSTeamSide::Defense, FVector(0.f, 600.f, 100.f));
    APSPlayerPawn* Runner = PSSelectedPlayerTests::SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB_1"), EPSTeamSide::Offense, FVector(0.f, -1200.f, 100.f));
    if (!TestNotNull(TEXT("Downed"), Downed) || !TestNotNull(TEXT("Near"), Near) || !TestNotNull(TEXT("Mid"), Mid) || !TestNotNull(TEXT("Far"), Far) || !TestNotNull(TEXT("Runner"), Runner))
    {
        PSSelectedPlayerTests::DestroyTestWorld(World);
        return false;
    }
    Downed->GetHealthComponent()->Kill();

    // The order: nearest the ball first, nobody downed, no offense.
    TestTrue(TEXT("Human starts on the far defender"), Controller->TakeControlOf(Far));
    const TArray<APSPlayerPawn*> Ranked = Handoff->RankSwitchCandidates(Ball);
    if (TestEqual(TEXT("Two others to switch to"), Ranked.Num(), 2))
    {
        TestTrue(TEXT("Nearest first"), Ranked[0] == Near);
        TestTrue(TEXT("Then the next nearest"), Ranked[1] == Mid);
    }

    // Presses inside the window walk the order, come back to the start, then wrap.
    double Now = 10.0;
    TestTrue(TEXT("First press: the nearest"), Handoff->CycleSwitch(Ball, Now) && Controller->GetPawn() == Near);
    Now += Window * 0.5;
    TestTrue(TEXT("Second press: the next nearest"), Handoff->CycleSwitch(Ball, Now) && Controller->GetPawn() == Mid);
    Now += Window * 0.5;
    TestTrue(TEXT("Third press: back to the start"), Handoff->CycleSwitch(Ball, Now) && Controller->GetPawn() == Far);
    Now += Window * 0.5;
    TestTrue(TEXT("Fourth press: round again"), Handoff->CycleSwitch(Ball, Now) && Controller->GetPawn() == Near);

    // After the window a press ranks afresh from where he is.
    Now += Window * 2.0;
    TestTrue(TEXT("A press after the window ranks again"), Handoff->CycleSwitch(Ball, Now));
    TestTrue(TEXT("...and takes the nearest other than the current"), Controller->GetPawn() == Mid);

    // The old rule still holds for the one-shot switch.
    TestTrue(TEXT("SwitchToBestPawn takes the nearest"), Controller->SwitchToBestPawn(Ball) && Controller->GetPawn() == Near);

    // The side's ball carrier always gets control, and keeps it.
    Controller->HumanSide = EPSTeamSide::Offense;
    APSPlayerPawn* Receiver = PSSelectedPlayerTests::SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_1"), EPSTeamSide::Offense, FVector(2000.f, 0.f, 100.f));
    TestTrue(TEXT("Human takes the back"), Controller->TakeControlOf(Runner));
    Receiver->GainPossession();
    Now += Window * 2.0;
    TestTrue(TEXT("A press goes to our carrier, however far"), Handoff->CycleSwitch(Ball, Now) && Controller->GetPawn() == Receiver);
    Now += Window * 0.5;
    TestFalse(TEXT("Nobody switches away from the carrier"), Handoff->CycleSwitch(Ball, Now));
    TestTrue(TEXT("Still on the carrier"), Controller->GetPawn() == Receiver);

    PSSelectedPlayerTests::DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Pre-snap direct pick
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDirectPickTest,
    "PlaySports.Control.PreSnapDirectPick",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDirectPickTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSSelectedPlayerTests::CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    APSPlayerController* Controller = PSSelectedPlayerTests::SpawnPlayerController(World);
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Controller"), Controller))
    {
        PSSelectedPlayerTests::DestroyTestWorld(World);
        return false;
    }
    Controller->HumanSide = EPSTeamSide::Defense;

    // Across the field (left is -Y): a corner, the middle linebacker, a safety, the far corner.
    APSPlayerPawn* LeftCorner = PSSelectedPlayerTests::SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_L"), EPSTeamSide::Defense, FVector(500.f, -1500.f, 100.f));
    APSPlayerPawn* Middle = PSSelectedPlayerTests::SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("MLB"), EPSTeamSide::Defense, FVector(500.f, 0.f, 100.f));
    APSPlayerPawn* Safety = PSSelectedPlayerTests::SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("FS"), EPSTeamSide::Defense, FVector(1500.f, 300.f, 100.f));
    APSPlayerPawn* RightCorner = PSSelectedPlayerTests::SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_R"), EPSTeamSide::Defense, FVector(500.f, 1500.f, 100.f));
    APSPlayerPawn* Receiver = PSSelectedPlayerTests::SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_1"), EPSTeamSide::Offense, FVector(0.f, 200.f, 100.f));
    if (!TestNotNull(TEXT("Left corner"), LeftCorner) || !TestNotNull(TEXT("Middle"), Middle) || !TestNotNull(TEXT("Safety"), Safety)
        || !TestNotNull(TEXT("Right corner"), RightCorner) || !TestNotNull(TEXT("Receiver"), Receiver))
    {
        PSSelectedPlayerTests::DestroyTestWorld(World);
        return false;
    }

    UPSControlHandoffComponent* Handoff = Controller->GetControlHandoffComponent();
    UPSInputBufferComponent* Buffer = Controller->GetInputBufferComponent();
    Controller->GetPlayContextComponent()->BindToBus();
    Handoff->BindToController();
    TestTrue(TEXT("Human takes the middle linebacker"), Controller->TakeControlOf(Middle));

    // The pick buttons, through the input buffer as a press arrives in the game.
    const FControlHandoffTuningRow& Tuning = Handoff->GetTuning();
    Buffer->PressAction(Tuning.PickRightAction);
    Buffer->ReleaseAction(Tuning.PickRightAction);
    TestTrue(TEXT("Right: the nearest teammate to the right (not the receiver)"), Controller->GetPawn() == Safety);
    TestTrue(TEXT("Right again: the far corner"), Handoff->PickAcross(1) && Controller->GetPawn() == RightCorner);
    TestFalse(TEXT("Nobody further right"), Handoff->PickAcross(1));
    Buffer->PressAction(Tuning.PickLeftAction);
    Buffer->ReleaseAction(Tuning.PickLeftAction);
    TestTrue(TEXT("Left: back to the safety"), Controller->GetPawn() == Safety);
    TestTrue(TEXT("Left: the linebacker"), Handoff->PickAcross(-1) && Controller->GetPawn() == Middle);
    TestTrue(TEXT("Left: the left corner"), Handoff->PickAcross(-1) && Controller->GetPawn() == LeftCorner);

    // A named pick, and never an opponent.
    TestTrue(TEXT("Pick the safety by name"), Handoff->PickPlayer(TEXT("FS")) && Controller->GetPawn() == Safety);
    TestFalse(TEXT("An opponent can't be picked"), Handoff->PickPlayer(TEXT("WR_1")));

    // During the play picking is off.
    FPSTelemetrySnapEvent Snap;
    Bus->PublishSnap(Snap);
    TestTrue(TEXT("The play is live"), Handoff->IsPlayLive());
    TestFalse(TEXT("No pick across in play"), Handoff->PickAcross(-1));
    TestFalse(TEXT("No named pick in play"), Handoff->PickPlayer(TEXT("CB_L")));
    TestTrue(TEXT("Still on the safety"), Controller->GetPawn() == Safety);

    PSSelectedPlayerTests::DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Handoffs keep momentum; the AI takes a receiver back without a pop
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSHandoffNoPopTest,
    "PlaySports.Control.HandoffWithoutPops",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSHandoffNoPopTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSSelectedPlayerTests::CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    APSPlayerController* Controller = PSSelectedPlayerTests::SpawnPlayerController(World);
    APSOffenseController* ReceiverAI = nullptr;
    APSPlayerPawn* Receiver = PSSelectedPlayerTests::SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_1"), EPSTeamSide::Offense, FVector(0.f, 0.f, 100.f), &ReceiverAI);
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("Receiver"), Receiver) || !TestNotNull(TEXT("Receiver's AI"), ReceiverAI))
    {
        PSSelectedPlayerTests::DestroyTestWorld(World);
        return false;
    }
    UPSSkillPlayerAIComponent* SkillAI = ReceiverAI->GetSkillAI();
    if (!TestNotNull(TEXT("Receiver's skill AI"), SkillAI))
    {
        PSSelectedPlayerTests::DestroyTestWorld(World);
        return false;
    }
    SkillAI->BindToBus();

    // Taking control keeps the speed the AI had him at (possession restarts the pawn).
    PSSelectedPlayerTests::SetVelocity(Receiver, FVector(500.f, 0.f, 0.f));
    TestTrue(TEXT("Human takes the receiver"), Controller->TakeControlOf(Receiver));
    TestTrue(TEXT("Taking control keeps his velocity"), PSSelectedPlayerTests::GetVelocity(Receiver).Equals(FVector(500.f, 0.f, 0.f), 0.01f));

    // The snap comes while the human has him; then the play's route reaches his AI.
    FPSTelemetrySnapEvent Snap;
    Bus->PublishSnap(Snap);
    TArray<FVector> Route;
    Route.Add(FVector(500.f, 0.f, 100.f));
    Route.Add(FVector(1000.f, 0.f, 100.f));
    Route.Add(FVector(1000.f, 800.f, 100.f));
    ReceiverAI->SetAssignedRoute(Route);

    // The human runs him past the first two waypoints and is heading across (+Y).
    Receiver->SetActorLocation(FVector(1100.f, 100.f, 100.f));
    PSSelectedPlayerTests::SetVelocity(Receiver, FVector(0.f, 400.f, 0.f));

    // Letting go hands him back to his AI at that speed, heading on, on the right waypoint.
    Controller->ReleaseControl();
    TestTrue(TEXT("His AI has him again"), Receiver->GetController() == ReceiverAI);
    TestTrue(TEXT("Releasing keeps his velocity"), PSSelectedPlayerTests::GetVelocity(Receiver).Equals(FVector(0.f, 400.f, 0.f), 0.01f));
    TestEqual(TEXT("The AI runs the route"), SkillAI->GetAction(), EPSSkillPlayerAction::RunRoute);
    TestEqual(TEXT("Waypoints he is past are skipped"), ReceiverAI->GetRouteWaypointIndex(), 2);
    TestTrue(TEXT("Until it decides, the AI holds his heading"), SkillAI->GetDesiredDirection().Equals(FVector(0.f, 1.f, 0.f), 0.01f));

    // Its first decision steers on toward the last waypoint, not back to the first.
    SkillAI->TickAI(0.1f);
    const FVector Steer = SkillAI->GetDesiredDirection();
    const FVector Expected = (FVector(1000.f, 800.f, 100.f) - FVector(1100.f, 100.f, 100.f)).GetSafeNormal2D();
    TestTrue(TEXT("First decision: toward the next waypoint"), Steer.Equals(Expected, 0.01f));

    PSSelectedPlayerTests::DestroyTestWorld(World);
    return true;
}

#endif
