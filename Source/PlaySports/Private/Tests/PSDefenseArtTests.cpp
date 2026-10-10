// PSDefenseArtTests.cpp -- Epic 31 (defensive assignment iconography)
//
// Tests covered:
//   1. The icons: a star at each zone landmark (on the defender's side, his own spot for a zone
//      with no offset), a line from each man defender to his receiver (named, else the nearest
//      open one, defenders in order; a star at his spot with nobody left), an arrow from each
//      rusher through the line (blitz or rush), nothing for a run fit.
//   2. What is drawn is what is run: the defense's call drawn before the snap against the
//      announced line, with a shadow and any press plan; after the snap every man defender
//      covers the receiver he was drawn to and every rusher rushes.
//   3. Who sees the icons: the defense and spectators, the offense only in study mode; the
//      DefenseIcons setting, the platform tier and the kicking game; head to head, only the
//      house rules, study mode or not.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSAIFieldSnapshot.h"
#include "PSCoverageMatchupSubsystem.h"
#include "PSDataIngestion.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenderPreSnapSubsystem.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSOverlayPlayArtSubsystem.h"
#include "PSPlayArt.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayResolution.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSSettingsSubsystem.h"
#include "PSTelemetryBus.h"
#include "PSUITeamCatalog.h"
#include "PSVersusSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSDefenseArtTests
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
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.PlayerId = FName(PlayerId);
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

    static FPSPlayAssignment Slot(EPlayerRole Role, EPSAssignmentKind Kind, const FVector& ZoneOffset = FVector::ZeroVector)
    {
        FPSPlayAssignment Assignment;
        Assignment.Role = Role;
        Assignment.Kind = Kind;
        Assignment.ZoneOffset = ZoneOffset;
        return Assignment;
    }

    static FPSPlayArtStyle LoadStyle()
    {
        FPSPlayArtStyle Style;
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        Ingestion->LoadPlayArtStyleFromJson(UPSOverlayPlayArtSubsystem::GetDefaultStylePath(), Style);
        return Style;
    }

    static FLinearColor ColorOf(const FString& Hex)
    {
        FLinearColor Parsed = FLinearColor::Black;
        UPSUITeamCatalog::ParseHexColor(Hex, Parsed);
        return Parsed;
    }

    /** Pawn's icon, or null. */
    static const FPSPlayArtPrimitive* IconOf(const TArray<FPSPlayArtPrimitive>& Art, const APSPlayerPawn* Pawn)
    {
        return Art.FindByPredicate([Pawn](const FPSPlayArtPrimitive& Piece) { return Piece.Pawn.Get() == Pawn; });
    }

    static bool SameOnGround(const FVector& A, const FVector& B)
    {
        return FVector::Dist2D(A, B) < 0.1f;
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
}

