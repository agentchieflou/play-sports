// PSPreSnapTests.cpp -- Epic 66 (offensive pre-snap interaction)
//
// Tests covered:
//   1. Audibles: only to another play of the same formation, while the window is open; the
//      call changes at the play-call authority, and the old call's changes go with it.
//   2. Hot routes: each alignment has its own allowed routes; a blocker can't be hot-routed;
//      at the snap the hot route replaces the called one.
//   3. Motion: the man in motion crosses the formation; in man coverage the defender over him
//      travels with him and the motion is announced with the man indicator; in zone nobody
//      travels; the snap ends it.
//   4. Protection: backs kept in and tight ends released, run that way at the snap; a slide
//      leaves the backside rusher over, and a back kept in picks him up.
//   5. The CPU quarterback's read: a run into a blitz look becomes a pass in the formation,
//      the back stays in and the slot goes in motion; a raw quarterback reads nothing.
//   6. The human's pre-snap buttons on a real player controller.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSPreSnapInputComponent.h"
#include "PSPreSnapSubsystem.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPreSnapTests
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

    /** A pawn at Location under its side's AI controller; offense AIs are bound to the bus. */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location, float Awareness = 0.f)
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
        Attributes.Awareness = Awareness;
        Pawn->InitializePlayer(Attributes);

        if (Pawn->TeamSide == EPSTeamSide::Defense)
        {
            if (APSDefenseController* AI = World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
            {
                AI->Possess(Pawn);
            }
        }
        else if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
            AI->GetSkillAI()->BindToBus();
        }
        return Pawn;
    }

    static FPSSituationContext FirstAndTen()
    {
        FPSSituationContext Situation;
        Situation.Down = 1;
        Situation.Distance = 10;
        Situation.YardLine = 20;
        return Situation;
    }

    static void Snap(UPSTelemetryBus* Bus)
    {
        FPSTelemetrySnapEvent Event;
        Event.Down = 1;
        Event.Distance = 10;
        Event.YardLine = 20;
        Event.LineOfScrimmage = FVector::ZeroVector;
        Bus->PublishSnap(Event);
    }

    static const APSOffenseController* AIOf(const APSPlayerPawn* Pawn)
    {
        return Pawn ? Cast<APSOffenseController>(Pawn->GetController()) : nullptr;
    }

    /** The first route waypoint the pawn's AI was handed, or zero with none. */
    static FVector FirstWaypoint(const APSPlayerPawn* Pawn)
    {
        const APSOffenseController* Controller = AIOf(Pawn);
        return Controller && Controller->GetRouteWaypointCount() > 0 ? Controller->GetCurrentTargetLocation() : FVector::ZeroVector;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Audibles stay in the formation
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPreSnapAudibleTest,
    "PlaySports.PreSnap.AudibleInFormation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPreSnapAudibleTest::RunTest(const FString& Parameters)
{
    using namespace PSPreSnapTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSPreSnapSubsystem* PreSnap = World ? World->GetSubsystem<UPSPreSnapSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Game worlds have a pre-snap subsystem"), PreSnap))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-100.f, 0.f, 100.f));
    APSPlayerPawn* WR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(-50.f, 900.f, 100.f));

    TArray<FPSTelemetryPreSnapEvent> Changes;
    const FDelegateHandle Handle = Bus->OnPreSnapMC.AddLambda([&Changes](const FPSTelemetryPreSnapEvent& Event) { Changes.Add(Event); });

    TestFalse(TEXT("Nothing to change before a call window"), PreSnap->IsAdjustable());
    TestFalse(TEXT("...so no audible"), PreSnap->AudibleToNext(true));

    PlayCall->OpenPlayCall(FirstAndTen());
    TestFalse(TEXT("Nothing to change before the offense calls"), PreSnap->IsAdjustable());
    TestTrue(TEXT("The human calls Slant-Flat"), PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human));
    TestTrue(TEXT("Now the call can change"), PreSnap->IsAdjustable());

    const TArray<FPSPlayDefinition> Audibles = PreSnap->GetAudibles();
    if (TestEqual(TEXT("Trips Right has one other play"), Audibles.Num(), 1))
    {
        TestEqual(TEXT("...the stretch"), Audibles[0].PlayId, FName(TEXT("Offense_TripsRightStretch")));
    }
    TestFalse(TEXT("No audible to another formation"), PreSnap->Audible(TEXT("Offense_FourVerts"), true));
    TestFalse(TEXT("...or to a defensive play"), PreSnap->Audible(TEXT("Defense_43Cover2"), true));
    TestEqual(TEXT("The call is unchanged"), PlayCall->GetCall(true).PlayId, FName(TEXT("Offense_SlantFlat")));

    TestTrue(TEXT("A hot route before the audible"), PreSnap->HotRoute(WR, TEXT("Go"), true));
    TestTrue(TEXT("Audible to the next play in the formation"), PreSnap->AudibleToNext(true));
    TestEqual(TEXT("The call authority has the new play"), PlayCall->GetCall(true).PlayId, FName(TEXT("Offense_TripsRightStretch")));
    TestTrue(TEXT("...as the human's call"), PlayCall->GetCall(true).Caller == EPSPlayCaller::Human);
    TestNull(TEXT("The old call's hot route went with it"), PreSnap->FindAdjustment(WR));
    if (TestTrue(TEXT("The audible is announced"), Changes.Num() > 0))
    {
        TestTrue(TEXT("...as an audible"), Changes.Last().Action == EPSPreSnapAction::Audible);
        TestEqual(TEXT("...to the stretch"), Changes.Last().Detail, FName(TEXT("Offense_TripsRightStretch")));
        TestTrue(TEXT("...by the human"), Changes.Last().bHumanCall);
    }

    TestTrue(TEXT("Audibling again wraps round the formation"), PreSnap->AudibleToNext(true));
    TestEqual(TEXT("...back to Slant-Flat"), PlayCall->GetCall(true).PlayId, FName(TEXT("Offense_SlantFlat")));

    Snap(Bus);
    TestFalse(TEXT("The snap closes the window"), PreSnap->IsAdjustable());
    TestFalse(TEXT("...and no audible after it"), PreSnap->AudibleToNext(true));

    Bus->OnPreSnapMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Hot routes by alignment, run at the snap
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPreSnapHotRouteTest,
    "PlaySports.PreSnap.HotRoutesByAlignment",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPreSnapHotRouteTest::RunTest(const FString& Parameters)
{
    using namespace PSPreSnapTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSPreSnapSubsystem* PreSnap = World ? World->GetSubsystem<UPSPreSnapSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Pre-snap subsystem"), PreSnap))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-100.f, 0.f, 100.f));
    APSPlayerPawn* Wide = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_WIDE"), FVector(-50.f, 900.f, 100.f));
    APSPlayerPawn* Slot = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_SLOT"), FVector(-50.f, -500.f, 100.f));
    APSPlayerPawn* TE = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE"), FVector(-50.f, 450.f, 100.f));
    APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-500.f, 0.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Wide WR"), Wide) || !TestNotNull(TEXT("Slot WR"), Slot) || !TestNotNull(TEXT("TE"), TE) || !TestNotNull(TEXT("RB"), RB))
    {
        DestroyTestWorld(World);
        return false;
    }

    TestTrue(TEXT("A receiver split wide is Wide"), PreSnap->GetAlignment(Wide) == EPSReceiverAlignment::Wide);
    TestTrue(TEXT("...one inside the slot split is Slot"), PreSnap->GetAlignment(Slot) == EPSReceiverAlignment::Slot);
    TestTrue(TEXT("A tight end is Tight"), PreSnap->GetAlignment(TE) == EPSReceiverAlignment::Tight);
    TestTrue(TEXT("A back is Backfield"), PreSnap->GetAlignment(RB) == EPSReceiverAlignment::Backfield);
    TestTrue(TEXT("A wide receiver may go deep"), PreSnap->GetAllowedHotRoutes(Wide).Contains(FName(TEXT("Go"))));
    TestFalse(TEXT("...but not run a back's flat"), PreSnap->GetAllowedHotRoutes(Wide).Contains(FName(TEXT("Flat"))));
    TestEqual(TEXT("The quarterback can't be hot-routed"), PreSnap->GetAllowedHotRoutes(QB).Num(), 0);

    TArray<FPSTelemetryPreSnapEvent> Changes;
    const FDelegateHandle Handle = Bus->OnPreSnapMC.AddLambda([&Changes](const FPSTelemetryPreSnapEvent& Event) { Changes.Add(Event); });

    PlayCall->OpenPlayCall(FirstAndTen());
    TestTrue(TEXT("Slant-Flat is called"), PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human));
    TestTrue(TEXT("The defense calls Cover 2"), PlayCall->CallPlay(TEXT("Defense_43Cover2"), EPSPlayCaller::CPU));

    TestTrue(TEXT("The wide receiver's slant becomes a go"), PreSnap->HotRoute(Wide, TEXT("Go"), true));
    TestFalse(TEXT("A route outside his set is refused"), PreSnap->HotRoute(Wide, TEXT("Flat"), true));
    TestFalse(TEXT("An unknown route is refused"), PreSnap->HotRoute(Wide, TEXT("Wheel"), true));
    TestFalse(TEXT("A blocking tight end can't be hot-routed"), PreSnap->HotRoute(TE, TEXT("Curl"), true));
    TestTrue(TEXT("The back's flat becomes a curl"), PreSnap->HotRoute(RB, TEXT("Curl"), true));
    TestTrue(TEXT("Cycling the slot moves him to his next route"), PreSnap->CycleHotRoute(Slot, true));
    const FPSPreSnapPlayerAdjustment* SlotChange = PreSnap->FindAdjustment(Slot);
    TestTrue(TEXT("...the out, after the slant he was called on"), SlotChange && SlotChange->HotRouteId == FName(TEXT("Out")));
    if (TestEqual(TEXT("Each hot route is announced"), Changes.Num(), 3))
    {
        TestTrue(TEXT("...as a hot route"), Changes[0].Action == EPSPreSnapAction::HotRoute);
        TestEqual(TEXT("...naming the receiver"), Changes[0].PlayerName, FString(TEXT("WR_WIDE")));
        TestEqual(TEXT("...and the route"), Changes[0].Detail, FName(TEXT("Go")));
    }

    Snap(Bus);
    TestTrue(TEXT("At the snap the wide receiver runs the go from his split"), FirstWaypoint(Wide).Equals(FVector(1500.f, 900.f, 0.f)));
    TestTrue(TEXT("The slot runs the out, mirrored on his side"), FirstWaypoint(Slot).Equals(FVector(600.f, -500.f, 0.f)));
    const APSOffenseController* SlotAI = AIOf(Slot);
    TestTrue(TEXT("...two legs"), SlotAI && SlotAI->GetRouteWaypointCount() == 2);
    TestTrue(TEXT("The back runs the curl"), FirstWaypoint(RB).Equals(FVector(800.f, 0.f, 0.f)));
    const APSOffenseController* TightEndAI = AIOf(TE);
    TestTrue(TEXT("The tight end blocks, as called"), TightEndAI && TightEndAI->GetRouteWaypointCount() == 0);

    Bus->OnPreSnapMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Motion and the man indicator
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPreSnapMotionTest,
    "PlaySports.PreSnap.MotionShowsMan",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPreSnapMotionTest::RunTest(const FString& Parameters)
{
    using namespace PSPreSnapTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSPreSnapSubsystem* PreSnap = World ? World->GetSubsystem<UPSPreSnapSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Pre-snap subsystem"), PreSnap))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-100.f, 0.f, 100.f));
    APSPlayerPawn* WR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(-50.f, 900.f, 100.f));
    APSPlayerPawn* Corner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_OVER"), FVector(900.f, 1000.f, 100.f));
    APSPlayerPawn* OtherCorner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB_AWAY"), FVector(900.f, -900.f, 100.f));
    if (!TestNotNull(TEXT("WR"), WR) || !TestNotNull(TEXT("Corner"), Corner) || !TestNotNull(TEXT("Other corner"), OtherCorner))
    {
        DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryPreSnapEvent> Changes;
    const FDelegateHandle Handle = Bus->OnPreSnapMC.AddLambda([&Changes](const FPSTelemetryPreSnapEvent& Event) { Changes.Add(Event); });

    // Man coverage: the corner over him goes with him.
    PlayCall->OpenPlayCall(FirstAndTen());
    PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human);
    PlayCall->CallPlay(TEXT("Defense_NickelManFree"), EPSPlayCaller::CPU);
    TestTrue(TEXT("The receiver goes in motion"), PreSnap->StartMotion(WR, true));
    TestTrue(TEXT("...and is moving"), PreSnap->IsMotionActive() && PreSnap->GetMotionPlayer() == WR);
    TestFalse(TEXT("One man in motion at a time"), PreSnap->StartMotion(WR, true));
    TestTrue(TEXT("In man coverage the corner over him travels"), PreSnap->GetTravellingDefender() == Corner);
    if (TestEqual(TEXT("The motion is announced"), Changes.Num(), 1))
    {
        TestTrue(TEXT("...as a motion"), Changes[0].Action == EPSPreSnapAction::Motion);
        TestTrue(TEXT("...with the man indicator"), Changes[0].bManIndicator);
        TestEqual(TEXT("...naming the corner who travelled"), Changes[0].DefenderName, FString(TEXT("DB_OVER")));
    }

    PreSnap->TickMotion(0.1f);
    const FVector MoverInput = WR->ConsumeMovementInputVector();
    const FVector CornerInput = Corner->ConsumeMovementInputVector();
    TestTrue(TEXT("He runs across the formation"), MoverInput.Y < 0.f && FMath::IsNearlyZero(MoverInput.X, 0.01f));
    TestTrue(TEXT("...and the corner goes with him"), CornerInput.Y < 0.f);
    TestTrue(TEXT("The other corner stays home"), OtherCorner->ConsumeMovementInputVector().IsNearlyZero());

    WR->SetActorLocation(FVector(-50.f, -300.f, 100.f));
    PreSnap->TickMotion(0.1f);
    TestFalse(TEXT("On the other side of the ball the motion is over"), PreSnap->IsMotionActive());
    TestTrue(TEXT("...though the corner keeps going until he is over him"), Corner->ConsumeMovementInputVector().Y < 0.f);

    Snap(Bus);
    PreSnap->TickMotion(0.1f);
    TestTrue(TEXT("The snap stops the corner's pre-snap travel"), Corner->ConsumeMovementInputVector().IsNearlyZero());
    TestTrue(TEXT("The motion man's route starts from where the motion took him"), FirstWaypoint(WR).Equals(FVector(300.f, -300.f, 0.f)));

    // Zone: nobody travels.
    PlayCall->OpenPlayCall(FirstAndTen());
    PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human);
    PlayCall->CallPlay(TEXT("Defense_43Cover2"), EPSPlayCaller::CPU);
    WR->SetActorLocation(FVector(-50.f, -900.f, 100.f));
    OtherCorner->SetActorLocation(FVector(900.f, -900.f, 100.f));
    TestTrue(TEXT("Against zone he goes in motion too"), PreSnap->StartMotion(WR, false));
    TestNull(TEXT("...but nobody travels"), PreSnap->GetTravellingDefender());
    TestTrue(TEXT("...so no man indicator"), Changes.Num() == 2 && !Changes[1].bManIndicator && Changes[1].DefenderName.IsEmpty() && !Changes[1].bHumanCall);

    Bus->OnPreSnapMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Protection: kept in, released, slid
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPreSnapProtectionTest,
    "PlaySports.PreSnap.ProtectionCalls",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPreSnapProtectionTest::RunTest(const FString& Parameters)
{
    using namespace PSPreSnapTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSPreSnapSubsystem* PreSnap = World ? World->GetSubsystem<UPSPreSnapSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Pre-snap subsystem"), PreSnap))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-200.f, 0.f, 100.f));
    APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-500.f, 0.f, 100.f));
    APSPlayerPawn* TE = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE"), FVector(-50.f, 450.f, 100.f));
    APSPlayerPawn* WR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(-50.f, 900.f, 100.f));
    APSPlayerPawn* LeftTackle = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL_L"), FVector(-50.f, -75.f, 100.f));
    APSPlayerPawn* RightTackle = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL_R"), FVector(-50.f, 75.f, 100.f));
    APSPlayerPawn* LeftEnd = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL_L"), FVector(100.f, -300.f, 100.f));
    APSPlayerPawn* Nose = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL_N"), FVector(100.f, 0.f, 100.f));
    APSPlayerPawn* RightEnd = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL_R"), FVector(100.f, 300.f, 100.f));
    if (!TestNotNull(TEXT("RB"), RB) || !TestNotNull(TEXT("TE"), TE) || !TestNotNull(TEXT("WR"), WR) || !TestNotNull(TEXT("OL_L"), LeftTackle)
        || !TestNotNull(TEXT("OL_R"), RightTackle) || !TestNotNull(TEXT("DL_L"), LeftEnd) || !TestNotNull(TEXT("DL_N"), Nose) || !TestNotNull(TEXT("DL_R"), RightEnd))
    {
        DestroyTestWorld(World);
        return false;
    }

    // The line's pairing: two blockers, three rushers.
    const TArray<APSPlayerPawn*> Line = { LeftTackle, RightTackle };
    const TArray<APSPlayerPawn*> Rushers = { LeftEnd, Nose, RightEnd };
    auto Unblocked = [&Rushers](const TArray<TPair<APSPlayerPawn*, APSPlayerPawn*>>& Pairs)
    {
        TArray<APSPlayerPawn*> Free = Rushers;
        for (const TPair<APSPlayerPawn*, APSPlayerPawn*>& Pair : Pairs)
        {
            Free.Remove(Pair.Value);
        }
        return Free;
    };
    const TArray<APSPlayerPawn*> NoSlideFree = Unblocked(UPSPreSnapSubsystem::ComputeBlockingPairs(Line, Rushers, FVector::ZeroVector));
    TestTrue(TEXT("With no slide the left end is left over"), NoSlideFree.Num() == 1 && NoSlideFree[0] == LeftEnd);

    PlayCall->OpenPlayCall(FirstAndTen());
    TestTrue(TEXT("Slant-Flat is called"), PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human));
    TestTrue(TEXT("The defense calls Cover 2"), PlayCall->CallPlay(TEXT("Defense_43Cover2"), EPSPlayCaller::CPU));

    TestTrue(TEXT("The back is kept in"), PreSnap->SetProtection(RB, EPSProtectionCall::Block, true));
    TestFalse(TEXT("A blocking tight end can't be kept in"), PreSnap->SetProtection(TE, EPSProtectionCall::Block, true));
    TestTrue(TEXT("...but can be released"), PreSnap->SetProtection(TE, EPSProtectionCall::Release, true));
    TestFalse(TEXT("A wide receiver has no protection call"), PreSnap->SetProtection(WR, EPSProtectionCall::Block, true));
    TestTrue(TEXT("Toggling the back puts him back as called"), PreSnap->ToggleProtection(RB, true));
    TestNull(TEXT("...with nothing changed"), PreSnap->FindAdjustment(RB));
    TestTrue(TEXT("...and again keeps him in"), PreSnap->ToggleProtection(RB, true));

    TestTrue(TEXT("The line slides left"), PreSnap->SetSlide(EPSSlideDirection::Left, true));
    TestTrue(TEXT("...aiming its linemen to the left"), PreSnap->GetSlideAimOffset().Equals(FVector(0.f, -PreSnap->GetTuning().SlideAimOffset, 0.f)));
    const TArray<TPair<APSPlayerPawn*, APSPlayerPawn*>> SlidPairs = UPSPreSnapSubsystem::ComputeBlockingPairs(Line, Rushers, PreSnap->GetSlideAimOffset());
    const TArray<APSPlayerPawn*> SlideFree = Unblocked(SlidPairs);
    TestTrue(TEXT("Sliding left, the backside (right) end is the one left over"), SlideFree.Num() == 1 && SlideFree[0] == RightEnd);

    Snap(Bus);
    const APSOffenseController* BackAI = AIOf(RB);
    TestTrue(TEXT("At the snap the back kept in has no route"), BackAI && BackAI->GetRouteWaypointCount() == 0);
    TestTrue(TEXT("The released tight end runs his release route (an out)"), FirstWaypoint(TE).Equals(FVector(600.f, 450.f, 0.f)));

    // The line engages as the slide paired it; the back takes the backside end.
    for (const TPair<APSPlayerPawn*, APSPlayerPawn*>& Pair : SlidPairs)
    {
        Pair.Key->bIsEngaged = true;
        Pair.Value->bIsEngaged = true;
    }
    UPSSkillPlayerAIComponent* BackBrain = BackAI ? BackAI->GetSkillAI() : nullptr;
    if (TestNotNull(TEXT("The back's AI"), BackBrain))
    {
        BackBrain->TickAI(0.1f);
        TestTrue(TEXT("The back blocks"), BackBrain->GetAction() == EPSSkillPlayerAction::Block);
        TestTrue(TEXT("...the free rusher on the backside of the slide"), BackBrain->GetDesiredDirection().Y > 0.f);
    }

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The CPU quarterback reads the defense
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPreSnapCpuReadTest,
    "PlaySports.PreSnap.CpuQuarterbackReads",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPreSnapCpuReadTest::RunTest(const FString& Parameters)
{
    using namespace PSPreSnapTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSPreSnapSubsystem* PreSnap = World ? World->GetSubsystem<UPSPreSnapSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Pre-snap subsystem"), PreSnap))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-100.f, 0.f, 100.f), 90.f);
    APSPlayerPawn* Slot = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_SLOT"), FVector(-50.f, -500.f, 100.f));
    SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_WIDE"), FVector(-50.f, 900.f, 100.f));
    APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-500.f, 0.f, 100.f));
    SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL"), FVector(-50.f, 0.f, 100.f));
    SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB"), FVector(450.f, 0.f, 100.f));
    APSPlayerPawn* Corner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB"), FVector(900.f, -500.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Slot"), Slot) || !TestNotNull(TEXT("RB"), RB) || !TestNotNull(TEXT("DB"), Corner))
    {
        DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryPreSnapEvent> Changes;
    const FDelegateHandle Handle = Bus->OnPreSnapMC.AddLambda([&Changes](const FPSTelemetryPreSnapEvent& Event) { Changes.Add(Event); });

    // A run called into a blitz: the sharp QB checks to the formation's pass.
    PlayCall->OpenPlayCall(FirstAndTen());
    TestTrue(TEXT("The CPU calls the stretch"), PlayCall->CallPlay(TEXT("Offense_TripsRightStretch"), EPSPlayCaller::CPU));
    TestTrue(TEXT("The defense shows the double-A blitz"), PlayCall->CallPlay(TEXT("Defense_DoubleABlitz"), EPSPlayCaller::CPU));

    const FPSDefensiveLook Look = PreSnap->GetDefensiveLook();
    TestTrue(TEXT("The look shows the blitz"), Look.bShowsBlitz);
    TestEqual(TEXT("...one man in the box (the linebacker)"), Look.BoxCount, 1);
    TestEqual(TEXT("...and its front"), Look.Front, FString(TEXT("Nickel")));

    TestEqual(TEXT("Once both calls are in, the QB checks out of the run"), PlayCall->GetCall(true).PlayId, FName(TEXT("Offense_SlantFlat")));
    TestTrue(TEXT("...a CPU call still"), PlayCall->GetCall(true).Caller == EPSPlayCaller::CPU);
    const FPSPreSnapPlayerAdjustment* BackChange = PreSnap->FindAdjustment(RB);
    TestTrue(TEXT("The back stays in against the blitz"), BackChange && BackChange->Protection == EPSProtectionCall::Block);
    TestTrue(TEXT("The slot goes in motion"), PreSnap->GetMotionPlayer() == Slot && PreSnap->IsMotionActive());
    TestTrue(TEXT("...and the man corner over him travels"), PreSnap->GetTravellingDefender() == Corner);
    TestTrue(TEXT("Every change is announced as the CPU's"), Changes.Num() >= 3 && !Changes.ContainsByPredicate([](const FPSTelemetryPreSnapEvent& Event) { return Event.bHumanCall; }));
    TestTrue(TEXT("...the audible first"), Changes.Num() > 0 && Changes[0].Action == EPSPreSnapAction::Audible);

    // The next down, a raw quarterback: he runs what was called.
    FPSTelemetryPhaseChangeEvent NextDown;
    NextDown.NewPhase = TEXT("PreSnap");
    Bus->PublishPhaseChange(NextDown);
    FPlayerAttributes Raw = QB->GetAttributes();
    Raw.Awareness = 10.f;
    QB->InitializePlayer(Raw);
    Changes.Reset();
    PlayCall->OpenPlayCall(FirstAndTen());
    PlayCall->CallPlay(TEXT("Offense_TripsRightStretch"), EPSPlayCaller::CPU);
    PlayCall->CallPlay(TEXT("Defense_DoubleABlitz"), EPSPlayCaller::CPU);
    TestEqual(TEXT("A raw QB doesn't read the blitz"), PlayCall->GetCall(true).PlayId, FName(TEXT("Offense_TripsRightStretch")));
    TestEqual(TEXT("...and changes nothing"), Changes.Num(), 0);
    TestFalse(TEXT("...not even a motion"), PreSnap->IsMotionActive());

    Bus->OnPreSnapMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- The human's buttons
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPreSnapButtonsTest,
    "PlaySports.PreSnap.HumanButtons",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPreSnapButtonsTest::RunTest(const FString& Parameters)
{
    using namespace PSPreSnapTests;

    UWorld* World = CreateTestWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSPreSnapSubsystem* PreSnap = World ? World->GetSubsystem<UPSPreSnapSubsystem>() : nullptr;
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World ? World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams) : nullptr;
    if (!TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Pre-snap subsystem"), PreSnap) || !TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-100.f, 0.f, 100.f));
    APSPlayerPawn* LeftWR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_L"), FVector(-50.f, -900.f, 100.f));
    APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-500.f, -10.f, 100.f));
    APSPlayerPawn* RightWR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_R"), FVector(-50.f, 900.f, 100.f));
    APSPlayerPawn* DB = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB"), FVector(900.f, 0.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("WR_L"), LeftWR) || !TestNotNull(TEXT("RB"), RB) || !TestNotNull(TEXT("WR_R"), RightWR) || !TestNotNull(TEXT("DB"), DB))
    {
        DestroyTestWorld(World);
        return false;
    }

    UPSPreSnapInputComponent* Input = Controller->GetPreSnapInputComponent();
    if (!TestNotNull(TEXT("The controller has pre-snap buttons"), Input))
    {
        DestroyTestWorld(World);
        return false;
    }
    Input->BindToController();
    const FPreSnapTuningRow& Tuning = PreSnap->GetTuning();

    TestTrue(TEXT("The human takes the QB"), Controller->TakeControlOf(QB));
    PlayCall->OpenPlayCall(FirstAndTen());
    TestTrue(TEXT("The human calls Slant-Flat"), PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human));

    const TArray<APSPlayerPawn*> Selectable = Input->GetSelectablePlayers();
    if (TestEqual(TEXT("Three receivers to pick from"), Selectable.Num(), 3))
    {
        TestTrue(TEXT("...left to right"), Selectable[0] == LeftWR && Selectable[1] == RB && Selectable[2] == RightWR);
    }
    TestTrue(TEXT("The first is picked to start"), Input->GetSelectedPlayer() == LeftWR);
    TestTrue(TEXT("Select picks the next"), Input->PressAction(Tuning.SelectAction));
    TestTrue(TEXT("...the back"), Input->GetSelectedPlayer() == RB);

    TestTrue(TEXT("The protection button keeps the back in"), Input->PressAction(Tuning.ProtectionAction));
    const FPSPreSnapPlayerAdjustment* BackChange = PreSnap->FindAdjustment(RB);
    TestTrue(TEXT("...kept in"), BackChange && BackChange->Protection == EPSProtectionCall::Block);
    TestTrue(TEXT("The slide button slides the line left"), Input->PressAction(Tuning.SlideAction));
    TestTrue(TEXT("...left"), PreSnap->GetSlide() == EPSSlideDirection::Left);

    Input->PressAction(Tuning.SelectAction);
    TestTrue(TEXT("The hot-route button changes the right receiver's route"), Input->PressAction(Tuning.HotRouteAction));
    const FPSPreSnapPlayerAdjustment* ReceiverChange = PreSnap->FindAdjustment(RightWR);
    TestTrue(TEXT("...to the next route of his set"), ReceiverChange && !ReceiverChange->HotRouteId.IsNone());
    TestTrue(TEXT("The motion button sends him in motion"), Input->PressAction(Tuning.MotionAction));
    TestTrue(TEXT("...him"), PreSnap->GetMotionPlayer() == RightWR);

    // Through the controller's catalog-action event, as a pressed button arrives.
    Controller->OnCatalogActionStarted.Broadcast(Tuning.AudibleAction);
    TestEqual(TEXT("The audible button checks to the formation's other play"), PlayCall->GetCall(true).PlayId, FName(TEXT("Offense_TripsRightStretch")));
    TestFalse(TEXT("No slide on a run"), Input->PressAction(Tuning.SlideAction));
    TestFalse(TEXT("A button that isn't a pre-snap action does nothing"), Input->PressAction(TEXT("Juke")));

    TestTrue(TEXT("The human switches to defense"), Controller->TakeControlOf(DB));
    TestFalse(TEXT("On defense the offense's buttons do nothing"), Input->PressAction(Tuning.AudibleAction));
    TestEqual(TEXT("...the call stands"), PlayCall->GetCall(true).PlayId, FName(TEXT("Offense_TripsRightStretch")));

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
