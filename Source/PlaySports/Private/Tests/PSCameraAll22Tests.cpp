// PSCameraAll22Tests.cpp -- Epic 40 (the all-22 coaches film camera)
//
// Headless runs can't render, so the camera is tested as math: a shot "holds" a player when his
// feet, centre and head all project inside its frame.
//
// Tests covered:
//   1. The rig data: Data/camera_all22.json loads, validates and matches the struct defaults, and
//      validation reports each kind of mistake.
//   2. The framing: both rigs hold all 22 from either end of the field and in either direction,
//      a pile zooms to the tightest angle, a spread-out field backs the rig away, and the end-zone
//      rig stands behind the offense.
//   3. The toggle: ViewToggle (a catalog action on the field, drawn by the Xbox glyph set) steps
//      broadcast -> sideline -> end zone -> broadcast on the controller looking through the
//      camera, and leaving film view restores the broadcast view.
//   4. The reframing: every step of a breakaway holds all 22, and the frame closes in gently once
//      play bunches up.
//   5. The snap: the end-zone rig takes the end behind the offense at the snap.
//   6. Frame export: a captured frame places every player, and an exported one round-trips
//      through its JSON file.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBroadcastCamera.h"
#include "PSCameraAll22Component.h"
#include "PSCameraFraming.h"
#include "PSDataIngestion.h"
#include "PSFieldGrid.h"
#include "PSInputConfig.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSCameraAll22Tests
{
    /** World X of a team's own 20-yard line when it attacks +X (goal line at -4572 cm). */
    static constexpr float OwnTwentyX = -2743.2f;

    /** World X of the opponent's 10-yard line when attacking +X. */
    static constexpr float OpponentTenX = 3657.6f;

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

    /** Eleven on eleven: the offense's line, backs and receivers, then the defense. */
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

    /** Index of the running back in All22Roles. */
    static constexpr int32 RunningBackIndex = 6;

    /** The game's own pre-snap lineup at ScrimmageX; mirrored when the offense attacks -X. */
    static TArray<FVector> Lineup(float ScrimmageX, bool bAttackPositiveX)
    {
        TArray<FVector> Locations = APSFieldGrid::ComputeLineup(All22Roles(), ScrimmageX);
        if (!bAttackPositiveX)
        {
            for (FVector& Location : Locations)
            {
                Location.X = 2.0 * ScrimmageX - Location.X;
            }
        }
        return Locations;
    }

    /** True when every player's feet, centre and head are inside Shot's frame. */
    static bool AllInShot(const FPSCameraShot& Shot, const TArray<FVector>& Locations, float PlayerHeight)
    {
        const double HalfHeight = PlayerHeight * 0.5;
        for (const FVector& Location : Locations)
        {
            for (const double Offset : { -HalfHeight, 0.0, HalfHeight })
            {
                if (!UPSCameraFraming::IsPointInShot(Shot, Location + FVector(0.0, 0.0, Offset)))
                {
                    return false;
                }
            }
        }
        return true;
    }

    static double MinX(const TArray<FVector>& Locations)
    {
        double Result = TNumericLimits<double>::Max();
        for (const FVector& Location : Locations)
        {
            Result = FMath::Min(Result, Location.X);
        }
        return Result;
    }

    static double MaxX(const TArray<FVector>& Locations)
    {
        double Result = TNumericLimits<double>::Lowest();
        for (const FVector& Location : Locations)
        {
            Result = FMath::Max(Result, Location.X);
        }
        return Result;
    }

    static bool LoadTuning(FPSAll22CameraTuning& OutTuning)
    {
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        return Ingestion->LoadAll22CameraTuningFromJson(UPSCameraAll22Component::GetDefaultTuningPath(), OutTuning);
    }

    static const FPSAll22RigDef* FindRig(const FPSAll22CameraTuning& Tuning, EPSAll22RigPlacement Placement)
    {
        return Tuning.All22Rigs.FindByPredicate([Placement](const FPSAll22RigDef& Rig) { return Rig.Placement == Placement; });
    }

    /** The 22, spawned at Locations with All22Roles (which sets each one's side). */
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
            Attributes.DisplayName = FString::Printf(TEXT("Player %d"), Index);
            Attributes.Role = Roles[Index];
            Pawn->InitializePlayer(Attributes);
            Pawn->SetActorLocation(Locations[Index]);
            Pawns.Add(Pawn);
        }
        return Pawns;
    }

    static void MoveAll(const TArray<APSPlayerPawn*>& Pawns, const TArray<FVector>& Locations)
    {
        for (int32 Index = 0; Index < Pawns.Num() && Index < Locations.Num(); ++Index)
        {
            Pawns[Index]->SetActorLocation(Locations[Index]);
        }
    }

    static TArray<FVector> LocationsOf(const TArray<APSPlayerPawn*>& Pawns)
    {
        TArray<FVector> Locations;
        for (const APSPlayerPawn* Pawn : Pawns)
        {
            Locations.Add(Pawn->GetActorLocation());
        }
        return Locations;
    }

    static APSBroadcastCamera* SpawnCamera(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<APSBroadcastCamera>(APSBroadcastCamera::StaticClass(), FVector(0.0, -2800.0, 600.0), FRotator(-10.0, 90.0, 0.0), SpawnParams);
    }

    static bool AnyProblemContains(const TArray<FString>& Problems, const TCHAR* Needle)
    {
        return Problems.ContainsByPredicate([Needle](const FString& Problem) { return Problem.Contains(Needle); });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The rig data
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraAll22TuningTest,
    "PlaySports.Camera.All22RigData",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraAll22TuningTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraAll22Tests;

    FPSAll22CameraTuning FromFile;
    if (!TestTrue(TEXT("Data/camera_all22.json loads through UPSDataIngestion"), LoadTuning(FromFile)))
    {
        return false;
    }
    for (const FString& Problem : UPSCameraFraming::ValidateTuning(FromFile))
    {
        AddError(FString::Printf(TEXT("camera_all22.json: %s"), *Problem));
    }

    const FPSAll22RigDef* Sideline = FindRig(FromFile, EPSAll22RigPlacement::Sideline);
    const FPSAll22RigDef* EndZone = FindRig(FromFile, EPSAll22RigPlacement::EndZone);
    if (!TestNotNull(TEXT("There is a sideline rig"), Sideline) || !TestNotNull(TEXT("There is an end-zone rig"), EndZone))
    {
        return false;
    }
    TestEqual(TEXT("The sideline rig comes first, so the first toggle goes to it"), FromFile.All22Rigs[0].Placement, EPSAll22RigPlacement::Sideline);
    TestTrue(TEXT("Both rigs are elevated"), Sideline->HeightCm > 1000.f && EndZone->HeightCm > 1000.f);
    TestTrue(TEXT("The sideline rig stands off the field (beyond the sideline at 2438 cm)"), Sideline->StandoffCm > 2438.4f);
    TestTrue(TEXT("The end-zone rig stands behind the end line (5486 cm)"), EndZone->StandoffCm > 5486.4f);

    // The struct defaults are the file's values (the sideline row for a rig).
    const FPSAll22CameraTuning Defaults;
    TestEqual(TEXT("FramingMarginCm default matches the file"), Defaults.FramingMarginCm, FromFile.FramingMarginCm);
    TestEqual(TEXT("PlayerHeightCm default matches the file"), Defaults.PlayerHeightCm, FromFile.PlayerHeightCm);
    TestEqual(TEXT("AspectRatio default matches the file"), Defaults.AspectRatio, FromFile.AspectRatio);
    TestEqual(TEXT("ReframeSpeed default matches the file"), Defaults.ReframeSpeed, FromFile.ReframeSpeed);
    const FPSAll22RigDef DefaultRig;
    TestEqual(TEXT("RigId default is the sideline rig's"), DefaultRig.RigId, Sideline->RigId);
    TestEqual(TEXT("HeightCm default matches"), DefaultRig.HeightCm, Sideline->HeightCm);
    TestEqual(TEXT("StandoffCm default matches"), DefaultRig.StandoffCm, Sideline->StandoffCm);
    TestTrue(TEXT("bTrackPlay default matches"), DefaultRig.bTrackPlay == Sideline->bTrackPlay);
    TestEqual(TEXT("RailHalfLengthCm default matches"), DefaultRig.RailHalfLengthCm, Sideline->RailHalfLengthCm);
    TestEqual(TEXT("MinFieldOfView default matches"), DefaultRig.MinFieldOfView, Sideline->MinFieldOfView);
    TestEqual(TEXT("MaxFieldOfView default matches"), DefaultRig.MaxFieldOfView, Sideline->MaxFieldOfView);

    // The component reads the same file.
    UPSCameraAll22Component* Component = NewObject<UPSCameraAll22Component>();
    TestEqual(TEXT("The component loads every rig"), Component->GetTuning().All22Rigs.Num(), FromFile.All22Rigs.Num());

    // Validation reports each kind of mistake.
    FPSAll22CameraTuning Broken = FromFile;
    Broken.All22Rigs[0].MinFieldOfView = 100.f;
    Broken.All22Rigs[1].RigId = Broken.All22Rigs[0].RigId;
    Broken.All22Rigs[1].HeightCm = 0.f;
    Broken.AspectRatio = 0.f;
    Broken.FramingMarginCm = -1.f;
    const TArray<FString> Problems = UPSCameraFraming::ValidateTuning(Broken);
    TestTrue(TEXT("An upside-down zoom range is reported"), AnyProblemContains(Problems, TEXT("zoom range")));
    TestTrue(TEXT("A repeated RigId is reported"), AnyProblemContains(Problems, TEXT("used twice")));
    TestTrue(TEXT("A rig on the ground is reported"), AnyProblemContains(Problems, TEXT("HeightCm")));
    TestTrue(TEXT("A zero aspect ratio is reported"), AnyProblemContains(Problems, TEXT("AspectRatio")));
    TestTrue(TEXT("A negative margin is reported"), AnyProblemContains(Problems, TEXT("FramingMarginCm")));
    TestTrue(TEXT("No rigs at all is reported"), AnyProblemContains(UPSCameraFraming::ValidateTuning(FPSAll22CameraTuning()), TEXT("empty")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The framing math
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraAll22FramingTest,
    "PlaySports.Camera.All22FramingHoldsAll22",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraAll22FramingTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraAll22Tests;

    FPSAll22CameraTuning Tuning;
    if (!TestTrue(TEXT("Tuning loads"), LoadTuning(Tuning)))
    {
        return false;
    }
    const FPSAll22RigDef* Sideline = FindRig(Tuning, EPSAll22RigPlacement::Sideline);
    const FPSAll22RigDef* EndZone = FindRig(Tuning, EPSAll22RigPlacement::EndZone);
    if (!TestNotNull(TEXT("Sideline rig"), Sideline) || !TestNotNull(TEXT("End-zone rig"), EndZone))
    {
        return false;
    }

    struct FSituation
    {
        const TCHAR* Name;
        float ScrimmageX;
        bool bAttackPositiveX;
    };
    const FSituation Situations[] = {
        { TEXT("own 20, attacking +X"), OwnTwentyX, true },
        { TEXT("midfield, attacking +X"), 0.f, true },
        { TEXT("opponent's 10, attacking +X"), OpponentTenX, true },
        { TEXT("own 20, attacking -X"), -OwnTwentyX, false },
        { TEXT("opponent's 10, attacking -X"), -OpponentTenX, false }
    };

    for (const FSituation& Situation : Situations)
    {
        const TArray<FVector> Locations = Lineup(Situation.ScrimmageX, Situation.bAttackPositiveX);
        const float Direction = Situation.bAttackPositiveX ? 1.f : -1.f;

        const FPSCameraShot FromSideline = UPSCameraFraming::FrameAll22(*Sideline, Tuning, Locations, Direction, Tuning.AspectRatio);
        TestTrue(*FString::Printf(TEXT("%s: the sideline rig holds all 22"), Situation.Name), AllInShot(FromSideline, Locations, Tuning.PlayerHeightCm));
        TestTrue(*FString::Printf(TEXT("%s: the sideline rig stays at its height"), Situation.Name), Near(FromSideline.Location.Z, Sideline->HeightCm, 0.5));
        TestTrue(*FString::Printf(TEXT("%s: ...on the -Y sideline"), Situation.Name), Near(FromSideline.Location.Y, -Sideline->StandoffCm, 0.5));
        TestTrue(*FString::Printf(TEXT("%s: ...zoomed within its range"), Situation.Name),
            FromSideline.FieldOfView >= Sideline->MinFieldOfView - 0.01f && FromSideline.FieldOfView <= Sideline->MaxFieldOfView + 0.01f);

        const FPSCameraShot FromEndZone = UPSCameraFraming::FrameAll22(*EndZone, Tuning, Locations, Direction, Tuning.AspectRatio);
        TestTrue(*FString::Printf(TEXT("%s: the end-zone rig holds all 22"), Situation.Name), AllInShot(FromEndZone, Locations, Tuning.PlayerHeightCm));
        TestTrue(*FString::Printf(TEXT("%s: the end-zone rig stays at its height"), Situation.Name), Near(FromEndZone.Location.Z, EndZone->HeightCm, 0.5));
        TestTrue(*FString::Printf(TEXT("%s: ...behind the offense"), Situation.Name),
            Situation.bAttackPositiveX ? FromEndZone.Location.X < MinX(Locations) : FromEndZone.Location.X > MaxX(Locations));
        TestTrue(*FString::Printf(TEXT("%s: ...behind the end line"), Situation.Name), Near(FMath::Abs(FromEndZone.Location.X), EndZone->StandoffCm, 0.5));
    }

    // The aim is the frame's centre, and points behind or far beside the camera are out.
    const TArray<FVector> Midfield = Lineup(0.f, true);
    const FPSCameraShot Shot = UPSCameraFraming::FrameAll22(*Sideline, Tuning, Midfield, 1.f, Tuning.AspectRatio);
    FVector2D Screen;
    TestTrue(TEXT("The players' centre is in frame"), UPSCameraFraming::ProjectToShot(Shot, UPSCameraFraming::ComputePlayerBox(Midfield, Tuning).GetCenter(), Screen));
    TestTrue(TEXT("...at the centre of the frame"), Screen.Equals(FVector2D(0.5, 0.5), 0.001));
    TestFalse(TEXT("A point behind the camera is out"), UPSCameraFraming::IsPointInShot(Shot, Shot.Location - Shot.Rotation.Vector() * 100.0));
    TestFalse(TEXT("A point far down the field is out"), UPSCameraFraming::IsPointInShot(Shot, FVector(50000.0, 0.0, 100.0)));

    // A goal-line pile: the rig zooms in only as far as its tightest angle.
    TArray<FVector> Pile;
    for (int32 Index = 0; Index < 22; ++Index)
    {
        Pile.Add(FVector(4300.0 + (Index % 4) * 60.0, -150.0 + (Index / 4) * 60.0, 100.0));
    }
    const FPSCameraShot PileShot = UPSCameraFraming::FrameAll22(*Sideline, Tuning, Pile, 1.f, Tuning.AspectRatio);
    TestTrue(TEXT("A pile is held"), AllInShot(PileShot, Pile, Tuning.PlayerHeightCm));
    TestTrue(TEXT("...at the tightest zoom"), Near(PileShot.FieldOfView, Sideline->MinFieldOfView, 0.01));

    // A kickoff spread over most of the field: wider than the widest zoom, so the rig backs
    // away along its line of sight, up and away from the field.
    TArray<FVector> Spread;
    for (int32 Index = 0; Index < 11; ++Index)
    {
        Spread.Add(FVector(-3000.0, -2200.0 + 440.0 * Index, 100.0));
        Spread.Add(FVector(500.0 * Index, ((Index % 2) ? 1.0 : -1.0) * 200.0 * Index, 100.0));
    }
    const FPSCameraShot SpreadShot = UPSCameraFraming::FrameAll22(*Sideline, Tuning, Spread, 1.f, Tuning.AspectRatio);
    TestTrue(TEXT("A spread-out field is held"), AllInShot(SpreadShot, Spread, Tuning.PlayerHeightCm));
    TestTrue(TEXT("...at the widest zoom"), Near(SpreadShot.FieldOfView, Sideline->MaxFieldOfView, 0.01));
    TestTrue(TEXT("...from further back than the rig's spot"), SpreadShot.Location.Y < -Sideline->StandoffCm && SpreadShot.Location.Z > Sideline->HeightCm);
    const FPSCameraShot SpreadFromEndZone = UPSCameraFraming::FrameAll22(*EndZone, Tuning, Spread, 1.f, Tuning.AspectRatio);
    TestTrue(TEXT("The end-zone rig holds the spread-out field too"), AllInShot(SpreadFromEndZone, Spread, Tuning.PlayerHeightCm));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The toggle, through the catalog action on the viewing controller
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraAll22ToggleTest,
    "PlaySports.Camera.All22ToggleThroughCatalog",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraAll22ToggleTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraAll22Tests;

    // The action: a Boolean on the field with a key and a pad button, drawn by the Xbox set.
    UPSInputConfig* Input = NewObject<UPSInputConfig>();
    Input->LoadDefaults();
    UPSCameraAll22Component* Probe = NewObject<UPSCameraAll22Component>();
    const FName ToggleId = Probe->ToggleActionId;
    const FName OnField(TEXT("OnField"));
    const FPSInputActionDef* Toggle = Input->Catalog.Actions.FindByPredicate(
        [ToggleId](const FPSInputActionDef& Candidate) { return Candidate.ActionId == ToggleId; });
    TestTrue(TEXT("The toggle is a Boolean catalog action in the OnField context"),
        Toggle && Toggle->ValueType == EInputActionValueType::Boolean && Toggle->Contexts.Contains(OnField));
    FPSInputGlyph Glyph;
    TestTrue(TEXT("The Xbox glyph set draws its pad button"), Input->GetGlyphForAction(ToggleId, OnField, EPSInputDevice::Gamepad, Glyph));
    TestEqual(TEXT("...Y, the camera button in every context"), Glyph.Label, FString(TEXT("Y")));
    TestTrue(TEXT("The keyboard set draws its key"), Input->GetGlyphForAction(ToggleId, OnField, EPSInputDevice::KeyboardMouse, Glyph));

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    const TArray<APSPlayerPawn*> Pawns = SpawnAll22(World, Lineup(0.f, true));
    APSBroadcastCamera* Camera = SpawnCamera(World);
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    UPSCameraAll22Component* Film = Camera ? Camera->GetAll22Component() : nullptr;
    UCameraComponent* Lens = Camera ? Camera->GetCameraComponent() : nullptr;
    if (!TestEqual(TEXT("22 players"), Pawns.Num(), 22) || !TestNotNull(TEXT("Camera"), Camera) || !TestNotNull(TEXT("Film component"), Film)
        || !TestNotNull(TEXT("Camera lens"), Lens) || !TestNotNull(TEXT("Controller"), Controller))
    {
        DestroyTestWorld(World);
        return false;
    }
    const FPSAll22CameraTuning Tuning = Film->GetTuning();
    const FPSAll22RigDef* Sideline = FindRig(Tuning, EPSAll22RigPlacement::Sideline);
    const FPSAll22RigDef* EndZone = FindRig(Tuning, EPSAll22RigPlacement::EndZone);
    if (!TestNotNull(TEXT("Sideline rig"), Sideline) || !TestNotNull(TEXT("End-zone rig"), EndZone))
    {
        DestroyTestWorld(World);
        return false;
    }
    const TArray<FVector> Locations = LocationsOf(Pawns);
    const FTransform BroadcastTransform = Camera->GetActorTransform();
    const float BroadcastFov = Lens->FieldOfView;

    // A controller that isn't looking through the camera can't toggle it.
    Controller->OnCatalogActionStarted.Broadcast(ToggleId);
    TestFalse(TEXT("No toggle before the controller views through the camera"), Film->IsFilmViewActive());

    // Viewing through it (SetViewTarget calls BecomeViewTarget).
    Camera->BecomeViewTarget(Controller);
    Controller->OnCatalogActionStarted.Broadcast(FName(TEXT("Confirm")));
    TestFalse(TEXT("Other actions leave the view alone"), Film->IsFilmViewActive());

    Controller->OnCatalogActionStarted.Broadcast(ToggleId);
    TestEqual(TEXT("The first press cuts to the sideline rig"), Film->GetActiveRigId(), Sideline->RigId);
    FPSCameraShot Shot = Film->GetCurrentShot();
    TestTrue(TEXT("...which holds all 22"), AllInShot(Shot, Locations, Tuning.PlayerHeightCm));
    TestTrue(TEXT("...from its height"), Near(Shot.Location.Z, Sideline->HeightCm, 0.5));
    TestTrue(TEXT("...on the -Y sideline"), Near(Shot.Location.Y, -Sideline->StandoffCm, 0.5));
    TestTrue(TEXT("Film view turns motion blur off"),
        Lens->PostProcessSettings.bOverride_MotionBlurAmount && Lens->PostProcessSettings.MotionBlurAmount == 0.f);

    Controller->OnCatalogActionStarted.Broadcast(ToggleId);
    TestEqual(TEXT("The second press cuts to the end-zone rig"), Film->GetActiveRigId(), EndZone->RigId);
    Shot = Film->GetCurrentShot();
    TestTrue(TEXT("...which holds all 22"), AllInShot(Shot, Locations, Tuning.PlayerHeightCm));
    TestTrue(TEXT("...from behind the offense"), Shot.Location.X < MinX(Locations));
    TestTrue(TEXT("...at its height"), Near(Shot.Location.Z, EndZone->HeightCm, 0.5));

    Controller->OnCatalogActionStarted.Broadcast(ToggleId);
    TestFalse(TEXT("The third press goes back to the broadcast view"), Film->IsFilmViewActive());
    TestEqual(TEXT("...which is no rig"), Film->GetActiveRigId(), FName(NAME_None));
    TestTrue(TEXT("...where the broadcast camera was"), Camera->GetActorTransform().Equals(BroadcastTransform, 0.01));
    TestTrue(TEXT("...at its zoom"), Near(Lens->FieldOfView, BroadcastFov, 0.01));
    TestFalse(TEXT("...with its own motion blur setting"), Lens->PostProcessSettings.bOverride_MotionBlurAmount != 0);

    // The broadcast follow is suspended while film is on: a tick doesn't drag the camera back.
    Film->SetFilmView(Sideline->RigId);
    const FVector FilmLocation = Camera->GetActorLocation();
    Camera->SetTargetActor(Pawns[RunningBackIndex]);
    Camera->Tick(0.1f);
    TestTrue(TEXT("Ticking in film view keeps the rig's shot"), Camera->GetActorLocation().Equals(FilmLocation, 1.0));
    TestTrue(TEXT("...on its rail, not the broadcast camera's"), Near(Camera->GetActorLocation().Y, -Sideline->StandoffCm, 0.5));

    // By name.
    TestFalse(TEXT("An unknown rig is refused"), Film->SetFilmView(FName(TEXT("Blimp"))));
    TestTrue(TEXT("A rig by name"), Film->SetFilmView(EndZone->RigId) && Film->GetActiveRigId() == EndZone->RigId);
    TestTrue(TEXT("None leaves film view"), Film->SetFilmView(NAME_None) && !Film->IsFilmViewActive());

    // Once the controller looks elsewhere, its presses no longer reach the camera.
    Camera->EndViewTarget(Controller);
    Controller->OnCatalogActionStarted.Broadcast(ToggleId);
    TestFalse(TEXT("No toggle after the controller stops viewing through the camera"), Film->IsFilmViewActive());

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Reframing every step
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraAll22ReframeTest,
    "PlaySports.Camera.All22ReframesEveryStep",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraAll22ReframeTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraAll22Tests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    const TArray<APSPlayerPawn*> Pawns = SpawnAll22(World, Lineup(0.f, true));
    APSBroadcastCamera* Camera = SpawnCamera(World);
    UPSCameraAll22Component* Film = Camera ? Camera->GetAll22Component() : nullptr;
    if (!TestEqual(TEXT("22 players"), Pawns.Num(), 22) || !TestNotNull(TEXT("Film component"), Film))
    {
        DestroyTestWorld(World);
        return false;
    }
    const FPSAll22CameraTuning Tuning = Film->GetTuning();
    const FPSAll22RigDef* Sideline = FindRig(Tuning, EPSAll22RigPlacement::Sideline);
    if (!TestNotNull(TEXT("Sideline rig"), Sideline) || !TestTrue(TEXT("Cut to the sideline rig"), Film->SetFilmView(Sideline->RigId)))
    {
        DestroyTestWorld(World);
        return false;
    }

    // A breakaway: the back runs 45 yards in five seconds and everyone else chases at half his
    // pace. Every step holds all 22.
    const float StepSeconds = 0.1f;
    float WidestFov = Film->GetCurrentShot().FieldOfView;
    bool bHeldEveryStep = true;
    for (int32 Step = 0; Step < 50 && bHeldEveryStep; ++Step)
    {
        for (int32 Index = 0; Index < Pawns.Num(); ++Index)
        {
            const double Stride = (Index == RunningBackIndex) ? 90.0 : 45.0;
            Pawns[Index]->SetActorLocation(Pawns[Index]->GetActorLocation() + FVector(Stride, 0.0, 0.0));
        }
        Film->StepFraming(StepSeconds);
        const FPSCameraShot Shot = Film->GetCurrentShot();
        WidestFov = FMath::Max(WidestFov, Shot.FieldOfView);
        if (!AllInShot(Shot, LocationsOf(Pawns), Tuning.PlayerHeightCm))
        {
            AddError(FString::Printf(TEXT("Breakaway step %d: a player left the frame"), Step));
            bHeldEveryStep = false;
        }
    }
    TestTrue(TEXT("The breakaway is held every step"), bHeldEveryStep);

    // Everyone piles onto the back: the frame closes in over several steps, not in one cut,
    // holding everyone throughout, and settles on the pile's own framing.
    const FVector Tackle = Pawns[RunningBackIndex]->GetActorLocation();
    TArray<FVector> Pile;
    for (int32 Index = 0; Index < Pawns.Num(); ++Index)
    {
        Pile.Add(Tackle + FVector((Index % 4) * 60.0 - 90.0, (Index / 4) * 60.0 - 150.0, 0.0));
    }
    MoveAll(Pawns, Pile);
    const FPSCameraShot Settled = UPSCameraFraming::FrameAll22(*Sideline, Tuning, Pile, Film->GetAttackDirection(), Tuning.AspectRatio);

    Film->StepFraming(StepSeconds);
    const FPSCameraShot FirstStep = Film->GetCurrentShot();
    TestTrue(TEXT("The pile is held at once"), AllInShot(FirstStep, Pile, Tuning.PlayerHeightCm));
    TestFalse(TEXT("...but the frame doesn't snap to it"), FirstStep.Location.Equals(Settled.Location, 10.0));
    bool bHeldWhileClosing = true;
    for (int32 Step = 0; Step < 100; ++Step)
    {
        Film->StepFraming(StepSeconds);
        if (!AllInShot(Film->GetCurrentShot(), Pile, Tuning.PlayerHeightCm))
        {
            bHeldWhileClosing = false;
        }
    }
    const FPSCameraShot Final = Film->GetCurrentShot();
    TestTrue(TEXT("The pile is held while the frame closes in"), bHeldWhileClosing);
    TestTrue(TEXT("The frame settles on the pile's framing"), Final.Location.Equals(Settled.Location, 1.0) && Near(Final.FieldOfView, Settled.FieldOfView, 0.1));
    TestTrue(TEXT("...tighter than during the breakaway"), Final.FieldOfView < WidestFov);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The snap sets which end the end-zone rig takes
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraAll22SnapTest,
    "PlaySports.Camera.All22EndZoneFollowsTheSnap",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraAll22SnapTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraAll22Tests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    const TArray<APSPlayerPawn*> Pawns = SpawnAll22(World, Lineup(0.f, true));
    APSBroadcastCamera* Camera = SpawnCamera(World);
    UPSCameraAll22Component* Film = Camera ? Camera->GetAll22Component() : nullptr;
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    if (!TestEqual(TEXT("22 players"), Pawns.Num(), 22) || !TestNotNull(TEXT("Film component"), Film) || !TestNotNull(TEXT("Bus"), Bus))
    {
        DestroyTestWorld(World);
        return false;
    }
    Film->BindToBus();
    const FPSAll22CameraTuning Tuning = Film->GetTuning();
    const FPSAll22RigDef* EndZone = FindRig(Tuning, EPSAll22RigPlacement::EndZone);
    if (!TestNotNull(TEXT("End-zone rig"), EndZone) || !TestTrue(TEXT("Cut to the end-zone rig"), Film->SetFilmView(EndZone->RigId)))
    {
        DestroyTestWorld(World);
        return false;
    }
    TestEqual(TEXT("The offense attacks +X"), Film->GetAttackDirection(), 1.f);
    TestTrue(TEXT("...so the rig stands at the -X end"), Film->GetCurrentShot().Location.X < 0.0);

    // Possession changes and the next play lines up going the other way. Until the snap the
    // rig holds its end (no swapping ends mid-play).
    const TArray<FVector> Reversed = Lineup(1000.f, false);
    MoveAll(Pawns, Reversed);
    Film->StepFraming(0.1f);
    TestTrue(TEXT("Before the snap the rig holds its end"), Film->GetCurrentShot().Location.X < 0.0);

    Bus->PublishSnap(FPSTelemetrySnapEvent());
    TestEqual(TEXT("At the snap the formation says the offense attacks -X"), Film->GetAttackDirection(), -1.f);
    const FPSCameraShot Shot = Film->GetCurrentShot();
    TestTrue(TEXT("...and the rig cuts to the +X end, behind the offense"), Shot.Location.X > MaxX(Reversed));
    TestTrue(TEXT("...holding all 22"), AllInShot(Shot, Reversed, Tuning.PlayerHeightCm));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- Frame export, the telestrator's hook
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCameraAll22FrameExportTest,
    "PlaySports.Camera.All22FrameExport",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCameraAll22FrameExportTest::RunTest(const FString& Parameters)
{
    using namespace PSCameraAll22Tests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    const TArray<APSPlayerPawn*> Pawns = SpawnAll22(World, Lineup(OwnTwentyX, true));
    APSBroadcastCamera* Camera = SpawnCamera(World);
    UPSCameraAll22Component* Film = Camera ? Camera->GetAll22Component() : nullptr;
    if (!TestEqual(TEXT("22 players"), Pawns.Num(), 22) || !TestNotNull(TEXT("Film component"), Film))
    {
        DestroyTestWorld(World);
        return false;
    }

    // In broadcast view a frame is still the live view, from no rig.
    const FPSFilmFrame Broadcast = Film->CaptureFrame();
    TestEqual(TEXT("A broadcast frame names no rig"), Broadcast.RigId, FName(NAME_None));
    TestTrue(TEXT("...and is the camera's view"), Broadcast.Shot.Location.Equals(Camera->GetActorLocation(), 0.1));

    const FName SidelineId = Film->GetTuning().All22Rigs[0].RigId;
    Film->SetFilmView(SidelineId);
    const FPSFilmFrame Frame = Film->CaptureFrame();
    TestEqual(TEXT("Frames count up"), Frame.FrameIndex, Broadcast.FrameIndex + 1);
    TestEqual(TEXT("A film frame names its rig"), Frame.RigId, SidelineId);
    TestTrue(TEXT("...and is the camera's view"), Frame.Shot.Location.Equals(Camera->GetActorLocation(), 0.1));
    TestEqual(TEXT("Every player is in the frame record"), Frame.Players.Num(), 22);
    for (const FPSFilmFramePlayer& Player : Frame.Players)
    {
        const bool bOnScreen = Player.ScreenPosition.X >= 0.0 && Player.ScreenPosition.X <= 1.0 && Player.ScreenPosition.Y >= 0.0 && Player.ScreenPosition.Y <= 1.0;
        if (!Player.bInFrame || !bOnScreen)
        {
            AddError(FString::Printf(TEXT("%s is not placed in the film frame (%.3f, %.3f)"), *Player.PlayerId.ToString(), Player.ScreenPosition.X, Player.ScreenPosition.Y));
        }
    }

    // Export writes the frame as JSON (no viewport headless, so no still is requested).
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/PSCameraAll22FrameExport");
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    FPSFilmFrame Exported;
    TestTrue(TEXT("ExportFrame writes the frame"), Film->ExportFrame(Directory, Exported));
    TestEqual(TEXT("...as the next frame"), Exported.FrameIndex, Frame.FrameIndex + 1);
    TestTrue(TEXT("...with no still requested headless"), Exported.ImageFile.IsEmpty());

    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(Directory / TEXT("*.json")), true, false);
    if (TestEqual(TEXT("One JSON file is written"), Files.Num(), 1))
    {
        FString Json;
        FPSFilmFrame Loaded;
        TestTrue(TEXT("The file reads back"), FFileHelper::LoadFileToString(Json, *(Directory / Files[0]))
            && FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Loaded, 0, 0));
        TestEqual(TEXT("FrameIndex round-trips"), Loaded.FrameIndex, Exported.FrameIndex);
        TestEqual(TEXT("RigId round-trips"), Loaded.RigId, Exported.RigId);
        TestTrue(TEXT("The shot round-trips"), Loaded.Shot.Location.Equals(Exported.Shot.Location, 0.1)
            && Near(Loaded.Shot.FieldOfView, Exported.Shot.FieldOfView, 0.001));
        if (TestEqual(TEXT("Every player round-trips"), Loaded.Players.Num(), Exported.Players.Num()) && Loaded.Players.Num() > 0)
        {
            TestEqual(TEXT("A player's ID round-trips"), Loaded.Players[0].PlayerId, Exported.Players[0].PlayerId);
            TestEqual(TEXT("...and side"), Loaded.Players[0].TeamSide, Exported.Players[0].TeamSide);
            TestTrue(TEXT("...and place in the frame"), Loaded.Players[0].ScreenPosition.Equals(Exported.Players[0].ScreenPosition, 0.001));
        }
    }
    IFileManager::Get().DeleteDirectory(*Directory, false, true);

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
