// PSHumanTeamTests.cpp -- a single human keeps their own team when the ball changes hands
//
// Tests covered:
//   1. The human plays the player's team: with both teams on the field, the home team's human
//      has the home QB; when the simulation (the possession authority) gives the away team the
//      ball after an interception, the human moves to the defense's linebacker, and after the
//      next play's refill (UPSFieldSides) that is a home player, not the away offense's QB. The
//      home team picks the ball back and the human is on the home QB again.
//   2. A human whose team is the visitor starts on defense; a human holding no pawn is only
//      pointed at the side; a controller seated in a head-to-head game is left to the versus
//      subsystem. The match says which team is the player's; the control roles are data
//      (Data/control_handoff.json) and a role on the wrong side of the ball is caught.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSControlHandoffComponent.h"
#include "PSFieldGrid.h"
#include "PSFieldSides.h"
#include "PSHumanTeamComponent.h"
#include "PSMatchSetup.h"
#include "PSPersonnelManager.h"
#include "PSPlaySimulation.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSRoster.h"
#include "PSTelemetryBus.h"
#include "PSVersusSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSHumanTeamTests
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

    struct FTeamRoleCount
    {
        EPlayerRole Role;
        int32 Players;
    };

    /** A team of 22 that fills the default packages (11 personnel, base 4-3): PlayerIds and
     *  names "<Prefix>_<Role>_<n>". */
    static TArray<FPlayerAttributes> MakeTeam(const TCHAR* Prefix)
    {
        static const FTeamRoleCount Counts[] = {
            { EPlayerRole::Quarterback, 1 }, { EPlayerRole::RunningBack, 1 }, { EPlayerRole::TightEnd, 1 },
            { EPlayerRole::WideReceiver, 3 }, { EPlayerRole::OffensiveLineman, 5 },
            { EPlayerRole::DefensiveLineman, 4 }, { EPlayerRole::Linebacker, 3 }, { EPlayerRole::DefensiveBack, 4 } };
        TArray<FPlayerAttributes> Team;
        for (const FTeamRoleCount& Count : Counts)
        {
            for (int32 Number = 1; Number <= Count.Players; ++Number)
            {
                FPlayerAttributes Player;
                Player.PlayerId = FName(*FString::Printf(TEXT("%s_%s_%d"), Prefix, *UEnum::GetValueAsString(Count.Role).RightChop(13), Number));
                Player.DisplayName = Player.PlayerId.ToString();
                Player.Role = Count.Role;
                Player.Speed = 80.f;
                Player.Agility = 80.f;
                Player.Strength = 80.f;
                Player.Acceleration = 80.f;
                Player.Awareness = 80.f;
                Player.Stamina = 90.f;
                Team.Add(Player);
            }
        }
        return Team;
    }

    static TArray<FName> IdsOf(const TArray<FPlayerAttributes>& Players)
    {
        TArray<FName> Ids;
        for (const FPlayerAttributes& Player : Players)
        {
            Ids.Add(Player.PlayerId);
        }
        return Ids;
    }

    static const FPlayerAttributes* FindRole(const TArray<FPlayerAttributes>& Team, EPlayerRole Role)
    {
        return Team.FindByPredicate([Role](const FPlayerAttributes& Player) { return Player.Role == Role; });
    }

    static bool IsOnTeam(const APSPlayerPawn* Pawn, const TArray<FPlayerAttributes>& Team)
    {
        const FName PlayerId = Pawn ? Pawn->GetAttributes().PlayerId : NAME_None;
        return Team.ContainsByPredicate([PlayerId](const FPlayerAttributes& Player) { return Player.PlayerId == PlayerId; });
    }

    /** The passer throws for his receiver; the other team's defensive back picks it off and is
     *  tackled where he caught it. */
    static void PublishPick(UPSTelemetryBus* Bus, const TArray<FPlayerAttributes>& Throwing, const TArray<FPlayerAttributes>& Picking)
    {
        FPSTelemetryThrowEvent Throw;
        Throw.PasserName = FindRole(Throwing, EPlayerRole::Quarterback)->DisplayName;
        Throw.TargetReceiverName = FindRole(Throwing, EPlayerRole::WideReceiver)->DisplayName;
        Bus->PublishThrow(Throw);
        FPSTelemetryCatchEvent Pick;
        Pick.ReceiverName = FindRole(Picking, EPlayerRole::DefensiveBack)->DisplayName;
        Pick.CatchLocation = FVector(6000.f, 0.f, 100.f);
        Pick.bIsInterception = true;
        Bus->PublishCatch(Pick);
        FPSTelemetryTackleEvent ReturnTackle;
        ReturnTackle.TacklerName = Throw.TargetReceiverName;
        ReturnTackle.BallCarrierName = Pick.ReceiverName;
        ReturnTackle.YardLine = 60;
        Bus->PublishTackle(ReturnTackle);
    }

    static void PublishPossession(UPSTelemetryBus* Bus, bool bHomeHasBall)
    {
        FPSTelemetryGameStateEvent State;
        State.Phase = TEXT("PreSnap");
        State.bHomeHasPossession = bHomeHasBall;
        Bus->PublishGameState(State);
    }

    static APSPlayerController* SpawnHuman(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    }

    /** A match between HOMETEAM and AWAYTEAM, the player's team named by UserTeam (or none). */
    static UPSMatchSetup* MakeMatch(const TCHAR* UserTeam)
    {
        UPSMatchSetup* Match = NewObject<UPSMatchSetup>();
        const FString Options = FString::Printf(TEXT("mode=PlayNow?home=HOMETEAM?away=AWAYTEAM%s%s"), UserTeam ? TEXT("?team=") : TEXT(""), UserTeam ? UserTeam : TEXT(""));
        Match->InitializeFromOptions(Options, { FName(TEXT("HOMETEAM")), FName(TEXT("AWAYTEAM")) });
        return Match;
    }

    /** Both teams on the field roster, as the game mode loads them: 22 pawns, the home team's
     *  eleven on offense, the snapper holding the ball. */
    struct FFieldRig
    {
        UWorld* World = nullptr;
        UPSTelemetryBus* Bus = nullptr;
        TArray<FPlayerAttributes> Home;
        TArray<FPlayerAttributes> Away;
        UPSRoster* Roster = nullptr;
        UPSFieldSides* Sides = nullptr;
        UPSPersonnelManager* Personnel = nullptr;
        TArray<APSPlayerPawn*> Pawns;

        bool Build(FAutomationTestBase& Test)
        {
            World = CreateTestWorld();
            Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
            if (!Test.TestNotNull(TEXT("Bus"), Bus))
            {
                return false;
            }
            Home = MakeTeam(TEXT("HOME"));
            Away = MakeTeam(TEXT("AWAY"));
            TArray<FPlayerAttributes> Field = Home;
            Field.Append(Away);
            Roster = NewObject<UPSRoster>();
            Roster->InitializeRoster(Field);
            Roster->BuildDefaultDepthChart();
            Sides = NewObject<UPSFieldSides>();
            Sides->Initialize(Roster, IdsOf(Home));
            Sides->BindToBus(Bus);
            Personnel = NewObject<UPSPersonnelManager>();
            Personnel->Initialize(Roster);
            if (!Test.TestTrue(TEXT("The personnel catalog loads"), Personnel->LoadCatalogFromJson(UPSPersonnelManager::GetDefaultCatalogPath())))
            {
                return false;
            }
            Pawns = APSFieldGrid::SpawnPlayersFromRoster(Personnel->GetStartingLineup(), 2000.f, World);
            if (!Test.TestEqual(TEXT("22 pawns take the field"), Pawns.Num(), 22))
            {
                return false;
            }
            Personnel->BindPawns(Pawns);
            Personnel->BeginNewPlay(2000.f, 1);
            APSPlayerPawn* const* Snapper = Pawns.FindByPredicate([](const APSPlayerPawn* Pawn) { return Pawn->GetAttributes().Role == EPlayerRole::OffensiveLineman; });
            if (!Test.TestNotNull(TEXT("A snapper"), Snapper))
            {
                return false;
            }
            (*Snapper)->GainPossession();
            return true;
        }

        void Teardown()
        {
            if (Sides)
            {
                Sides->UnbindFromBus();
            }
            if (World)
            {
                DestroyTestWorld(World);
                World = nullptr;
            }
        }
    };
}

