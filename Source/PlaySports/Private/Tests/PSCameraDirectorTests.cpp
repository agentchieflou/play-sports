// PSCameraDirectorTests.cpp -- Epic 38 (the camera director)
//
// Headless runs can't render, so the director is tested as math and as a state machine: which
// shot is live, when it cut, who the subject is, and where the camera stands.
//
// Tests covered:
//   1. The data: Data/camera_director.json loads, validates against the all-22 rigs, matches the
//      struct defaults, and cuts wide before the snap, follows from it and goes tight at the
//      whistle; validation reports each kind of mistake.
//   2. The shot vocabulary: each of the five shots stands where it should, aims at its target,
//      and stays on the camera side; the two high shots are Epic 40's rigs and hold all 22.
//   3. Interest scoring and subject choice: the ball, closeness to it, breakaways and big hits
//      each count, big hits fade, and the subject only changes for a clearly better one.
//   4. The cut rules through the bus: the opening wide shot, the snap's follow held back until
//      the wide shot has run its minimum, the latest of several quick asks winning, no cut to
//      the live shot, easing within a shot, every shot on the camera side, and the film view
//      overriding the director.
//   5. The subject follows interest through Epic 26's snapshots: a breakaway runner, then the
//      two men in a tackle, then the runner again once the hit fades.
//   6. The 180-degree rule: a shot across the line of action moves back to the camera side and
//      re-aims; a shot on the line is left alone.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBroadcastCamera.h"
#include "PSCameraAll22Component.h"
#include "PSCameraDirectorComponent.h"
#include "PSCameraFraming.h"
#include "PSDataIngestion.h"
#include "PSFieldGrid.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "PSTelemetrySamplingSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/FloatingPawnMovement.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSCameraDirectorTests
{
    static constexpr int32 QuarterbackIndex = 5;
    static constexpr int32 RunningBackIndex = 6;
    static constexpr int32 ReceiverIndex = 7;
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

    /** Eleven on eleven, offense first (the order Epic 26's snapshots list them in). */
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

    static FName PlayerIdOf(int32 Index)
    {
        return FName(*FString::Printf(TEXT("P%02d"), Index));
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
            Attributes.PlayerId = PlayerIdOf(Index);
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

    /** Samples at 10 Hz on a budget it never exceeds, so every 0.1 s step takes a frame. */
    static void SteadySampling(UPSTelemetrySamplingSubsystem* Sampler)
    {
        FPSTelemetrySamplingTuning Steady = Sampler->GetTuning();
        Steady.SampleRateHz = 10.f;
        Steady.SampleBudgetMs = 1000.f;
        Steady.RecoverAfterSamples = 100000;
        Sampler->SetTuning(Steady);
    }

    static bool LoadTunings(FPSCameraDirectorTuning& OutDirector, FPSAll22CameraTuning& OutAll22)
    {
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        return Ingestion->LoadCameraDirectorTuningFromJson(UPSCameraDirectorComponent::GetDefaultTuningPath(), OutDirector)
            && Ingestion->LoadAll22CameraTuningFromJson(UPSCameraAll22Component::GetDefaultTuningPath(), OutAll22);
    }

    static const FPSDirectorShotDef* FindShot(const FPSCameraDirectorTuning& Tuning, EPSDirectorShot Shot)
    {
        return Tuning.Shots.FindByPredicate([Shot](const FPSDirectorShotDef& Def) { return Def.Shot == Shot; });
    }

    static EPSDirectorShot RuleFor(const FPSCameraDirectorTuning& Tuning, EPSDirectorTrigger Trigger)
    {
        const FPSDirectorCutRule* Rule = Tuning.CutRules.FindByPredicate([Trigger](const FPSDirectorCutRule& Candidate) { return Candidate.Trigger == Trigger; });
        return Rule ? Rule->Shot : EPSDirectorShot::None;
    }

    static bool AnyProblemContains(const TArray<FString>& Problems, const TCHAR* Needle)
    {
        return Problems.ContainsByPredicate([Needle](const FString& Problem) { return Problem.Contains(Needle); });
    }

    static bool AllInShot(const FPSCameraShot& Shot, const TArray<FVector>& Locations)
    {
        for (const FVector& Location : Locations)
        {
            if (!UPSCameraFraming::IsPointInShot(Shot, Location))
            {
                return false;
            }
        }
        return true;
    }

    /** True when Target lands at the middle of Shot's frame. */
    static bool AimsAt(const FPSCameraShot& Shot, const FVector& Target)
    {
        FVector2D Screen;
        return UPSCameraFraming::ProjectToShot(Shot, Target, Screen) && Screen.Equals(FVector2D(0.5, 0.5), 0.002);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The data
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraDirectorTuningTest,
    "PlaySports.Camera.DirectorTuning",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraDirectorTuningTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraDirectorTests;

    FPSCameraDirectorTuning FromFile;
    FPSAll22CameraTuning All22;
    if (!TestTrue(TEXT("camera_director.json and camera_all22.json load through UPSDataIngestion"), LoadTunings(FromFile, All22)))
    {
        return false;
    }
    for (const FString& Problem : UPSCameraDirectorComponent::ValidateTuning(FromFile, &All22))
    {
        AddError(FString::Printf(TEXT("camera_director.json: %s"), *Problem));
    }

    // The story's rhythm: wide before the snap, follow from it, tight after the whistle.
    TestEqual(TEXT("Pre-snap is wide"), RuleFor(FromFile, EPSDirectorTrigger::PreSnap), EPSDirectorShot::LosWide);
    TestEqual(TEXT("The snap follows"), RuleFor(FromFile, EPSDirectorTrigger::Snap), EPSDirectorShot::TightFollow);
    TestEqual(TEXT("The whistle goes tight"), RuleFor(FromFile, EPSDirectorTrigger::PlayEnd), EPSDirectorShot::SidelineReaction);
    const FPSDirectorShotDef* All22High = FindShot(FromFile, EPSDirectorShot::All22High);
    const FPSDirectorShotDef* EndZone = FindShot(FromFile, EPSDirectorShot::EndZone);
    TestTrue(TEXT("The all-22 high shot is Epic 40's sideline rig"), All22High && All22.All22Rigs.ContainsByPredicate(
        [All22High](const FPSAll22RigDef& Rig) { return Rig.RigId == All22High->RigId && Rig.Placement == EPSAll22RigPlacement::Sideline; }));
    TestTrue(TEXT("The end-zone shot is Epic 40's end-zone rig"), EndZone && All22.All22Rigs.ContainsByPredicate(
        [EndZone](const FPSAll22RigDef& Rig) { return Rig.RigId == EndZone->RigId && Rig.Placement == EPSAll22RigPlacement::EndZone; }));

    // The struct defaults are the file's values.
    const FPSCameraDirectorTuning Defaults;
    TestTrue(TEXT("bDirectorEnabled default matches"), Defaults.bDirectorEnabled == FromFile.bDirectorEnabled);
    TestEqual(TEXT("MinShotSeconds default matches"), Defaults.MinShotSeconds, FromFile.MinShotSeconds);
    TestEqual(TEXT("FollowInterpSpeed default matches"), Defaults.FollowInterpSpeed, FromFile.FollowInterpSpeed);
    TestEqual(TEXT("CameraSide default matches"), Defaults.CameraSide, FromFile.CameraSide);
    TestEqual(TEXT("NeutralBandCm default matches"), Defaults.NeutralBandCm, FromFile.NeutralBandCm);
    TestEqual(TEXT("BallWeight default matches"), Defaults.Interest.BallWeight, FromFile.Interest.BallWeight);
    TestEqual(TEXT("ProximityWeight default matches"), Defaults.Interest.ProximityWeight, FromFile.Interest.ProximityWeight);
    TestEqual(TEXT("ProximityRadiusCm default matches"), Defaults.Interest.ProximityRadiusCm, FromFile.Interest.ProximityRadiusCm);
    TestEqual(TEXT("BreakawayWeight default matches"), Defaults.Interest.BreakawayWeight, FromFile.Interest.BreakawayWeight);
    TestEqual(TEXT("BreakawaySpeedCms default matches"), Defaults.Interest.BreakawaySpeedCms, FromFile.Interest.BreakawaySpeedCms);
    TestEqual(TEXT("BreakawayClearanceCm default matches"), Defaults.Interest.BreakawayClearanceCm, FromFile.Interest.BreakawayClearanceCm);
    TestEqual(TEXT("BigHitWeight default matches"), Defaults.Interest.BigHitWeight, FromFile.Interest.BigHitWeight);
    TestEqual(TEXT("BigHitSeconds default matches"), Defaults.Interest.BigHitSeconds, FromFile.Interest.BigHitSeconds);
    TestEqual(TEXT("BigHitDamage default matches"), Defaults.Interest.BigHitDamage, FromFile.Interest.BigHitDamage);
    TestEqual(TEXT("SwitchMargin default matches"), Defaults.Interest.SwitchMargin, FromFile.Interest.SwitchMargin);

    // Validation reports each kind of mistake.
    FPSCameraDirectorTuning Broken = FromFile;
    if (Broken.Shots.Num() > 0 && Broken.CutRules.Num() > 1)
    {
        const FPSDirectorShotDef Twin = Broken.Shots[0];
        Broken.Shots.Add(Twin);
        Broken.CutRules[1].Trigger = Broken.CutRules[0].Trigger;
    }
    for (FPSDirectorShotDef& Def : Broken.Shots)
    {
        if (Def.Shot == EPSDirectorShot::All22High)
        {
            Def.RigId = TEXT("Blimp");
        }
        if (Def.Shot == EPSDirectorShot::SidelineReaction)
        {
            Def.FieldOfView = 0.f;
        }
    }
    Broken.CameraSide = 0;
    Broken.MinShotSeconds = -1.f;
    const TArray<FString> Problems = UPSCameraDirectorComponent::ValidateTuning(Broken, &All22);
    TestTrue(TEXT("A shot defined twice is reported"), AnyProblemContains(Problems, TEXT("defined twice")));
    TestTrue(TEXT("A trigger ruled twice is reported"), AnyProblemContains(Problems, TEXT("two rules")));
    TestTrue(TEXT("An all-22 shot on a missing rig is reported"), AnyProblemContains(Problems, TEXT("not a rig")));
    TestTrue(TEXT("A zero field of view is reported"), AnyProblemContains(Problems, TEXT("FieldOfView")));
    TestTrue(TEXT("A camera side of 0 is reported"), AnyProblemContains(Problems, TEXT("CameraSide")));
    TestTrue(TEXT("A negative minimum shot length is reported"), AnyProblemContains(Problems, TEXT("MinShotSeconds")));
    const TArray<FString> EmptyProblems = UPSCameraDirectorComponent::ValidateTuning(FPSCameraDirectorTuning(), nullptr);
    TestTrue(TEXT("Missing shots are reported"), AnyProblemContains(EmptyProblems, TEXT("not defined")));
    TestTrue(TEXT("A missing opening rule is reported"), AnyProblemContains(EmptyProblems, TEXT("PreSnap")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The shot vocabulary
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraDirectorVocabularyTest,
    "PlaySports.Camera.DirectorShotVocabulary",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraDirectorVocabularyTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraDirectorTests;

    FPSCameraDirectorTuning Tuning;
    FPSAll22CameraTuning All22;
    if (!TestTrue(TEXT("Tunings load"), LoadTunings(Tuning, All22)))
    {
        return false;
    }
    const FPSDirectorShotDef* LosWide = FindShot(Tuning, EPSDirectorShot::LosWide);
    const FPSDirectorShotDef* All22High = FindShot(Tuning, EPSDirectorShot::All22High);
    const FPSDirectorShotDef* TightFollow = FindShot(Tuning, EPSDirectorShot::TightFollow);
    const FPSDirectorShotDef* EndZone = FindShot(Tuning, EPSDirectorShot::EndZone);
    const FPSDirectorShotDef* Reaction = FindShot(Tuning, EPSDirectorShot::SidelineReaction);
    if (!LosWide || !All22High || !TightFollow || !EndZone || !Reaction)
    {
        AddError(TEXT("A shot of the vocabulary is missing"));
        return false;
    }

    // A midfield snap: the ball at the line, the back running right at 6 m/s.
    FPSDirectorView View;
    View.PlayerLocations = APSFieldGrid::ComputeLineup(All22Roles(), 0.f);
    View.BallLocation = FVector(0.0, 0.0, 100.0);
    View.SubjectLocation = View.PlayerLocations[RunningBackIndex];
    View.SubjectVelocity = FVector(600.0, 0.0, 0.0);
    View.AttackDirection = 1.f;
    View.AspectRatio = All22.AspectRatio;
    const int32 Side = Tuning.CameraSide;
    const double LineY = View.BallLocation.Y;

    FVector Target;
    const FPSCameraShot Wide = UPSCameraDirectorComponent::ComputeShot(*LosWide, View, &All22, Side, Target);
    TestTrue(TEXT("LOS wide: level with the ball"), Near(Wide.Location.X, View.BallLocation.X, 0.5));
    TestTrue(TEXT("...its distance off on the camera side"), Near(Wide.Location.Y, LineY + Side * LosWide->DistanceCm, 0.5));
    TestTrue(TEXT("...at its height"), Near(Wide.Location.Z, LosWide->HeightCm, 0.5));
    TestTrue(TEXT("...aimed at the ball"), AimsAt(Wide, Target) && Near(Target.X, View.BallLocation.X, 0.5));
    TestTrue(TEXT("...at its zoom"), Near(Wide.FieldOfView, LosWide->FieldOfView, 0.01));

    const FPSCameraShot Follow = UPSCameraDirectorComponent::ComputeShot(*TightFollow, View, &All22, Side, Target);
    const double LeadX = View.SubjectLocation.X + View.SubjectVelocity.X * TightFollow->LeadSeconds;
    TestTrue(TEXT("Tight follow: aimed ahead of the runner by his lead"), AimsAt(Follow, Target) && Near(Target.X, LeadX, 0.5));
    TestTrue(TEXT("...with the runner in frame"), UPSCameraFraming::IsPointInShot(Follow, View.SubjectLocation));
    TestTrue(TEXT("...tighter than the wide shot"), Follow.FieldOfView < Wide.FieldOfView);
    TestTrue(TEXT("...on the camera side"), UPSCameraDirectorComponent::IsOnCameraSide(Follow, LineY, Side, 0.f));

    const FPSCameraShot React = UPSCameraDirectorComponent::ComputeShot(*Reaction, View, &All22, Side, Target);
    TestTrue(TEXT("Sideline reaction: aimed at the subject"), AimsAt(React, Target) && Near(Target.X, View.SubjectLocation.X, 0.5));
    TestTrue(TEXT("...lower than the follow and the wide shot"), React.Location.Z < Follow.Location.Z && Follow.Location.Z < Wide.Location.Z);
    TestTrue(TEXT("...and closer"), FVector::Dist(React.Location, Target) < FVector::Dist(Follow.Location, View.SubjectLocation));

    const FPSAll22RigDef* SidelineRig = All22.All22Rigs.FindByPredicate([All22High](const FPSAll22RigDef& Rig) { return Rig.RigId == All22High->RigId; });
    const FPSCameraShot High = UPSCameraDirectorComponent::ComputeShot(*All22High, View, &All22, Side, Target);
    if (TestNotNull(TEXT("The all-22 high shot's rig"), SidelineRig))
    {
        const FPSCameraShot Film = UPSCameraFraming::FrameAll22(*SidelineRig, All22, View.PlayerLocations, View.AttackDirection, View.AspectRatio);
        TestTrue(TEXT("All-22 high is Epic 40's sideline framing"), High.Location.Equals(Film.Location, 0.5) && Near(High.FieldOfView, Film.FieldOfView, 0.01));
    }
    TestTrue(TEXT("...holding all 22"), AllInShot(High, View.PlayerLocations));
    TestTrue(TEXT("...on the camera side"), UPSCameraDirectorComponent::IsOnCameraSide(High, LineY, Side, 0.f));

    const FPSCameraShot FromEndZone = UPSCameraDirectorComponent::ComputeShot(*EndZone, View, &All22, Side, Target);
    double MinX = TNumericLimits<double>::Max();
    for (const FVector& Location : View.PlayerLocations)
    {
        MinX = FMath::Min(MinX, Location.X);
    }
    TestTrue(TEXT("End zone: behind the offense"), FromEndZone.Location.X < MinX);
    TestTrue(TEXT("...holding all 22"), AllInShot(FromEndZone, View.PlayerLocations));
    TestTrue(TEXT("...on the line of action, which either side may cut to"), UPSCameraDirectorComponent::IsOnCameraSide(FromEndZone, LineY, Side, Tuning.NeutralBandCm)
        && UPSCameraDirectorComponent::IsOnCameraSide(FromEndZone, LineY, -Side, Tuning.NeutralBandCm));

    // With no all-22 rigs (a director on another camera), the high shots stand off the ball.
    const FPSCameraShot Fallback = UPSCameraDirectorComponent::ComputeShot(*All22High, View, nullptr, Side, Target);
    TestTrue(TEXT("Without rigs the all-22 shot still aims at the ball from the camera side"),
        AimsAt(Fallback, Target) && UPSCameraDirectorComponent::IsOnCameraSide(Fallback, LineY, Side, 0.f));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Interest scoring and subject choice
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraDirectorInterestTest,
    "PlaySports.Camera.DirectorInterestScoring",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraDirectorInterestTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraDirectorTests;

    FPSCameraDirectorTuning Tuning;
    FPSAll22CameraTuning All22;
    if (!TestTrue(TEXT("Tunings load"), LoadTunings(Tuning, All22)))
    {
        return false;
    }
    const FPSDirectorInterestTuning& Interest = Tuning.Interest;

    FPSDirectorInterestInput Carrier;
    Carrier.bHasBall = true;
    Carrier.DistanceToBallCm = 0.f;
    Carrier.NearestOpponentCm = 100.f;
    TestTrue(TEXT("The carrier scores the ball and full closeness"),
        Near(UPSCameraDirectorComponent::ScoreInterest(Carrier, Interest), Interest.BallWeight + Interest.ProximityWeight, 0.001));

    FPSDirectorInterestInput Near1;
    Near1.DistanceToBallCm = Interest.ProximityRadiusCm * 0.25f;
    Near1.NearestOpponentCm = 100.f;
    FPSDirectorInterestInput Far1 = Near1;
    Far1.DistanceToBallCm = Interest.ProximityRadiusCm * 0.75f;
    FPSDirectorInterestInput Away = Near1;
    Away.DistanceToBallCm = Interest.ProximityRadiusCm * 2.f;
    TestTrue(TEXT("Closer to the ball is more interesting"),
        UPSCameraDirectorComponent::ScoreInterest(Near1, Interest) > UPSCameraDirectorComponent::ScoreInterest(Far1, Interest));
    TestTrue(TEXT("...and past the radius counts nothing"), Near(UPSCameraDirectorComponent::ScoreInterest(Away, Interest), 0.0, 0.001));

    FPSDirectorInterestInput Breakaway = Away;
    Breakaway.SpeedCms = Interest.BreakawaySpeedCms + 100.f;
    Breakaway.NearestOpponentCm = Interest.BreakawayClearanceCm + 100.f;
    FPSDirectorInterestInput Covered = Breakaway;
    Covered.NearestOpponentCm = Interest.BreakawayClearanceCm * 0.5f;
    FPSDirectorInterestInput Jogging = Breakaway;
    Jogging.SpeedCms = Interest.BreakawaySpeedCms * 0.5f;
    TestTrue(TEXT("A fast man in the clear is a breakaway"), Near(UPSCameraDirectorComponent::ScoreInterest(Breakaway, Interest), Interest.BreakawayWeight, 0.001));
    TestTrue(TEXT("...not with a defender on him"), Near(UPSCameraDirectorComponent::ScoreInterest(Covered, Interest), 0.0, 0.001));
    TestTrue(TEXT("...nor at a jog"), Near(UPSCameraDirectorComponent::ScoreInterest(Jogging, Interest), 0.0, 0.001));

    FPSDirectorInterestInput Hit = Away;
    Hit.SecondsSinceBigHit = 0.f;
    TestTrue(TEXT("A big hit counts in full at once"), Near(UPSCameraDirectorComponent::ScoreInterest(Hit, Interest), Interest.BigHitWeight, 0.001));
    Hit.SecondsSinceBigHit = Interest.BigHitSeconds * 0.5f;
    TestTrue(TEXT("...half way through, half"), Near(UPSCameraDirectorComponent::ScoreInterest(Hit, Interest), Interest.BigHitWeight * 0.5f, 0.001));
    Hit.SecondsSinceBigHit = Interest.BigHitSeconds;
    TestTrue(TEXT("...and nothing once it has faded"), Near(UPSCameraDirectorComponent::ScoreInterest(Hit, Interest), 0.0, 0.001));

    // Hysteresis: the subject changes only for a clearly better one.
    TestEqual(TEXT("No players, no subject"), UPSCameraDirectorComponent::PickSubject(TArray<float>(), INDEX_NONE, Interest.SwitchMargin), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("With no subject yet, the best"), UPSCameraDirectorComponent::PickSubject({ 1.f, 5.f, 3.f }, INDEX_NONE, Interest.SwitchMargin), 1);
    const float Current = 10.f;
    TestEqual(TEXT("A slightly better player doesn't take over"),
        UPSCameraDirectorComponent::PickSubject({ Current, Current + Interest.SwitchMargin * 0.5f }, 0, Interest.SwitchMargin), 0);
    TestEqual(TEXT("A clearly better one does"),
        UPSCameraDirectorComponent::PickSubject({ Current, Current + Interest.SwitchMargin * 1.5f }, 0, Interest.SwitchMargin), 1);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The cut rules, through the bus
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraDirectorCutRulesTest,
    "PlaySports.Camera.DirectorCutsOnThePlay",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraDirectorCutRulesTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraDirectorTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    const TArray<APSPlayerPawn*> Pawns = SpawnAll22(World, APSFieldGrid::ComputeLineup(All22Roles(), 0.f));
    APSBroadcastCamera* Camera = SpawnCamera(World);
    UPSCameraDirectorComponent* Director = Camera ? Camera->GetDirectorComponent() : nullptr;
    UPSCameraAll22Component* Film = Camera ? Camera->GetAll22Component() : nullptr;
    UPSTelemetrySamplingSubsystem* Sampler = World->GetSubsystem<UPSTelemetrySamplingSubsystem>();
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    if (!TestEqual(TEXT("22 players"), Pawns.Num(), 22) || !TestNotNull(TEXT("Director"), Director) || !TestNotNull(TEXT("Film component"), Film)
        || !TestNotNull(TEXT("Sampler (Epic 26)"), Sampler) || !TestNotNull(TEXT("Bus"), Bus))
    {
        DestroyTestWorld(World);
        return false;
    }
    SteadySampling(Sampler);
    Director->BindToBus();
    Film->BindToBus();
    const FPSCameraDirectorTuning Tuning = Director->GetTuning();
    TestTrue(TEXT("The director is on"), Director->IsDirectorEnabled());
    Pawns[QuarterbackIndex]->GainPossession();

    // Every step samples a frame, steps the director, and checks the 180-degree rule and the
    // minimum shot length.
    bool bSideKept = true;
    bool bCutTooSoon = false;
    auto Step = [&]()
    {
        const int32 CutsBefore = Director->GetCutCount();
        const float AgeBefore = Director->GetShotAge();
        Sampler->AdvanceTime(StepSeconds);
        Director->AdvanceTime(StepSeconds);
        if (CutsBefore > 0 && Director->GetCutCount() != CutsBefore && AgeBefore + StepSeconds < Tuning.MinShotSeconds - 0.001f)
        {
            bCutTooSoon = true;
        }
        if (!UPSCameraDirectorComponent::IsOnCameraSide(Director->GetTargetShot(), Director->GetLineOfActionY(), Tuning.CameraSide, Tuning.NeutralBandCm))
        {
            bSideKept = false;
        }
    };
    auto StepUntilShotAge = [&](float Seconds)
    {
        for (int32 Guard = 0; Guard < 100 && Director->GetShotAge() < Seconds; ++Guard)
        {
            Step();
        }
    };

    // The opening shot is the pre-snap rule's, cut to at once on the ball.
    Step();
    TestEqual(TEXT("Before the snap: the LOS wide shot"), Director->GetCurrentShot(), EPSDirectorShot::LosWide);
    TestEqual(TEXT("...one cut"), Director->GetCutCount(), 1);
    TestEqual(TEXT("The subject is the man with the ball"), Director->GetSubjectId(), PlayerIdOf(QuarterbackIndex));
    TestTrue(TEXT("A cut is instant: the camera is on the shot"), Camera->GetActorLocation().Equals(Director->GetTargetShot().Location, 1.0));

    // The snap comes early: its follow waits out the wide shot's minimum.
    Step();
    Step();
    Bus->PublishSnap(FPSTelemetrySnapEvent());
    TestEqual(TEXT("A snap inside the minimum doesn't cut yet"), Director->GetCurrentShot(), EPSDirectorShot::LosWide);
    TestEqual(TEXT("...it waits"), Director->GetPendingShot(), EPSDirectorShot::TightFollow);
    StepUntilShotAge(Tuning.MinShotSeconds);
    Step();
    TestEqual(TEXT("Once the wide shot has run its minimum, the snap's follow"), Director->GetCurrentShot(), EPSDirectorShot::TightFollow);
    TestEqual(TEXT("...two cuts in all"), Director->GetCutCount(), 2);

    // Within a shot the camera eases after its target rather than jumping.
    APSPlayerPawn* QB = Pawns[QuarterbackIndex];
    QB->SetActorLocation(QB->GetActorLocation() + FVector(800.0, 0.0, 0.0));
    const FVector Before = Camera->GetActorLocation();
    Step();
    const double Left = FVector::Dist(Camera->GetActorLocation(), Director->GetTargetShot().Location);
    TestTrue(TEXT("Within a shot the camera eases toward its target"), Left > 1.0 && Left < FVector::Dist(Before, Director->GetTargetShot().Location));

    // A throw after the minimum cuts at once; a catch and a tackle right after it both wait,
    // and the later one wins.
    StepUntilShotAge(Tuning.MinShotSeconds);
    Bus->PublishThrow(FPSTelemetryThrowEvent());
    TestEqual(TEXT("The throw cuts at once"), Director->GetCurrentShot(), RuleFor(Tuning, EPSDirectorTrigger::Throw));
    const int32 CutsAfterThrow = Director->GetCutCount();
    Bus->PublishCatch(FPSTelemetryCatchEvent());
    FPSTelemetryTackleEvent Tackle;
    Tackle.TacklerName = DisplayNameOf(LinebackerIndex);
    Tackle.BallCarrierName = DisplayNameOf(QuarterbackIndex);
    Bus->PublishTackle(Tackle);
    TestEqual(TEXT("A catch and a tackle inside the minimum wait"), Director->GetCutCount(), CutsAfterThrow);
    TestEqual(TEXT("...the latest ask wins"), Director->GetPendingShot(), RuleFor(Tuning, EPSDirectorTrigger::Tackle));
    StepUntilShotAge(Tuning.MinShotSeconds);
    Step();
    TestEqual(TEXT("The tackle's shot comes up"), Director->GetCurrentShot(), RuleFor(Tuning, EPSDirectorTrigger::Tackle));
    TestEqual(TEXT("...with one cut, not two"), Director->GetCutCount(), CutsAfterThrow + 1);

    // The whistle asks for the shot already live: no cut.
    FPSTelemetryPhaseChangeEvent Whistle;
    Whistle.NewPhase = TEXT("Scoring");
    Bus->PublishPhaseChange(Whistle);
    StepUntilShotAge(Tuning.MinShotSeconds);
    TestEqual(TEXT("Asking for the live shot doesn't cut"), Director->GetCutCount(), CutsAfterThrow + 1);
    TestEqual(TEXT("...and leaves nothing waiting"), Director->GetPendingShot(), EPSDirectorShot::None);

    // The next down lines up: wide again.
    FPSTelemetryPhaseChangeEvent NextDown;
    NextDown.NewPhase = TEXT("PreSnap");
    Bus->PublishPhaseChange(NextDown);
    Step();
    TestEqual(TEXT("The next down is wide again"), Director->GetCurrentShot(), EPSDirectorShot::LosWide);

    TestTrue(TEXT("Every shot stayed on the camera side of the line of action"), bSideKept);
    TestFalse(TEXT("No shot was cut away from before its minimum"), bCutTooSoon);

    // The film view (Epic 40) overrides the director, which picks up again afterwards.
    Film->SetFilmView(Film->GetTuning().All22Rigs[0].RigId);
    const FVector FilmLocation = Camera->GetActorLocation();
    Camera->Tick(StepSeconds);
    TestTrue(TEXT("While the film view is on, the director leaves the camera alone"), Camera->GetActorLocation().Equals(FilmLocation, 1.0));
    Film->SetFilmView(NAME_None);
    const int32 CutsBeforeResume = Director->GetCutCount();
    Camera->Tick(StepSeconds);
    TestEqual(TEXT("...and afterwards it carries on with its shot"), Director->GetCurrentShot(), EPSDirectorShot::LosWide);
    TestEqual(TEXT("...without a cut"), Director->GetCutCount(), CutsBeforeResume);

    // Switched off, the broadcast camera's plain follow takes back over.
    Director->SetDirectorEnabled(false);
    TestFalse(TEXT("The director can be switched off"), Director->IsDirectorEnabled());

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The subject follows interest through Epic 26's snapshots
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraDirectorSubjectTest,
    "PlaySports.Camera.DirectorFollowsInterest",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraDirectorSubjectTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraDirectorTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    TArray<FVector> Locations = APSFieldGrid::ComputeLineup(All22Roles(), 0.f);
    // The ball is in the air (nobody has it) and a receiver is streaking clear downfield.
    Locations[ReceiverIndex] = FVector(3000.0, 1500.0, 100.0);
    const TArray<APSPlayerPawn*> Pawns = SpawnAll22(World, Locations);
    APSBroadcastCamera* Camera = SpawnCamera(World);
    UPSCameraDirectorComponent* Director = Camera ? Camera->GetDirectorComponent() : nullptr;
    UPSTelemetrySamplingSubsystem* Sampler = World->GetSubsystem<UPSTelemetrySamplingSubsystem>();
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    if (!TestEqual(TEXT("22 players"), Pawns.Num(), 22) || !TestNotNull(TEXT("Director"), Director)
        || !TestNotNull(TEXT("Sampler (Epic 26)"), Sampler) || !TestNotNull(TEXT("Bus"), Bus))
    {
        DestroyTestWorld(World);
        return false;
    }
    SteadySampling(Sampler);
    Director->BindToBus();
    const FPSCameraDirectorTuning Tuning = Director->GetTuning();
    Pawns[ReceiverIndex]->GetFloatingMovementComponent()->Velocity = FVector(Tuning.Interest.BreakawaySpeedCms + 200.f, 0.0, 0.0);

    auto Step = [&]()
    {
        Sampler->AdvanceTime(StepSeconds);
        Director->AdvanceTime(StepSeconds);
    };

    Step();
    TestEqual(TEXT("The breakaway runner is the subject"), Director->GetSubjectId(), PlayerIdOf(ReceiverIndex));

    // A big hit back at the line: the two men in it take over.
    FPSTelemetryTackleEvent Tackle;
    Tackle.TacklerName = DisplayNameOf(LinebackerIndex);
    Tackle.BallCarrierName = DisplayNameOf(RunningBackIndex);
    Bus->PublishTackle(Tackle);
    Step();
    const FName AfterHit = Director->GetSubjectId();
    TestTrue(TEXT("A big hit takes the subject"), AfterHit == PlayerIdOf(LinebackerIndex) || AfterHit == PlayerIdOf(RunningBackIndex));

    // Once the hit has faded, the runner is the story again.
    const int32 FadeSteps = FMath::CeilToInt(Tuning.Interest.BigHitSeconds / StepSeconds) + 2;
    for (int32 Index = 0; Index < FadeSteps; ++Index)
    {
        Step();
    }
    TestEqual(TEXT("After the hit fades, back to the runner"), Director->GetSubjectId(), PlayerIdOf(ReceiverIndex));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- The 180-degree rule
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraDirectorCameraSideTest,
    "PlaySports.Camera.DirectorKeepsCameraSide",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraDirectorCameraSideTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraDirectorTests;

    FPSCameraDirectorTuning Tuning;
    FPSAll22CameraTuning All22;
    if (!TestTrue(TEXT("Tunings load"), LoadTunings(Tuning, All22)))
    {
        return false;
    }
    const FPSDirectorShotDef* Reaction = FindShot(Tuning, EPSDirectorShot::SidelineReaction);
    const FPSDirectorShotDef* All22High = FindShot(Tuning, EPSDirectorShot::All22High);
    if (!Reaction || !All22High)
    {
        AddError(TEXT("A shot of the vocabulary is missing"));
        return false;
    }
    const int32 Side = Tuning.CameraSide;
    const float Band = Tuning.NeutralBandCm;

    // A reaction on a man far across the line of action from the ball would stand on the far
    // side; the rule moves it back to ours and re-aims at him.
    FPSDirectorView View;
    View.PlayerLocations = APSFieldGrid::ComputeLineup(All22Roles(), 0.f);
    View.BallLocation = FVector(0.0, 0.0, 100.0);
    View.SubjectLocation = FVector(500.0, -Side * 2400.0, 100.0);
    View.AspectRatio = All22.AspectRatio;
    FVector Target;
    FPSCameraShot Shot = UPSCameraDirectorComponent::ComputeShot(*Reaction, View, &All22, Side, Target);
    TestFalse(TEXT("Across the line, the raw shot is on the wrong side"), UPSCameraDirectorComponent::IsOnCameraSide(Shot, View.BallLocation.Y, Side, Band));
    TestTrue(TEXT("The rule moves it"), UPSCameraDirectorComponent::EnforceCameraSide(Shot, Target, View.BallLocation.Y, Side, Band));
    TestTrue(TEXT("...to the camera side"), UPSCameraDirectorComponent::IsOnCameraSide(Shot, View.BallLocation.Y, Side, 0.f));
    TestTrue(TEXT("...still aimed at the subject"), AimsAt(Shot, Target));

    // A shot on the line (inside the neutral band) is left where it is.
    FPSCameraShot OnLine;
    OnLine.Location = FVector(-6000.0, -Side * Band * 0.5, 2500.0);
    OnLine.Rotation = (FVector(0.0, 0.0, 100.0) - OnLine.Location).Rotation();
    const FPSCameraShot Untouched = OnLine;
    TestFalse(TEXT("A shot on the line of action is allowed"), UPSCameraDirectorComponent::EnforceCameraSide(OnLine, FVector(0.0, 0.0, 100.0), 0.0, Side, Band));
    TestTrue(TEXT("...and not moved"), OnLine.Location.Equals(Untouched.Location, 0.01));

    // A broadcast shot from the other sideline: the all-22 sideline rig is mirrored over.
    const FPSCameraShot FromRig = UPSCameraDirectorComponent::ComputeShot(*All22High, View, &All22, -Side, Target);
    FPSCameraShot Mirrored = FromRig;
    TestTrue(TEXT("With the camera side flipped, the sideline rig is across the line"),
        UPSCameraDirectorComponent::EnforceCameraSide(Mirrored, Target, View.BallLocation.Y, -Side, Band));
    TestTrue(TEXT("...and moves to the other sideline"), UPSCameraDirectorComponent::IsOnCameraSide(Mirrored, View.BallLocation.Y, -Side, 0.f)
        && Near(Mirrored.Location.Y, -FromRig.Location.Y, 0.5));
    TestTrue(TEXT("...aimed at the players"), AimsAt(Mirrored, Target));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
