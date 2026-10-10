// PSTelestratorTests.cpp -- Epic 44 (telestrator and analysis mode)
//
// A broadcast camera over three players in a headless world. The telestrator draws on its film
// view (Epic 40) or on a paused replay (Epic 41), highlights players through Epic 36's emphasis,
// saves annotated stills, and takes coaching notes through its bridge-gated hook.
//
// Tests covered:
//   1. Data/telestrator.json loads and validates; bad tunings are refused; a field point and its
//      place on the screen convert both ways, and a point in the sky has no place on the field.
//   2. Drawing on the film view: freehand (close points dropped), arrows, circles (radius on the
//      field), a player tap (lit up through emphasis, a tap on nobody drawing nothing), undo, the
//      mark limit, and nothing to draw on without film view or a replay.
//   3. A paused replay: analysis holds a playing replay and lets it play on after; a still is
//      saved and read back with its marks.
//   4. The auto-annotation hook: refused with no bridge; with one, the request carries the frame,
//      and the answer's notes and field-space marks land on the screen; a listener can answer
//      at once; an answer for an old frame is refused.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "PSBroadcastCamera.h"
#include "PSCameraAll22Component.h"
#include "PSCameraFraming.h"
#include "PSDataIngestion.h"
#include "PSOverlayEmphasisSubsystem.h"
#include "PSPlayerPawn.h"
#include "PSReplaySubsystem.h"
#include "PSTelestratorSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSTelestratorTests
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

    APSPlayerPawn* SpawnPlayer(UWorld* World, const TCHAR* PlayerId, EPlayerRole Role, EPSTeamSide Side, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (Pawn)
        {
            FPlayerAttributes Attributes;
            Attributes.PlayerId = PlayerId;
            Attributes.DisplayName = PlayerId;
            Attributes.Role = Role;
            Pawn->InitializePlayer(Attributes);
            Pawn->TeamSide = Side;
            Pawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
        }
        return Pawn;
    }

    /** Three players and the broadcast camera, in a world with the telestrator. */
    struct FFilmFixture
    {
        UWorld* World = nullptr;
        UPSTelestratorSubsystem* Telestrator = nullptr;
        UPSOverlayEmphasisSubsystem* Emphasis = nullptr;
        UPSReplaySubsystem* Replay = nullptr;
        APSBroadcastCamera* Camera = nullptr;
        APSPlayerPawn* Quarterback = nullptr;
        APSPlayerPawn* Receiver = nullptr;
        APSPlayerPawn* Corner = nullptr;

        bool Setup()
        {
            World = CreateTestWorld();
            if (!World)
            {
                return false;
            }
            Telestrator = World->GetSubsystem<UPSTelestratorSubsystem>();
            Emphasis = World->GetSubsystem<UPSOverlayEmphasisSubsystem>();
            Replay = World->GetSubsystem<UPSReplaySubsystem>();
            Quarterback = SpawnPlayer(World, TEXT("QB_01"), EPlayerRole::Quarterback, EPSTeamSide::Offense, FVector(0.0, 0.0, 90.0));
            Receiver = SpawnPlayer(World, TEXT("WR_01"), EPlayerRole::WideReceiver, EPSTeamSide::Offense, FVector(1500.0, 900.0, 90.0));
            Corner = SpawnPlayer(World, TEXT("DB_01"), EPlayerRole::DefensiveBack, EPSTeamSide::Defense, FVector(-800.0, -700.0, 90.0));
            FActorSpawnParameters SpawnParams;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Camera = World->SpawnActor<APSBroadcastCamera>(APSBroadcastCamera::StaticClass(), FVector(0.0, -2800.0, 600.0), FRotator(-10.0, 90.0, 0.0), SpawnParams);
            return Telestrator && Emphasis && Replay && Camera && Quarterback && Receiver && Corner;
        }

        void Teardown()
        {
            if (Telestrator)
            {
                Telestrator->EndAnalysis();
            }
            if (Replay)
            {
                Replay->StopReplay();
            }
            if (World)
            {
                DestroyTestWorld(World);
            }
        }

        /** The film view from the sideline rig, and analysis on it. */
        bool BeginFilmAnalysis()
        {
            return Camera->GetAll22Component()->SetFilmView(TEXT("Sideline")) && Telestrator->BeginAnalysis();
        }

        FVector2D ScreenOf(FName PlayerId) const
        {
            const FPSTelestration Telestration = Telestrator->GetTelestration();
            const FPSFilmFramePlayer* Player = Telestration.Frame.Players.FindByPredicate([PlayerId](const FPSFilmFramePlayer& Candidate) { return Candidate.PlayerId == PlayerId; });
            return Player ? Player->ScreenPosition : FVector2D(-1.0, -1.0);
        }
    };
}

