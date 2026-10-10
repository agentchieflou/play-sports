// PSOverlayBallFlightTests.cpp -- Epic 32 (live ball-trajectory and pass indicators)
//
// Tests covered:
//   1. The style loads and validates, and a bad one is caught. The flight arithmetic: landing
//      time, place and apex; progress along the arc from the ball's position; the receiver's
//      lead; a kick judged good, wide left or right, or short, at either end of the field.
//   2. A real pass: the arc starts from the ball's physics state at the throw and comes down
//      exactly where the engine aimed the throw; the landing ring is the receiver's catch
//      radius; the lead turns off target when he runs past the spot; on a Full tier the arc
//      shortens behind the ball, Simplified keeps it whole, Minimal draws only the landing
//      spot. A ball knocked off its path, or caught, ends the flight; its marks linger, then go.
//      The telemetry sampler's throw keyframe carries the same launch velocity.
//   3. A kick: a ball leaving the kicker during a FieldGoal phase is followed and judged at the
//      posts (good, wide right, short), with the readout above the crossbar; the same kick
//      outside a kick phase isn't.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSDataIngestion.h"
#include "PSOverlayBallFlight.h"
#include "PSOverlayBallFlightActor.h"
#include "PSOverlayBallFlightSubsystem.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "PSTelemetrySamplingSubsystem.h"
#include "PSUITeamCatalog.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSOverlayBallFlightTests
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

    /** A pawn with no controller, so nothing moves him but the test. */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* Name, const FVector& Location, float Yaw = 0.f)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator(0.f, Yaw, 0.f), SpawnParams);
        if (Pawn)
        {
            FPlayerAttributes Attributes;
            Attributes.PlayerId = FName(Name);
            Attributes.DisplayName = Name;
            Attributes.Role = Role;
            // A perfect passer: the throw goes exactly where it is aimed.
            Attributes.Awareness = 100.f;
            Attributes.Strength = 50.f;
            Pawn->InitializePlayer(Attributes);
        }
        return Pawn;
    }

    static APSBall* SpawnBall(UWorld* World, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<APSBall>(APSBall::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
    }

    static void GiveBall(APSBall* Ball, APSPlayerPawn* Carrier)
    {
        Ball->AttachToCarrier(Carrier, TEXT("HandSocket"));
        Carrier->GainPossession();
    }

    /** The game's play phase, as the simulation would announce it. */
    static void AnnouncePhase(UPSTelemetryBus* Bus, const TCHAR* Phase)
    {
        FPSTelemetryGameStateEvent Event;
        Event.Phase = Phase;
        Bus->PublishGameState(Event);
    }

    static FPSBallFlightStyle LoadStyle()
    {
        FPSBallFlightStyle Style;
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        Ingestion->LoadBallFlightStyleFromJson(UPSOverlayBallFlightSubsystem::GetDefaultStylePath(), Style);
        return Style;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Style and the flight arithmetic
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSBallFlightMathTest,
    "PlaySports.Overlay.BallFlightStyleAndMath",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSBallFlightMathTest::RunTest(const FString& Parameters)
{
    FPSBallFlightStyle Style;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    if (TestTrue(TEXT("ball_flight_overlay.json loads"), Ingestion->LoadBallFlightStyleFromJson(UPSOverlayBallFlightSubsystem::GetDefaultStylePath(), Style)))
    {
        for (const FString& Problem : UPSOverlayBallFlightSubsystem::ValidateStyle(Style))
        {
            AddError(FString::Printf(TEXT("ball_flight_overlay.json: %s"), *Problem));
        }
        TestEqual(TEXT("Posts at both end lines"), Style.GoalPostX.Num(), 2);
    }

    FPSBallFlightStyle Bad = Style;
    Bad.ArcColor = TEXT("white");
    Bad.ArcPoints = 1;
    Bad.UprightWidth = 0.f;
    Bad.GoalPostX.Reset();
    Bad.GoodLabel.Reset();
    TestEqual(TEXT("A bad style is caught: color, points, width, posts, label"), UPSOverlayBallFlightSubsystem::ValidateStyle(Bad).Num(), 5);

    // Thrown from 2 m up at 10 m/s across and 9.8 m/s up under 9.8 m/s^2: back at 2 m after 2 s.
    const float G = -980.f;
    const FVector Start(0.f, 0.f, 200.f);
    const FVector Velocity(1000.f, 0.f, 980.f);
    const FPSBallFlightPrediction Pass = PSOverlayBallFlight::Predict(EPSBallFlightKind::Pass, Start, Velocity, G, 200.f, 0.f, 11, 8.f);
    TestTrue(TEXT("It lands"), Pass.bLands);
    TestEqual(TEXT("Back at catch height after 2 s"), Pass.LandingSeconds, 2.f, 0.001f);
    TestTrue(TEXT("20 m downfield"), Pass.LandingLocation.Equals(FVector(2000.f, 0.f, 200.f), 0.1f));
    TestEqual(TEXT("Apex after 1 s"), Pass.ApexSeconds, 1.f, 0.001f);
    TestEqual(TEXT("...4.9 m above the release"), static_cast<float>(Pass.ApexLocation.Z), 690.f, 0.1f);
    TestEqual(TEXT("Eleven arc points"), Pass.ArcPoints.Num(), 11);
    if (Pass.ArcPoints.Num() == 11)
    {
        TestTrue(TEXT("The arc starts at the release"), Pass.ArcPoints[0].Equals(Start, 0.01f));
        TestTrue(TEXT("...is at the apex halfway"), Pass.ArcPoints[5].Equals(Pass.ApexLocation, 0.1f));
        TestTrue(TEXT("...and ends at the landing"), Pass.ArcPoints[10].Equals(Pass.LandingLocation, 0.01f));
    }
    TestEqual(TEXT("Released under catch height and never rising to it: down on the ground"),
        PSOverlayBallFlight::Predict(EPSBallFlightKind::Pass, FVector(0.f, 0.f, 100.f), FVector(1000.f, 0.f, 0.f), G, 200.f, 0.f, 4, 8.f).LandingHeight, 0.f);
    TestFalse(TEXT("No gravity and heading up: it never lands"),
        PSOverlayBallFlight::Predict(EPSBallFlightKind::Pass, Start, Velocity, 0.f, 200.f, 0.f, 4, 8.f).bLands);

    // Progress is read from where the ball is.
    const FVector Midway = PSOverlayBallFlight::PositionAt(Pass, 1.25f);
    TestEqual(TEXT("A ball at the 1.25 s point is 1.25 s along"), PSOverlayBallFlight::ProgressSeconds(Pass, Midway, 0.f), 1.25f, 0.001f);
    TestEqual(TEXT("...and on its path"), PSOverlayBallFlight::DeviationAt(Pass, Midway, 1.25f), 0.f, 0.01f);
    TestTrue(TEXT("A ball 2 m off the path is off it"), PSOverlayBallFlight::DeviationAt(Pass, Midway + FVector(0.f, 200.f, 0.f), 1.25f) > Style.DeviationTolerance);

    // The lead: where he is plus his velocity until the ball comes down.
    const FPSPassLead OnTime = PSOverlayBallFlight::ComputeLead(FVector(1000.f, 0.f, 100.f), FVector(500.f, 0.f, 0.f), 2.f, FVector(2000.f, 30.f, 100.f), 59.f, 0.f);
    TestTrue(TEXT("Running onto the ball: lead at 20 m"), OnTime.LeadLocation.Equals(FVector(2000.f, 0.f, 0.f), 0.01f));
    TestEqual(TEXT("...30 cm from the spot"), OnTime.MissDistance, 30.f, 0.01f);
    TestTrue(TEXT("...on target"), OnTime.bOnTarget);
    const FPSPassLead Late = PSOverlayBallFlight::ComputeLead(FVector(1000.f, 0.f, 100.f), FVector(300.f, 0.f, 0.f), 2.f, FVector(2000.f, 30.f, 100.f), 59.f, 0.f);
    TestFalse(TEXT("Too slow: off target"), Late.bOnTarget);

    // Kicks at the far posts (X = 11000), from the opponent's 35.
    FPSBallFlightStyle Posts;
    Posts.GoalPostX.Reset();
    Posts.GoalPostX.Add(-1000.f);
    Posts.GoalPostX.Add(11000.f);
    Posts.GroundZ = 0.f;
    Posts.UprightWidth = 617.f;
    Posts.CrossbarHeight = 305.f;
    auto Judge = [&Posts, G](const FVector& KickStart, const FVector& KickVelocity)
    {
        return PSOverlayBallFlight::JudgeKick(PSOverlayBallFlight::Predict(EPSBallFlightKind::Kick, KickStart, KickVelocity, G, 0.f, 0.f, 8, 8.f), Posts);
    };
    const FVector Spot(7500.f, 0.f, 100.f);
    const FPSKickReadout Good = Judge(Spot, FVector(2000.f, 0.f, 1500.f));
    TestEqual(TEXT("Straight and high: good"), Good.Verdict, EPSKickVerdict::Good);
    TestEqual(TEXT("...judged at the far posts"), Good.GoalPostX, 11000.f);
    TestEqual(TEXT("...after 1.75 s"), Good.CrossingSeconds, 1.75f, 0.001f);
    TestEqual(TEXT("...dead center"), Good.MarginInside, 308.5f, 0.01f);
    TestEqual(TEXT("...with the style's label"), Good.Label, Posts.GoodLabel);
    TestEqual(TEXT("Drifting to +Y: wide right"), Judge(Spot, FVector(2000.f, 400.f, 1500.f)).Verdict, EPSKickVerdict::WideRight);
    TestEqual(TEXT("Drifting to -Y: wide left"), Judge(Spot, FVector(2000.f, -400.f, 1500.f)).Verdict, EPSKickVerdict::WideLeft);
    const FPSKickReadout Under = Judge(Spot, FVector(2000.f, 0.f, 900.f));
    TestEqual(TEXT("Low: short"), Under.Verdict, EPSKickVerdict::Short);
    TestTrue(TEXT("...under the bar at the posts"), Under.bReachesGoal && Under.HeightOverBar < 0.f);
    const FPSKickReadout Down = Judge(Spot, FVector(1000.f, 0.f, 900.f));
    TestEqual(TEXT("Weak: short"), Down.Verdict, EPSKickVerdict::Short);
    TestFalse(TEXT("...down before the posts"), Down.bReachesGoal);
    // The other way, at the near posts: the kicker's right is now -Y.
    const FVector OtherSpot(2500.f, 0.f, 100.f);
    const FPSKickReadout Back = Judge(OtherSpot, FVector(-2000.f, 0.f, 1500.f));
    TestEqual(TEXT("Toward -X: good at the near posts"), Back.Verdict, EPSKickVerdict::Good);
    TestEqual(TEXT("...judged at X = -1000"), Back.GoalPostX, -1000.f);
    TestEqual(TEXT("Toward -X drifting to +Y: wide left"), Judge(OtherSpot, FVector(-2000.f, 400.f, 1500.f)).Verdict, EPSKickVerdict::WideLeft);
    TestEqual(TEXT("Straight up: not judged"), Judge(Spot, FVector(0.f, 0.f, 1500.f)).Verdict, EPSKickVerdict::None);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- A pass's arc, landing spot and lead
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPassArcTest,
    "PlaySports.Overlay.PassArcFollowsTheThrow",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPassArcTest::RunTest(const FString& Parameters)
{
    using namespace PSOverlayBallFlightTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSOverlayBallFlightSubsystem* Overlay = World ? World->GetSubsystem<UPSOverlayBallFlightSubsystem>() : nullptr;
    UPSTelemetrySamplingSubsystem* Sampler = World ? World->GetSubsystem<UPSTelemetrySamplingSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Ball-flight overlay"), Overlay) || !TestNotNull(TEXT("Sampler"), Sampler))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    Overlay->SetOverlayDetail(EPSOverlayDetail::Full);
    const FPSBallFlightStyle& Style = Overlay->GetStyle();

    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB_1"), FVector(2000.f, 0.f, 100.f));
    APSPlayerPawn* WR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_1"), FVector(3500.f, 600.f, 100.f));
    APSBall* Ball = QB ? SpawnBall(World, QB->GetActorLocation()) : nullptr;
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("WR"), WR) || !TestNotNull(TEXT("Ball"), Ball))
    {
        DestroyTestWorld(World);
        return false;
    }
    GiveBall(Ball, QB);
    Overlay->AdvanceTime(0.016f);
    TestFalse(TEXT("Nothing to show while the QB holds the ball"), Overlay->GetFlight().bVisible);

    FPSTelemetryThrowEvent Throw;
    const FDelegateHandle ThrowHandle = Bus->OnThrowMC.AddLambda([&Throw](const FPSTelemetryThrowEvent& Event) { Throw = Event; });
    TestTrue(TEXT("The QB throws to WR_1"), QB->ThrowPass(Ball, WR->GetActorLocation(), false, WR));
    Bus->OnThrowMC.Remove(ThrowHandle);

    FPSBallFlightState Flight = Overlay->GetFlight();
    const FPSBallFlightPrediction Arc = Flight.Prediction;
    TestEqual(TEXT("A pass is followed"), Flight.Kind, EPSBallFlightKind::Pass);
    TestTrue(TEXT("...from the throw"), Flight.bActive && Flight.bVisible && Arc.bValid && Arc.bLands);
    TestTrue(TEXT("The arc starts at the ball"), Arc.ReleaseLocation.Equals(Ball->GetActorLocation(), 0.01f));
    TestTrue(TEXT("...with its projectile velocity"), Arc.ReleaseVelocity.Equals(Ball->GetProjectileMovement()->Velocity, 0.01f));
    TestEqual(TEXT("...under its gravity"), Arc.GravityZ, Ball->GetProjectileMovement()->GetGravityZ(), 0.001f);
    TestEqual(TEXT("It comes down at the throw's catch height"), Arc.LandingHeight, static_cast<float>(Throw.LandingLocation.Z), 0.01f);
    TestTrue(TEXT("...exactly where the throw was aimed"), Arc.LandingLocation.Equals(Throw.LandingLocation, 2.f));
    TestTrue(TEXT("...which is the receiver, for a perfect passer"), FVector::Dist2D(Arc.LandingLocation, WR->GetActorLocation()) < 2.f);
    TestTrue(TEXT("...in under two seconds"), Arc.LandingSeconds > 0.2f && Arc.LandingSeconds < 2.f);
    TestEqual(TEXT("The landing ring is the catch radius"), Flight.LandingRadius,
        WR->GetSimpleCollisionRadius() + Ball->GetCollisionComponent()->GetScaledSphereRadius(), 0.01f);

    // Epic 26's throw keyframe carries the same launch.
    FPSSnapshotFrame AtThrow;
    FPSTelemetryEvent ThrowRecord;
    if (TestTrue(TEXT("The sampler has a frame at the throw"), Sampler->GetFrameAtLatestEvent(EPSTelemetryEventType::Throw, AtThrow, ThrowRecord)))
    {
        TestTrue(TEXT("...with the ball's launch velocity"), AtThrow.BallVelocity.Equals(Arc.ReleaseVelocity, 0.01f));
    }

    // The lead: standing on the spot he is on target; running past it he isn't.
    TestTrue(TEXT("A lead for WR_1"), Flight.Lead.bValid && Flight.Lead.ReceiverName == TEXT("WR_1"));
    TestTrue(TEXT("Standing on the spot: on target"), Flight.Lead.bOnTarget);
    WR->GetFloatingMovementComponent()->Velocity = FVector(400.f, 0.f, 0.f);
    Overlay->AdvanceTime(0.016f);
    Flight = Overlay->GetFlight();
    TestFalse(TEXT("Running on past it: off target"), Flight.Lead.bOnTarget);
    TestEqual(TEXT("...by his speed times the time left"), Flight.Lead.MissDistance, 400.f * Flight.Lead.ArrivalSeconds, 1.f);

    // What is drawn.
    APSOverlayBallFlight* Drawn = Overlay->GetOverlayActor();
    if (TestNotNull(TEXT("The overlay is drawn"), Drawn))
    {
        TestEqual(TEXT("The spline carries the arc"), Drawn->GetArcSpline()->GetNumberOfSplinePoints(), Style.ArcPoints);
        TestEqual(TEXT("A dot per point before the ball moves"), Drawn->GetShownDotCount(), Style.ArcPoints);
        TestTrue(TEXT("The landing ring is up"), Drawn->GetLandingRing()->IsVisible());
        TestTrue(TEXT("...on the ground under the landing spot"),
            FVector::Dist2D(Drawn->GetLandingRing()->GetComponentLocation(), Arc.LandingLocation) < 0.1f
            && Drawn->GetLandingRing()->GetComponentLocation().Z < Style.GroundZ + 10.f);
        TestTrue(TEXT("The lead ring is up"), Drawn->GetLeadRing()->IsVisible());
        FLinearColor OffTarget;
        UPSUITeamCatalog::ParseHexColor(Style.LeadOffTargetColor, OffTarget);
        TestTrue(TEXT("...in the off-target color"), Drawn->GetLeadColor().Equals(OffTarget));
        TestFalse(TEXT("No kick readout on a pass"), Drawn->GetReadoutText()->IsVisible());
    }

    // Halfway: the ball's own position says how far along it is.
    Ball->SetActorLocation(PSOverlayBallFlight::PositionAt(Arc, Arc.LandingSeconds * 0.5f));
    Overlay->AdvanceTime(0.016f);
    Flight = Overlay->GetFlight();
    TestTrue(TEXT("Still on its path"), Flight.bActive);
    TestEqual(TEXT("Halfway along"), Flight.ElapsedSeconds, Arc.LandingSeconds * 0.5f, 0.01f);
    TestEqual(TEXT("Half the time left for the receiver"), Flight.Lead.ArrivalSeconds, Arc.LandingSeconds * 0.5f, 0.01f);
    if (Drawn)
    {
        const int32 Shown = Drawn->GetShownDotCount();
        TestTrue(TEXT("Full: the dots behind the ball are gone"), Shown > 0 && Shown < Style.ArcPoints);
        Overlay->SetOverlayDetail(EPSOverlayDetail::Simplified);
        TestEqual(TEXT("Simplified: the whole arc stays"), Drawn->GetShownDotCount(), Style.ArcPoints);
        Overlay->SetOverlayDetail(EPSOverlayDetail::Minimal);
        TestEqual(TEXT("Minimal: no arc"), Drawn->GetShownDotCount(), 0);
        TestFalse(TEXT("Minimal: no lead"), Drawn->GetLeadRing()->IsVisible());
        TestTrue(TEXT("Minimal: the landing spot stays"), Drawn->GetLandingRing()->IsVisible());
        Overlay->SetOverlayDetail(EPSOverlayDetail::Full);
    }

    // Knocked off its path: the flight is over; its marks linger, then go.
    Ball->SetActorLocation(Ball->GetActorLocation() + FVector(0.f, 0.f, Style.DeviationTolerance + 100.f));
    Overlay->AdvanceTime(0.016f);
    Flight = Overlay->GetFlight();
    TestFalse(TEXT("Deflected: the prediction no longer holds"), Flight.bActive);
    TestTrue(TEXT("...its marks linger"), Flight.bVisible);
    Overlay->AdvanceTime(Style.LingerSeconds + 0.1f);
    TestFalse(TEXT("...then go"), Overlay->GetFlight().bVisible);
    if (Drawn)
    {
        TestTrue(TEXT("Nothing drawn"), Drawn->IsHidden() && Drawn->GetShownDotCount() == 0);
    }

    // A second pass, caught.
    GiveBall(Ball, QB);
    Overlay->AdvanceTime(0.016f);
    TestTrue(TEXT("The QB throws again"), QB->ThrowPass(Ball, WR->GetActorLocation(), true, WR));
    TestTrue(TEXT("A new flight"), Overlay->GetFlight().bActive);
    TestTrue(TEXT("A lob comes down later than the line drive"), Overlay->GetFlight().Prediction.LandingSeconds > Arc.LandingSeconds);
    GiveBall(Ball, WR);
    Overlay->AdvanceTime(0.016f);
    TestFalse(TEXT("Caught: the flight is over"), Overlay->GetFlight().bActive);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- A kick against the uprights
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSKickArcTest,
    "PlaySports.Overlay.KickArcReadsTheUprights",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSKickArcTest::RunTest(const FString& Parameters)
{
    using namespace PSOverlayBallFlightTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSOverlayBallFlightSubsystem* Overlay = World ? World->GetSubsystem<UPSOverlayBallFlightSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Ball-flight overlay"), Overlay))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    Overlay->SetOverlayDetail(EPSOverlayDetail::Full);
    const FPSBallFlightStyle& Style = Overlay->GetStyle();

    // From the opponent's 25 in the game mode's frame (X = 7500), facing the far posts.
    APSPlayerPawn* Kicker = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("K_1"), FVector(7500.f, 0.f, 100.f));
    APSBall* Ball = Kicker ? SpawnBall(World, Kicker->GetActorLocation()) : nullptr;
    if (!TestNotNull(TEXT("Kicker"), Kicker) || !TestNotNull(TEXT("Ball"), Ball))
    {
        DestroyTestWorld(World);
        return false;
    }

    // Outside a kick phase a ball leaving the kicker isn't a kick.
    AnnouncePhase(Bus, TEXT("PassRush"));
    GiveBall(Ball, Kicker);
    Overlay->AdvanceTime(0.016f);
    TestTrue(TEXT("Launched"), Kicker->ExecuteKick(Ball, 2500.f, 35.f));
    Overlay->AdvanceTime(0.016f);
    TestEqual(TEXT("Not during a pass rush"), Overlay->GetFlight().Kind, EPSBallFlightKind::None);

    // A field goal: straight and strong.
    AnnouncePhase(Bus, TEXT("FieldGoal"));
    GiveBall(Ball, Kicker);
    Overlay->AdvanceTime(0.016f);
    TestTrue(TEXT("Kicked"), Kicker->ExecuteKick(Ball, 2500.f, 35.f));
    Overlay->AdvanceTime(0.016f);
    FPSBallFlightState Flight = Overlay->GetFlight();
    TestEqual(TEXT("A kick is followed"), Flight.Kind, EPSBallFlightKind::Kick);
    TestTrue(TEXT("...from the ball's flight"), Flight.bActive && Flight.Prediction.bValid);
    TestEqual(TEXT("It comes down on the ground"), Flight.Prediction.LandingHeight, Style.GroundZ, 0.01f);
    TestEqual(TEXT("Good"), Flight.Kick.Verdict, EPSKickVerdict::Good);
    TestEqual(TEXT("...at the far posts"), Flight.Kick.GoalPostX, 11000.f);
    TestTrue(TEXT("...over the bar, between the uprights"), Flight.Kick.HeightOverBar > 0.f && Flight.Kick.MarginInside > 0.f);
    TestFalse(TEXT("No receiver lead on a kick"), Flight.Lead.bValid);
    TestEqual(TEXT("The landing ring is the style's"), Flight.LandingRadius, Style.LandingRadiusFallback, 0.01f);

    APSOverlayBallFlight* Drawn = Overlay->GetOverlayActor();
    if (TestNotNull(TEXT("The overlay is drawn"), Drawn))
    {
        TestTrue(TEXT("The readout is up"), Drawn->GetReadoutText()->IsVisible());
        TestEqual(TEXT("...saying GOOD"), Drawn->GetReadoutLabel(), Style.GoodLabel);
        const FVector Above = Drawn->GetReadoutText()->GetComponentLocation();
        TestTrue(TEXT("...above the crossbar of those posts"), FMath::IsNearlyEqual(static_cast<float>(Above.X), 11000.f, 0.1f)
            && Above.Z > Style.GroundZ + Style.CrossbarHeight);
        TestFalse(TEXT("...and no lead ring"), Drawn->GetLeadRing()->IsVisible());
        TestTrue(TEXT("The arc is drawn"), Drawn->GetShownDotCount() > 0);
    }

    // Down on the ground: the flight is over, the readout lingers, then goes.
    Ball->SetActorLocation(PSOverlayBallFlight::PositionAt(Flight.Prediction, Flight.Prediction.LandingSeconds + 0.01f));
    Overlay->AdvanceTime(0.016f);
    TestFalse(TEXT("Down: the flight is over"), Overlay->GetFlight().bActive);
    Overlay->AdvanceTime(Style.ReadoutSeconds * 0.5f);
    TestTrue(TEXT("The readout lingers"), Overlay->GetFlight().bVisible);
    Overlay->AdvanceTime(Style.ReadoutSeconds);
    TestFalse(TEXT("...then goes"), Overlay->GetFlight().bVisible);

    // Hooked to the kicker's right.
    Kicker->SetActorRotation(FRotator(0.f, 10.f, 0.f));
    GiveBall(Ball, Kicker);
    Overlay->AdvanceTime(0.016f);
    Kicker->ExecuteKick(Ball, 2500.f, 35.f);
    Overlay->AdvanceTime(0.016f);
    TestEqual(TEXT("Ten degrees right: wide right"), Overlay->GetFlight().Kick.Verdict, EPSKickVerdict::WideRight);
    if (Drawn)
    {
        TestEqual(TEXT("...saying WIDE RIGHT"), Drawn->GetReadoutLabel(), Style.WideRightLabel);
    }

    // Too weak to get there.
    Kicker->SetActorRotation(FRotator::ZeroRotator);
    GiveBall(Ball, Kicker);
    Overlay->AdvanceTime(0.016f);
    Kicker->ExecuteKick(Ball, 1500.f, 35.f);
    Overlay->AdvanceTime(0.016f);
    const FPSKickReadout Weak = Overlay->GetFlight().Kick;
    TestEqual(TEXT("Weak: short"), Weak.Verdict, EPSKickVerdict::Short);
    TestFalse(TEXT("...down before the posts"), Weak.bReachesGoal);

    // Minimal keeps the readout and the landing spot, without the arc.
    Overlay->SetOverlayDetail(EPSOverlayDetail::Minimal);
    if (Drawn)
    {
        TestTrue(TEXT("Minimal: the readout stays"), Drawn->GetReadoutText()->IsVisible());
        TestEqual(TEXT("Minimal: no arc"), Drawn->GetShownDotCount(), 0);
    }

    DestroyTestWorld(World);
    return true;
}

#endif