// ---------------------------------------------------------------------------
// Test 1 -- The human keeps their team across a turnover
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSHumanTeamTurnoverTest,
    "PlaySports.Match.HumanKeepsTeamAcrossTurnover",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSHumanTeamTurnoverTest::RunTest(const FString& Parameters)
{
    using namespace PSHumanTeamTests;

    FFieldRig Rig;
    if (!Rig.Build(*this))
    {
        Rig.Teardown();
        return false;
    }
    UPSTelemetryBus* Bus = Rig.Bus;

    // The player picked the home team; the game mode hands the match to the human.
    APSPlayerController* Human = SpawnHuman(Rig.World);
    UPSHumanTeamComponent* HumanTeam = Human ? Human->GetHumanTeamComponent() : nullptr;
    if (!TestNotNull(TEXT("The controller has a human-team component"), HumanTeam))
    {
        Rig.Teardown();
        return false;
    }
    HumanTeam->BindToBus();
    HumanTeam->SetMatchSetup(MakeMatch(TEXT("HOMETEAM")));
    TestTrue(TEXT("The human plays for the home team"), HumanTeam->PlaysForHome());
    TestEqual(TEXT("...which has the ball: offense"), Human->HumanSide, EPSTeamSide::Offense);
    TestEqual(TEXT("...on the offense's control role"), Human->DefaultControlRole, EPlayerRole::Quarterback);
    TestTrue(TEXT("The human takes the QB"), Human->TakeDefaultControl());
    const APSPlayerPawn* Controlled = Cast<APSPlayerPawn>(Human->GetPawn());
    TestTrue(TEXT("...the home team's"), Controlled && Controlled->GetAttributes().PlayerId == FindRole(Rig.Home, EPlayerRole::Quarterback)->PlayerId);

    // The home passer is picked off: the simulation gives the away team the ball.
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    Sim->InitializePlay(Rig.Home, Rig.Away);
    Sim->InitializeWithWorld(Rig.World);
    Sim->TriggerSnap();
    Sim->ActivePenalty = EPSPenaltyType::None;
    PublishPick(Bus, Rig.Home, Rig.Away);
    Sim->EndPlayAndPrepareNext();
    TestFalse(TEXT("The away team has the ball"), Sim->GetPlayState().bHomeHasPossession);
    TestEqual(TEXT("The human's team defends"), Human->HumanSide, EPSTeamSide::Defense);
    TestEqual(TEXT("...on the defense's control role"), Human->DefaultControlRole, EPlayerRole::Linebacker);
    Controlled = Cast<APSPlayerPawn>(Human->GetPawn());
    TestTrue(TEXT("The human let go of the offense and has a linebacker"),
        Controlled && Controlled->TeamSide == EPSTeamSide::Defense && Controlled->GetAttributes().Role == EPlayerRole::Linebacker);

    // The next play's refill brings the teams on: the human is with his own team.
    Rig.Personnel->BeginNewPlay(4000.f, 2);
    Controlled = Cast<APSPlayerPawn>(Human->GetPawn());
    TestTrue(TEXT("After the refill the human plays a home linebacker"), IsOnTeam(Controlled, Rig.Home));
    const APSPlayerPawn* const* OffenseQB = Rig.Pawns.FindByPredicate([](const APSPlayerPawn* Pawn) { return Pawn->TeamSide == EPSTeamSide::Offense && Pawn->GetAttributes().Role == EPlayerRole::Quarterback; });
    TestTrue(TEXT("...and the away team's QB is the AI's"), OffenseQB && IsOnTeam(*OffenseQB, Rig.Away) && !(*OffenseQB)->IsUserControlled());

    // The away passer is picked off in turn: the human is back on the home offense.
    Sim->TriggerSnap();
    Sim->ActivePenalty = EPSPenaltyType::None;
    PublishPick(Bus, Rig.Away, Rig.Home);
    Sim->EndPlayAndPrepareNext();
    Rig.Personnel->BeginNewPlay(4000.f, 3);
    TestTrue(TEXT("The home team has the ball back"), Sim->GetPlayState().bHomeHasPossession);
    TestEqual(TEXT("The human is on offense again"), Human->HumanSide, EPSTeamSide::Offense);
    Controlled = Cast<APSPlayerPawn>(Human->GetPawn());
    TestTrue(TEXT("...on the home QB"), Controlled && Controlled->GetAttributes().PlayerId == FindRole(Rig.Home, EPlayerRole::Quarterback)->PlayerId);

    HumanTeam->UnbindFromBus();
    Rig.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The visitor's human, a human without a pawn, a versus seat, the data
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSHumanTeamSidesTest,
    "PlaySports.Match.HumanTeamSides",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSHumanTeamSidesTest::RunTest(const FString& Parameters)
{
    using namespace PSHumanTeamTests;

    // Which team is the player's is the match's.
    TestTrue(TEXT("The player's home team"), MakeMatch(TEXT("HOMETEAM"))->IsUserTeamHome());
    TestFalse(TEXT("The player's visiting team"), MakeMatch(TEXT("AWAYTEAM"))->IsUserTeamHome());
    TestTrue(TEXT("No team named: the human plays the home team"), MakeMatch(nullptr)->IsUserTeamHome());

    FFieldRig Rig;
    if (!Rig.Build(*this))
    {
        Rig.Teardown();
        return false;
    }
    UPSTelemetryBus* Bus = Rig.Bus;

    // The visitor's human: the home team has the ball, so he starts on defense.
    APSPlayerController* Visitor = SpawnHuman(Rig.World);
    UPSHumanTeamComponent* VisitorTeam = Visitor ? Visitor->GetHumanTeamComponent() : nullptr;
    if (!TestNotNull(TEXT("The visitor's human-team component"), VisitorTeam))
    {
        Rig.Teardown();
        return false;
    }
    VisitorTeam->BindToBus();
    VisitorTeam->SetMatchSetup(MakeMatch(TEXT("AWAYTEAM")));
    TestFalse(TEXT("The visitor plays for the away team"), VisitorTeam->PlaysForHome());
    TestEqual(TEXT("...on defense at kickoff"), Visitor->HumanSide, EPSTeamSide::Defense);
    TestTrue(TEXT("He takes the defense's control role"), Visitor->TakeDefaultControl());
    const APSPlayerPawn* Controlled = Cast<APSPlayerPawn>(Visitor->GetPawn());
    TestTrue(TEXT("...an away linebacker"), Controlled && Controlled->GetAttributes().Role == EPlayerRole::Linebacker && IsOnTeam(Controlled, Rig.Away));

    // The away team gets the ball: the visitor goes to his offense.
    PublishPossession(Bus, false);
    Controlled = Cast<APSPlayerPawn>(Visitor->GetPawn());
    TestTrue(TEXT("With the ball the visitor has the offense's QB"),
        Visitor->HumanSide == EPSTeamSide::Offense && Controlled && Controlled->TeamSide == EPSTeamSide::Offense && Controlled->GetAttributes().Role == EPlayerRole::Quarterback);
    PublishPossession(Bus, true);
    Visitor->ReleaseControl();
    VisitorTeam->UnbindFromBus();

    // A human with no pawn (a CPU-only game) is pointed at his side and takes nobody.
    APSPlayerController* Watcher = SpawnHuman(Rig.World);
    UPSHumanTeamComponent* WatcherTeam = Watcher ? Watcher->GetHumanTeamComponent() : nullptr;
    if (TestNotNull(TEXT("The watcher's human-team component"), WatcherTeam))
    {
        WatcherTeam->BindToBus();
        WatcherTeam->SetMatchSetup(MakeMatch(nullptr));
        PublishPossession(Bus, false);
        TestEqual(TEXT("The watcher's side follows his team to defense"), Watcher->HumanSide, EPSTeamSide::Defense);
        TestNull(TEXT("...without taking a player"), Cast<APSPlayerPawn>(Watcher->GetPawn()));
        PublishPossession(Bus, true);
        TestEqual(TEXT("...and back to offense"), Watcher->HumanSide, EPSTeamSide::Offense);
        WatcherTeam->UnbindFromBus();
    }

    // A head-to-head seat is the versus subsystem's: the component leaves it where it is.
    APSPlayerController* Seated = SpawnHuman(Rig.World);
    UPSHumanTeamComponent* SeatedTeam = Seated ? Seated->GetHumanTeamComponent() : nullptr;
    UPSVersusSubsystem* Versus = Rig.World->GetSubsystem<UPSVersusSubsystem>();
    if (TestNotNull(TEXT("The seated human-team component"), SeatedTeam) && TestNotNull(TEXT("Versus subsystem"), Versus))
    {
        SeatedTeam->BindToBus();
        SeatedTeam->SetMatchSetup(MakeMatch(TEXT("HOMETEAM")));
        TestTrue(TEXT("The seated human takes the QB"), Seated->TakeDefaultControl());
        const APSPlayerPawn* SeatedPawn = Cast<APSPlayerPawn>(Seated->GetPawn());
        TestEqual(TEXT("He sits in a versus seat"), Versus->ClaimSeat(Seated, 0), 0);
        PublishPossession(Bus, false);
        TestTrue(TEXT("The ball changing hands leaves a versus seat alone"), Seated->GetPawn() == SeatedPawn && Seated->HumanSide == EPSTeamSide::Offense);
        PublishPossession(Bus, true);
        SeatedTeam->UnbindFromBus();
    }

    // The control roles are data; a role on the wrong side of the ball is caught.
    if (UPSControlHandoffComponent* Handoff = Visitor ? Visitor->GetControlHandoffComponent() : nullptr)
    {
        const FControlHandoffTuningRow Defaults;
        const FControlHandoffTuningRow& Tuning = Handoff->GetTuning();
        TestEqual(TEXT("control_handoff.json's offense role is the struct's default"), Tuning.OffenseControlRole, Defaults.OffenseControlRole);
        TestEqual(TEXT("...and its defense role"), Tuning.DefenseControlRole, Defaults.DefenseControlRole);
        TestEqual(TEXT("The loaded tuning is sound"), UPSControlHandoffComponent::ValidateTuning(Tuning).Num(), 0);
        FControlHandoffTuningRow Swapped = Tuning;
        Swapped.OffenseControlRole = EPlayerRole::Linebacker;
        Swapped.DefenseControlRole = EPlayerRole::Quarterback;
        TestEqual(TEXT("Roles on the wrong sides are caught"), UPSControlHandoffComponent::ValidateTuning(Swapped).Num(), 2);
    }

    Rig.Teardown();
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
