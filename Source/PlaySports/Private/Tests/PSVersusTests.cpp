// PSVersusTests.cpp -- Epic 107 (local head-to-head)
//
// Tests covered:
//   1. Data/versus_rules.json loads through UPSDataIngestion, matches the struct's defaults and
//      validates; unsound rules are reported and refused.
//   2. Session flow: two seats, one per controller and per user; side select refuses a team
//      the other seat has and un-readies a seat that changes team; the session starts only
//      with both ready, and each seat takes its side's player. Travel options and the URL
//      helpers name a versus game and its home seat.
//   3. The ball changing hands swaps the seats' sides without either taking the other's
//      player; each seat keeps its own tempo; a new down puts both back on their roles.
//   4. Hidden picks: both players call at once on their own side's screens, each learns only
//      whether the other has called, the hike waits for the defense's call, and after the ball
//      changes hands neither sees the other's recent plays or tendencies.
//   5. Split contexts: the two controllers are in their own depth contexts at once; the
//      defense's switching follows the rules (a takeaway still goes to the carrier) and
//      nobody takes the other human's player.
//   6. Competitive integrity: who may see route art and defensive icons, shared or split.
//   7. Pause etiquette: pauses between plays only and limited per half, resume when both are
//      ready after a countdown, a disconnect pauses until the controller is back, quitting
//      forfeits -- all through the menus' pause screen.
//   8. Each seat's device component counts only its own user's input, and each player's
//      rumble follows only their own control and device events.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSControlHandoffComponent.h"
#include "PSDataIngestion.h"
#include "PSDefenseController.h"
#include "PSForceFeedbackComponent.h"
#include "PSInputDeviceComponent.h"
#include "PSLocalization.h"
#include "PSMenuComponent.h"
#include "PSOffenseController.h"
#include "PSPlayCallComponent.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayContextComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "PSVersusSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSVersusTests
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

    static APSPlayerController* SpawnHuman(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    }

    /** A head-to-head world: a QB, a WR, a linebacker and a safety, two humans. */
    struct FVersusRig
    {
        UWorld* World = nullptr;
        UPSTelemetryBus* Bus = nullptr;
        UPSVersusSubsystem* Versus = nullptr;
        UPSPlayCallSubsystem* PlayCall = nullptr;
        APSPlayerController* P1 = nullptr;
        APSPlayerController* P2 = nullptr;
        APSPlayerPawn* QB = nullptr;
        APSPlayerPawn* WR = nullptr;
        APSPlayerPawn* LB = nullptr;
        APSPlayerPawn* FS = nullptr;

        bool Build()
        {
            World = CreateTestWorld();
            if (!World)
            {
                return false;
            }
            Bus = World->GetSubsystem<UPSTelemetryBus>();
            Versus = World->GetSubsystem<UPSVersusSubsystem>();
            PlayCall = World->GetSubsystem<UPSPlayCallSubsystem>();
            P1 = SpawnHuman(World);
            P2 = SpawnHuman(World);
            QB = SpawnPlayer<APSOffenseController>(World, EPlayerRole::Quarterback, TEXT("QB_V"), FVector(0.f, 0.f, 100.f));
            WR = SpawnPlayer<APSOffenseController>(World, EPlayerRole::WideReceiver, TEXT("WR_V"), FVector(0.f, 800.f, 100.f));
            LB = SpawnPlayer<APSDefenseController>(World, EPlayerRole::Linebacker, TEXT("LB_V"), FVector(500.f, 0.f, 100.f));
            FS = SpawnPlayer<APSDefenseController>(World, EPlayerRole::DefensiveBack, TEXT("FS_V"), FVector(1500.f, 600.f, 100.f));
            return Bus && Versus && PlayCall && P1 && P2 && QB && WR && LB && FS;
        }

        /** Both seated (users 0 and 1), player 1 home, both ready, started. */
        bool Start()
        {
            return Versus->ClaimSeat(P1, 0) == 0 && Versus->ClaimSeat(P2, 1) == 1
                && Versus->SelectTeam(0, EPSVersusTeam::Home) && Versus->SelectTeam(1, EPSVersusTeam::Away)
                && Versus->SetReady(0, true) && Versus->SetReady(1, true) && Versus->StartSession();
        }

        void Teardown()
        {
            if (World)
            {
                DestroyTestWorld(World);
                World = nullptr;
            }
        }
    };

    static void PublishPossession(UPSTelemetryBus* Bus, bool bHomeHasBall, int32 Quarter = 1)
    {
        FPSTelemetryGameStateEvent State;
        State.Phase = TEXT("PreSnap");
        State.Quarter = Quarter;
        State.bHomeHasPossession = bHomeHasBall;
        Bus->PublishGameState(State);
    }

    static void PublishNewDown(UPSTelemetryBus* Bus)
    {
        FPSTelemetryPhaseChangeEvent Whistle;
        Whistle.OldPhase = TEXT("BallCarrierMovement");
        Whistle.NewPhase = TEXT("PreSnap");
        Bus->PublishPhaseChange(Whistle);
    }

    static void PublishSnap(UPSTelemetryBus* Bus)
    {
        FPSTelemetrySnapEvent Snap;
        Snap.Down = 1;
        Snap.Distance = 10;
        Snap.YardLine = 20;
        Snap.LineOfScrimmage = FVector(1000.f, 0.f, 0.f);
        Bus->PublishSnap(Snap);
    }

    static FPSSituationContext FirstAndTen(bool bHomeHasBall)
    {
        FPSSituationContext Situation;
        Situation.Down = 1;
        Situation.Distance = 10;
        Situation.YardLine = 20;
        Situation.bHomeHasPossession = bHomeHasBall;
        return Situation;
    }

    static bool HasOption(const TArray<FPSMenuOptionDef>& Options, FName OptionId)
    {
        return Options.ContainsByPredicate([OptionId](const FPSMenuOptionDef& Option) { return Option.OptionId == OptionId; });
    }

    static int32 CountKind(const TArray<FPSTelemetryVersusEvent>& Events, EPSVersusEventKind Kind)
    {
        int32 Count = 0;
        for (const FPSTelemetryVersusEvent& Event : Events)
        {
            Count += Event.Kind == Kind ? 1 : 0;
        }
        return Count;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The rules load and validate
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSVersusRulesTest,
    "PlaySports.Versus.RulesLoadAndValidate",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSVersusRulesTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSVersusRules FromFile;
    if (!TestTrue(TEXT("versus_rules.json loads through UPSDataIngestion"), Ingestion->LoadVersusRulesFromJson(UPSVersusSubsystem::GetDefaultRulesPath(), FromFile)))
    {
        return false;
    }
    for (const FString& Problem : UPSVersusSubsystem::ValidateRules(FromFile))
    {
        AddError(FString::Printf(TEXT("versus_rules.json: %s"), *Problem));
    }

    // The struct's defaults are the file's, so code without the file plays the same game.
    const FPSVersusRules Defaults;
    TestEqual(TEXT("Offense control role"), FromFile.OffenseControlRole, Defaults.OffenseControlRole);
    TestEqual(TEXT("Defense control role"), FromFile.DefenseControlRole, Defaults.DefenseControlRole);
    TestTrue(TEXT("Reset each down"), FromFile.bResetControlEachDown == Defaults.bResetControlEachDown);
    TestTrue(TEXT("Defense switches during the play"), FromFile.bDefenseSwitchDuringPlay == Defaults.bDefenseSwitchDuringPlay);
    TestTrue(TEXT("Defense pre-snap picks"), FromFile.bDefensePreSnapPicks == Defaults.bDefensePreSnapPicks);
    TestEqual(TEXT("Screen"), FromFile.Screen, Defaults.Screen);
    TestEqual(TEXT("Route art audience"), FromFile.RouteArtAudience, Defaults.RouteArtAudience);
    TestEqual(TEXT("Defensive icons audience"), FromFile.DefensiveIconsAudience, Defaults.DefensiveIconsAudience);
    TestEqual(TEXT("Pauses per half"), FromFile.PausesPerHalf, Defaults.PausesPerHalf);
    TestTrue(TEXT("Pause only between plays"), FromFile.bPauseOnlyBetweenPlays == Defaults.bPauseOnlyBetweenPlays);
    TestTrue(TEXT("Resume needs both"), FromFile.bResumeNeedsBoth == Defaults.bResumeNeedsBoth);
    TestEqual(TEXT("Resume countdown"), FromFile.ResumeCountdownSeconds, Defaults.ResumeCountdownSeconds);
    TestTrue(TEXT("Pause on disconnect"), FromFile.bPauseOnDisconnect == Defaults.bPauseOnDisconnect);
    TestTrue(TEXT("Quit forfeits"), FromFile.bQuitForfeits == Defaults.bQuitForfeits);

    FPSVersusRules Broken;
    Broken.OffenseControlRole = EPlayerRole::Linebacker;
    Broken.DefenseControlRole = EPlayerRole::Quarterback;
    Broken.PausesPerHalf = -2;
    Broken.ResumeCountdownSeconds = -1.f;
    TestEqual(TEXT("Every unsound rule is reported"), UPSVersusSubsystem::ValidateRules(Broken).Num(), 4);

    UWorld* World = PSVersusTests::CreateTestWorld();
    UPSVersusSubsystem* Versus = World ? World->GetSubsystem<UPSVersusSubsystem>() : nullptr;
    if (TestNotNull(TEXT("Versus subsystem"), Versus))
    {
        TestFalse(TEXT("Unsound rules are refused"), Versus->SetRules(Broken));
        TestEqual(TEXT("...and the file's stay"), Versus->GetRules().PausesPerHalf, FromFile.PausesPerHalf);
    }
    if (World)
    {
        PSVersusTests::DestroyTestWorld(World);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Seats, side select and the start of a session
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSVersusSessionFlowTest,
    "PlaySports.Versus.SeatsAndSideSelect",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSVersusSessionFlowTest::RunTest(const FString& Parameters)
{
    PSVersusTests::FVersusRig Rig;
    if (!TestTrue(TEXT("Head-to-head world"), Rig.Build()))
    {
        Rig.Teardown();
        return false;
    }
    UPSVersusSubsystem* Versus = Rig.Versus;
    APSPlayerController* Third = PSVersusTests::SpawnHuman(Rig.World);

    TArray<FPSTelemetryVersusEvent> Events;
    const FDelegateHandle Handle = Rig.Bus->OnVersusMC.AddLambda([&Events](const FPSTelemetryVersusEvent& Event) { Events.Add(Event); });

    TestEqual(TEXT("No session yet"), Versus->GetPhase(), EPSVersusPhase::Inactive);
    TestEqual(TEXT("Player 1 takes seat 0"), Versus->ClaimSeat(Rig.P1, 0), 0);
    TestEqual(TEXT("A controller sits once"), Versus->ClaimSeat(Rig.P1, 3), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("A user's devices drive one seat"), Versus->ClaimSeat(Rig.P2, 0), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("Player 2 takes seat 1"), Versus->ClaimSeat(Rig.P2, 1), 1);
    TestEqual(TEXT("Two seats only"), Versus->ClaimSeat(Third, 2), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("Side select is on"), Versus->GetPhase(), EPSVersusPhase::SideSelect);
    TestEqual(TEXT("Seat 1's controller is human 1"), Rig.P2->HumanIndex, 1);
    TestEqual(TEXT("...and its devices are user 1's"), Rig.P2->GetInputDeviceComponent()->OwnerUserIndex, 1);
    TestFalse(TEXT("Nobody has a team: no start"), Versus->CanStart());

    // Side select: Away on the left, Home on the right; never the same team twice.
    TestTrue(TEXT("Seat 0 picks Home"), Versus->SelectTeam(0, EPSVersusTeam::Home));
    TestFalse(TEXT("Seat 1 can't also be Home"), Versus->SelectTeam(1, EPSVersusTeam::Home));
    TestEqual(TEXT("A step onto the other seat's team is refused"), Versus->StepTeam(1, 1), EPSVersusTeam::None);
    TestEqual(TEXT("A step the other way picks Away"), Versus->StepTeam(1, -1), EPSVersusTeam::Away);
    TestFalse(TEXT("No ready without a team"), Versus->SetReady(1, false) && Versus->SelectTeam(1, EPSVersusTeam::None) && Versus->SetReady(1, true));
    TestTrue(TEXT("Seat 1 back on Away"), Versus->SelectTeam(1, EPSVersusTeam::Away));
    TestTrue(TEXT("Seat 0 ready"), Versus->SetReady(0, true));
    TestFalse(TEXT("One ready is not enough"), Versus->CanStart() || Versus->StartSession());
    TestTrue(TEXT("Seat 1 ready"), Versus->SetReady(1, true));
    TestTrue(TEXT("Both ready on different teams: can start"), Versus->CanStart());
    TestTrue(TEXT("Changing team takes the seat out of ready"), Versus->SelectTeam(1, EPSVersusTeam::None) && !Versus->GetSeat(1).bReady && !Versus->CanStart());
    TestTrue(TEXT("Seat 1 back, ready"), Versus->SelectTeam(1, EPSVersusTeam::Away) && Versus->SetReady(1, true));

    TestTrue(TEXT("The session starts"), Versus->StartSession());
    TestEqual(TEXT("Playing"), Versus->GetPhase(), EPSVersusPhase::Playing);
    TestEqual(TEXT("Started is announced"), PSVersusTests::CountKind(Events, EPSVersusEventKind::Started), 1);

    // Home has the ball: seat 0 plays offense on the QB, seat 1 defense on the linebacker.
    TestEqual(TEXT("Seat 0 is on offense"), Versus->GetSeatSide(0), EPSTeamSide::Offense);
    TestEqual(TEXT("Seat 1 is on defense"), Versus->GetSeatSide(1), EPSTeamSide::Defense);
    TestTrue(TEXT("Player 1 controls the QB"), Rig.P1->GetPawn() == Rig.QB);
    TestTrue(TEXT("Player 2 controls the linebacker"), Rig.P2->GetPawn() == Rig.LB);
    TestEqual(TEXT("Player 2 plays the defense's control role"), Rig.P2->DefaultControlRole, EPlayerRole::Linebacker);
    TestTrue(TEXT("Humans on both sides: a head-to-head call"), Rig.PlayCall->IsHeadToHead());
    TestEqual(TEXT("No seats once it has started"), Versus->ClaimSeat(Third, 2), static_cast<int32>(INDEX_NONE));
    TestFalse(TEXT("No team changes once it has started"), Versus->SelectTeam(0, EPSVersusTeam::Away));

    // From the front end: the Head to Head screen's travel options, read back by the URL helpers.
    UPSMenuComponent* Menu = Rig.P1->GetMenuComponent();
    TestEqual(TEXT("Player 2 home travels as a versus game with home seat 1"), Menu->BuildTravelOptions(EPSMenuCommand::StartVersus, TEXT("1")), FString(TEXT("mode=Versus?home=1")));
    FURL VersusURL;
    VersusURL.AddOption(TEXT("mode=Versus"));
    VersusURL.AddOption(TEXT("home=1"));
    FURL PlayNowURL;
    PlayNowURL.AddOption(TEXT("mode=PlayNow"));
    TestTrue(TEXT("A versus URL is recognised"), UPSVersusSubsystem::IsVersusURL(VersusURL));
    TestEqual(TEXT("...with its home seat"), UPSVersusSubsystem::GetHomeSeatFromURL(VersusURL), 1);
    TestFalse(TEXT("Play Now is not versus"), UPSVersusSubsystem::IsVersusURL(PlayNowURL));
    TestEqual(TEXT("Without a home option, player 1 is home"), UPSVersusSubsystem::GetHomeSeatFromURL(PlayNowURL), 0);
    TestFalse(TEXT("Headless there are no local players to seat from a URL"), Versus->BeginFromTravel(VersusURL));

    Rig.Bus->OnVersusMC.Remove(Handle);
    Rig.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The ball changes hands: the seats swap sides
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSVersusSidesSwapTest,
    "PlaySports.Versus.PossessionSwapsSides",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSVersusSidesSwapTest::RunTest(const FString& Parameters)
{
    PSVersusTests::FVersusRig Rig;
    if (!TestTrue(TEXT("Head-to-head world"), Rig.Build()) || !TestTrue(TEXT("Session started"), Rig.Start()))
    {
        Rig.Teardown();
        return false;
    }
    UPSVersusSubsystem* Versus = Rig.Versus;

    TArray<FPSTelemetryControlChangeEvent> Controls;
    const FDelegateHandle ControlHandle = Rig.Bus->OnControlChangeMC.AddLambda([&Controls](const FPSTelemetryControlChangeEvent& Event) { Controls.Add(Event); });
    TArray<FPSTelemetryVersusEvent> Events;
    const FDelegateHandle VersusHandle = Rig.Bus->OnVersusMC.AddLambda([&Events](const FPSTelemetryVersusEvent& Event) { Events.Add(Event); });

    // Home's offense plays hurry-up.
    Rig.PlayCall->SetHumanTempo(EPSTempo::HurryUp);

    // A turnover: the away team has the ball.
    PSVersusTests::PublishPossession(Rig.Bus, false);
    TestEqual(TEXT("Seat 0 now defends"), Versus->GetSeatSide(0), EPSTeamSide::Defense);
    TestEqual(TEXT("Seat 1 now has the ball"), Versus->GetSeatSide(1), EPSTeamSide::Offense);
    TestTrue(TEXT("Player 1 is on the linebacker"), Rig.P1->GetPawn() == Rig.LB);
    TestTrue(TEXT("Player 2 is on the QB"), Rig.P2->GetPawn() == Rig.QB);
    TestEqual(TEXT("Player 1's side is the defense"), Rig.P1->HumanSide, EPSTeamSide::Defense);
    TestEqual(TEXT("The swap is announced"), PSVersusTests::CountKind(Events, EPSVersusEventKind::SidesChanged), 1);
    TestTrue(TEXT("Still both sides human"), Rig.PlayCall->IsHeadToHead());
    const FPSTelemetryControlChangeEvent* TookQB = Controls.FindByPredicate([](const FPSTelemetryControlChangeEvent& Event) { return Event.bHumanControlled && Event.PlayerId == FName(TEXT("QB_V")); });
    TestTrue(TEXT("Player 2's take of the QB names human 1"), TookQB && TookQB->HumanIndex == 1);

    // Each seat has its own tempo: away's offense starts in the huddle, home's comes back.
    TestEqual(TEXT("Away's offense plays its own tempo"), Rig.PlayCall->GetHumanTempo(), EPSTempo::Huddle);
    Rig.PlayCall->SetHumanTempo(EPSTempo::NoHuddle);
    PSVersusTests::PublishPossession(Rig.Bus, true);
    TestTrue(TEXT("Back again: player 1 on the QB"), Rig.P1->GetPawn() == Rig.QB && Rig.P2->GetPawn() == Rig.LB);
    TestEqual(TEXT("Home's hurry-up is back"), Rig.PlayCall->GetHumanTempo(), EPSTempo::HurryUp);
    PSVersusTests::PublishPossession(Rig.Bus, false);
    TestEqual(TEXT("Away's no-huddle is kept"), Rig.PlayCall->GetHumanTempo(), EPSTempo::NoHuddle);
    PSVersusTests::PublishPossession(Rig.Bus, true);

    // A new down puts a player who switched back on their side's role.
    TestTrue(TEXT("Player 2 switches to the safety"), Rig.P2->TakeControlOf(Rig.FS));
    PSVersusTests::PublishNewDown(Rig.Bus);
    TestTrue(TEXT("A new down: player 2 back on the linebacker"), Rig.P2->GetPawn() == Rig.LB);

    // Without the reset rule a player keeps whoever they had.
    FPSVersusRules Keep = Versus->GetRules();
    Keep.bResetControlEachDown = false;
    TestTrue(TEXT("Rules without the reset"), Versus->SetRules(Keep));
    TestTrue(TEXT("Player 2 switches to the safety again"), Rig.P2->TakeControlOf(Rig.FS));
    PSVersusTests::PublishNewDown(Rig.Bus);
    TestTrue(TEXT("...and keeps him into the next down"), Rig.P2->GetPawn() == Rig.FS);

    Rig.Bus->OnControlChangeMC.Remove(ControlHandle);
    Rig.Bus->OnVersusMC.Remove(VersusHandle);
    Rig.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Simultaneous play calling with hidden picks
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSVersusHiddenPicksTest,
    "PlaySports.Versus.SimultaneousHiddenPicks",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSVersusHiddenPicksTest::RunTest(const FString& Parameters)
{
    PSVersusTests::FVersusRig Rig;
    if (!TestTrue(TEXT("Head-to-head world"), Rig.Build()) || !TestTrue(TEXT("Session started"), Rig.Start()))
    {
        Rig.Teardown();
        return false;
    }
    UPSVersusSubsystem* Versus = Rig.Versus;
    UPSPlayCallSubsystem* PlayCall = Rig.PlayCall;
    UPSMenuComponent* Menu1 = Rig.P1->GetMenuComponent();
    UPSMenuComponent* Menu2 = Rig.P2->GetMenuComponent();
    // Headless worlds don't run BeginPlay, which is where the game binds.
    Rig.P1->GetPlayCallComponent()->BindToPlayCall();
    Rig.P2->GetPlayCallComponent()->BindToPlayCall();
    const FName CallScreen = Menu1->GetCatalog().PlayCallScreen;

    TestEqual(TEXT("No window: nothing to know"), Versus->GetOpponentCallState(0), EPSVersusCallState::Closed);
    PlayCall->OpenPlayCall(PSVersusTests::FirstAndTen(true));
    TestEqual(TEXT("Player 1's call screen opens"), Menu1->GetTopScreenId(), CallScreen);
    TestEqual(TEXT("Player 2's call screen opens at the same time"), Menu2->GetTopScreenId(), CallScreen);
    const FPSMenuScreenDef Offense = Menu1->GetPresentedScreen(CallScreen);
    const FPSMenuScreenDef Defense = Menu2->GetPresentedScreen(CallScreen);
    TestTrue(TEXT("Player 1 browses offensive formations"), PSVersusTests::HasOption(Offense.Options, TEXT("Trips Right")) && !PSVersusTests::HasOption(Offense.Options, TEXT("Base 4-3")));
    TestTrue(TEXT("Player 2 browses defensive formations"), PSVersusTests::HasOption(Defense.Options, TEXT("Base 4-3")) && !PSVersusTests::HasOption(Defense.Options, TEXT("Trips Right")));
    TestEqual(TEXT("Player 2 sees the offense choosing"), Versus->GetOpponentCallState(1), EPSVersusCallState::Choosing);

    // Player 1 calls Slant-Flat: player 2 learns only that the offense has called.
    Menu1->ChooseOption(TEXT("Trips Right"));
    Menu1->ChooseOption(TEXT("Offense_SlantFlat"));
    TestFalse(TEXT("Player 1's screens close"), Menu1->IsMenuOpen());
    TestEqual(TEXT("Player 2 sees the offense has called"), Versus->GetOpponentCallState(1), EPSVersusCallState::Called);
    TestEqual(TEXT("Player 2's own side is still choosing"), Versus->GetCallState(1), EPSVersusCallState::Choosing);
    TestEqual(TEXT("Player 2 is still on the call screen"), Menu2->GetTopScreenId(), CallScreen);
    const FPSMenuScreenDef StillCalling = Menu2->GetPresentedScreen(CallScreen);
    TestFalse(TEXT("Nothing on player 2's screen names the offense's play"),
        StillCalling.Body.Contains(TEXT("Slant")) || PSVersusTests::HasOption(StillCalling.Options, TEXT("Offense_SlantFlat")));

    // The offense can't hike until the defense has called.
    Rig.P1->OnCatalogActionStarted.Broadcast(TEXT("Confirm"));
    TestFalse(TEXT("A hike before the defense calls is refused"), PlayCall->RequestSnap() || PlayCall->PollReadyToSnap(10.f));
    Menu2->ChooseOption(TEXT("Base 4-3"));
    Menu2->ChooseOption(TEXT("Defense_43Cover2"));
    Menu2->Resume();
    TestEqual(TEXT("Player 1 sees the defense has called"), Versus->GetOpponentCallState(0), EPSVersusCallState::Called);
    TestFalse(TEXT("The refused hike was not held"), PlayCall->PollReadyToSnap(10.f));
    Rig.P1->OnCatalogActionStarted.Broadcast(TEXT("Confirm"));
    TestTrue(TEXT("Both in: the offense hikes"), PlayCall->PollReadyToSnap(0.f));
    PSVersusTests::PublishSnap(Rig.Bus);
    TestEqual(TEXT("Both calls ran as the humans'"), PlayCall->GetCallHistory().Num(), 2);

    // The ball changes hands: the away player's offense never sees home's calls.
    PSVersusTests::PublishPossession(Rig.Bus, false);
    PlayCall->OpenPlayCall(PSVersusTests::FirstAndTen(false));
    TestTrue(TEXT("Player 2 calls for the offense now"), Rig.P2->GetPlayCallComponent()->IsCallingForOffense());
    TestEqual(TEXT("Away's offense has no recent plays"), PlayCall->GetRecentCalls(true, 5).Num(), 0);
    TestTrue(TEXT("...and no tendencies"), PlayCall->DescribeTendencies(true).IsEmpty());
    TestEqual(TEXT("Home's defense has none either"), PlayCall->GetRecentCalls(false, 5).Num(), 0);
    TestFalse(TEXT("Player 2's offense screen offers no recent plays (player 1's are not theirs)"),
        PSVersusTests::HasOption(Menu2->GetPresentedScreen(CallScreen).Options, TEXT("Recent")));

    // Home on offense again: its own history is back.
    PSVersusTests::PublishSnap(Rig.Bus);
    PSVersusTests::PublishPossession(Rig.Bus, true);
    PlayCall->OpenPlayCall(PSVersusTests::FirstAndTen(true));
    const TArray<FName> HomeRecent = PlayCall->GetRecentCalls(true, 5);
    TestTrue(TEXT("Home's offense sees its own Slant-Flat"), HomeRecent.Num() == 1 && HomeRecent[0] == FName(TEXT("Offense_SlantFlat")));
    TestFalse(TEXT("Home's tendencies are its own"), PlayCall->DescribeTendencies(true).IsEmpty());
    TestTrue(TEXT("Player 1's offense screen offers their recent plays"), PSVersusTests::HasOption(Menu1->GetPresentedScreen(CallScreen).Options, TEXT("Recent")));

    Rig.P1->GetPlayCallComponent()->UnbindFromPlayCall();
    Rig.P2->GetPlayCallComponent()->UnbindFromPlayCall();
    Rig.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Split input contexts and the defense's switching rules
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSVersusSplitContextsTest,
    "PlaySports.Versus.SplitContextsAndDefenseSwitching",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSVersusSplitContextsTest::RunTest(const FString& Parameters)
{
    PSVersusTests::FVersusRig Rig;
    if (!TestTrue(TEXT("Head-to-head world"), Rig.Build()) || !TestTrue(TEXT("Session started"), Rig.Start()))
    {
        Rig.Teardown();
        return false;
    }
    UPSVersusSubsystem* Versus = Rig.Versus;
    UPSPlayContextComponent* Context1 = Rig.P1->GetPlayContextComponent();
    UPSPlayContextComponent* Context2 = Rig.P2->GetPlayContextComponent();
    UPSControlHandoffComponent* Handoff1 = Rig.P1->GetControlHandoffComponent();
    UPSControlHandoffComponent* Handoff2 = Rig.P2->GetControlHandoffComponent();
    Context1->BindToBus();
    Context2->BindToBus();

    // The defense is held to the player it picked, and has no pre-snap picks.
    FPSVersusRules Strict = Versus->GetRules();
    Strict.bDefenseSwitchDuringPlay = false;
    Strict.bDefensePreSnapPicks = false;
    TestTrue(TEXT("Strict defense rules"), Versus->SetRules(Strict));
    Versus->ApplySides(false);
    TestFalse(TEXT("The defending seat's switch is held during the play"), Handoff2->bSwitchDuringPlay);
    TestTrue(TEXT("The offense's is not"), Handoff1->bSwitchDuringPlay);

    // Before the snap: each controller in its own side's context at once.
    Context1->Refresh();
    Context2->Refresh();
    TestEqual(TEXT("Player 1 (offense) is in PreSnap"), Rig.P1->GetDepthContext(), FName(TEXT("PreSnap")));
    TestEqual(TEXT("Player 2 (defense) is in DefensePreSnap at the same time"), Rig.P2->GetDepthContext(), FName(TEXT("DefensePreSnap")));
    TestFalse(TEXT("Player 1's stack has no defense context"), Rig.P1->IsInputContextActive(TEXT("DefensePreSnap")));

    TestFalse(TEXT("The defense's pre-snap pick is off"), Handoff2->PickAcross(1));
    TestTrue(TEXT("The offense's pick still works"), Handoff1->PickAcross(1) && Rig.P1->GetPawn() == Rig.WR);
    TestTrue(TEXT("Player 1 back on the QB"), Rig.P1->TakeControlOf(Rig.QB));

    // Nobody takes the other human's player.
    TestFalse(TEXT("Player 1 can't take player 2's linebacker"), Rig.P1->TakeControlOf(Rig.LB));
    TestTrue(TEXT("...who stays player 2's"), Rig.P2->GetPawn() == Rig.LB && Rig.P1->GetPawn() == Rig.QB);

    // The snap: the QB has the ball behind the line.
    PSVersusTests::PublishSnap(Rig.Bus);
    Rig.QB->GainPossession();
    Context1->Refresh();
    Context2->Refresh();
    TestEqual(TEXT("Player 1 is in Passing"), Rig.P1->GetDepthContext(), FName(TEXT("Passing")));
    TestEqual(TEXT("Player 2 is in Defense"), Rig.P2->GetDepthContext(), FName(TEXT("Defense")));

    const FVector Ball = Rig.QB->GetActorLocation();
    TestFalse(TEXT("The held defense can't switch during the play"), Handoff2->CycleSwitch(Ball, 10.0));
    TestTrue(TEXT("...and keeps the linebacker"), Rig.P2->GetPawn() == Rig.LB);

    // A takeaway: the safety has the ball, and the switch goes to him.
    Rig.QB->LosePossession();
    Rig.FS->GainPossession();
    TestTrue(TEXT("A takeaway's carrier is always the defense's to take"), Handoff2->CycleSwitch(Ball, 20.0) && Rig.P2->GetPawn() == Rig.FS);
    TestFalse(TEXT("Player 1 can't take the carrier player 2 holds"), Rig.P1->TakeControlOf(Rig.FS));

    Rig.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- Route art and defensive icons in a head-to-head game
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSVersusOverlayTest,
    "PlaySports.Versus.CompetitiveOverlayVisibility",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSVersusOverlayTest::RunTest(const FString& Parameters)
{
    PSVersusTests::FVersusRig Rig;
    if (!TestTrue(TEXT("Head-to-head world"), Rig.Build()))
    {
        Rig.Teardown();
        return false;
    }
    UPSVersusSubsystem* Versus = Rig.Versus;
    TestTrue(TEXT("Without a session the player's settings decide"), Versus->ShouldShowOverlay(EPSVersusOverlay::RouteArt, Rig.P1));
    if (!TestTrue(TEXT("Session started"), Rig.Start()))
    {
        Rig.Teardown();
        return false;
    }

    // The authored rules: owner only, on one shared screen -- so nobody.
    TestFalse(TEXT("Shared screen: no route art for the offense either"), Versus->ShouldShowOverlay(EPSVersusOverlay::RouteArt, Rig.P1));
    TestFalse(TEXT("Shared screen: no defensive icons"), Versus->ShouldShowOverlay(EPSVersusOverlay::DefensiveIcons, Rig.P2));

    FPSVersusRules Split = Versus->GetRules();
    Split.Screen = EPSVersusScreen::Split;
    TestTrue(TEXT("Split-screen rules"), Versus->SetRules(Split));
    TestTrue(TEXT("Split: the offense sees its route art"), Versus->ShouldShowOverlay(EPSVersusOverlay::RouteArt, Rig.P1));
    TestFalse(TEXT("...the defense does not"), Versus->ShouldShowOverlay(EPSVersusOverlay::RouteArt, Rig.P2));
    TestTrue(TEXT("Split: the defense sees its icons"), Versus->ShouldShowOverlay(EPSVersusOverlay::DefensiveIcons, Rig.P2));
    TestFalse(TEXT("...the offense does not"), Versus->ShouldShowOverlay(EPSVersusOverlay::DefensiveIcons, Rig.P1));
    TestFalse(TEXT("A view with no seat sees neither"), Versus->ShouldShowOverlay(EPSVersusOverlay::RouteArt, nullptr));

    PSVersusTests::PublishPossession(Rig.Bus, false);
    TestTrue(TEXT("After the turnover the route art is player 2's"), Versus->ShouldShowOverlay(EPSVersusOverlay::RouteArt, Rig.P2) && !Versus->ShouldShowOverlay(EPSVersusOverlay::RouteArt, Rig.P1));

    Split.RouteArtAudience = EPSVersusAudience::Everyone;
    Split.DefensiveIconsAudience = EPSVersusAudience::Nobody;
    TestTrue(TEXT("Open route art, no icons"), Versus->SetRules(Split));
    TestTrue(TEXT("Everyone sees route art"), Versus->ShouldShowOverlay(EPSVersusOverlay::RouteArt, Rig.P1) && Versus->ShouldShowOverlay(EPSVersusOverlay::RouteArt, Rig.P2));
    TestFalse(TEXT("Nobody sees the icons"), Versus->ShouldShowOverlay(EPSVersusOverlay::DefensiveIcons, Rig.P1) || Versus->ShouldShowOverlay(EPSVersusOverlay::DefensiveIcons, Rig.P2));

    Rig.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// Test 7 -- Pause, resume and quit etiquette
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSVersusPauseTest,
    "PlaySports.Versus.PauseEtiquette",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSVersusPauseTest::RunTest(const FString& Parameters)
{
    PSVersusTests::FVersusRig Rig;
    if (!TestTrue(TEXT("Head-to-head world"), Rig.Build()) || !TestTrue(TEXT("Session started"), Rig.Start()))
    {
        Rig.Teardown();
        return false;
    }
    UPSLocalization::RegisterStringTables();
    UPSLocalization::SetPseudoLocalization(false);
    UPSVersusSubsystem* Versus = Rig.Versus;
    UPSMenuComponent* Menu1 = Rig.P1->GetMenuComponent();
    UPSMenuComponent* Menu2 = Rig.P2->GetMenuComponent();
    const FName PauseScreen = Menu1->GetCatalog().PauseScreen;
    const FPSVersusRules& Rules = Versus->GetRules();
    const int32 PerHalf = Rules.PausesPerHalf;

    TArray<FPSTelemetryVersusEvent> Events;
    const FDelegateHandle Handle = Rig.Bus->OnVersusMC.AddLambda([&Events](const FPSTelemetryVersusEvent& Event) { Events.Add(Event); });

    // Player 2 pauses between plays: both get the pause screen.
    Menu2->TogglePause();
    TestEqual(TEXT("Paused"), Versus->GetPhase(), EPSVersusPhase::Paused);
    TestEqual(TEXT("...by seat 1"), Versus->GetPausedBySeat(), 1);
    TestEqual(TEXT("Player 1 sees the pause screen"), Menu1->GetTopScreenId(), PauseScreen);
    TestEqual(TEXT("Player 2 sees the pause screen"), Menu2->GetTopScreenId(), PauseScreen);
    TestEqual(TEXT("One of player 2's pauses is used"), Versus->GetPausesLeft(1), PerHalf - 1);
    TestTrue(TEXT("The pause is announced with the pauses left"), Events.Num() > 0 && Events.Last().Kind == EPSVersusEventKind::Paused && Events.Last().PausesLeft == PerHalf - 1);
    TestTrue(TEXT("The HUD names who paused"), Versus->DescribeStatus().ToString().Contains(TEXT("Player 2")));

    // Player 2 resumes from the pause screen: ready, but player 1 isn't yet.
    Menu2->ChooseOption(TEXT("Resume"));
    TestFalse(TEXT("Player 2's pause screen closes"), Menu2->IsMenuOpen());
    TestEqual(TEXT("Still paused: player 1 isn't ready"), Versus->GetPhase(), EPSVersusPhase::Paused);
    TestTrue(TEXT("The HUD says who it waits for"), Versus->DescribeStatus().ToString().Contains(TEXT("Player 1")));
    TestTrue(TEXT("No countdown yet"), Versus->GetResumeCountdown() < 0.f);

    // Player 1 backs out of the pause screen: both ready, the countdown runs.
    TestTrue(TEXT("Back on the pause screen is player 1's ready"), Menu1->HandleBack());
    TestEqual(TEXT("Still paused during the countdown"), Versus->GetPhase(), EPSVersusPhase::Paused);
    TestEqual(TEXT("The countdown is the rules'"), Versus->GetResumeCountdown(), Rules.ResumeCountdownSeconds);
    Versus->AdvanceResume(Rules.ResumeCountdownSeconds * 0.5f);
    TestEqual(TEXT("Halfway: still paused"), Versus->GetPhase(), EPSVersusPhase::Paused);
    Versus->AdvanceResume(Rules.ResumeCountdownSeconds);
    TestEqual(TEXT("The countdown ends: play"), Versus->GetPhase(), EPSVersusPhase::Playing);
    TestFalse(TEXT("Every pause screen is closed"), Menu1->IsMenuOpen() || Menu2->IsMenuOpen());
    TestEqual(TEXT("Resumed is announced"), PSVersusTests::CountKind(Events, EPSVersusEventKind::Resumed), 1);

    // No pausing a live play.
    PSVersusTests::PublishSnap(Rig.Bus);
    Menu1->TogglePause();
    TestEqual(TEXT("A pause during the play is refused"), Versus->GetPhase(), EPSVersusPhase::Playing);
    TestFalse(TEXT("...and opens no screen"), Menu1->IsMenuOpen());
    TestTrue(TEXT("...with its reason on the bus"), Events.Num() > 0 && Events.Last().Kind == EPSVersusEventKind::PauseRefused && Events.Last().Reason == TEXT("Versus.Refused.PlayLive"));
    TestEqual(TEXT("A refused pause costs nothing"), Versus->GetPausesLeft(0), PerHalf);
    PSVersusTests::PublishNewDown(Rig.Bus);

    // Player 2 uses the rest of their pauses this half.
    for (int32 Used = 1; Used < PerHalf; ++Used)
    {
        TestTrue(TEXT("Player 2 pauses again"), Versus->RequestPause(1));
        Versus->ConfirmResume(0);
        Versus->ConfirmResume(1);
        Versus->AdvanceResume(Rules.ResumeCountdownSeconds + 1.f);
    }
    TestEqual(TEXT("Player 2 has no pauses left"), Versus->GetPausesLeft(1), 0);
    TestFalse(TEXT("...so a pause is refused"), Versus->RequestPause(1));
    TestTrue(TEXT("...for that reason"), Events.Last().Kind == EPSVersusEventKind::PauseRefused && Events.Last().Reason == TEXT("Versus.Refused.NoPausesLeft"));
    PSVersusTests::PublishPossession(Rig.Bus, true, 3);
    TestEqual(TEXT("The second half gives them back"), Versus->GetPausesLeft(1), PerHalf);

    // Player 1's controller disconnects mid-play: paused anyway, uncounted, until it is back.
    UPSInputDeviceComponent* Devices1 = Rig.P1->GetInputDeviceComponent();
    PSVersusTests::PublishSnap(Rig.Bus);
    Devices1->NotifyUserConnectionChange(1, false, true);
    TestEqual(TEXT("Another user's disconnect is not player 1's"), Versus->GetPhase(), EPSVersusPhase::Playing);
    Devices1->NotifyUserConnectionChange(0, false, true);
    TestEqual(TEXT("Player 1's disconnect pauses mid-play"), Versus->GetPhase(), EPSVersusPhase::Paused);
    TestEqual(TEXT("...not counted"), Versus->GetPausesLeft(0), PerHalf);
    TestTrue(TEXT("The HUD asks for the controller"), Versus->DescribeStatus().ToString().Contains(TEXT("reconnect")));
    Versus->ConfirmResume(0);
    Versus->ConfirmResume(1);
    TestTrue(TEXT("Both ready, but no countdown while a controller is gone"), Versus->GetResumeCountdown() < 0.f);
    Devices1->NotifyUserConnectionChange(0, true, true);
    TestTrue(TEXT("Back: the countdown starts"), Versus->GetResumeCountdown() >= 0.f);
    Versus->AdvanceResume(Rules.ResumeCountdownSeconds + 1.f);
    TestEqual(TEXT("Play on"), Versus->GetPhase(), EPSVersusPhase::Playing);
    PSVersusTests::PublishNewDown(Rig.Bus);

    // Player 1 pauses and player 2 quits from the pause screen: player 1 wins by forfeit.
    Menu1->TogglePause();
    TestEqual(TEXT("Paused for the quit"), Menu2->GetTopScreenId(), PauseScreen);
    Menu2->ChooseOption(TEXT("QuitToMenu"));
    TestEqual(TEXT("Quitting ends the session"), Versus->GetPhase(), EPSVersusPhase::Ended);
    TestEqual(TEXT("...and player 1 wins"), Versus->GetWinnerSeat(), 0);
    TestEqual(TEXT("The forfeit is announced"), PSVersusTests::CountKind(Events, EPSVersusEventKind::Forfeit), 1);
    TestTrue(TEXT("The HUD names the winner"), Versus->DescribeStatus().ToString().Contains(TEXT("Player 1")));

    Rig.Bus->OnVersusMC.Remove(Handle);
    Rig.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// Test 8 -- Each seat's devices and rumble are its own
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSVersusDevicesTest,
    "PlaySports.Versus.PerSeatDevicesAndRumble",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSVersusDevicesTest::RunTest(const FString& Parameters)
{
    PSVersusTests::FVersusRig Rig;
    if (!TestTrue(TEXT("Head-to-head world"), Rig.Build()))
    {
        Rig.Teardown();
        return false;
    }
    UPSVersusSubsystem* Versus = Rig.Versus;
    APSPlayerController* Unseated = PSVersusTests::SpawnHuman(Rig.World);
    UPSInputDeviceComponent* Devices1 = Rig.P1->GetInputDeviceComponent();
    UPSInputDeviceComponent* Devices2 = Rig.P2->GetInputDeviceComponent();
    UPSForceFeedbackComponent* Rumble1 = Rig.P1->GetForceFeedbackComponent();
    UPSForceFeedbackComponent* Rumble2 = Rig.P2->GetForceFeedbackComponent();
    Rumble1->BindToBus();
    Rumble2->BindToBus();

    TArray<FPSTelemetryInputDeviceEvent> DeviceEvents;
    const FDelegateHandle Handle = Rig.Bus->OnInputDeviceChangeMC.AddLambda([&DeviceEvents](const FPSTelemetryInputDeviceEvent& Event) { DeviceEvents.Add(Event); });

    TestTrue(TEXT("Both seated"), Versus->ClaimSeat(Rig.P1, 0) == 0 && Versus->ClaimSeat(Rig.P2, 1) == 1);
    const EPSInputDevice Start1 = Devices1->GetActiveDevice();

    // Player 2 presses A on the second pad: every component hears it, only seat 1's counts it.
    Devices1->NotifyUserInput(1, EKeys::Gamepad_FaceButton_Bottom, 1.f);
    Devices2->NotifyUserInput(1, EKeys::Gamepad_FaceButton_Bottom, 1.f);
    TestTrue(TEXT("Player 2 is on the gamepad"), Devices2->GetActiveDevice() == EPSInputDevice::Gamepad);
    TestTrue(TEXT("Player 1's device didn't move"), Devices1->GetActiveDevice() == Start1);
    TestTrue(TEXT("The change names human 1"), DeviceEvents.Num() == 1 && DeviceEvents[0].HumanIndex == 1);
    TestTrue(TEXT("Player 2's rumble follows player 2's pad"), Rumble2->GetActiveDevice() == EPSInputDevice::Gamepad);
    TestTrue(TEXT("Player 1's rumble does not"), Rumble1->GetActiveDevice() != EPSInputDevice::Gamepad);

    // A controller nobody seated still counts every user's input, as for one player.
    Unseated->GetInputDeviceComponent()->NotifyUserInput(1, EKeys::Gamepad_FaceButton_Bottom, 1.f);
    TestTrue(TEXT("An unseated controller hears any pad"), Unseated->GetInputDeviceComponent()->GetActiveDevice() == EPSInputDevice::Gamepad);

    // Each player's rumble tracks their own player.
    TestTrue(TEXT("Player 1 takes the QB"), Rig.P1->TakeControlOf(Rig.QB));
    TestTrue(TEXT("Player 2 takes the linebacker"), Rig.P2->TakeControlOf(Rig.LB));
    TestEqual(TEXT("Player 1's rumble is on the QB"), Rumble1->GetControlledPlayerName(), FString(TEXT("QB_V")));
    TestEqual(TEXT("Player 2's rumble is on the linebacker"), Rumble2->GetControlledPlayerName(), FString(TEXT("LB_V")));

    Rig.Bus->OnInputDeviceChangeMC.Remove(Handle);
    Rig.Teardown();
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