// ---------------------------------------------------------------------------
// 1. Tuning and projection
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSTelestratorTuningTest,
    "PlaySports.Telestrator.TuningAndProjection",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelestratorTuningTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSTelestratorTuning Loaded;
    if (!TestTrue(TEXT("Data/telestrator.json loads"), Ingestion->LoadTelestratorTuningFromJson(UPSTelestratorSubsystem::GetDefaultTuningPath(), Loaded)))
    {
        return false;
    }
    TestEqual(TEXT("The shipped tuning is sound"), UPSTelestratorSubsystem::ValidateTuning(Loaded).Num(), 0);
    const FPSTelestratorTuning Defaults;
    TestEqual(TEXT("Defaults equal the file: the pick radius"), Loaded.PlayerPickRadius, Defaults.PlayerPickRadius);
    TestEqual(TEXT("... the mark limit"), Loaded.MaxMarks, Defaults.MaxMarks);

    FPSTelestratorTuning Bad;
    Bad.MinPointSpacing = -0.1f;
    Bad.PlayerPickRadius = 0.f;
    Bad.MaxStrokePoints = 1;
    Bad.MaxMarks = 0;
    TestEqual(TEXT("Each problem is named"), UPSTelestratorSubsystem::ValidateTuning(Bad).Num(), 4);

    // A camera high on the sideline, looking down at midfield.
    FPSCameraShot Shot;
    Shot.Location = FVector(0.0, -5000.0, 2700.0);
    Shot.Rotation = (FVector::ZeroVector - Shot.Location).Rotation();
    Shot.FieldOfView = 60.f;
    Shot.AspectRatio = 1.7778f;
    for (const FVector& OnField : { FVector(0.0, 0.0, 0.0), FVector(1500.0, 800.0, 0.0), FVector(-2000.0, -1200.0, 0.0) })
    {
        FVector2D Screen;
        TestTrue(*FString::Printf(TEXT("%s is in the shot"), *OnField.ToString()), UPSCameraFraming::ProjectToShot(Shot, OnField, Screen));
        FVector Back;
        TestTrue(TEXT("... and its place on the screen comes back down to the field"), UPSCameraFraming::DeprojectToField(Shot, Screen, 0.f, Back));
        TestTrue(*FString::Printf(TEXT("... at the same point (%s)"), *Back.ToString()), Back.Equals(OnField, 1.0));
    }
    FVector Raised;
    FVector2D RaisedScreen;
    UPSCameraFraming::ProjectToShot(Shot, FVector(300.0, 200.0, 150.0), RaisedScreen);
    TestTrue(TEXT("A point on a raised field plane comes back to it"), UPSCameraFraming::DeprojectToField(Shot, RaisedScreen, 150.f, Raised)
        && Raised.Equals(FVector(300.0, 200.0, 150.0), 1.0));

    FPSCameraShot Level = Shot;
    Level.Rotation = FRotator(0.0, 90.0, 0.0);
    FVector Sky;
    TestFalse(TEXT("Above the horizon there is no field"), UPSCameraFraming::DeprojectToField(Level, FVector2D(0.5, 0.2), 0.f, Sky));
    TestTrue(TEXT("Below it there is"), UPSCameraFraming::DeprojectToField(Level, FVector2D(0.5, 0.8), 0.f, Sky));
    return true;
}

