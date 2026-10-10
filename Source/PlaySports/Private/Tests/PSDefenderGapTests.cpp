// PSDefenderGapTests.cpp -- run defense as gap accounting (Epic 81)
//
// Tests covered:
//   1. The gap model: the shipped fronts load and are sound; gap spots follow the line (the
//      center is the lineman over the ball, an inline tight end extends the line); a front maps
//      each role's defenders left to right onto its gaps.
//   2. Front and call: the call takes defenders in coverage out of the fit, leaving their gaps
//      open; the outermost fitter on each side forces; an unknown front falls back to the
//      default; gap integrity goes on the bus when it changes.
//   3. Fits on a run: a lineman fills his gap with spill leverage, a linebacker flows with the
//      carrier, a blocked force player works outside across his blocker, and a fitter attacks
//      once the carrier comes to his gap or crosses the line.
//   4. Scrape exchange: when the gap the carrier heads for belongs to a blocked lineman, the
//      nearest free linebacker scrapes into it and the two swap gaps -- once.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSDataIngestion.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenderGapSubsystem.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSDefenderGapTests
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

    /** A pawn at Location under its side's AI controller (defense AIs bound to the bus). */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const FString& PlayerId, const FVector& Location, float Awareness = 100.f)
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
        Attributes.Awareness = Awareness;
        Attributes.Strength = 70.f;
        Attributes.Agility = 70.f;
        Attributes.Stamina = 100.f;
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

    /** Five offensive linemen on the line, 150 apart, centered on the ball (Y 0). */
    static TArray<APSPlayerPawn*> SpawnLine(UWorld* World)
    {
        TArray<APSPlayerPawn*> Line;
        for (int32 Index = 0; Index < 5; ++Index)
        {
            Line.Add(SpawnPlayer(World, EPlayerRole::OffensiveLineman, FString::Printf(TEXT("OL_%d"), Index), FVector(-50.f, (Index - 2) * 150.f, 100.f)));
        }
        return Line;
    }

    /** A 4-3 front as the lineup places it: linemen at Y -300, -100, 100, 300 (index 0-3) and
     *  linebackers at -400, 0, 400 (index 4-6). */
    static TArray<APSPlayerPawn*> SpawnFront(UWorld* World)
    {
        TArray<APSPlayerPawn*> Front;
        const float LinemanY[] = { -300.f, -100.f, 100.f, 300.f };
        for (int32 Index = 0; Index < 4; ++Index)
        {
            Front.Add(SpawnPlayer(World, EPlayerRole::DefensiveLineman, FString::Printf(TEXT("DL_%d"), Index), FVector(100.f, LinemanY[Index], 100.f)));
        }
        const float LinebackerY[] = { -400.f, 0.f, 400.f };
        for (int32 Index = 0; Index < 3; ++Index)
        {
            Front.Add(SpawnPlayer(World, EPlayerRole::Linebacker, FString::Printf(TEXT("LB_%d"), Index), FVector(450.f, LinebackerY[Index], 100.f)));
        }
        return Front;
    }

    static bool AllSpawned(const TArray<APSPlayerPawn*>& Pawns)
    {
        return !Pawns.Contains(nullptr);
    }

    static UPSDefenderAIComponent* AIOf(APSPlayerPawn* Pawn)
    {
        const APSDefenseController* Controller = Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr;
        return Controller ? Controller->GetDefenderAI() : nullptr;
    }

    static void Assign(APSPlayerPawn* Pawn, EPSDefensiveAssignmentType Assignment)
    {
        if (APSDefenseController* Controller = Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr)
        {
            Controller->SetAssignment(Assignment);
        }
    }

    /** Snaps, then (a snap outside a call window hands out CPU plays first) gives the front
     *  its assignments: linemen rush, linebackers fit. */
    static void SnapAndAssign(UPSTelemetryBus* Bus, const TArray<APSPlayerPawn*>& Front)
    {
        FPSTelemetrySnapEvent Event;
        Bus->PublishSnap(Event);
        for (APSPlayerPawn* Defender : Front)
        {
            Assign(Defender, Defender->GetAttributes().Role == EPlayerRole::DefensiveLineman ? EPSDefensiveAssignmentType::PassRush : EPSDefensiveAssignmentType::RunFit);
        }
    }

    static bool PointsToward(const FVector& Direction, const FVector& From, const FVector& To)
    {
        FVector Expected = To - From;
        Expected.Z = 0.f;
        return Direction.Equals(Expected.GetSafeNormal(), 0.01f);
    }

    static int32 CountIntegrityEvents(const UPSTelemetryBus* Bus, FPSTelemetryEvent* OutLast = nullptr)
    {
        int32 Count = 0;
        for (const FPSTelemetryEvent& Event : Bus->GetEventHistory())
        {
            if (Event.EventType == EPSTelemetryEventType::GapIntegrity)
            {
                ++Count;
                if (OutLast)
                {
                    *OutLast = Event;
                }
            }
        }
        return Count;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The gap model
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenderGapModelTest,
    "PlaySports.AI.RunFit.GapModel",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenderGapModelTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenderGapTests;

    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSRunFitCatalog Catalog;
    if (!TestTrue(TEXT("The shipped fronts load"), Ingestion->LoadRunFitsFromJson(UPSDefenderGapSubsystem::GetDefaultCatalogPath(), Catalog)))
    {
        return false;
    }
    const TArray<FString> Problems = PSDefenderGaps::ValidateCatalog(Catalog);
    TestEqual(TEXT("...and are sound"), Problems.Num(), 0);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    TestEqual(TEXT("B is one gap out from the ball"), PSDefenderGaps::GetGapLevel(EPSRunGap::BRight), 1);
    TestTrue(TEXT("...left and right"), PSDefenderGaps::IsLeftGap(EPSRunGap::BLeft) && !PSDefenderGaps::IsLeftGap(EPSRunGap::BRight));
    TestEqual(TEXT("Three gaps out on the left is D"), PSDefenderGaps::MakeGap(3, true), EPSRunGap::DLeft);

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("World"), World))
    {
        return false;
    }
    TArray<APSPlayerPawn*> Line = SpawnLine(World);
    APSPlayerPawn* TightEnd = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE"), FVector(-50.f, 700.f, 100.f));
    APSPlayerPawn* Receiver = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(-50.f, -500.f, 100.f));
    TArray<APSPlayerPawn*> Front = SpawnFront(World);
    if (!TestTrue(TEXT("Everyone spawned"), AllSpawned(Line) && AllSpawned(Front) && TightEnd && Receiver))
    {
        DestroyTestWorld(World);
        return false;
    }
    TArray<APSPlayerPawn*> Field = Line;
    Field.Append(Front);
    Field.Add(TightEnd);
    Field.Add(Receiver);

    // The line centered on the ball, the tight end split out: every gap 150 wide.
    TArray<FVector> Spots;
    TestTrue(TEXT("The line can be read"), PSDefenderGaps::ComputeGapSpots(Field, 0.f, Catalog, Spots));
    const float Expected[] = { 0.f, -525.f, -375.f, -225.f, -75.f, 75.f, 225.f, 375.f, 525.f };
    for (int32 Index = 1; Index < 9; ++Index)
    {
        TestTrue(FString::Printf(TEXT("Gap %d sits between its linemen"), Index), FMath::IsNearlyEqual(Spots[Index].Y, Expected[Index]));
    }
    TestTrue(TEXT("...on the line"), FMath::IsNearlyEqual(Spots[static_cast<int32>(EPSRunGap::ALeft)].X, -50.f));

    // An inline tight end extends the line: C is inside him, D outside.
    TightEnd->SetActorLocation(FVector(-50.f, 500.f, 100.f));
    PSDefenderGaps::ComputeGapSpots(Field, 0.f, Catalog, Spots);
    TestTrue(TEXT("An inline tight end: C between the tackle and him"), FMath::IsNearlyEqual(Spots[static_cast<int32>(EPSRunGap::CRight)].Y, 400.f));
    TestTrue(TEXT("...D outside him"), FMath::IsNearlyEqual(Spots[static_cast<int32>(EPSRunGap::DRight)].Y, 575.f));
    TestTrue(TEXT("...the other side unchanged"), FMath::IsNearlyEqual(Spots[static_cast<int32>(EPSRunGap::CLeft)].Y, -375.f));

    // The center is the lineman over the ball.
    PSDefenderGaps::ComputeGapSpots(Field, 150.f, Catalog, Spots);
    TestTrue(TEXT("The ball over the right guard makes him the center"),
        FMath::IsNearlyEqual(Spots[static_cast<int32>(EPSRunGap::ARight)].Y, 225.f) && FMath::IsNearlyEqual(Spots[static_cast<int32>(EPSRunGap::ALeft)].Y, 75.f));

    TArray<APSPlayerPawn*> NoLine = Front;
    TestFalse(TEXT("No linemen, no gaps"), PSDefenderGaps::ComputeGapSpots(NoLine, 0.f, Catalog, Spots));

    // The 4-3 front, its defenders listed out of order.
    const FPSRunFitFront* FourThree = Catalog.Fronts.FindByPredicate([](const FPSRunFitFront& Candidate) { return Candidate.Front == TEXT("4-3"); });
    if (!TestNotNull(TEXT("The 4-3 front"), FourThree))
    {
        DestroyTestWorld(World);
        return false;
    }
    TArray<APSPlayerPawn*> Shuffled = { Front[6], Front[2], Front[4], Front[0], Front[5], Front[3], Front[1] };
    TArray<EPSRunGap> Gaps;
    PSDefenderGaps::AssignFrontGaps(*FourThree, Shuffled, Gaps);
    TestEqual(TEXT("Leftmost lineman: C left"), Gaps[3], EPSRunGap::CLeft);
    TestEqual(TEXT("Next lineman: A left"), Gaps[6], EPSRunGap::ALeft);
    TestEqual(TEXT("Next lineman: A right"), Gaps[1], EPSRunGap::ARight);
    TestEqual(TEXT("Rightmost lineman: C right"), Gaps[5], EPSRunGap::CRight);
    TestEqual(TEXT("Leftmost linebacker: B left"), Gaps[2], EPSRunGap::BLeft);
    TestEqual(TEXT("Middle linebacker: B right"), Gaps[4], EPSRunGap::BRight);
    TestEqual(TEXT("Rightmost linebacker: D right"), Gaps[0], EPSRunGap::DRight);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Front and call, and gap integrity on the bus
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenderGapCallTest,
    "PlaySports.AI.RunFit.FrontAndCall",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenderGapCallTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenderGapTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSDefenderGapSubsystem* Gaps = World ? World->GetSubsystem<UPSDefenderGapSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Gap subsystem"), Gaps))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    TArray<APSPlayerPawn*> Line = SpawnLine(World);
    TArray<APSPlayerPawn*> Front = SpawnFront(World);
    if (!TestTrue(TEXT("Everyone spawned"), AllSpawned(Line) && AllSpawned(Front)))
    {
        DestroyTestWorld(World);
        return false;
    }

    // The call drops the middle linebacker into a zone.
    SnapAndAssign(Bus, Front);
    Assign(Front[5], EPSDefensiveAssignmentType::ZoneCoverage);
    Gaps->AssignGaps(TEXT("4-3"));
    TestEqual(TEXT("The 4-3"), Gaps->GetFront(), FString(TEXT("4-3")));
    TestEqual(TEXT("The left end has C left"), Gaps->GetFit(Front[0]).Gap, EPSRunGap::CLeft);
    TestEqual(TEXT("...and forces: he's the outermost fitter on his side"), Gaps->GetFit(Front[0]).Technique, EPSFitTechnique::Box);
    TestEqual(TEXT("The tackle beside him spills"), Gaps->GetFit(Front[1]).Technique, EPSFitTechnique::Spill);
    TestTrue(TEXT("Linemen fit at the line"), Gaps->GetFit(Front[1]).bFirstLevel && !Gaps->GetFit(Front[4]).bFirstLevel);
    TestEqual(TEXT("On the right the D-gap linebacker forces"), Gaps->GetFit(Front[6]).Technique, EPSFitTechnique::Box);
    TestEqual(TEXT("...so the right end spills"), Gaps->GetFit(Front[3]).Technique, EPSFitTechnique::Spill);
    TestEqual(TEXT("The linebacker in coverage has no gap"), Gaps->GetFit(Front[5]).Gap, EPSRunGap::None);
    TestNull(TEXT("...so his B right has no owner"), Gaps->GetGapOwner(EPSRunGap::BRight));
    TestTrue(TEXT("The C-left owner is the left end"), Gaps->GetGapOwner(EPSRunGap::CLeft) == Front[0]);

    // Integrity: published on the first update, then only on a change.
    const int32 EventsBefore = CountIntegrityEvents(Bus);
    Gaps->UpdateFits(0.1f);
    FPSTelemetryEvent Last;
    TestEqual(TEXT("The first update publishes the gaps' integrity"), CountIntegrityEvents(Bus, &Last) - EventsBefore, 1);
    TestTrue(TEXT("...with B right open"), Last.Description.Contains(TEXT("BRight")));
    const TArray<FPSGapStatus>& Integrity = Gaps->GetIntegrity();
    const FPSGapStatus* BRight = Integrity.FindByPredicate([](const FPSGapStatus& Status) { return Status.Gap == EPSRunGap::BRight; });
    const FPSGapStatus* CLeft = Integrity.FindByPredicate([](const FPSGapStatus& Status) { return Status.Gap == EPSRunGap::CLeft; });
    TestTrue(TEXT("B right: no owner, open"), BRight && !BRight->Owner.IsValid() && !BRight->bFilled);
    TestTrue(TEXT("C left: its end is lined up on it"), CLeft && CLeft->bFilled);
    const int32 OpenBefore = Gaps->GetOpenGapCount();
    Gaps->UpdateFits(0.1f);
    TestEqual(TEXT("No change, no new event"), CountIntegrityEvents(Bus) - EventsBefore, 1);
    Front[4]->SetActorLocation(FVector(450.f, -225.f, 100.f));
    Gaps->UpdateFits(0.1f);
    TestEqual(TEXT("The B-left linebacker steps into his gap: a new event"), CountIntegrityEvents(Bus) - EventsBefore, 2);
    TestEqual(TEXT("...one gap fewer open"), Gaps->GetOpenGapCount(), OpenBefore - 1);

    // A front the data doesn't list plays the default.
    Gaps->AssignGaps(TEXT("5-2"));
    TestEqual(TEXT("An unknown front plays the default"), Gaps->GetFront(), FString(TEXT("4-3")));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Fits on a run: spill, flow, force, attack
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenderFitTest,
    "PlaySports.AI.RunFit.FitAndFlow",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenderFitTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenderGapTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSDefenderGapSubsystem* Gaps = World ? World->GetSubsystem<UPSDefenderGapSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Gap subsystem"), Gaps))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    TArray<APSPlayerPawn*> Line = SpawnLine(World);
    TArray<APSPlayerPawn*> Front = SpawnFront(World);
    // The back lines up wide right, away from the inside gaps.
    APSPlayerPawn* RunningBack = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-500.f, 600.f, 100.f));
    if (!TestTrue(TEXT("Everyone spawned"), AllSpawned(Line) && AllSpawned(Front) && RunningBack))
    {
        DestroyTestWorld(World);
        return false;
    }
    const FPSRunFitCatalog& Data = Gaps->GetCatalog();

    SnapAndAssign(Bus, Front);
    Gaps->AssignGaps(TEXT("4-3"));
    for (APSPlayerPawn* Defender : Front)
    {
        AIOf(Defender)->TickAI(0.05f);
    }
    // The hand-off: a run read.
    RunningBack->GainPossession();

    // The A-left tackle, knocked off the line, fills his gap, spilling: on its inside half.
    APSPlayerPawn* Tackle = Front[1];
    Tackle->SetActorLocation(FVector(400.f, -100.f, 100.f));
    UPSDefenderAIComponent* TackleAI = AIOf(Tackle);
    TackleAI->TickAI(0.05f);
    TestEqual(TEXT("On a run the tackle fits his gap"), TackleAI->GetAction(), EPSDefenderAction::Fit);
    const FVector TackleTarget(Data.FitDepth, -75.f + Data.LeverageOffset, 100.f);
    TestTrue(TEXT("...A left, at the line, on its inside half"), PointsToward(TackleAI->GetDesiredDirection(), Tackle->GetActorLocation(), TackleTarget));

    // The B-left linebacker flows with the carrier.
    APSPlayerPawn* Linebacker = Front[4];
    UPSDefenderAIComponent* LinebackerAI = AIOf(Linebacker);
    LinebackerAI->TickAI(0.05f);
    const float GapY = -225.f + Data.LeverageOffset;
    TestEqual(TEXT("The linebacker fits too"), LinebackerAI->GetAction(), EPSDefenderAction::Fit);
    TestTrue(TEXT("...at the second level, drawn toward the carrier"), PointsToward(LinebackerAI->GetDesiredDirection(), Linebacker->GetActorLocation(),
        FVector(Data.SecondLevelDepth, FMath::Lerp(GapY, 600.f, Data.FlowWeight), 100.f)));
    RunningBack->SetActorLocation(FVector(-500.f, 250.f, 100.f));
    LinebackerAI->TickAI(0.05f);
    TestTrue(TEXT("The carrier cuts back: the linebacker flows with him"), PointsToward(LinebackerAI->GetDesiredDirection(), Linebacker->GetActorLocation(),
        FVector(Data.SecondLevelDepth, FMath::Lerp(GapY, 250.f, Data.FlowWeight), 100.f)));

    // The left end, blocked, works outside across his blocker: he's the force player.
    APSPlayerPawn* End = Front[0];
    End->bIsEngaged = true;
    End->EngagedOpponent = Line[0];
    Line[0]->bIsEngaged = true;
    Line[0]->EngagedOpponent = End;
    UPSDefenderAIComponent* EndAI = AIOf(End);
    EndAI->TickAI(0.05f);
    TestEqual(TEXT("The blocked end still fits"), EndAI->GetAction(), EPSDefenderAction::Fit);
    TestTrue(TEXT("...working outside, across the blocker's face"), EndAI->GetDesiredDirection().Y < 0.f && FMath::IsNearlyZero(EndAI->GetDesiredDirection().X));

    // The carrier comes to the B-left gap: its linebacker attacks.
    RunningBack->SetActorLocation(FVector(-500.f, -230.f, 100.f));
    LinebackerAI->TickAI(0.05f);
    TestEqual(TEXT("The carrier at his gap: the linebacker attacks"), LinebackerAI->GetAction(), EPSDefenderAction::Pursue);
    RunningBack->SetActorLocation(FVector(-500.f, 250.f, 100.f));
    LinebackerAI->TickAI(0.05f);
    TestEqual(TEXT("...and stays on him"), LinebackerAI->GetAction(), EPSDefenderAction::Pursue);

    // Past the line, everyone attacks.
    RunningBack->SetActorLocation(FVector(100.f, 250.f, 100.f));
    TackleAI->TickAI(0.05f);
    TestEqual(TEXT("The carrier past the line: the tackle pursues"), TackleAI->GetAction(), EPSDefenderAction::Pursue);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Scrape exchange
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenderScrapeExchangeTest,
    "PlaySports.AI.RunFit.ScrapeExchange",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenderScrapeExchangeTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenderGapTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSDefenderGapSubsystem* Gaps = World ? World->GetSubsystem<UPSDefenderGapSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Gap subsystem"), Gaps))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    TArray<APSPlayerPawn*> Line = SpawnLine(World);
    TArray<APSPlayerPawn*> Front = SpawnFront(World);
    APSPlayerPawn* RunningBack = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-500.f, -60.f, 100.f));
    if (!TestTrue(TEXT("Everyone spawned"), AllSpawned(Line) && AllSpawned(Front) && RunningBack))
    {
        DestroyTestWorld(World);
        return false;
    }

    SnapAndAssign(Bus, Front);
    Gaps->AssignGaps(TEXT("4-3"));
    RunningBack->GainPossession();

    // The run heads for A left, whose tackle is free: no exchange.
    Gaps->UpdateFits(0.1f);
    TestTrue(TEXT("A free owner fills his own gap"), Gaps->GetGapOwner(EPSRunGap::ALeft) == Front[1] && !Gaps->GetFit(Front[1]).bExchanged);

    // The run heads for A right, whose tackle is blocked.
    APSPlayerPawn* Tackle = Front[2];
    Tackle->bIsEngaged = true;
    Tackle->EngagedOpponent = Line[3];
    Line[3]->bIsEngaged = true;
    Line[3]->EngagedOpponent = Tackle;
    RunningBack->SetActorLocation(FVector(-500.f, 60.f, 100.f));
    Gaps->UpdateFits(0.1f);
    APSPlayerPawn* Mike = Front[5];
    TestTrue(TEXT("The nearest free linebacker scrapes into the blocked tackle's gap"), Gaps->GetGapOwner(EPSRunGap::ARight) == Mike);
    TestTrue(TEXT("...exchanging: the tackle takes his B right"), Gaps->GetFit(Tackle).Gap == EPSRunGap::BRight && Gaps->GetFit(Tackle).bExchanged);
    FPSTelemetryEvent Last;
    CountIntegrityEvents(Bus, &Last);
    TestTrue(TEXT("The exchange goes on the bus"), Last.Description.Contains(TEXT("exchanges 1")));

    // Once per gap: the tackle, still blocked, doesn't take it back.
    Gaps->UpdateFits(0.1f);
    TestTrue(TEXT("No exchange back"), Gaps->GetGapOwner(EPSRunGap::ARight) == Mike);

    // The linebacker now plays A right: on the run he fits it.
    RunningBack->SetActorLocation(FVector(-500.f, 300.f, 100.f));
    UPSDefenderAIComponent* MikeAI = AIOf(Mike);
    MikeAI->TickAI(0.05f);
    TestEqual(TEXT("The scraping linebacker fits his new gap"), MikeAI->GetAction(), EPSDefenderAction::Fit);
    FVector Target;
    TestTrue(TEXT("...A right"), Gaps->GetFitTarget(Mike, RunningBack, Target) && FMath::Abs(Target.Y - FMath::Lerp(75.f - Gaps->GetCatalog().LeverageOffset, 300.f, Gaps->GetCatalog().FlowWeight)) < 1.f);

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
