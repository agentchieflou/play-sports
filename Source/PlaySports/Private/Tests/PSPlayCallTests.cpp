// PSPlayCallTests.cpp -- Epic 102 (play-call interface, part 1)
//
// Tests covered:
//   1. The playbook browses by side and formation; menu options for the play-call screens
//      carry the right targets, payloads and text; the situation and tuning come through.
//   2. The call window: the CPU calls sides no human controls, a human arriving takes the
//      call over, a human offense snaps on its hike and a CPU offense after the tuned delay,
//      and at the snap both calls reach the AI pawns through the play orchestrator.
//   3. The menu flow on a real APSPlayerController: the call screen opens when the human's
//      side waits, formation -> plays -> call closes it, Back stops at the formations, and
//      Confirm reopens the screen when no call is in or hikes once one is.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSDefenseController.h"
#include "PSMenuComponent.h"
#include "PSOffenseController.h"
#include "PSPlayCallComponent.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlaySimulation.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "AIController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPlayCallTests
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

    /** A pawn with Role (its side follows the role) under the given AI controller class. */
    template <typename ControllerType>
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

        if (ControllerType* AI = World->SpawnActor<ControllerType>(ControllerType::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
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

    static const FPSMenuOptionDef* FindOption(const TArray<FPSMenuOptionDef>& Options, FName OptionId)
    {
        return Options.FindByPredicate([OptionId](const FPSMenuOptionDef& Option) { return Option.OptionId == OptionId; });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Playbook browsing, menu options, situation and tuning
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlaybookBrowsingTest,
    "PlaySports.PlayCall.PlaybookBrowsing",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlaybookBrowsingTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayCallTests;

    UWorld* World = CreateTestWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Game worlds have a play-call subsystem"), PlayCall))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    const TArray<FString> OffenseFormations = PlayCall->GetFormations(true);
    const TArray<FString> DefenseFormations = PlayCall->GetFormations(false);
    TestTrue(TEXT("The playbook has offensive formations"), OffenseFormations.Num() > 1);
    TestTrue(TEXT("Offense lists Trips Right"), OffenseFormations.Contains(TEXT("Trips Right")));
    TestFalse(TEXT("Offense lists no defensive formation"), OffenseFormations.Contains(TEXT("Base 4-3")));
    TestTrue(TEXT("Defense lists Base 4-3"), DefenseFormations.Contains(TEXT("Base 4-3")));

    const TArray<FPSPlayDefinition> TripsRight = PlayCall->GetPlaysInFormation(TEXT("Trips Right"), true);
    TestTrue(TEXT("Slant-Flat is a Trips Right play"), TripsRight.ContainsByPredicate([](const FPSPlayDefinition& Play) { return Play.PlayId == FName(TEXT("Offense_SlantFlat")); }));
    TestEqual(TEXT("Base 4-3 has its two calls"), PlayCall->GetPlaysInFormation(TEXT("Base 4-3"), false).Num(), 2);

    FPSPlayDefinition Unknown;
    TestFalse(TEXT("An unknown play is not found"), PlayCall->FindPlay(TEXT("Offense_Nope"), Unknown));
    TestNotNull(TEXT("The route library loaded"), PlayCall->GetRouteLibrary());

    // Formation options open the plays screen with the formation as payload.
    const TArray<FPSMenuOptionDef> FormationOptions = PlayCall->BuildFormationOptions(true, TEXT("PlayCallPlays"));
    TestEqual(TEXT("One option per offensive formation"), FormationOptions.Num(), OffenseFormations.Num());
    if (const FPSMenuOptionDef* Trips = FindOption(FormationOptions, TEXT("Trips Right")))
    {
        TestEqual(TEXT("A formation opens the plays screen"), Trips->TargetScreen, FName(TEXT("PlayCallPlays")));
        TestEqual(TEXT("...for that formation"), Trips->Payload, FName(TEXT("Trips Right")));
    }
    else
    {
        AddError(TEXT("No Trips Right formation option"));
    }

    // Play options call the play and describe it in text until play art exists.
    const TArray<FPSMenuOptionDef> PlayOptions = PlayCall->BuildPlayOptions(TEXT("Trips Right"), true);
    if (const FPSMenuOptionDef* SlantFlat = FindOption(PlayOptions, TEXT("Offense_SlantFlat")))
    {
        TestTrue(TEXT("Choosing a play calls it"), SlantFlat->Command == EPSMenuCommand::CallPlay);
        TestEqual(TEXT("...with its PlayId as payload"), SlantFlat->Payload, FName(TEXT("Offense_SlantFlat")));
        TestTrue(TEXT("The label names the play and its category"), SlantFlat->Label.Contains(TEXT("Slant-Flat")) && SlantFlat->Label.Contains(TEXT("Short pass")));
        TestTrue(TEXT("The detail lists the receivers' routes"), SlantFlat->Detail.Contains(TEXT("WR Slant")) && SlantFlat->Detail.Contains(TEXT("RB Flat")));
        TestFalse(TEXT("The quarterback's implied dropback is left out"), SlantFlat->Detail.Contains(TEXT("QB")));
    }
    else
    {
        AddError(TEXT("No Slant-Flat play option"));
    }
    const TArray<FPSMenuOptionDef> DefenseOptions = PlayCall->BuildPlayOptions(TEXT("Base 4-3"), false);
    if (const FPSMenuOptionDef* Cover2 = FindOption(DefenseOptions, TEXT("Defense_43Cover2")))
    {
        TestTrue(TEXT("A defensive call shows its front and coverage"), Cover2->Label.Contains(TEXT("4-3 Cover2")));
        TestTrue(TEXT("...and what each level does"), Cover2->Detail.Contains(TEXT("DB zone")));
    }
    else
    {
        AddError(TEXT("No 4-3 Cover 2 play option"));
    }

    // The coaching AI sees the game from the possessing team's side.
    FPlayState State;
    State.Down = 3;
    State.Distance = 7;
    State.bHomeHasPossession = false;
    State.HomeScore = 7;
    State.AwayScore = 3;
    State.AwayTimeoutsRemaining = 1;
    const FPSSituationContext Situation = UPSPlayCallSubsystem::MakeSituation(State);
    TestEqual(TEXT("Down comes through"), Situation.Down, 3);
    TestEqual(TEXT("Score differential is the possessing team's"), Situation.ScoreDifferential, -4);
    TestEqual(TEXT("Timeouts are the possessing team's"), Situation.TimeoutsRemaining, 1);

    FPlayCallTuningRow FromFile;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    TestTrue(TEXT("play_call.json loads through UPSDataIngestion"), Ingestion->LoadPlayCallTuningFromJson(UPSPlayCallSubsystem::GetDefaultTuningPath(), FromFile));
    TestEqual(TEXT("The CPU snap delay is the file's"), PlayCall->GetTuning().CpuSnapDelaySeconds, FromFile.CpuSnapDelaySeconds);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The call window, snap readiness and distribution at the snap
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCallWindowTest,
    "PlaySports.PlayCall.CallWindowAndSnap",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCallWindowTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayCallTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    APSPlayerPawn* QB = World ? SpawnPlayer<APSOffenseController>(World, EPlayerRole::Quarterback, TEXT("QB_TEST"), FVector::ZeroVector) : nullptr;
    APSPlayerPawn* WR = World ? SpawnPlayer<APSOffenseController>(World, EPlayerRole::WideReceiver, TEXT("WR_TEST"), FVector(0.f, 500.f, 0.f)) : nullptr;
    APSPlayerPawn* LB = World ? SpawnPlayer<APSDefenseController>(World, EPlayerRole::Linebacker, TEXT("LB_TEST"), FVector(300.f, 0.f, 0.f)) : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("WR"), WR) || !TestNotNull(TEXT("LB"), LB))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    TArray<FPSTelemetryPlayCallEvent> Calls;
    const FDelegateHandle CallsHandle = Bus->OnPlayCallMC.AddLambda([&Calls](const FPSTelemetryPlayCallEvent& Event) { Calls.Add(Event); });
    TArray<bool> HumanCallsNeeded;
    const FDelegateHandle NeededHandle = PlayCall->OnHumanCallNeeded.AddLambda([&HumanCallsNeeded](bool bOffense) { HumanCallsNeeded.Add(bOffense); });

    const float Delay = PlayCall->GetTuning().CpuSnapDelaySeconds;
    TestFalse(TEXT("No calls before a window opens"), PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human));

    // CPU vs CPU: both sides are called on the first poll; the offense snaps after the delay.
    PlayCall->OpenPlayCall(FirstAndTen());
    TestTrue(TEXT("The window is open"), PlayCall->IsCallWindowOpen());
    TestFalse(TEXT("Not ready before the delay"), PlayCall->PollReadyToSnap(0.f));
    TestTrue(TEXT("The CPU called the offense"), PlayCall->GetCall(true).Caller == EPSPlayCaller::CPU);
    TestTrue(TEXT("The CPU called the defense"), PlayCall->GetCall(false).Caller == EPSPlayCaller::CPU);
    TestEqual(TEXT("Both CPU calls are announced on the bus"), Calls.Num(), 2);
    TestTrue(TEXT("...as CPU calls"), Calls.Num() == 2 && !Calls[0].bHumanCall && !Calls[1].bHumanCall);
    TestTrue(TEXT("A CPU offense snaps once the delay has passed"), PlayCall->PollReadyToSnap(Delay));

    // A human takes the QB mid-window: the offense's call becomes theirs to make.
    FPSTelemetryControlChangeEvent TakeQB;
    TakeQB.PlayerName = TEXT("QB_TEST");
    TakeQB.PlayerId = TEXT("QB_TEST");
    TakeQB.bHumanControlled = true;
    Bus->PublishControlChange(TakeQB);
    TestTrue(TEXT("The offense is a human side"), PlayCall->IsHumanSide(true));
    TestFalse(TEXT("The defense is not"), PlayCall->IsHumanSide(false));
    TestFalse(TEXT("The CPU's offensive call is dropped for the human"), PlayCall->GetCall(true).IsSet());
    TestTrue(TEXT("The offense waits for its human"), PlayCall->IsWaitingForHuman(true));
    TestTrue(TEXT("The human's call is asked for"), HumanCallsNeeded.Num() == 1 && HumanCallsNeeded[0]);
    TestFalse(TEXT("No snap while the human hasn't called"), PlayCall->PollReadyToSnap(10.f));
    TestFalse(TEXT("A hike without a human call does nothing"), PlayCall->RequestSnap());

    TestTrue(TEXT("The human calls Slant-Flat"), PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human));
    TestTrue(TEXT("The call is announced as the human's"), Calls.Num() > 0 && Calls.Last().bHumanCall && Calls.Last().PlayId == FName(TEXT("Offense_SlantFlat")));
    TestFalse(TEXT("A human offense never snaps on the CPU's delay"), PlayCall->PollReadyToSnap(10.f));
    TestTrue(TEXT("The human hikes"), PlayCall->RequestSnap());
    TestTrue(TEXT("...and the offense snaps"), PlayCall->PollReadyToSnap(0.f));

    // Pin the defense so the distributed assignment is known: the double-A blitz sends the LB.
    TestTrue(TEXT("The defense call can be replaced"), PlayCall->CallPlay(TEXT("Defense_DoubleABlitz"), EPSPlayCaller::CPU));
    APSDefenseController* LBController = Cast<APSDefenseController>(LB->GetController());
    APSOffenseController* WRController = Cast<APSOffenseController>(WR->GetController());
    TestTrue(TEXT("The LB starts on its default run fit"), LBController && LBController->GetAssignment() == EPSDefensiveAssignmentType::RunFit);

    FPSTelemetrySnapEvent Snap;
    Snap.Down = 1;
    Snap.Distance = 10;
    Snap.YardLine = 10;
    Snap.LineOfScrimmage = FVector(1000.f, 0.f, 0.f);
    Bus->PublishSnap(Snap);

    TestFalse(TEXT("The snap closes the window"), PlayCall->IsCallWindowOpen());
    TestTrue(TEXT("The WR runs the called Slant from the line of scrimmage"),
        WRController && WRController->GetCurrentTargetLocation().Equals(Snap.LineOfScrimmage + FVector(300.f, 0.f, 0.f)));
    TestTrue(TEXT("The LB runs the called blitz"), LBController && LBController->GetAssignment() == EPSDefensiveAssignmentType::PassRush);
    TestFalse(TEXT("No calls after the snap"), PlayCall->CallPlay(TEXT("Offense_FourVerts"), EPSPlayCaller::Human));
    TestFalse(TEXT("Nothing to snap after the snap"), PlayCall->PollReadyToSnap(10.f));

    // Released: the side is the CPU's again.
    FPSTelemetryControlChangeEvent ReleaseQB = TakeQB;
    ReleaseQB.bHumanControlled = false;
    Bus->PublishControlChange(ReleaseQB);
    TestFalse(TEXT("No human side after release"), PlayCall->IsHumanSide(true));

    // A snap with no window (the functional gym, scripts) still runs CPU calls.
    const int32 CallsBefore = Calls.Num();
    Bus->PublishSnap(Snap);
    TestEqual(TEXT("A snap outside a window calls both sides"), Calls.Num(), CallsBefore + 2);

    Bus->OnPlayCallMC.Remove(CallsHandle);
    PlayCall->OnHumanCallNeeded.Remove(NeededHandle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The play-call screens on a real player controller
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayCallMenuFlowTest,
    "PlaySports.PlayCall.MenuCallFlow",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayCallMenuFlowTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayCallTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world created"), World))
    {
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    APSPlayerPawn* QB = SpawnPlayer<APSOffenseController>(World, EPlayerRole::Quarterback, TEXT("QB_TEST"), FVector::ZeroVector);
    UPSPlayCallSubsystem* PlayCall = World->GetSubsystem<UPSPlayCallSubsystem>();
    UPSMenuComponent* Menu = Controller ? Controller->GetMenuComponent() : nullptr;
    UPSPlayCallComponent* Caller = Controller ? Controller->GetPlayCallComponent() : nullptr;
    if (!TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall)
        || !TestNotNull(TEXT("Menu component"), Menu) || !TestNotNull(TEXT("Play-call component"), Caller))
    {
        DestroyTestWorld(World);
        return false;
    }

    TestEqual(TEXT("The authored menu catalog is valid"), UPSMenuComponent::ValidateCatalog(Menu->GetCatalog()).Num(), 0);
    const FName CallScreen = Menu->GetCatalog().PlayCallScreen;

    // Headless worlds don't run BeginPlay, which is where the game binds.
    Caller->BindToPlayCall();
    TestTrue(TEXT("The human takes the QB"), Controller->TakeControlOf(QB));
    TestTrue(TEXT("...so this player calls for the offense"), Caller->IsCallingForOffense());

    PlayCall->OpenPlayCall(FirstAndTen());
    TestEqual(TEXT("A new down opens the play-call screen"), Menu->GetTopScreenId(), CallScreen);

    const FPSMenuScreenDef Formations = Menu->GetPresentedScreen(CallScreen);
    TestEqual(TEXT("One option per offensive formation"), Formations.Options.Num(), PlayCall->GetFormations(true).Num());

    Menu->ChooseOption(TEXT("Trips Right"));
    const FName PlaysScreen = Menu->GetTopScreenId();
    TestTrue(TEXT("A formation opens its plays"), PlaysScreen != CallScreen && !PlaysScreen.IsNone());
    const FPSMenuScreenDef Plays = Menu->GetPresentedScreen(PlaysScreen);
    TestEqual(TEXT("The plays screen is titled with the formation"), Plays.Title, FString(TEXT("Trips Right")));
    TestNotNull(TEXT("...and offers Slant-Flat"), FindOption(Plays.Options, TEXT("Offense_SlantFlat")));

    TestTrue(TEXT("Back returns to the formations"), Menu->HandleBack());
    TestEqual(TEXT("...on the call screen"), Menu->GetTopScreenId(), CallScreen);
    TestFalse(TEXT("Back can't leave the call screen without a call"), Menu->HandleBack());

    // Closed without a call (say the player paused and resumed): Confirm brings it back.
    Menu->Resume();
    TestFalse(TEXT("The menu is closed"), Menu->IsMenuOpen());
    Controller->OnCatalogActionStarted.Broadcast(TEXT("Confirm"));
    TestEqual(TEXT("Confirm with no call reopens the call screen"), Menu->GetTopScreenId(), CallScreen);
    TestFalse(TEXT("...and does not hike"), PlayCall->PollReadyToSnap(10.f));

    Menu->ChooseOption(TEXT("Trips Right"));
    Menu->ChooseOption(TEXT("Offense_SlantFlat"));
    TestFalse(TEXT("Calling the play closes the screens"), Menu->IsMenuOpen());
    TestTrue(TEXT("The call is the human's Slant-Flat"),
        PlayCall->GetCall(true).Caller == EPSPlayCaller::Human && PlayCall->GetCall(true).PlayId == FName(TEXT("Offense_SlantFlat")));
    TestFalse(TEXT("The offense waits for the hike"), PlayCall->PollReadyToSnap(10.f));

    Controller->OnCatalogActionStarted.Broadcast(TEXT("Confirm"));
    TestFalse(TEXT("Confirm with a call opens no screen"), Menu->IsMenuOpen());
    TestTrue(TEXT("Confirm hikes"), PlayCall->PollReadyToSnap(0.f));

    // A catalog whose play-call screen is not a formations screen is refused.
    FPSMenuCatalog Broken = Menu->GetCatalog();
    Broken.PlayCallScreen = Broken.RootScreen;
    TestTrue(TEXT("A PlayCallScreen without formations content is reported"),
        UPSMenuComponent::ValidateCatalog(Broken).ContainsByPredicate([](const FString& Error) { return Error.Contains(TEXT("must have Content PlayCallFormations")); }));

    Caller->UnbindFromPlayCall();
    Controller->ReleaseControl();
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