// ---------------------------------------------------------------------------
// Test 1 -- The icons
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenseArtCompileTest,
    "PlaySports.PlayArt.DefenseIcons",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenseArtCompileTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenseArtTests;

    const FPSPlayArtStyle Style = LoadStyle();
    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    const FVector Line(1000.f, 0.f, 0.f);
    APSPlayerPawn* WideRight = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_R"), FVector(950.f, 900.f, 100.f));
    APSPlayerPawn* WideLeft = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_L"), FVector(950.f, -900.f, 100.f));
    APSPlayerPawn* TE = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE"), FVector(950.f, 450.f, 100.f));
    APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(500.f, 0.f, 100.f));
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(800.f, 0.f, 100.f));
    APSPlayerPawn* CornerRight = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_R"), FVector(1500.f, 900.f, 100.f));
    APSPlayerPawn* CornerLeft = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_L"), FVector(1500.f, -900.f, 100.f));
    APSPlayerPawn* Safety = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("S"), FVector(2500.f, 0.f, 100.f));
    APSPlayerPawn* BackerRight = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB_R"), FVector(1400.f, 300.f, 100.f));
    APSPlayerPawn* BackerLeft = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB_L"), FVector(1400.f, -300.f, 100.f));
    APSPlayerPawn* Tackle = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL"), FVector(1050.f, 100.f, 100.f));
    if (!TestTrue(TEXT("Players spawned"), WideRight && WideLeft && TE && RB && QB && CornerRight && CornerLeft && Safety && BackerRight && BackerLeft && Tackle))
    {
        DestroyTestWorld(World);
        return false;
    }
    const TArray<APSPlayerPawn*> Field = { WideRight, WideLeft, TE, RB, QB, CornerRight, CornerLeft, Safety, BackerRight, BackerLeft, Tackle };
    TArray<EPlayerRole> Roles;
    for (const APSPlayerPawn* Player : Field)
    {
        Roles.Add(Player->GetAttributes().Role);
    }

    // Man across the board, the linemen rushing.
    FPSPlayDefinition Man;
    Man.bIsOffensivePlay = false;
    Man.Assignments = { Slot(EPlayerRole::DefensiveBack, EPSAssignmentKind::ManCoverage), Slot(EPlayerRole::Linebacker, EPSAssignmentKind::ManCoverage),
        Slot(EPlayerRole::DefensiveLineman, EPSAssignmentKind::PassRush) };
    TArray<FPSResolvedAssignment> Resolved = PSPlayResolution::ResolvePlay(Man, Field, nullptr, Line);
    PSPlayResolution::ResolveManMatchups(Resolved, Field, Roles, nullptr);
    TArray<FPSPlayArtPrimitive> Art = PSPlayArt::CompileDefenseArt(Resolved, Style, Line);

    const FPSPlayArtPrimitive* Right = IconOf(Art, CornerRight);
    TestTrue(TEXT("The right corner is joined to the receiver nearest him"),
        Right && Right->Shape == EPSPlayArtShape::Connector && Right->Target.Get() == WideRight && Right->Source == FName(TEXT("Man")));
    if (Right)
    {
        TestTrue(TEXT("...from his feet to the receiver's, on the turf"), SameOnGround(Right->Points[0], CornerRight->GetActorLocation()) && SameOnGround(Right->Points[1], WideRight->GetActorLocation())
            && FMath::IsNearlyEqual(Right->Points[1].Z, Line.Z + Style.GroundOffset));
        TestTrue(TEXT("...in the man line's color and width"), Right->Color.Equals(ColorOf(Style.ManLineColor)) && FMath::IsNearlyEqual(Right->Size, Style.ManLineWidth));
    }
    const FPSPlayArtPrimitive* Left = IconOf(Art, CornerLeft);
    TestTrue(TEXT("The left corner has the left receiver"), Left && Left->Target.Get() == WideLeft);
    const FPSPlayArtPrimitive* Deep = IconOf(Art, Safety);
    TestTrue(TEXT("The safety takes the nearest one left, the tight end"), Deep && Deep->Target.Get() == TE);
    const FPSPlayArtPrimitive* Backer = IconOf(Art, BackerRight);
    TestTrue(TEXT("The first linebacker takes the back"), Backer && Backer->Target.Get() == RB);
    const FPSPlayArtPrimitive* Spare = IconOf(Art, BackerLeft);
    TestTrue(TEXT("With nobody left, the other plays his spot: a star there"),
        Spare && Spare->Shape == EPSPlayArtShape::Star && SameOnGround(Spare->Points[0], BackerLeft->GetActorLocation()));
    TestFalse(TEXT("The quarterback is nobody's man"), Art.ContainsByPredicate([QB](const FPSPlayArtPrimitive& Piece) { return Piece.Target.Get() == QB; }));
    const FPSPlayArtPrimitive* Rush = IconOf(Art, Tackle);
    if (TestTrue(TEXT("The lineman rushes: an arrow"), Rush && Rush->Shape == EPSPlayArtShape::Arrow))
    {
        TestTrue(TEXT("...from his spot through the line, RushArrowDepth behind it"),
            SameOnGround(Rush->Points[0], Tackle->GetActorLocation()) && SameOnGround(Rush->Points[1], FVector(Line.X - Style.RushArrowDepth, 100.f, 0.f)));
        TestTrue(TEXT("...a lineman's rush, not a blitz"), Rush->Source == FName(TEXT("Rush")) && Rush->Color.Equals(ColorOf(Style.RushArrowColor)));
    }

    // A shadow (the play names the man) comes first, whoever is nearer.
    Resolved = PSPlayResolution::ResolvePlay(Man, Field, nullptr, Line);
    Resolved[5].CoverageTarget = TE;
    PSPlayResolution::ResolveManMatchups(Resolved, Field, Roles, nullptr);
    Art = PSPlayArt::CompileDefenseArt(Resolved, Style, Line);
    const FPSPlayArtPrimitive* Shadowing = IconOf(Art, CornerRight);
    TestTrue(TEXT("A shadowing corner is joined to his man"), Shadowing && Shadowing->Target.Get() == TE && Shadowing->Source == FName(TEXT("Shadow")));
    const FPSPlayArtPrimitive* Freed = IconOf(Art, Safety);
    TestTrue(TEXT("...and the safety after him takes the open receiver nearest him instead"), Freed && Freed->Target.Get() == WideRight);

    // Zones and a blitz.
    FPSPlayDefinition Zone;
    Zone.bIsOffensivePlay = false;
    Zone.Assignments = { Slot(EPlayerRole::DefensiveBack, EPSAssignmentKind::ZoneCoverage, FVector(1200.f, 600.f, 0.f)),
        Slot(EPlayerRole::Linebacker, EPSAssignmentKind::Blitz), Slot(EPlayerRole::DefensiveLineman, EPSAssignmentKind::RunFit) };
    Resolved = PSPlayResolution::ResolvePlay(Zone, Field, nullptr, Line);
    PSPlayResolution::ResolveManMatchups(Resolved, Field, Roles, nullptr);
    Art = PSPlayArt::CompileDefenseArt(Resolved, Style, Line);
    const FPSPlayArtPrimitive* StarRight = IconOf(Art, CornerRight);
    TestTrue(TEXT("A zone defender gets a star at his landmark"),
        StarRight && StarRight->Shape == EPSPlayArtShape::Star && SameOnGround(StarRight->Points[0], FVector(2200.f, 600.f, 0.f)) && StarRight->Source == FName(TEXT("Zone")));
    if (StarRight)
    {
        TestTrue(TEXT("...white, of the star's size"), StarRight->Color.Equals(ColorOf(Style.ZoneStarColor)) && FMath::IsNearlyEqual(StarRight->Size, Style.ZoneStarRadius));
    }
    const FPSPlayArtPrimitive* StarLeft = IconOf(Art, CornerLeft);
    TestTrue(TEXT("...played on his own side of the field"), StarLeft && SameOnGround(StarLeft->Points[0], FVector(2200.f, -600.f, 0.f)));
    const FPSPlayArtPrimitive* Blitz = IconOf(Art, BackerLeft);
    TestTrue(TEXT("A blitzer gets the blitz arrow"), Blitz && Blitz->Shape == EPSPlayArtShape::Arrow && Blitz->Source == FName(TEXT("Blitz"))
        && Blitz->Color.Equals(ColorOf(Style.BlitzArrowColor)));
    TestNull(TEXT("A run fit draws nothing"), IconOf(Art, Tackle));

    FPSPlayDefinition OwnSpot;
    OwnSpot.bIsOffensivePlay = false;
    OwnSpot.Assignments = { Slot(EPlayerRole::DefensiveBack, EPSAssignmentKind::ZoneCoverage) };
    Resolved = PSPlayResolution::ResolvePlay(OwnSpot, Field, nullptr, Line);
    Art = PSPlayArt::CompileDefenseArt(Resolved, Style, Line);
    const FPSPlayArtPrimitive* Spot = IconOf(Art, Safety);
    TestTrue(TEXT("A zone with no offset is played from where he stands"), Spot && SameOnGround(Spot->Points[0], Safety->GetActorLocation()));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- What the defense is drawn to do is what it does
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenseArtRoundTripTest,
    "PlaySports.PlayArt.DefenseDrawnIsRun",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenseArtRoundTripTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenseArtTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSDefenderPreSnapSubsystem* DefensePreSnap = World ? World->GetSubsystem<UPSDefenderPreSnapSubsystem>() : nullptr;
    UPSCoverageMatchupSubsystem* Matchups = World ? World->GetSubsystem<UPSCoverageMatchupSubsystem>() : nullptr;
    UPSOverlayPlayArtSubsystem* Overlay = World ? World->GetSubsystem<UPSOverlayPlayArtSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Defensive pre-snap"), DefensePreSnap)
        || !TestNotNull(TEXT("Coverage engine"), Matchups) || !TestNotNull(TEXT("Play art"), Overlay))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    Overlay->SetOverlayDetail(EPSOverlayDetail::Full);
    Overlay->SetRefreshHz(0.f);

    const FVector Line(2000.f, 0.f, 0.f);
    SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(1800.f, 0.f, 100.f));
    APSPlayerPawn* WideRight = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_R"), FVector(1950.f, 1000.f, 100.f));
    SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_L"), FVector(1950.f, -1000.f, 100.f));
    APSPlayerPawn* TE = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE"), FVector(1950.f, 450.f, 100.f));
    SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(1500.f, 0.f, 100.f));
    SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("C"), FVector(1980.f, 0.f, 100.f));
    SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL_R"), FVector(2050.f, 150.f, 100.f));
    SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL_L"), FVector(2050.f, -150.f, 100.f));
    SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB"), FVector(2450.f, 100.f, 100.f));
    SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_R"), FVector(2600.f, 1000.f, 100.f));
    SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_L"), FVector(2600.f, -1000.f, 100.f));
    APSPlayerPawn* Safety = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("S"), FVector(3300.f, 0.f, 100.f));
    if (!TestTrue(TEXT("Players spawned"), WideRight && TE && Safety))
    {
        DestroyTestWorld(World);
        return false;
    }

    AnnounceLine(Bus, Line);
    PlayCall->OpenPlayCall(FirstAndTen());
    TestTrue(TEXT("The offense calls Slant-Flat"), PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human));
    TestTrue(TEXT("The defense calls Nickel Man-Free"), PlayCall->CallPlay(TEXT("Defense_NickelManFree"), EPSPlayCaller::Human));
    // The coverage engine plans its press before the snap; the safety shadows the tight end.
    Matchups->UpdateCoverage(0.f);
    TestTrue(TEXT("The safety shadows the tight end"), DefensePreSnap->SetShadow(Safety, TE, true));
    Overlay->AdvanceTime(0.f);

    const TArray<FPSPlayArtPrimitive> Art = Overlay->GetDefenseArt();
    TestEqual(TEXT("The icons are of the defense's call"), Overlay->GetDefensePlayId(), FName(TEXT("Defense_NickelManFree")));
    const FPSPlayArtPrimitive* Shadow = IconOf(Art, Safety);
    TestTrue(TEXT("The shadow is drawn to his man"), Shadow && Shadow->Shape == EPSPlayArtShape::Connector && Shadow->Target.Get() == TE && Shadow->Source == FName(TEXT("Shadow")));
    int32 Lines = 0;
    int32 Stars = 0;
    int32 Arrows = 0;
    for (const FPSPlayArtPrimitive& Piece : Art)
    {
        Lines += Piece.Shape == EPSPlayArtShape::Connector ? 1 : 0;
        Stars += Piece.Shape == EPSPlayArtShape::Star ? 1 : 0;
        Arrows += Piece.Shape == EPSPlayArtShape::Arrow ? 1 : 0;
        const APSPlayerPawn* Defender = Piece.Pawn.Get();
        if (Piece.Shape == EPSPlayArtShape::Connector && Defender)
        {
            const APSPlayerPawn* Pressed = Matchups->GetPlannedReceiver(Defender);
            if (Pressed && Piece.Source != FName(TEXT("Shadow")))
            {
                TestTrue(FString::Printf(TEXT("%s's press is drawn as a press"), *Defender->GetAttributes().DisplayName), Piece.Target.Get() == Pressed && Piece.Source == FName(TEXT("Press")));
            }
        }
    }
    TestEqual(TEXT("Each of the four man defenders has a man (a line) or, with none left, his spot (a star)"), Lines + Stars, 4);
    TestTrue(TEXT("...at least the corners and the shadow have men"), Lines >= 3);
    TestEqual(TEXT("Both linemen rush"), Arrows, 2);

    // The snap: every defender takes up his job on his first tick, in the field's order.
    Snap(Bus, Line);
    const TArray<APSPlayerPawn*> Players = UPSAIFieldSnapshot::GetFieldPawns(World);
    for (APSPlayerPawn* Player : Players)
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
        if (!TestNotNull(TEXT("Defense AI"), AI))
        {
            continue;
        }
        const FString Name = Defender->GetAttributes().DisplayName;
        if (Piece.Shape == EPSPlayArtShape::Connector)
        {
            TestTrue(FString::Printf(TEXT("%s covers the man he was drawn to"), *Name), AI->GetAction() == EPSDefenderAction::Cover && AI->GetCoveredReceiver() == Piece.Target.Get());
        }
        else if (Piece.Shape == EPSPlayArtShape::Arrow)
        {
            TestTrue(FString::Printf(TEXT("%s rushes, as drawn"), *Name), AI->GetAction() == EPSDefenderAction::Rush);
        }
        else if (Piece.Shape == EPSPlayArtShape::Star)
        {
            TestTrue(FString::Printf(TEXT("%s plays the spot he was drawn on"), *Name), AI->GetAction() == EPSDefenderAction::Zone && SameOnGround(AI->GetZoneSpot(), Piece.Points[0]));
        }
    }
    TestTrue(TEXT("At the snap the icons fade with the route art"), Overlay->IsFading() && Overlay->GetDefenseArt().Num() == Art.Num());

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Who sees the icons
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenseArtPolicyTest,
    "PlaySports.PlayArt.DefenseIconPolicy",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenseArtPolicyTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenseArtTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSOverlayPlayArtSubsystem* Overlay = World ? World->GetSubsystem<UPSOverlayPlayArtSubsystem>() : nullptr;
    UPSVersusSubsystem* Versus = World ? World->GetSubsystem<UPSVersusSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Play art"), Overlay) || !TestNotNull(TEXT("Versus"), Versus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    Overlay->SetOverlayDetail(EPSOverlayDetail::Full);
    Overlay->SetRefreshHz(0.f);
    UPSSettingsSubsystem* Settings = NewObject<UPSSettingsSubsystem>(NewObject<UGameInstance>());
    Overlay->SetSettings(Settings);

    const FVector Line(1000.f, 0.f, 0.f);
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB_D"), FVector(800.f, 0.f, 100.f));
    SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_D"), FVector(950.f, 800.f, 100.f));
    APSPlayerPawn* LB = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB_D"), FVector(1500.f, 0.f, 100.f));
    SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("FS_D"), FVector(2500.f, 600.f, 100.f));
    if (!TestTrue(TEXT("Players spawned"), QB && LB))
    {
        DestroyTestWorld(World);
        return false;
    }
    AnnounceLine(Bus, Line);
    PlayCall->OpenPlayCall(FirstAndTen());
    PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human);
    PlayCall->CallPlay(TEXT("Defense_43Cover2"), EPSPlayCaller::Human);
    Overlay->AdvanceTime(0.f);
    TestEqual(TEXT("Cover 2: the back's zone star (the linebacker fits the run)"), Overlay->GetDefenseArt().Num(), 1);

    // The settings, the tier and the kicking game.
    TestNotNull(TEXT("DefenseIcons is a setting"), Settings->GetCatalog().FindSetting(UPSOverlayPlayArtSubsystem::DefenseIconsSettingId));
    TestNotNull(TEXT("StudyMode is a setting"), Settings->GetCatalog().FindSetting(UPSOverlayPlayArtSubsystem::StudyModeSettingId));
    TestFalse(TEXT("Study mode is off by default"), Overlay->IsSettingOn(UPSOverlayPlayArtSubsystem::StudyModeSettingId, false));
    Settings->SetValue(UPSOverlayPlayArtSubsystem::DefenseIconsSettingId, 0.f);
    Overlay->Refresh();
    TestEqual(TEXT("Icons turned off, none are drawn"), Overlay->GetDefenseArt().Num(), 0);
    TestTrue(TEXT("...while the route art stays"), Overlay->GetRouteArt().Num() > 0);
    Settings->SetValue(UPSOverlayPlayArtSubsystem::DefenseIconsSettingId, 1.f);
    Overlay->SetOverlayDetail(EPSOverlayDetail::Minimal);
    TestEqual(TEXT("A Minimal tier draws no icons"), Overlay->GetDefenseArt().Num(), 0);
    Overlay->SetOverlayDetail(EPSOverlayDetail::Full);
    TestEqual(TEXT("A Full tier does"), Overlay->GetDefenseArt().Num(), 1);
    PlayCall->CallPlay(TEXT("Defense_HandsTeam"), EPSPlayCaller::Human);
    Overlay->AdvanceTime(0.f);
    TestEqual(TEXT("A hands team draws none"), Overlay->GetDefenseArt().Num(), 0);
    PlayCall->CallPlay(TEXT("Defense_43Cover2"), EPSPlayCaller::Human);
    Overlay->AdvanceTime(0.f);
    TestEqual(TEXT("Back to Cover 2"), Overlay->GetDefenseArt().Num(), 1);

    // Who sees them: the defense's own, a spectator, and the offense only in study mode.
    TestTrue(TEXT("A spectator sees the icons"), Overlay->IsDefenseArtVisibleTo(nullptr));
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* P1 = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    APSPlayerController* P2 = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (TestTrue(TEXT("Two humans"), P1 && P2 && P1->TakeControlOf(QB) && P2->TakeControlOf(LB)))
    {
        TestTrue(TEXT("The defending player sees his call"), Overlay->IsDefenseArtVisibleTo(P2));
        TestFalse(TEXT("The quarterback's player doesn't"), Overlay->IsDefenseArtVisibleTo(P1));
        Settings->SetValue(UPSOverlayPlayArtSubsystem::StudyModeSettingId, 1.f);
        TestTrue(TEXT("...until he studies it in study mode"), Overlay->IsDefenseArtVisibleTo(P1));

        // Head to head -- the competitive context -- only the house rules decide (Epic 107).
        const bool bSession = Versus->ClaimSeat(P1, 0) == 0 && Versus->ClaimSeat(P2, 1) == 1
            && Versus->SelectTeam(0, EPSVersusTeam::Home) && Versus->SelectTeam(1, EPSVersusTeam::Away)
            && Versus->SetReady(0, true) && Versus->SetReady(1, true) && Versus->StartSession();
        if (TestTrue(TEXT("Head-to-head session"), bSession))
        {
            TestFalse(TEXT("One shared screen: the defense doesn't see its own either"), Overlay->IsDefenseArtVisibleTo(P2));
            FPSVersusRules Rules = Versus->GetRules();
            Rules.Screen = EPSVersusScreen::Split;
            Versus->SetRules(Rules);
            TestTrue(TEXT("Split screen: the defense sees its icons"), Overlay->IsDefenseArtVisibleTo(P2));
            TestFalse(TEXT("...and study mode doesn't show them to the offense"), Overlay->IsDefenseArtVisibleTo(P1));
            Rules.DefensiveIconsAudience = EPSVersusAudience::Everyone;
            Versus->SetRules(Rules);
            TestTrue(TEXT("House rules showing them to everyone show the offense too"), Overlay->IsDefenseArtVisibleTo(P1));
        }
    }

    DestroyTestWorld(World);
    return true;
}

#endif
