// PSPlayArtPipelineTests.cpp -- Epic 35 (the play-art authoring pipeline)
//
// Tests covered:
//   1. The annotation layer: an assignment's Art block colors and emphasizes its art (route or
//      icon) as it compiles; a bad annotation is reported; badge letters reach the position
//      badges for a viewer who may see that side's art, and nobody else.
//   2. Validation: a clean compile is consistent with the jobs the players are handed; drift is
//      caught -- a ribbon off the waypoints, a route with no end, art on a blocker, a ranked route
//      with nothing to draw, a man line to the wrong receiver, a missing rush arrow, art for
//      nobody, art on a kick.
//   3. The round trip over the whole shipped playbook: every play, lined up in its formation's
//      personnel, compiles to art that validates, and after the snap the AI runs what was drawn:
//      every receiver his ribbon, every defender his star, line or arrow.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSAIFieldSnapshot.h"
#include "PSCoverageMatchupSubsystem.h"
#include "PSDataIngestion.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenderPreSnapSubsystem.h"
#include "PSDefenseController.h"
#include "PSFieldGrid.h"
#include "PSOffenseController.h"
#include "PSOverlayBadgeComponent.h"
#include "PSOverlayPlayArtSubsystem.h"
#include "PSPersonnelManager.h"
#include "PSPlayArt.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayContextComponent.h"
#include "PSPlayResolution.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSRosterData.h"
#include "PSTelemetryBus.h"
#include "PSUITeamCatalog.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPlayArtPipelineTests
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

    /** A pawn at Location under its side's AI controller, bound to the bus. */
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
                AI->GetDefenderAI()->BindToBus();
            }
        }
        else if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
        }
        return Pawn;
    }

    /** The roles a play's formation puts on the field (its personnel package), in role order. */
    static TArray<EPlayerRole> PackageRoles(const UPSPersonnelManager* Personnel, const FPSPlayDefinition& Play)
    {
        TArray<EPlayerRole> Roles;
        const FPSPersonnelPackage* Package = Personnel->FindPackage(Personnel->GetPackageForFormation(Play.Formation, Play.bIsOffensivePlay));
        const UEnum* RoleEnum = StaticEnum<EPlayerRole>();
        if (!Package || !RoleEnum)
        {
            return Roles;
        }
        for (const EPlayerRole Role : { EPlayerRole::Quarterback, EPlayerRole::RunningBack, EPlayerRole::WideReceiver, EPlayerRole::TightEnd,
            EPlayerRole::OffensiveLineman, EPlayerRole::DefensiveLineman, EPlayerRole::Linebacker, EPlayerRole::DefensiveBack })
        {
            const int32* Count = Package->RoleCounts.Find(RoleEnum->GetNameStringByValue(static_cast<int64>(Role)));
            for (int32 Index = 0; Count && Index < *Count; ++Index)
            {
                Roles.Add(Role);
            }
        }
        return Roles;
    }

    /** Lines up Roles at the line as the game mode does (APSFieldGrid::ComputeLineup). */
    static void LineUp(UWorld* World, const TArray<EPlayerRole>& Roles, const FVector& Line, const TCHAR* Side)
    {
        const TArray<FVector> Spots = APSFieldGrid::ComputeLineup(Roles, Line.X);
        for (int32 Index = 0; Index < Roles.Num() && Index < Spots.Num(); ++Index)
        {
            SpawnPlayer(World, Roles[Index], FString::Printf(TEXT("%s_%d"), Side, Index), Spots[Index]);
        }
    }

    static FPSSituationContext FirstAndTen()
    {
        FPSSituationContext Situation;
        Situation.Down = 1;
        Situation.Distance = 10;
        Situation.YardLine = 20;
        return Situation;
    }

    static void AnnounceLine(UPSTelemetryBus* Bus, const FVector& Line)
    {
        FPSTelemetryGameStateEvent Event;
        Event.Phase = TEXT("PreSnap");
        Event.LineOfScrimmage = Line;
        Bus->PublishGameState(Event);
    }

    static void Snap(UPSTelemetryBus* Bus, const FVector& Line)
    {
        FPSTelemetrySnapEvent Event;
        Event.Down = 1;
        Event.Distance = 10;
        Event.YardLine = 20;
        Event.LineOfScrimmage = Line;
        Bus->PublishSnap(Event);
    }

    static FPSPlayArtStyle LoadStyle()
    {
        FPSPlayArtStyle Style;
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        Ingestion->LoadPlayArtStyleFromJson(UPSOverlayPlayArtSubsystem::GetDefaultStylePath(), Style);
        return Style;
    }

    static UDataTable* MakeRouteLibrary()
    {
        UDataTable* Table = NewObject<UDataTable>();
        Table->RowStruct = FPSRoute::StaticStruct();
        FPSRoute Slant;
        Slant.RouteId = TEXT("Slant");
        FPSRouteWaypoint Stem;
        Stem.Offset = FVector(300.f, 0.f, 0.f);
        FPSRouteWaypoint Break;
        Break.Offset = FVector(500.f, -400.f, 0.f);
        Slant.Waypoints = { Stem, Break };
        Table->AddRow(Slant.RouteId, Slant);
        return Table;
    }

    static FPSPlayAssignment Slot(EPlayerRole Role, EPSAssignmentKind Kind, const TCHAR* RouteId = nullptr)
    {
        FPSPlayAssignment Assignment;
        Assignment.Role = Role;
        Assignment.Kind = Kind;
        Assignment.RouteId = RouteId ? FName(RouteId) : NAME_None;
        return Assignment;
    }

    static const FPSPlayArtPrimitive* ArtOf(const TArray<FPSPlayArtPrimitive>& Art, const APSPlayerPawn* Pawn, EPSPlayArtShape Shape)
    {
        return Art.FindByPredicate([Pawn, Shape](const FPSPlayArtPrimitive& Piece) { return Piece.Pawn.Get() == Pawn && Piece.Shape == Shape && !Piece.bBranch; });
    }

    static bool HasProblem(const TArray<FString>& Problems, const TCHAR* Fragment)
    {
        return Problems.ContainsByPredicate([Fragment](const FString& Problem) { return Problem.Contains(Fragment); });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The annotation layer
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayArtAnnotationTest,
    "PlaySports.PlayArt.Annotations",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayArtAnnotationTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayArtPipelineTests;

    const FPSPlayArtStyle Style = LoadStyle();
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
    Overlay->SetOverlayDetail(EPSOverlayDetail::Full);
    Overlay->SetRefreshHz(0.f);

    const FVector Line(1000.f, 0.f, 0.f);
    APSPlayerPawn* WR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(950.f, 900.f, 100.f));
    APSPlayerPawn* LB = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB"), FVector(1500.f, 0.f, 100.f));
    if (!TestTrue(TEXT("Players spawned"), WR && LB))
    {
        DestroyTestWorld(World);
        return false;
    }
    UDataTable* Routes = MakeRouteLibrary();

    // A colored, emphasized slant.
    FPSPlayDefinition Offense;
    Offense.PlayId = TEXT("Test_Annotated");
    Offense.bIsOffensivePlay = true;
    FPSPlayAssignment Slant = Slot(EPlayerRole::WideReceiver, EPSAssignmentKind::Route, TEXT("Slant"));
    Slant.Art.Color = TEXT("#3FB950");
    Slant.Art.bEmphasis = true;
    Offense.Assignments = { Slant };
    TArray<FPSResolvedAssignment> Resolved = PSPlayResolution::ResolvePlay(Offense, { WR, LB }, Routes, Line);
    TArray<FPSPlayArtPrimitive> Art = PSPlayArt::CompilePlayArt(Offense, Resolved, Routes, Style, 30.f, Line);
    FLinearColor Green;
    UPSUITeamCatalog::ParseHexColor(TEXT("#3FB950"), Green);
    const FPSPlayArtPrimitive* Ribbon = ArtOf(Art, WR, EPSPlayArtShape::Ribbon);
    if (TestNotNull(TEXT("The slant compiles"), Ribbon))
    {
        TestTrue(TEXT("...in the annotation's color, not its read's"), Ribbon->Color.Equals(Green));
        TestTrue(TEXT("...emphasized: EmphasisScale wider"), Ribbon->bEmphasized && FMath::IsNearlyEqual(Ribbon->Size, Style.RibbonWidth * Style.EmphasisScale));
    }
    const FPSPlayArtPrimitive* Ring = ArtOf(Art, WR, EPSPlayArtShape::Ring);
    TestTrue(TEXT("Its ring too"), Ring && Ring->bEmphasized && Ring->Color.Equals(Green) && FMath::IsNearlyEqual(Ring->Size, Style.RingRadius * Style.EmphasisScale));
    TestEqual(TEXT("A sound annotation validates"), PSPlayArt::ValidatePlayArt(Offense, Resolved, Art, Routes, Style).Num(), 0);

    // An emphasized blitzer.
    FPSPlayDefinition Defense;
    Defense.PlayId = TEXT("Test_Blitz");
    Defense.bIsOffensivePlay = false;
    FPSPlayAssignment Blitz = Slot(EPlayerRole::Linebacker, EPSAssignmentKind::Blitz);
    Blitz.Art.bEmphasis = true;
    Defense.Assignments = { Blitz };
    Resolved = PSPlayResolution::ResolvePlay(Defense, { WR, LB }, nullptr, Line);
    Art = PSPlayArt::CompilePlayArt(Defense, Resolved, nullptr, Style, 30.f, Line);
    const FPSPlayArtPrimitive* Arrow = ArtOf(Art, LB, EPSPlayArtShape::Arrow);
    TestTrue(TEXT("The blitzer's arrow is emphasized"), Arrow && Arrow->bEmphasized && FMath::IsNearlyEqual(Arrow->Size, Style.RushArrowWidth * Style.EmphasisScale));

    // A bad annotation.
    FPSPlayArtAnnotation Bad;
    Bad.Color = TEXT("green");
    Bad.BadgeLetter = TEXT("xyz");
    TestEqual(TEXT("A bad color and a bad letter are both reported"), PSPlayArt::ValidateAnnotation(Bad).Num(), 2);
    TestTrue(TEXT("Letters: one or two capitals or digits"), PSPlayArt::IsValidBadgeLetter(TEXT("Y")) && PSPlayArt::IsValidBadgeLetter(TEXT("H2")) && !PSPlayArt::IsValidBadgeLetter(TEXT("y")) && !PSPlayArt::IsValidBadgeLetter(TEXT("")));

    // Badge letters: Slant-Flat names its back F and its tight end Y.
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(900.f, 0.f, 100.f));
    APSPlayerPawn* TE = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE"), FVector(950.f, 450.f, 100.f));
    APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(500.f, 0.f, 100.f));
    if (!TestTrue(TEXT("Offense spawned"), QB && TE && RB))
    {
        DestroyTestWorld(World);
        return false;
    }
    AnnounceLine(Bus, Line);
    PlayCall->OpenPlayCall(FirstAndTen());
    PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human);
    Overlay->AdvanceTime(0.f);
    TestEqual(TEXT("A spectator sees the back's letter"), Overlay->GetBadgeLetter(RB, nullptr), FString(TEXT("F")));
    TestEqual(TEXT("...and the tight end's, though he blocks"), Overlay->GetBadgeLetter(TE, nullptr), FString(TEXT("Y")));
    TestEqual(TEXT("A slot with no letter gives none"), Overlay->GetBadgeLetter(WR, nullptr), FString());

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Human = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    UPSOverlayBadgeComponent* Badges = Human ? Human->GetOverlayBadgeComponent() : nullptr;
    if (TestNotNull(TEXT("Badge component"), Badges) && TestTrue(TEXT("The human takes the receiver"), Human->TakeControlOf(WR)))
    {
        Human->GetPlayContextComponent()->BindToBus();
        FPSBadgeView View;
        View.CameraLocation = FVector(-1500.0, 0.0, 800.0);
        View.CameraRotation = FRotator(-20.f, 0.f, 0.f);
        Badges->Refresh(View);
        const FPSPositionBadge* Back = Badges->FindBadge(RB);
        const FPSPositionBadge* Tight = Badges->FindBadge(TE);
        TestTrue(TEXT("The back's badge wears the play's letter"), Back && Back->Label == TEXT("F"));
        TestTrue(TEXT("...and the tight end's"), Tight && Tight->Label == TEXT("Y"));
        const FPSPositionBadge* Passer = Badges->FindBadge(QB);
        TestTrue(TEXT("A player the play gives no letter keeps his role's label"), Passer && Passer->Label == UPSOverlayBadgeComponent::LocalizedRoleLabel(Badges->GetStyle(), EPlayerRole::Quarterback));

        TestTrue(TEXT("The human takes the linebacker"), Human->TakeControlOf(LB));
        TestEqual(TEXT("A defender doesn't see the offense's letters"), Overlay->GetBadgeLetter(RB, Human), FString());
        Badges->Refresh(View);
        const FPSPositionBadge* BackSeenFromDefense = Badges->FindBadge(RB);
        TestTrue(TEXT("...his badges show the back's role"), BackSeenFromDefense && BackSeenFromDefense->Label == UPSOverlayBadgeComponent::LocalizedRoleLabel(Badges->GetStyle(), EPlayerRole::RunningBack));
    }

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Validation catches drift between the art and the jobs
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayArtValidationTest,
    "PlaySports.PlayArt.ValidationCatchesDrift",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayArtValidationTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayArtPipelineTests;

    const FPSPlayArtStyle Style = LoadStyle();
    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    const FVector Line(1000.f, 0.f, 0.f);
    APSPlayerPawn* WR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(950.f, 900.f, 100.f));
    APSPlayerPawn* TE = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE"), FVector(950.f, 450.f, 100.f));
    APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(500.f, 0.f, 100.f));
    APSPlayerPawn* CB = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB"), FVector(1500.f, 900.f, 100.f));
    APSPlayerPawn* DL = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL"), FVector(1050.f, 100.f, 100.f));
    if (!TestTrue(TEXT("Players spawned"), WR && TE && RB && CB && DL))
    {
        DestroyTestWorld(World);
        return false;
    }
    UDataTable* Routes = MakeRouteLibrary();
    const TArray<APSPlayerPawn*> Field = { WR, TE, RB, CB, DL };

    FPSPlayDefinition Offense;
    Offense.PlayId = TEXT("Test_Offense");
    Offense.bIsOffensivePlay = true;
    FPSPlayAssignment Ghost = Slot(EPlayerRole::RunningBack, EPSAssignmentKind::Route, TEXT("Wheel"));
    Ghost.ReadOrder = 2;
    FPSPlayAssignment Slant = Slot(EPlayerRole::WideReceiver, EPSAssignmentKind::Route, TEXT("Slant"));
    Slant.ReadOrder = 1;
    Offense.Assignments = { Slant, Slot(EPlayerRole::TightEnd, EPSAssignmentKind::PassBlock), Ghost };
    const TArray<FPSResolvedAssignment> Resolved = PSPlayResolution::ResolvePlay(Offense, Field, Routes, Line, false);
    const TArray<FPSPlayArtPrimitive> Clean = PSPlayArt::CompilePlayArt(Offense, Resolved, Routes, Style, 30.f, Line);
    const TArray<FString> Ghosts = PSPlayArt::ValidatePlayArt(Offense, Resolved, Clean, Routes, Style);
    TestTrue(TEXT("A route no library has is reported"), HasProblem(Ghosts, TEXT("which no route library has")));
    TestTrue(TEXT("...and a ranked route with nothing to draw"), HasProblem(Ghosts, TEXT("is read 2, but runs no route to draw")));
    TestEqual(TEXT("...and nothing else"), Ghosts.Num(), 2);

    TArray<FPSPlayArtPrimitive> Drifted = Clean;
    for (FPSPlayArtPrimitive& Piece : Drifted)
    {
        if (Piece.Shape == EPSPlayArtShape::Ribbon)
        {
            Piece.Points.Last() += FVector(0.f, 200.f, 0.f);
        }
    }
    TestTrue(TEXT("A ribbon off the waypoints he is handed is caught"), HasProblem(PSPlayArt::ValidatePlayArt(Offense, Resolved, Drifted, Routes, Style), TEXT("isn't the Slant he is handed")));

    TArray<FPSPlayArtPrimitive> NoEnd = Clean.FilterByPredicate([](const FPSPlayArtPrimitive& Piece) { return Piece.Shape != EPSPlayArtShape::Ring; });
    TestTrue(TEXT("A route with no end is caught"), HasProblem(PSPlayArt::ValidatePlayArt(Offense, Resolved, NoEnd, Routes, Style), TEXT("has no end")));

    TArray<FPSPlayArtPrimitive> OnBlocker = Clean;
    FPSPlayArtPrimitive Stray = Clean[0];
    Stray.Pawn = TE;
    OnBlocker.Add(Stray);
    TestTrue(TEXT("Art on a blocker is caught"), HasProblem(PSPlayArt::ValidatePlayArt(Offense, Resolved, OnBlocker, Routes, Style), TEXT("has no route to draw, but has art")));

    TArray<FPSPlayArtPrimitive> ForNobody = Clean;
    FPSPlayArtPrimitive Orphan = Clean[0];
    Orphan.Pawn = nullptr;
    ForNobody.Add(Orphan);
    TestTrue(TEXT("Art for nobody in the play is caught"), HasProblem(PSPlayArt::ValidatePlayArt(Offense, Resolved, ForNobody, Routes, Style), TEXT("for nobody in the play")));

    FPSPlayDefinition Punt = Offense;
    Punt.PlayCategory = TEXT("Punt");
    TestTrue(TEXT("Art on a kick is caught"), HasProblem(PSPlayArt::ValidatePlayArt(Punt, Resolved, Clean, Routes, Style), TEXT("draws no art")));

    // The defense: a man corner and a rushing lineman.
    FPSPlayDefinition Defense;
    Defense.PlayId = TEXT("Test_Defense");
    Defense.bIsOffensivePlay = false;
    Defense.Assignments = { Slot(EPlayerRole::DefensiveBack, EPSAssignmentKind::ManCoverage), Slot(EPlayerRole::DefensiveLineman, EPSAssignmentKind::PassRush) };
    TArray<FPSResolvedAssignment> Coverage = PSPlayResolution::ResolvePlay(Defense, Field, nullptr, Line);
    TArray<EPlayerRole> Roles;
    for (const APSPlayerPawn* Player : Field)
    {
        Roles.Add(Player->GetAttributes().Role);
    }
    PSPlayResolution::ResolveManMatchups(Coverage, Field, Roles, nullptr);
    const TArray<FPSPlayArtPrimitive> Icons = PSPlayArt::CompilePlayArt(Defense, Coverage, nullptr, Style, 30.f, Line);
    TestEqual(TEXT("The defense's clean icons validate"), PSPlayArt::ValidatePlayArt(Defense, Coverage, Icons, nullptr, Style).Num(), 0);

    TArray<FPSPlayArtPrimitive> WrongMan = Icons;
    for (FPSPlayArtPrimitive& Piece : WrongMan)
    {
        if (Piece.Shape == EPSPlayArtShape::Connector)
        {
            Piece.Target = RB;
        }
    }
    TestTrue(TEXT("A line to the wrong receiver is caught"), HasProblem(PSPlayArt::ValidatePlayArt(Defense, Coverage, WrongMan, nullptr, Style), TEXT("not one line to the receiver he covers")));
    TArray<FPSPlayArtPrimitive> NoRush = Icons.FilterByPredicate([](const FPSPlayArtPrimitive& Piece) { return Piece.Shape != EPSPlayArtShape::Arrow; });
    TestTrue(TEXT("A missing rush arrow is caught"), HasProblem(PSPlayArt::ValidatePlayArt(Defense, Coverage, NoRush, nullptr, Style), TEXT("his rush isn't one arrow")));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The round trip over the shipped playbook
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayArtPlaybookRoundTripTest,
    "PlaySports.PlayArt.PlaybookRoundTrip",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayArtPlaybookRoundTripTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayArtPipelineTests;

    UPSPersonnelManager* Personnel = NewObject<UPSPersonnelManager>();
    if (!TestTrue(TEXT("The personnel packages load"), Personnel->LoadCatalogFromJson(UPSPersonnelManager::GetDefaultCatalogPath())))
    {
        return false;
    }

    // The playbook, read through a play-call subsystem of its own world.
    TArray<FPSPlayDefinition> Playbook;
    {
        UWorld* Library = CreateTestWorld();
        UPSPlayCallSubsystem* PlayCall = Library ? Library->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
        if (PlayCall)
        {
            Playbook = PlayCall->GetPlaybook();
        }
        if (Library)
        {
            DestroyTestWorld(Library);
        }
    }
    if (!TestTrue(TEXT("The playbook loads"), Playbook.Num() > 10))
    {
        return false;
    }
    const FPSPlayDefinition* BaseOffense = Playbook.FindByPredicate([](const FPSPlayDefinition& Play) { return Play.PlayId == FName(TEXT("Offense_SlantFlat")); });
    const FPSPlayDefinition* BaseDefense = Playbook.FindByPredicate([](const FPSPlayDefinition& Play) { return Play.PlayId == FName(TEXT("Defense_43Cover2")); });
    if (!TestTrue(TEXT("The plays each side is paired against"), BaseOffense && BaseDefense))
    {
        return false;
    }

    const FVector Line(2000.f, 0.f, 0.f);
    int32 DrawnPlays = 0;
    bool bSawEmphasis = false;
    for (const FPSPlayDefinition& Play : Playbook)
    {
        const FString Name = Play.PlayId.ToString();
        const FPSPlayDefinition& Offense = Play.bIsOffensivePlay ? Play : *BaseOffense;
        const FPSPlayDefinition& Defense = Play.bIsOffensivePlay ? *BaseDefense : Play;

        UWorld* World = CreateTestWorld();
        UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
        UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
        UPSDefenderPreSnapSubsystem* DefensePreSnap = World ? World->GetSubsystem<UPSDefenderPreSnapSubsystem>() : nullptr;
        UPSCoverageMatchupSubsystem* Matchups = World ? World->GetSubsystem<UPSCoverageMatchupSubsystem>() : nullptr;
        UPSOverlayPlayArtSubsystem* Overlay = World ? World->GetSubsystem<UPSOverlayPlayArtSubsystem>() : nullptr;
        UPSAIFieldSnapshot* Snapshot = World ? World->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
        if (!Bus || !PlayCall || !DefensePreSnap || !Matchups || !Overlay || !Snapshot)
        {
            AddError(FString::Printf(TEXT("%s: the world's subsystems"), *Name));
            if (World)
            {
                DestroyTestWorld(World);
            }
            continue;
        }
        Overlay->SetOverlayDetail(EPSOverlayDetail::Full);
        Overlay->SetRefreshHz(0.f);

        // Each side lined up in its call's personnel, as the game mode lines them up.
        LineUp(World, PackageRoles(Personnel, Offense), Line, TEXT("O"));
        LineUp(World, PackageRoles(Personnel, Defense), Line, TEXT("D"));
        AnnounceLine(Bus, Line);
        PlayCall->OpenPlayCall(FirstAndTen());
        const bool bCalled = PlayCall->CallPlay(Offense.PlayId, EPSPlayCaller::Human) && PlayCall->CallPlay(Defense.PlayId, EPSPlayCaller::Human);
        if (!TestTrue(FString::Printf(TEXT("%s is called"), *Name), bCalled))
        {
            DestroyTestWorld(World);
            continue;
        }
        // The defense lines up for its call and plans its press, as its ticks would.
        DefensePreSnap->EnsureAligned();
        Matchups->UpdateCoverage(0.f);
        Overlay->AdvanceTime(0.f);
        const TArray<FPSPlayArtPrimitive> Art = Play.bIsOffensivePlay ? Overlay->GetRouteArt() : Overlay->GetDefenseArt();

        // The art against the jobs the players are handed (the resolution the snap will use).
        const TArray<APSPlayerPawn*> Pawns = Snapshot->GetPawns();
        const TArray<EPlayerRole> Roles = Snapshot->GetRoles();
        FPSPlayDefinition PlayToRun = Play;
        if (!Play.bIsOffensivePlay)
        {
            PlayCall->GetDefensivePlayToRun(PlayToRun);
        }
        TArray<FPSResolvedAssignment> Resolved = PSPlayResolution::ResolvePlay(PlayToRun, Pawns, PlayCall->GetRouteLibrary(), Line, false);
        if (!Play.bIsOffensivePlay)
        {
            PSPlayResolution::ResolveManMatchups(Resolved, Pawns, Roles, Matchups);
        }
        for (const FString& Problem : PSPlayArt::ValidatePlayArt(PlayToRun, Resolved, Art, PlayCall->GetRouteLibrary(), Overlay->GetStyle()))
        {
            AddError(Problem);
        }
        if (PSPlayArt::DrawsNoArt(Play, Overlay->GetStyle()))
        {
            DestroyTestWorld(World);
            continue;
        }
        TestTrue(FString::Printf(TEXT("%s draws art"), *Name), Art.Num() > 0);
        ++DrawnPlays;

        // The snap: the AI runs what was drawn.
        Snap(Bus, Line);
        if (Play.bIsOffensivePlay)
        {
            for (const FPSPlayArtPrimitive& Piece : Art)
            {
                const APSPlayerPawn* Runner = Piece.Pawn.Get();
                if (Piece.Shape != EPSPlayArtShape::Ribbon || Piece.bBranch || !Runner)
                {
                    continue;
                }
                bSawEmphasis = bSawEmphasis || Piece.bEmphasized;
                const APSOffenseController* AI = Cast<APSOffenseController>(Runner->GetController());
                const TArray<FVector> Run = AI ? AI->GetRouteWaypoints() : TArray<FVector>();
                const bool bBranches = Art.ContainsByPredicate([Runner](const FPSPlayArtPrimitive& Other) { return Other.bBranch && Other.Pawn.Get() == Runner; });
                bool bSame = Run.Num() >= Piece.Points.Num() - 1 && (bBranches || Run.Num() == Piece.Points.Num() - 1);
                for (int32 Index = 1; bSame && Index < Piece.Points.Num(); ++Index)
                {
                    bSame = FVector::Dist2D(Run[Index - 1], Piece.Points[Index]) < 1.f;
                }
                TestTrue(FString::Printf(TEXT("%s: %s runs the %s he was drawn"), *Name, *Runner->GetAttributes().DisplayName, *Piece.Source.ToString()), bSame);
            }
        }
        else
        {
            // Each defender takes up his job on his first tick, in the field's order.
            for (APSPlayerPawn* Player : Pawns)
            {
                const APSDefenseController* Controller = Player ? Cast<APSDefenseController>(Player->GetController()) : nullptr;
                if (Controller && Controller->GetDefenderAI())
                {
                    Controller->GetDefenderAI()->TickAI(0.01f);
                }
            }
            for (const FPSPlayArtPrimitive& Piece : Art)
            {
                const APSPlayerPawn* Defender = Piece.Pawn.Get();
                const APSDefenseController* Controller = Defender ? Cast<APSDefenseController>(Defender->GetController()) : nullptr;
                const UPSDefenderAIComponent* AI = Controller ? Controller->GetDefenderAI() : nullptr;
                if (!AI)
                {
                    AddError(FString::Printf(TEXT("%s: an icon for a player with no defense AI"), *Name));
                    continue;
                }
                bSawEmphasis = bSawEmphasis || Piece.bEmphasized;
                const FString Who = FString::Printf(TEXT("%s: %s"), *Name, *Defender->GetAttributes().DisplayName);
                if (Piece.Shape == EPSPlayArtShape::Connector)
                {
                    TestTrue(FString::Printf(TEXT("%s covers the man he was drawn to"), *Who), AI->GetAction() == EPSDefenderAction::Cover && AI->GetCoveredReceiver() == Piece.Target.Get());
                }
                else if (Piece.Shape == EPSPlayArtShape::Arrow)
                {
                    TestTrue(FString::Printf(TEXT("%s rushes, as drawn"), *Who), AI->GetAction() == EPSDefenderAction::Rush);
                }
                else if (Piece.Shape == EPSPlayArtShape::Star)
                {
                    TestTrue(FString::Printf(TEXT("%s plays the spot he was drawn on"), *Who),
                        AI->GetAction() == EPSDefenderAction::Zone && FVector::Dist2D(AI->GetZoneSpot(), Piece.Points[0]) < 1.f);
                }
            }
        }
        DestroyTestWorld(World);
    }
    TestTrue(TEXT("Most of the playbook draws art"), DrawnPlays >= 20);
    TestTrue(TEXT("...and the authored emphasis shows (Play-Action Post, the Double-A blitz)"), bSawEmphasis);
    return true;
}

#endif
