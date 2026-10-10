// PSFieldSidesTests.cpp -- the team with the ball lines up on offense (UPSFieldSides)
//
// Tests covered:
//   1. A turnover swaps the field's teams: with both teams on the field roster, the home team
//      lines up on offense at kickoff; after an interception the simulation (the possession
//      authority) gives the away team the ball, and at the next play's refill the away team's
//      players take the offense's pawns and the home team's the defense's, role for role, the
//      snapper keeping the ball. A second interception swaps them back. A team short of a
//      package fields only its own players, and one roster playing both sides is left alone.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSFieldGrid.h"
#include "PSFieldSides.h"
#include "PSPersonnelManager.h"
#include "PSPlaySimulation.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerPawn.h"
#include "PSRoster.h"
#include "PSTelemetryBus.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSFieldSidesTests
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

    /** The passer throws for his receiver; the other team's defensive back picks it off at its
     *  own 40 and is tackled where he caught it. */
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

    /** How many pawns of the side carry one of Team's players. */
    static int32 CountOnSide(const TArray<APSPlayerPawn*>& Pawns, EPSTeamSide Side, const TArray<FPlayerAttributes>& Team)
    {
        int32 Count = 0;
        for (const APSPlayerPawn* Pawn : Pawns)
        {
            const FName PlayerId = Pawn->GetAttributes().PlayerId;
            if (Pawn->TeamSide == Side && Team.ContainsByPredicate([PlayerId](const FPlayerAttributes& Player) { return Player.PlayerId == PlayerId; }))
            {
                ++Count;
            }
        }
        return Count;
    }

    static TArray<EPlayerRole> RolesOf(const TArray<APSPlayerPawn*>& Pawns)
    {
        TArray<EPlayerRole> Roles;
        for (const APSPlayerPawn* Pawn : Pawns)
        {
            Roles.Add(Pawn->GetAttributes().Role);
        }
        return Roles;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- A turnover swaps the field's teams
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFieldSidesTurnoverTest,
    "PlaySports.Match.TurnoverSwapsTheFieldsTeams",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFieldSidesTurnoverTest::RunTest(const FString& Parameters)
{
    using namespace PSFieldSidesTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    // Both teams on the field roster, as the game mode loads them; the home team has the ball.
    const TArray<FPlayerAttributes> Home = MakeTeam(TEXT("HOME"));
    const TArray<FPlayerAttributes> Away = MakeTeam(TEXT("AWAY"));
    TArray<FPlayerAttributes> Field = Home;
    Field.Append(Away);
    UPSRoster* Roster = NewObject<UPSRoster>();
    Roster->InitializeRoster(Field);
    Roster->BuildDefaultDepthChart();
    UPSFieldSides* Sides = NewObject<UPSFieldSides>();
    Sides->Initialize(Roster, IdsOf(Home));
    Sides->BindToBus(Bus);
    TestTrue(TEXT("The home team starts with the ball"), Sides->IsHomeOnOffense());

    UPSPersonnelManager* Personnel = NewObject<UPSPersonnelManager>();
    Personnel->Initialize(Roster);
    if (!TestTrue(TEXT("The personnel catalog loads"), Personnel->LoadCatalogFromJson(UPSPersonnelManager::GetDefaultCatalogPath())))
    {
        DestroyTestWorld(World);
        return false;
    }
    const TArray<APSPlayerPawn*> Pawns = APSFieldGrid::SpawnPlayersFromRoster(Personnel->GetStartingLineup(), 2000.f, World);
    if (!TestEqual(TEXT("22 pawns take the field"), Pawns.Num(), 22))
    {
        DestroyTestWorld(World);
        return false;
    }
    Personnel->BindPawns(Pawns);
    Personnel->BeginNewPlay(2000.f, 1);
    TestEqual(TEXT("At kickoff the home team's eleven are the offense"), CountOnSide(Pawns, EPSTeamSide::Offense, Home), 11);
    TestEqual(TEXT("...and the away team's eleven the defense"), CountOnSide(Pawns, EPSTeamSide::Defense, Away), 11);
    const TArray<EPlayerRole> KickoffRoles = RolesOf(Pawns);

    // The snapper has the ball, as the game mode's reset gives it to him.
    APSPlayerPawn* const* SnapperSlot = Pawns.FindByPredicate([](const APSPlayerPawn* Pawn) { return Pawn->GetAttributes().Role == EPlayerRole::OffensiveLineman; });
    APSPlayerPawn* Snapper = SnapperSlot ? *SnapperSlot : nullptr;
    if (!TestNotNull(TEXT("A snapper"), Snapper))
    {
        DestroyTestWorld(World);
        return false;
    }
    Snapper->GainPossession();

    // The home passer is picked off: the simulation, the possession authority, gives the away
    // team the ball.
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    Sim->InitializePlay(Home, Away);
    Sim->InitializeWithWorld(World);
    Sim->TriggerSnap();
    Sim->ActivePenalty = EPSPenaltyType::None;
    PublishPick(Bus, Home, Away);
    Sim->EndPlayAndPrepareNext();
    TestFalse(TEXT("The away team has the ball"), Sim->GetPlayState().bHomeHasPossession);
    TestFalse(TEXT("...and the field follows it"), Sides->IsHomeOnOffense());

    // The next play's refill (the game mode's pawn reset) brings the new teams on.
    Personnel->BeginNewPlay(4000.f, 2);
    TestEqual(TEXT("The home team, which had the ball, lines up on defense"), CountOnSide(Pawns, EPSTeamSide::Defense, Home), 11);
    TestEqual(TEXT("...against the away team's offense"), CountOnSide(Pawns, EPSTeamSide::Offense, Away), 11);
    TestTrue(TEXT("Every pawn kept its role"), RolesOf(Pawns) == KickoffRoles);
    TestTrue(TEXT("The snapper is still a lineman with the ball"),
        Snapper->GetAttributes().Role == EPlayerRole::OffensiveLineman && Snapper->HasPossession());
    TestTrue(TEXT("...the away team's"), Away.ContainsByPredicate([Snapper](const FPlayerAttributes& Player) { return Player.PlayerId == Snapper->GetAttributes().PlayerId; }));
    TestEqual(TEXT("The away team's offense calls from its own packages"), Personnel->GetCurrentPackage(true), FName(TEXT("P11")));
    TArray<FName> Backups = Roster->GetDepthChartForRole(EPlayerRole::Linebacker);
    TestTrue(TEXT("The defense's depth chart is the home team's"), Backups.Num() == 3 && Backups[0] == FindRole(Home, EPlayerRole::Linebacker)->PlayerId);

    // The away passer is picked off in turn: the home team is the offense again.
    Sim->TriggerSnap();
    Sim->ActivePenalty = EPSPenaltyType::None;
    PublishPick(Bus, Away, Home);
    Sim->EndPlayAndPrepareNext();
    Personnel->BeginNewPlay(4000.f, 3);
    TestTrue(TEXT("The home team has the ball back"), Sim->GetPlayState().bHomeHasPossession && Sides->IsHomeOnOffense());
    TestEqual(TEXT("...and its offense is back on"), CountOnSide(Pawns, EPSTeamSide::Offense, Home), 11);
    TestEqual(TEXT("...against the away defense"), CountOnSide(Pawns, EPSTeamSide::Defense, Away), 11);
    Sides->UnbindFromBus();

    // A team short of its default package fields only its own players on that side.
    TArray<FPlayerAttributes> ShortAway = Away;
    ShortAway.RemoveAll([](const FPlayerAttributes& Player) { return Player.PlayerId == FName(TEXT("AWAY_DefensiveBack_4")); });
    TArray<FPlayerAttributes> ShortField = Home;
    ShortField.Append(ShortAway);
    UPSRoster* ShortRoster = NewObject<UPSRoster>();
    ShortRoster->InitializeRoster(ShortField);
    ShortRoster->BuildDefaultDepthChart();
    UPSFieldSides* ShortSides = NewObject<UPSFieldSides>();
    ShortSides->Initialize(ShortRoster, IdsOf(Home));
    UPSPersonnelManager* ShortPersonnel = NewObject<UPSPersonnelManager>();
    ShortPersonnel->Initialize(ShortRoster);
    ShortPersonnel->SetCatalog(Personnel->GetCatalog());
    int32 HomeDefenders = 0;
    int32 AwayDefenders = 0;
    for (const FPlayerAttributes* Player : ShortPersonnel->GetStartingLineup())
    {
        const bool bDefense = APSFieldGrid::GetSideForRole(Player->Role) == EPSTeamSide::Defense;
        const bool bHome = Home.ContainsByPredicate([Player](const FPlayerAttributes& Candidate) { return Candidate.PlayerId == Player->PlayerId; });
        HomeDefenders += bDefense && bHome ? 1 : 0;
        AwayDefenders += bDefense && !bHome ? 1 : 0;
    }
    TestEqual(TEXT("Short a corner, the away defense fields its ten"), AwayDefenders, 10);
    TestEqual(TEXT("...and no home defender"), HomeDefenders, 0);

    // One roster playing both sides (no home players named) keeps its depth chart.
    UPSRoster* OneRoster = NewObject<UPSRoster>();
    OneRoster->InitializeRoster(Home);
    OneRoster->BuildDefaultDepthChart();
    UPSFieldSides* OneSides = NewObject<UPSFieldSides>();
    OneSides->Initialize(OneRoster, TArray<FName>());
    OneSides->ArrangeForPossession(false);
    TestEqual(TEXT("One roster plays both sides"), OneRoster->GetDepthChartForRole(EPlayerRole::Quarterback).Num(), 1);
    TestEqual(TEXT("...on offense and defense"), OneRoster->GetDepthChartForRole(EPlayerRole::DefensiveBack).Num(), 4);

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
