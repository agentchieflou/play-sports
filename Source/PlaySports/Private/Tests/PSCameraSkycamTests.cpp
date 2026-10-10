// PSCameraSkycamTests.cpp -- Epic 39 (the skycam)
//
// Headless runs can't render, so the skycam is tested as a simulation: where the rig may fly,
// how it flies there, and when the director puts it on air.
//
// Tests covered:
//   1. The data: Data/camera_skycam.json loads, validates and matches the struct defaults, and
//      validation reports each kind of mistake; the director has a skycam shot and cuts to it
//      on a breakaway.
//   2. The envelope: the cables' catenary ceiling (anchor height at the towers, lowest over
//      midfield), points outside it are brought inside, and a long flight after random targets
//      never leaves it or breaks the winches' limits.
//   3. Following: parked behind the quarterback before the snap and looking downfield, then
//      chasing the runner from the snap with lag (no jump, settling to his speed, behind him).
//   4. The handoff: the director cuts to the skycam when the ball carrier breaks clear, the
//      camera is the rig's shot while it is on air, and the director cuts away on the tackle.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBroadcastCamera.h"
#include "PSCameraAll22Component.h"
#include "PSCameraDirectorComponent.h"
#include "PSCameraSkycamComponent.h"
#include "PSDataIngestion.h"
#include "PSFieldGrid.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "PSTelemetrySamplingSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Math/RandomStream.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSCameraSkycamTests
{
    static constexpr int32 QuarterbackIndex = 5;
    static constexpr int32 RunningBackIndex = 6;
    static constexpr int32 LinebackerIndex = 15;
    static constexpr float StepSeconds = 0.1f;

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

    static bool Near(double A, double B, double Tolerance)
    {
        return FMath::Abs(A - B) <= Tolerance;
    }

    static double ExpectedSag(double HalfSpan, double Offset, double CatenaryA)
    {
        const double Clamped = FMath::Min(FMath::Abs(Offset), HalfSpan);
        return CatenaryA * (0.5 * (FMath::Exp(HalfSpan / CatenaryA) + FMath::Exp(-HalfSpan / CatenaryA))
            - 0.5 * (FMath::Exp(Clamped / CatenaryA) + FMath::Exp(-Clamped / CatenaryA)));
    }

    static TArray<EPlayerRole> All22Roles()
    {
        return {
            EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman,
            EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman, EPlayerRole::Quarterback,
            EPlayerRole::RunningBack, EPlayerRole::WideReceiver, EPlayerRole::WideReceiver,
            EPlayerRole::WideReceiver, EPlayerRole::TightEnd,
            EPlayerRole::DefensiveLineman, EPlayerRole::DefensiveLineman, EPlayerRole::DefensiveLineman,
            EPlayerRole::DefensiveLineman, EPlayerRole::Linebacker, EPlayerRole::Linebacker,
            EPlayerRole::Linebacker, EPlayerRole::DefensiveBack, EPlayerRole::DefensiveBack,
            EPlayerRole::DefensiveBack, EPlayerRole::DefensiveBack
        };
    }

    static FString DisplayNameOf(int32 Index)
    {
        return FString::Printf(TEXT("Player %d"), Index);
    }

    static TArray<APSPlayerPawn*> SpawnAll22(UWorld* World, const TArray<FVector>& Locations)
    {
        const TArray<EPlayerRole> Roles = All22Roles();
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        TArray<APSPlayerPawn*> Pawns;
        for (int32 Index = 0; Index < Roles.Num() && Index < Locations.Num(); ++Index)
        {
            APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Locations[Index], FRotator::ZeroRotator, SpawnParams);
            if (!Pawn)
            {
                continue;
            }
            FPlayerAttributes Attributes;
            Attributes.PlayerId = FName(*FString::Printf(TEXT("P%02d"), Index));
            Attributes.DisplayName = DisplayNameOf(Index);
            Attributes.Role = Roles[Index];
            Pawn->InitializePlayer(Attributes);
            Pawn->SetActorLocation(Locations[Index]);
            Pawns.Add(Pawn);
        }
        return Pawns;
    }

    static APSBroadcastCamera* SpawnCamera(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<APSBroadcastCamera>(APSBroadcastCamera::StaticClass(), FVector(0.0, -2800.0, 600.0), FRotator(-10.0, 90.0, 0.0), SpawnParams);
    }

    static void SteadySampling(UPSTelemetrySamplingSubsystem* Sampler)
    {
        FPSTelemetrySamplingTuning Steady = Sampler->GetTuning();
        Steady.SampleRateHz = 10.f;
        Steady.SampleBudgetMs = 1000.f;
        Steady.RecoverAfterSamples = 100000;
        Sampler->SetTuning(Steady);
    }

    static bool AnyProblemContains(const TArray<FString>& Problems, const TCHAR* Needle)
    {
        return Problems.ContainsByPredicate([Needle](const FString& Problem) { return Problem.Contains(Needle); });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The data
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraSkycamTuningTest,
    "PlaySports.Camera.SkycamTuning",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraSkycamTuningTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraSkycamTests;

    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSSkycamTuning FromFile;
    if (!TestTrue(TEXT("Data/camera_skycam.json loads through UPSDataIngestion"), Ingestion->LoadSkycamTuningFromJson(UPSCameraSkycamComponent::GetDefaultTuningPath(), FromFile)))
    {
        return false;
    }
    for (const FString& Problem : UPSCameraSkycamComponent::ValidateTuning(FromFile))
    {
        AddError(FString::Printf(TEXT("camera_skycam.json: %s"), *Problem));
    }

    const FPSSkycamTuning Defaults;
    TestEqual(TEXT("AnchorHalfLengthCm default matches"), Defaults.AnchorHalfLengthCm, FromFile.AnchorHalfLengthCm);
    TestEqual(TEXT("AnchorHalfWidthCm default matches"), Defaults.AnchorHalfWidthCm, FromFile.AnchorHalfWidthCm);
    TestEqual(TEXT("AnchorHeightCm default matches"), Defaults.AnchorHeightCm, FromFile.AnchorHeightCm);
    TestEqual(TEXT("CatenaryParameterCm default matches"), Defaults.CatenaryParameterCm, FromFile.CatenaryParameterCm);
    TestEqual(TEXT("EdgeMarginCm default matches"), Defaults.EdgeMarginCm, FromFile.EdgeMarginCm);
    TestEqual(TEXT("MinHeightCm default matches"), Defaults.MinHeightCm, FromFile.MinHeightCm);
    TestEqual(TEXT("StiffnessPerSecSq default matches"), Defaults.StiffnessPerSecSq, FromFile.StiffnessPerSecSq);
    TestEqual(TEXT("DampingPerSec default matches"), Defaults.DampingPerSec, FromFile.DampingPerSec);
    TestEqual(TEXT("MaxSpeedCms default matches"), Defaults.MaxSpeedCms, FromFile.MaxSpeedCms);
    TestEqual(TEXT("MaxAccelerationCms2 default matches"), Defaults.MaxAccelerationCms2, FromFile.MaxAccelerationCms2);
    TestEqual(TEXT("BehindQuarterbackDistanceCm default matches"), Defaults.BehindQuarterbackDistanceCm, FromFile.BehindQuarterbackDistanceCm);
    TestEqual(TEXT("BehindQuarterbackHeightCm default matches"), Defaults.BehindQuarterbackHeightCm, FromFile.BehindQuarterbackHeightCm);
    TestEqual(TEXT("ChaseDistanceCm default matches"), Defaults.ChaseDistanceCm, FromFile.ChaseDistanceCm);
    TestEqual(TEXT("ChaseHeightCm default matches"), Defaults.ChaseHeightCm, FromFile.ChaseHeightCm);
    TestEqual(TEXT("LookAheadCm default matches"), Defaults.LookAheadCm, FromFile.LookAheadCm);
    TestEqual(TEXT("FieldOfView default matches"), Defaults.FieldOfView, FromFile.FieldOfView);
    TestTrue(TEXT("The rig is critically damped (it settles without swinging past)"),
        Near(FromFile.DampingPerSec, 2.0 * FMath::Sqrt(FromFile.StiffnessPerSecSq), 0.01));

    FPSSkycamTuning Broken = FromFile;
    Broken.MinHeightCm = FromFile.AnchorHeightCm;
    Broken.MaxSpeedCms = 0.f;
    Broken.EdgeMarginCm = FromFile.AnchorHalfWidthCm;
    Broken.FieldOfView = 0.f;
    const TArray<FString> Problems = UPSCameraSkycamComponent::ValidateTuning(Broken);
    TestTrue(TEXT("A floor above the cables is reported"), AnyProblemContains(Problems, TEXT("MinHeightCm")));
    TestTrue(TEXT("A rig that can't move is reported"), AnyProblemContains(Problems, TEXT("MaxSpeedCms")));
    TestTrue(TEXT("A margin that leaves no room is reported"), AnyProblemContains(Problems, TEXT("EdgeMarginCm")));
    TestTrue(TEXT("A zero field of view is reported"), AnyProblemContains(Problems, TEXT("FieldOfView")));

    // The director knows the skycam: a shot, and a cut to it on a breakaway.
    FPSCameraDirectorTuning Director;
    FPSAll22CameraTuning All22;
    TestTrue(TEXT("The director and all-22 data load"), Ingestion->LoadCameraDirectorTuningFromJson(UPSCameraDirectorComponent::GetDefaultTuningPath(), Director)
        && Ingestion->LoadAll22CameraTuningFromJson(UPSCameraAll22Component::GetDefaultTuningPath(), All22));
    for (const FString& Problem : UPSCameraDirectorComponent::ValidateTuning(Director, &All22))
    {
        AddError(FString::Printf(TEXT("camera_director.json: %s"), *Problem));
    }
    TestTrue(TEXT("The director has a skycam shot"), Director.Shots.ContainsByPredicate([](const FPSDirectorShotDef& Def) { return Def.Shot == EPSDirectorShot::Skycam; }));
    TestTrue(TEXT("...and cuts to it on a breakaway"), Director.CutRules.ContainsByPredicate([](const FPSDirectorCutRule& Rule)
    {
        return Rule.Trigger == EPSDirectorTrigger::Breakaway && Rule.Shot == EPSDirectorShot::Skycam;
    }));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The cable envelope
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraSkycamEnvelopeTest,
    "PlaySports.Camera.SkycamEnvelope",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraSkycamEnvelopeTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraSkycamTests;

    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSSkycamTuning Tuning;
    if (!TestTrue(TEXT("Tuning loads"), Ingestion->LoadSkycamTuningFromJson(UPSCameraSkycamComponent::GetDefaultTuningPath(), Tuning)))
    {
        return false;
    }
    const double HalfLength = Tuning.AnchorHalfLengthCm;
    const double HalfWidth = Tuning.AnchorHalfWidthCm;

    // The ceiling is the catenaries': anchor height at the towers, lowest over midfield.
    TestTrue(TEXT("At a tower the cables are at anchor height"),
        Near(UPSCameraSkycamComponent::CeilingAt(Tuning, HalfLength, -HalfWidth), Tuning.AnchorHeightCm, 0.01));
    const double Expected = Tuning.AnchorHeightCm - ExpectedSag(HalfLength, 0.0, Tuning.CatenaryParameterCm) - ExpectedSag(HalfWidth, 0.0, Tuning.CatenaryParameterCm);
    TestTrue(TEXT("Over midfield the cables hang by both catenaries' sag"), Near(UPSCameraSkycamComponent::CeilingAt(Tuning, 0.0, 0.0), Expected, 0.01));
    TestTrue(TEXT("...the lowest point"), UPSCameraSkycamComponent::CeilingAt(Tuning, 0.0, 0.0) < UPSCameraSkycamComponent::CeilingAt(Tuning, HalfLength * 0.5, 0.0)
        && UPSCameraSkycamComponent::CeilingAt(Tuning, HalfLength * 0.5, 0.0) < UPSCameraSkycamComponent::CeilingAt(Tuning, HalfLength, 0.0));
    TestTrue(TEXT("...and the sag is real, not a flat lid"), UPSCameraSkycamComponent::CeilingAt(Tuning, 0.0, 0.0) < Tuning.AnchorHeightCm - 100.0);

    // Points outside are brought inside.
    const FVector Above = UPSCameraSkycamComponent::ConstrainToEnvelope(Tuning, FVector(0.0, 0.0, Tuning.AnchorHeightCm));
    TestTrue(TEXT("Above the cables comes down to them"), Near(Above.Z, UPSCameraSkycamComponent::CeilingAt(Tuning, 0.0, 0.0), 0.01));
    const FVector Below = UPSCameraSkycamComponent::ConstrainToEnvelope(Tuning, FVector(0.0, 0.0, 0.0));
    TestTrue(TEXT("Down among the players comes up to the floor"), Near(Below.Z, Tuning.MinHeightCm, 0.01));
    const FVector Outside = UPSCameraSkycamComponent::ConstrainToEnvelope(Tuning, FVector(HalfLength * 2.0, -HalfWidth * 2.0, 1500.0));
    TestTrue(TEXT("Past the towers comes back inside the margin"),
        Near(Outside.X, HalfLength - Tuning.EdgeMarginCm, 0.01) && Near(Outside.Y, -(HalfWidth - Tuning.EdgeMarginCm), 0.01));

    // A long flight after random targets, some far outside: always inside, never over speed.
    FRandomStream Random(40391);
    FVector Location = UPSCameraSkycamComponent::ConstrainToEnvelope(Tuning, FVector(0.0, 0.0, 1500.0));
    FVector Velocity = FVector::ZeroVector;
    FVector Desired = Location;
    const float Dt = 1.f / 30.f;
    bool bInside = true;
    bool bUnderSpeed = true;
    for (int32 Step = 0; Step < 3000; ++Step)
    {
        if (Step % 45 == 0)
        {
            const float ReachX = static_cast<float>(HalfLength * 1.5);
            const float ReachY = static_cast<float>(HalfWidth * 1.5);
            Desired = FVector(Random.FRandRange(-ReachX, ReachX), Random.FRandRange(-ReachY, ReachY),
                Random.FRandRange(-500.f, Tuning.AnchorHeightCm + 1000.f));
        }
        UPSCameraSkycamComponent::StepRig(Tuning, Location, Velocity, Desired, Dt);
        if (!(UPSCameraSkycamComponent::IsInsideEnvelope(Tuning, Location, 0.01)))
        {
            bInside = false;
        }
        if (!(Velocity.Size() <= Tuning.MaxSpeedCms + 0.01))
        {
            bUnderSpeed = false;
        }
    }
    TestTrue(TEXT("A long flight never leaves the envelope"), bInside);
    TestTrue(TEXT("...or flies faster than the winches"), bUnderSpeed);

    // In open air the winches limit the acceleration too.
    FVector Free = FVector(0.0, 0.0, 1500.0);
    FVector FreeVelocity = FVector::ZeroVector;
    bool bUnderAcceleration = true;
    for (int32 Step = 0; Step < 60; ++Step)
    {
        const FVector Before = FreeVelocity;
        UPSCameraSkycamComponent::StepRig(Tuning, Free, FreeVelocity, FVector(3000.0, 1000.0, 1500.0), Dt);
        if (!((FreeVelocity - Before).Size() <= Tuning.MaxAccelerationCms2 * Dt + 0.01))
        {
            bUnderAcceleration = false;
        }
    }
    TestTrue(TEXT("...or accelerates harder than they can"), bUnderAcceleration);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Following: behind the quarterback, then the chase
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraSkycamFollowTest,
    "PlaySports.Camera.SkycamFollows",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraSkycamFollowTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraSkycamTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    const TArray<APSPlayerPawn*> Pawns = SpawnAll22(World, APSFieldGrid::ComputeLineup(All22Roles(), 0.f));
    APSBroadcastCamera* Camera = SpawnCamera(World);
    UPSCameraSkycamComponent* Skycam = Camera ? Camera->GetSkycamComponent() : nullptr;
    UPSTelemetrySamplingSubsystem* Sampler = World->GetSubsystem<UPSTelemetrySamplingSubsystem>();
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    if (!TestEqual(TEXT("22 players"), Pawns.Num(), 22) || !TestNotNull(TEXT("Skycam"), Skycam)
        || !TestNotNull(TEXT("Sampler (Epic 26)"), Sampler) || !TestNotNull(TEXT("Bus"), Bus))
    {
        DestroyTestWorld(World);
        return false;
    }
    SteadySampling(Sampler);
    Skycam->BindToBus();
    const FPSSkycamTuning Tuning = Skycam->GetTuning();
    auto Step = [&]()
    {
        Sampler->AdvanceTime(StepSeconds);
        Skycam->AdvanceTime(StepSeconds);
    };

    // Before the snap: parked behind the quarterback, high, looking downfield.
    const FVector Quarterback = Pawns[QuarterbackIndex]->GetActorLocation();
    Step();
    TestTrue(TEXT("The skycam is flying"), Skycam->IsFlying());
    TestEqual(TEXT("Before the snap it is behind the quarterback"), Skycam->GetMode(), EPSSkycamMode::BehindQuarterback);
    const FVector Parked = Skycam->GetRigLocation();
    TestTrue(TEXT("...its distance behind him"), Near(Parked.X, Quarterback.X - Tuning.BehindQuarterbackDistanceCm, 1.0) && Near(Parked.Y, Quarterback.Y, 1.0));
    TestTrue(TEXT("...at its height"), Near(Parked.Z, Tuning.BehindQuarterbackHeightCm, 1.0));
    TestTrue(TEXT("...looking downfield, past him"), Skycam->GetLookTarget().X > Quarterback.X && FMath::Abs(Skycam->GetShot().Rotation.Yaw) < 5.0);

    // The snap: the back takes the ball and runs right at 9 m/s.
    Bus->PublishSnap(FPSTelemetrySnapEvent());
    TestEqual(TEXT("From the snap it chases"), Skycam->GetMode(), EPSSkycamMode::Chase);
    APSPlayerPawn* Runner = Pawns[RunningBackIndex];
    Runner->GainPossession();
    const double RunSpeed = 900.0;
    Runner->GetFloatingMovementComponent()->Velocity = FVector(RunSpeed, 0.0, 0.0);

    bool bInside = true;
    bool bUnderSpeed = true;
    bool bBehind = true;
    double FirstLag = 0.0;
    for (int32 Index = 0; Index < 40; ++Index)
    {
        Runner->SetActorLocation(Runner->GetActorLocation() + FVector(RunSpeed * StepSeconds, 0.0, 0.0));
        Step();
        if (Index == 0)
        {
            FirstLag = FVector::Dist(Skycam->GetRigLocation(), Skycam->GetDesiredLocation());
        }
        if (!(UPSCameraSkycamComponent::IsInsideEnvelope(Tuning, Skycam->GetRigLocation(), 0.01)))
        {
            bInside = false;
        }
        if (!(Skycam->GetRigVelocity().Size() <= Tuning.MaxSpeedCms + 0.01))
        {
            bUnderSpeed = false;
        }
        if (!(Skycam->GetRigLocation().X < Runner->GetActorLocation().X))
        {
            bBehind = false;
        }
    }
    TestTrue(TEXT("The rig has mass: it doesn't jump to the chase"), FirstLag > 100.0);
    TestTrue(TEXT("It stays inside the cables' envelope"), bInside);
    TestTrue(TEXT("...within the winches' speed"), bUnderSpeed);
    TestTrue(TEXT("...and behind the runner"), bBehind);
    TestTrue(TEXT("It settles to the runner's pace"), Near(Skycam->GetRigVelocity().X, RunSpeed, 50.0));
    TestTrue(TEXT("...at its chase height"), Near(Skycam->GetRigLocation().Z, Tuning.ChaseHeightCm, 25.0));
    TestTrue(TEXT("...looking ahead of him"), Skycam->GetLookTarget().X > Runner->GetActorLocation().X);

    // The next down lines up: back behind the quarterback.
    FPSTelemetryPhaseChangeEvent NextDown;
    NextDown.NewPhase = TEXT("PreSnap");
    Bus->PublishPhaseChange(NextDown);
    TestEqual(TEXT("The next down parks it behind the quarterback again"), Skycam->GetMode(), EPSSkycamMode::BehindQuarterback);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The director's handoff
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraSkycamHandoffTest,
    "PlaySports.Camera.SkycamDirectorHandoff",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraSkycamHandoffTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraSkycamTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    const TArray<APSPlayerPawn*> Pawns = SpawnAll22(World, APSFieldGrid::ComputeLineup(All22Roles(), 0.f));
    APSBroadcastCamera* Camera = SpawnCamera(World);
    UPSCameraSkycamComponent* Skycam = Camera ? Camera->GetSkycamComponent() : nullptr;
    UPSCameraDirectorComponent* Director = Camera ? Camera->GetDirectorComponent() : nullptr;
    UPSCameraAll22Component* Film = Camera ? Camera->GetAll22Component() : nullptr;
    UPSTelemetrySamplingSubsystem* Sampler = World->GetSubsystem<UPSTelemetrySamplingSubsystem>();
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    if (!TestEqual(TEXT("22 players"), Pawns.Num(), 22) || !TestNotNull(TEXT("Skycam"), Skycam) || !TestNotNull(TEXT("Director"), Director)
        || !TestNotNull(TEXT("Film component"), Film) || !TestNotNull(TEXT("Sampler (Epic 26)"), Sampler) || !TestNotNull(TEXT("Bus"), Bus))
    {
        DestroyTestWorld(World);
        return false;
    }
    SteadySampling(Sampler);
    Skycam->BindToBus();
    Director->BindToBus();
    Film->BindToBus();
    const float MinShot = Director->GetTuning().MinShotSeconds;
    APSPlayerPawn* Runner = Pawns[RunningBackIndex];
    Runner->GainPossession();

    // The broadcast camera's own tick flies the skycam and runs the director.
    auto Step = [&]()
    {
        Sampler->AdvanceTime(StepSeconds);
        Camera->Tick(StepSeconds);
    };
    auto StepUntilShotAge = [&](float Seconds)
    {
        for (int32 Guard = 0; Guard < 100 && Director->GetShotAge() < Seconds; ++Guard)
        {
            Step();
        }
    };

    Step();
    TestEqual(TEXT("The director opens wide"), Director->GetCurrentShot(), EPSDirectorShot::LosWide);
    TestTrue(TEXT("...while the skycam flies off air"), Skycam->IsFlying());
    Bus->PublishSnap(FPSTelemetrySnapEvent());
    StepUntilShotAge(MinShot);
    Step();
    TestEqual(TEXT("The snap's follow"), Director->GetCurrentShot(), EPSDirectorShot::TightFollow);

    // The back breaks into the clear at 9 m/s: the director asks for the skycam, which comes up
    // once the follow has run its minimum.
    Runner->SetActorLocation(FVector(1500.0, 0.0, 100.0));
    Runner->GetFloatingMovementComponent()->Velocity = FVector(900.0, 0.0, 0.0);
    bool bSkycamOnAir = false;
    bool bCameraIsRig = true;
    for (int32 Index = 0; Index < 30; ++Index)
    {
        Runner->SetActorLocation(Runner->GetActorLocation() + FVector(90.0, 0.0, 0.0));
        Step();
        if (Director->GetCurrentShot() == EPSDirectorShot::Skycam)
        {
            bSkycamOnAir = true;
            if (!(Camera->GetActorLocation().Equals(Skycam->GetRigLocation(), 1.0)
                && Director->GetTargetShot().Location.Equals(Skycam->GetShot().Location, 1.0)))
            {
                bCameraIsRig = false;
            }
        }
    }
    TestTrue(TEXT("The breakaway puts the skycam on air"), bSkycamOnAir);
    TestTrue(TEXT("...and while it is, the camera is the rig's shot"), bCameraIsRig);

    // The tackle: the director cuts away from the skycam once its minimum has run.
    Runner->GetFloatingMovementComponent()->Velocity = FVector::ZeroVector;
    FPSTelemetryTackleEvent Tackle;
    Tackle.TacklerName = DisplayNameOf(LinebackerIndex);
    Tackle.BallCarrierName = DisplayNameOf(RunningBackIndex);
    Bus->PublishTackle(Tackle);
    StepUntilShotAge(MinShot);
    Step();
    TestEqual(TEXT("The tackle cuts away from the skycam"), Director->GetCurrentShot(), EPSDirectorShot::SidelineReaction);
    TestFalse(TEXT("...and the camera leaves the rig"), Camera->GetActorLocation().Equals(Skycam->GetRigLocation(), 1.0));

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