// ---------------------------------------------------------------------------
// 2. Drawing on the film view
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSTelestratorDrawTest,
    "PlaySports.Telestrator.DrawOnTheFilmView",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelestratorDrawTest::RunTest(const FString& Parameters)
{
    using namespace PSTelestratorTests;

    FFilmFixture Fixture;
    if (!TestTrue(TEXT("The world is set up"), Fixture.Setup()))
    {
        Fixture.Teardown();
        return false;
    }
    UPSTelestratorSubsystem* Telestrator = Fixture.Telestrator;
    TestFalse(TEXT("Nothing to draw on without the film view or a replay"), Telestrator->BeginAnalysis());
    Telestrator->BeginStroke(FVector2D(0.5, 0.5));
    TestEqual(TEXT("... and no stroke outside analysis"), Telestrator->EndStroke(FVector2D(0.6, 0.6)), static_cast<int32>(INDEX_NONE));

    if (!TestTrue(TEXT("Analysis on the film view"), Fixture.BeginFilmAnalysis()))
    {
        Fixture.Teardown();
        return false;
    }
    const FPSFilmFrame Frame = Telestrator->GetTelestration().Frame;
    TestEqual(TEXT("The frame has every player"), Frame.Players.Num(), 3);
    TestEqual(TEXT("... from the sideline rig"), Frame.RigId, FName(TEXT("Sideline")));
    const float FieldHeight = Telestrator->GetTuning().FieldHeightCm;

    // Freehand: a point too close to the last is dropped; every point is pinned to the field.
    Telestrator->SetTool(EPSTelestratorTool::Freehand);
    Telestrator->BeginStroke(FVector2D(0.40, 0.60));
    Telestrator->ExtendStroke(FVector2D(0.401, 0.60));
    Telestrator->ExtendStroke(FVector2D(0.42, 0.60));
    Telestrator->ExtendStroke(FVector2D(0.45, 0.62));
    const int32 Line = Telestrator->EndStroke(FVector2D(0.50, 0.65));
    TestEqual(TEXT("A freehand line is the first mark"), Line, 0);
    FPSTelestration Telestration = Telestrator->GetTelestration();
    if (Telestration.Marks.IsValidIndex(Line))
    {
        const FPSTelestratorMark& Mark = Telestration.Marks[Line];
        TestEqual(TEXT("... with its points, the close one dropped"), Mark.ScreenPoints.Num(), 4);
        TestEqual(TEXT("... each pinned to the field"), Mark.FieldPoints.Num(), 4);
        for (int32 Index = 0; Index < FMath::Min(Mark.ScreenPoints.Num(), Mark.FieldPoints.Num()); ++Index)
        {
            FVector2D Back;
            UPSCameraFraming::ProjectToShot(Frame.Shot, Mark.FieldPoints[Index], Back);
            TestTrue(*FString::Printf(TEXT("Point %d is on the field where it was drawn"), Index),
                FMath::IsNearlyEqual(static_cast<float>(Mark.FieldPoints[Index].Z), FieldHeight, 0.5f) && Back.Equals(Mark.ScreenPoints[Index], 0.001));
        }
    }

    // An arrow: tail and head, whatever happens between.
    Telestrator->SetTool(EPSTelestratorTool::Arrow);
    Telestrator->BeginStroke(FVector2D(0.30, 0.50));
    Telestrator->ExtendStroke(FVector2D(0.35, 0.55));
    const int32 Arrow = Telestrator->EndStroke(FVector2D(0.60, 0.70));
    Telestration = Telestrator->GetTelestration();
    TestTrue(TEXT("An arrow is tail and head"), Telestration.Marks.IsValidIndex(Arrow) && Telestration.Marks[Arrow].ScreenPoints.Num() == 2
        && Telestration.Marks[Arrow].ScreenPoints[1].Equals(FVector2D(0.60, 0.70)));

    // A circle: its radius on the field.
    Telestrator->SetTool(EPSTelestratorTool::Circle);
    Telestrator->BeginStroke(FVector2D(0.50, 0.55));
    const int32 Ring = Telestrator->EndStroke(FVector2D(0.56, 0.55));
    Telestration = Telestrator->GetTelestration();
    if (TestTrue(TEXT("A circle is drawn"), Telestration.Marks.IsValidIndex(Ring)))
    {
        const FPSTelestratorMark& Mark = Telestration.Marks[Ring];
        TestTrue(TEXT("... with its radius on the field"), Mark.FieldPoints.Num() == 2
            && FMath::IsNearlyEqual(Mark.FieldRadiusCm, static_cast<float>(FVector::Dist(Mark.FieldPoints[0], Mark.FieldPoints[1])), 0.1f) && Mark.FieldRadiusCm > 0.f);
    }

    // A tap on a player lights him up; a tap on nobody draws nothing.
    Telestrator->SetTool(EPSTelestratorTool::Player);
    const FVector2D ReceiverOnScreen = Fixture.ScreenOf(TEXT("WR_01"));
    Telestrator->BeginStroke(ReceiverOnScreen + FVector2D(0.01, 0.0));
    const int32 Tapped = Telestrator->EndStroke(ReceiverOnScreen + FVector2D(0.01, 0.0));
    Telestration = Telestrator->GetTelestration();
    TestTrue(TEXT("A tap near the receiver picks him"), Telestration.Marks.IsValidIndex(Tapped) && Telestration.Marks[Tapped].PlayerId == FName(TEXT("WR_01")));
    TestTrue(TEXT("... at his place in the frame"), Telestration.Marks.IsValidIndex(Tapped) && Telestration.Marks[Tapped].ScreenPoints[0].Equals(ReceiverOnScreen, 0.0001));
    TestTrue(TEXT("... and lights him up (Epic 36)"), Fixture.Emphasis->GetEmphasis(Fixture.Receiver).Look != EPSEmphasisLook::None);
    TestEqual(TEXT("... nobody else"), Fixture.Emphasis->GetEmphasis(Fixture.Corner).Look, EPSEmphasisLook::None);
    TestEqual(TEXT("PickPlayer agrees"), UPSTelestratorSubsystem::PickPlayer(Frame, ReceiverOnScreen, 0.04f), FName(TEXT("WR_01")));
    Telestrator->BeginStroke(FVector2D(0.02, 0.02));
    TestEqual(TEXT("A tap on nobody draws nothing"), Telestrator->EndStroke(FVector2D(0.02, 0.02)), static_cast<int32>(INDEX_NONE));

    // Undo takes the last mark back, emphasis and all.
    TestEqual(TEXT("Four marks"), Telestrator->GetTelestration().Marks.Num(), 4);
    TestTrue(TEXT("Undo"), Telestrator->Undo());
    TestEqual(TEXT("... three left"), Telestrator->GetTelestration().Marks.Num(), 3);
    TestEqual(TEXT("... and the receiver dark again"), Fixture.Emphasis->GetEmphasis(Fixture.Receiver).Look, EPSEmphasisLook::None);

    // The mark limit.
    FPSTelestratorTuning Few = Telestrator->GetTuning();
    Few.MaxMarks = 3;
    TestTrue(TEXT("A limit of three"), Telestrator->SetTuning(Few));
    Telestrator->SetTool(EPSTelestratorTool::Arrow);
    Telestrator->BeginStroke(FVector2D(0.2, 0.6));
    TestEqual(TEXT("A frame that's full takes no more"), Telestrator->EndStroke(FVector2D(0.3, 0.6)), static_cast<int32>(INDEX_NONE));

    // Leaving analysis clears it.
    Telestrator->EndAnalysis();
    TestFalse(TEXT("Analysis ends"), Telestrator->IsAnalysisActive());
    TestEqual(TEXT("... and its marks go"), Telestrator->GetTelestration().Marks.Num(), 0);

    Fixture.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// 3. A paused replay, and stills
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSTelestratorReplayTest,
    "PlaySports.Telestrator.PausedReplayAndStills",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelestratorReplayTest::RunTest(const FString& Parameters)
{
    using namespace PSTelestratorTests;

    FFilmFixture Fixture;
    if (!TestTrue(TEXT("The world is set up"), Fixture.Setup()))
    {
        Fixture.Teardown();
        return false;
    }
    UPSTelestratorSubsystem* Telestrator = Fixture.Telestrator;
    UPSReplaySubsystem* Replay = Fixture.Replay;

    // A two-frame clip of the receiver running.
    FPSReplayRecording Clip = UPSReplayFormat::MakeRecording(FPlayState(), TArray<FPlayerAttributes>(), TArray<FPlayerAttributes>());
    for (int32 Index = 0; Index < 2; ++Index)
    {
        FPSSnapshotFrame& Frame = Clip.Frames.AddDefaulted_GetRef();
        Frame.FrameIndex = Index;
        Frame.Time = static_cast<float>(Index);
        FPSPawnSnapshot& Snapshot = Frame.Pawns.AddDefaulted_GetRef();
        Snapshot.PlayerId = TEXT("WR_01");
        Snapshot.Location = FVector(500.0 * Index, 300.0, 90.0);
    }
    if (!TestTrue(TEXT("The clip plays"), Replay->StartReplay(Clip)))
    {
        Fixture.Teardown();
        return false;
    }

    TestTrue(TEXT("Analysis on the playing replay"), Telestrator->BeginAnalysis());
    TestEqual(TEXT("... holds it"), Replay->GetState(), EPSReplayState::Paused);
    const FPSFilmFrame Frame = Telestrator->GetTelestration().Frame;
    const FPSFilmFramePlayer* Receiver = Frame.Players.FindByPredicate([](const FPSFilmFramePlayer& Player) { return Player.PlayerId == FName(TEXT("WR_01")); });
    TestTrue(TEXT("... and draws on the replayed field"), Receiver && Receiver->WorldLocation.Equals(FVector(0.0, 300.0, 90.0), 1.0));

    Telestrator->SetTool(EPSTelestratorTool::Arrow);
    Telestrator->BeginStroke(FVector2D(0.4, 0.7));
    Telestrator->EndStroke(FVector2D(0.6, 0.75));
    Telestrator->SetTool(EPSTelestratorTool::Circle);
    Telestrator->BeginStroke(FVector2D(0.5, 0.8));
    Telestrator->EndStroke(FVector2D(0.55, 0.8));

    // The annotated still.
    const FString Directory = FPaths::AutomationTransientDir() / TEXT("TelestratorTests");
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    FString StillPath;
    TestTrue(TEXT("The still is saved"), Telestrator->ExportStill(Directory, StillPath));
    TestTrue(TEXT("... to a file"), IFileManager::Get().FileExists(*StillPath));
    FPSTelestration Still;
    if (TestTrue(TEXT("... which reads back"), UPSTelestratorSubsystem::LoadStill(StillPath, Still)))
    {
        const FPSTelestration Drawn = Telestrator->GetTelestration();
        TestEqual(TEXT("... with its marks"), Still.Marks.Num(), Drawn.Marks.Num());
        TestTrue(TEXT("... as drawn"), Still.Marks.Num() == 2 && Still.Marks[0].Tool == EPSTelestratorTool::Arrow
            && Still.Marks[0].ScreenPoints.Num() == 2 && Still.Marks[0].ScreenPoints[1].Equals(FVector2D(0.6, 0.75), 0.0001)
            && FMath::IsNearlyEqual(Still.Marks[1].FieldRadiusCm, Drawn.Marks[1].FieldRadiusCm, 0.01f));
        TestEqual(TEXT("... its frame"), Still.Frame.Players.Num(), Frame.Players.Num());
        TestTrue(TEXT("No screenshot without a viewport"), Still.Frame.ImageFile.IsEmpty());
    }

    Telestrator->EndAnalysis();
    TestEqual(TEXT("Leaving analysis lets the replay play on"), Replay->GetState(), EPSReplayState::Playing);
    TestFalse(TEXT("No still outside analysis"), Telestrator->ExportStill(Directory, StillPath));

    // A replay held by the viewer stays held.
    Replay->SetPaused(true);
    Telestrator->BeginAnalysis();
    Telestrator->EndAnalysis();
    TestEqual(TEXT("A replay the viewer held stays held"), Replay->GetState(), EPSReplayState::Paused);

    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    Fixture.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// 4. The auto-annotation hook
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSTelestratorAutoAnnotationTest,
    "PlaySports.Telestrator.AutoAnnotationHook",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelestratorAutoAnnotationTest::RunTest(const FString& Parameters)
{
    using namespace PSTelestratorTests;

    FFilmFixture Fixture;
    if (!TestTrue(TEXT("The world is set up"), Fixture.Setup()) || !TestTrue(TEXT("Analysis on the film view"), Fixture.BeginFilmAnalysis()))
    {
        Fixture.Teardown();
        return false;
    }
    UPSTelestratorSubsystem* Telestrator = Fixture.Telestrator;

    // No bridge: nothing to ask.
    TestFalse(TEXT("No bridge, no annotator"), Telestrator->IsAutoAnnotationAvailable());
    TestEqual(TEXT("... so a request is refused"), Telestrator->RequestAutoAnnotation(), static_cast<int32>(INDEX_NONE));

    // An agent polling through the bridge.
    Telestrator->SetAutoAnnotationBridgeOnline(true);
    const int32 RequestId = Telestrator->RequestAutoAnnotation();
    TestTrue(TEXT("With the bridge online a request is made"), RequestId != INDEX_NONE);
    const TArray<FPSAnnotationRequest> Pending = Telestrator->GetPendingAnnotationRequests();
    TestTrue(TEXT("... and waits for its answer, with the frame"), Pending.Num() == 1 && Pending[0].RequestId == RequestId && Pending[0].Frame.Players.Num() == 3);

    TArray<FPSAnnotationSuggestion> Suggestions;
    FPSAnnotationSuggestion& Highlight = Suggestions.AddDefaulted_GetRef();
    Highlight.Tool = EPSTelestratorTool::Player;
    Highlight.PlayerId = TEXT("WR_01");
    FPSAnnotationSuggestion& Route = Suggestions.AddDefaulted_GetRef();
    Route.Tool = EPSTelestratorTool::Arrow;
    Route.FieldPoints = { FVector(1500.0, 900.0, 0.0), FVector(2500.0, 900.0, 0.0) };
    FPSAnnotationSuggestion& Zone = Suggestions.AddDefaulted_GetRef();
    Zone.Tool = EPSTelestratorTool::Circle;
    Zone.FieldPoints = { FVector(-800.0, -700.0, 0.0) };
    Zone.RadiusCm = 300.f;
    FPSAnnotationSuggestion& Stranger = Suggestions.AddDefaulted_GetRef();
    Stranger.Tool = EPSTelestratorTool::Player;
    Stranger.PlayerId = TEXT("NOBODY");

    TestTrue(TEXT("The answer is taken"), Telestrator->SubmitAutoAnnotation(RequestId, TEXT("The corner bit on the out; the post was open."), Suggestions));
    const FPSTelestration Annotated = Telestrator->GetTelestration();
    TestEqual(TEXT("Its notes are kept"), Annotated.Notes.Num(), 1);
    TestEqual(TEXT("A mark for each suggestion about someone or somewhere on the field"), Annotated.Marks.Num(), 3);
    TestTrue(TEXT("... all of them the annotator's"), !Annotated.Marks.ContainsByPredicate([](const FPSTelestratorMark& Mark) { return !Mark.bAuto; }));
    TestTrue(TEXT("The receiver is lit up"), Fixture.Emphasis->GetEmphasis(Fixture.Receiver).Look != EPSEmphasisLook::None);
    if (Annotated.Marks.Num() == 3)
    {
        FVector2D Head;
        UPSCameraFraming::ProjectToShot(Annotated.Frame.Shot, FVector(2500.0, 900.0, 0.0), Head);
        TestTrue(TEXT("The arrow's head is on the screen where its field point shows"), Annotated.Marks[1].ScreenPoints.Num() == 2 && Annotated.Marks[1].ScreenPoints[1].Equals(Head, 0.0001));
        TestEqual(TEXT("The circle keeps its radius"), Annotated.Marks[2].FieldRadiusCm, 300.f);
    }
    TestEqual(TEXT("Nothing left pending"), Telestrator->GetPendingAnnotationRequests().Num(), 0);
    TestFalse(TEXT("An answer twice is refused"), Telestrator->SubmitAutoAnnotation(RequestId, TEXT("Again"), Suggestions));

    // A listener answering at once (a bridge bound in code).
    Telestrator->SetAutoAnnotationBridgeOnline(false);
    const FDelegateHandle Listener = Telestrator->OnAutoAnnotationRequestedMC.AddLambda([Telestrator](const FPSAnnotationRequest& Request)
    {
        Telestrator->SubmitAutoAnnotation(Request.RequestId, TEXT("Quick note."), TArray<FPSAnnotationSuggestion>());
    });
    TestTrue(TEXT("A bound listener makes the hook available"), Telestrator->IsAutoAnnotationAvailable());
    TestTrue(TEXT("... and answers"), Telestrator->RequestAutoAnnotation() != INDEX_NONE && Telestrator->GetTelestration().Notes.Num() == 2);
    Telestrator->OnAutoAnnotationRequestedMC.Remove(Listener);

    // An answer for a frame no longer on screen is refused.
    Telestrator->SetAutoAnnotationBridgeOnline(true);
    const int32 Stale = Telestrator->RequestAutoAnnotation();
    Telestrator->EndAnalysis();
    Fixture.BeginFilmAnalysis();
    TestFalse(TEXT("An answer for an old frame is refused"), Telestrator->SubmitAutoAnnotation(Stale, TEXT("Late."), Suggestions));
    TestEqual(TEXT("... and draws nothing"), Telestrator->GetTelestration().Marks.Num(), 0);

    Fixture.Teardown();
    return true;
}

#endif
