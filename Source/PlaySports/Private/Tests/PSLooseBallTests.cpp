// PSLooseBallTests.cpp -- Epic 17.4: blocked-kick chaos, the players play the loose ball
//
// Tests covered:
//   1. A blocked punt: the simulation announces the block and waits; the ball comes out of the
//      punter's hands onto the ground behind the line; both sides near it go for it, those far
//      away don't. A muff squirts it away; a defender in the clear scoops it up and returns it,
//      chased by the kicking team, and scores. The play's outcome is that touchdown.
//   2. A blocked field goal: loose at the hold; a defender with a blocker on him falls on it; the
//      ball goes over at that spot.
//   3. The kicking team falls on it, and the turnover is still at the spot; with no ball on the
//      field to play, a block ends the play on the special-teams model's roll as before.
//   4. The shipped tuning loads and is sound.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSLooseBallSubsystem.h"
#include "PSOffenseController.h"
#include "PSPlaySimulation.h"
#include "PSPlayerPawn.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSSpecialTeamsModel.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSLooseBallTests
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
            AI->GetSkillAI()->BindToBus();
        }
        return Pawn;
    }

    static UPSDefenderAIComponent* DefenseOf(const APSPlayerPawn* Pawn)
    {
        const APSDefenseController* Controller = Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr;
        return Controller ? Controller->GetDefenderAI() : nullptr;
    }

    static UPSSkillPlayerAIComponent* OffenseOf(const APSPlayerPawn* Pawn)
    {
        const APSOffenseController* Controller = Pawn ? Cast<APSOffenseController>(Pawn->GetController()) : nullptr;
        return Controller ? Controller->GetSkillAI() : nullptr;
    }

    static APSBall* GiveBall(UWorld* World, APSPlayerPawn* Holder)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), Holder->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);
        if (Ball)
        {
            Ball->AttachToCarrier(Holder, TEXT("HandSocket"));
            Holder->GainPossession();
        }
        return Ball;
    }

    /** Sure hands (or none) for whoever gets to the loose ball. */
    static void SetRecoveryChance(APSBall* Ball, float Chance)
    {
        Ball->CatchTuningSettings.FumbleRecoveryChanceMin = Chance;
        Ball->CatchTuningSettings.FumbleRecoveryChanceMax = Chance;
    }

    /** The simulation's own 20 (its starting spot) is the line of scrimmage: X 2000. */
    static constexpr float LineX = 2000.f;
    static constexpr int32 LineYards = 20;

    /** A play simulation on World's bus whose kicks are always blocked. */
    static UPSPlaySimulation* MakeBlockingSim(UWorld* World)
    {
        UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>(World);
        Sim->InitializeWithWorld(World);
        Sim->InitializePlay(TArray<FPlayerAttributes>(), TArray<FPlayerAttributes>());
        FPSSpecialTeamsTuning Blocks = Sim->GetSpecialTeams()->GetTuning();
        Blocks.PuntBlockChance = 1.f;
        Blocks.FieldGoalBlockChance = 1.f;
        Blocks.MaxBlockChance = 1.f;
        Sim->GetSpecialTeams()->SetTuning(Blocks);
        return Sim;
    }

    /** The snap at the 20, then the kick: the CPU kicker kicks after two seconds. */
    static void SnapAndKick(UPSTelemetryBus* Bus, UPSPlaySimulation* Sim, EPlayPhase Kick)
    {
        FPSTelemetrySnapEvent Snap;
        Snap.LineOfScrimmage = FVector(LineX, 0.f, 0.f);
        Snap.YardLine = LineYards;
        Snap.Down = 4;
        Bus->PublishSnap(Snap);
        Sim->SetPlayPhase(Kick);
        Sim->ActivePenalty = EPSPenaltyType::None;
        Sim->AdvancePlay(2.1f);
    }

    static bool PointsToward(const FVector& Direction, const FVector& From, const FVector& To)
    {
        FVector Expected = To - From;
        Expected.Z = 0.f;
        FVector Actual = Direction;
        Actual.Z = 0.f;
        return Actual.GetSafeNormal().Equals(Expected.GetSafeNormal(), 0.01f);
    }

    static const FPSTelemetryLooseBallEvent* FindKind(const TArray<FPSTelemetryLooseBallEvent>& Events, EPSLooseBallEventKind Kind)
    {
        return Events.FindByPredicate([Kind](const FPSTelemetryLooseBallEvent& Event) { return Event.Kind == Kind; });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- A blocked punt: the chase, a muff, a scoop and score
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLooseBallBlockedPuntTest,
    "PlaySports.BrokenPlay.BlockedPuntScoopAndScore",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLooseBallBlockedPuntTest::RunTest(const FString& Parameters)
{
    using namespace PSLooseBallTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSLooseBallSubsystem* Loose = World ? World->GetSubsystem<UPSLooseBallSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Loose-ball subsystem"), Loose))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    // The punter 14 yards deep with the ball, a lineman up front; the rusher who blocked it, and
    // a returner far downfield.
    APSPlayerPawn* Punter = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("P"), FVector(LineX - 1400.f, 0.f, 100.f));
    APSPlayerPawn* Guard = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OG"), FVector(LineX - 50.f, 200.f, 100.f));
    APSPlayerPawn* Rusher = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL"), FVector(LineX - 700.f, 300.f, 100.f));
    APSPlayerPawn* Returner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("PR"), FVector(LineX + 4000.f, 0.f, 100.f));
    APSBall* Ball = Punter ? GiveBall(World, Punter) : nullptr;
    if (!TestNotNull(TEXT("Punter"), Punter) || !TestNotNull(TEXT("Guard"), Guard) || !TestNotNull(TEXT("Rusher"), Rusher)
        || !TestNotNull(TEXT("Returner"), Returner) || !TestNotNull(TEXT("Ball"), Ball))
    {
        DestroyTestWorld(World);
        return false;
    }
    TArray<FPSTelemetryLooseBallEvent> Events;
    const FDelegateHandle Handle = Bus->OnLooseBallMC.AddLambda([&Events](const FPSTelemetryLooseBallEvent& Event) { Events.Add(Event); });

    UPSPlaySimulation* Sim = MakeBlockingSim(World);
    const int32 Recoil = Sim->GetSpecialTeams()->GetTuning().BlockedPuntRecoilYards;
    SnapAndKick(Bus, Sim, EPlayPhase::Punt);

    // Blocked: announced, taken live, and the play goes on.
    const FPSTelemetryLooseBallEvent* Block = FindKind(Events, EPSLooseBallEventKind::Blocked);
    TestTrue(TEXT("The punt is blocked and announced"), Block && Block->KickType == TEXT("Punt") && Block->YardsBehindLine == Recoil);
    TestTrue(TEXT("The ball is loose"), Loose->IsLoose());
    TestEqual(TEXT("...and the play goes on"), Sim->GetPlayState().Phase, EPlayPhase::BallCarrierMovement);
    TestFalse(TEXT("It is out of the punter's hands"), Punter->HasPossession());
    TestNull(TEXT("...on the ground"), Ball->GetAttachParentActor());
    const FVector LooseSpot(LineX - Recoil * 100.f, 0.f, Ball->GetActorLocation().Z);
    TestTrue(TEXT("...the recoil behind the line"), Ball->GetActorLocation().Equals(LooseSpot, 1.f));

    // Everyone near it goes for it, whatever his call; the returner far downfield doesn't.
    DefenseOf(Rusher)->TickAI(0.1f);
    OffenseOf(Punter)->TickAI(0.1f);
    DefenseOf(Returner)->TickAI(0.1f);
    TestEqual(TEXT("The rusher goes for the ball"), DefenseOf(Rusher)->GetAction(), EPSDefenderAction::LooseBall);
    TestTrue(TEXT("...at it"), PointsToward(DefenseOf(Rusher)->GetDesiredDirection(), Rusher->GetActorLocation(), Ball->GetActorLocation()));
    TestEqual(TEXT("So does the punter"), OffenseOf(Punter)->GetAction(), EPSSkillPlayerAction::LooseBall);
    TestTrue(TEXT("...at it"), PointsToward(OffenseOf(Punter)->GetDesiredDirection(), Punter->GetActorLocation(), Ball->GetActorLocation()));
    TestTrue(TEXT("The returner far downfield doesn't"), DefenseOf(Returner)->GetAction() != EPSDefenderAction::LooseBall);

    // The rusher gets there first and can't hold on: it squirts away.
    SetRecoveryChance(Ball, 0.f);
    Rusher->SetActorLocation(Ball->GetActorLocation() + FVector(0.f, 50.f, 0.f));
    Loose->UpdateLooseBall(0.1f);
    const FPSTelemetryLooseBallEvent* Muff = FindKind(Events, EPSLooseBallEventKind::Muffed);
    TestTrue(TEXT("A muff"), Muff && Muff->PlayerName == TEXT("DL"));
    TestTrue(TEXT("...squirts the ball away"), FMath::IsNearlyEqual(FVector::Dist2D(Ball->GetActorLocation(), LooseSpot), Loose->GetTuning().SquirtDistance, 1.f));
    TestTrue(TEXT("...still loose"), Loose->IsLoose());

    // He gets to it again, in the clear: he scoops it up and goes.
    SetRecoveryChance(Ball, 1.f);
    Punter->SetActorLocation(FVector(LineX - 1400.f, 1500.f, 100.f));
    Guard->SetActorLocation(FVector(LineX + 500.f, 1500.f, 100.f));
    Rusher->SetActorLocation(Ball->GetActorLocation() + FVector(0.f, 50.f, 0.f));
    Loose->UpdateLooseBall(Loose->GetTuning().RetrySeconds + 0.1f);
    const FPSTelemetryLooseBallEvent* Scoop = FindKind(Events, EPSLooseBallEventKind::Recovered);
    TestTrue(TEXT("The rusher scoops it up"), Scoop && Scoop->PlayerName == TEXT("DL") && Scoop->bScooped && !Scoop->bKickingTeam);
    TestTrue(TEXT("...and has it"), Rusher->HasPossession() && Ball->GetAttachParentActor() == Rusher);
    TestTrue(TEXT("...returning it"), Loose->IsReturning() && Loose->GetReturner() == Rusher);
    DefenseOf(Rusher)->TickAI(0.1f);
    TestEqual(TEXT("He runs it back"), DefenseOf(Rusher)->GetAction(), EPSDefenderAction::Return);
    TestTrue(TEXT("...toward the kicking team's goal line"), DefenseOf(Rusher)->GetDesiredDirection().X < 0.f);
    OffenseOf(Punter)->TickAI(0.1f);
    TestTrue(TEXT("The punter runs him down"), OffenseOf(Punter)->GetAction() == EPSSkillPlayerAction::LooseBall
        && PointsToward(OffenseOf(Punter)->GetDesiredDirection(), Punter->GetActorLocation(), Rusher->GetActorLocation()));

    // Into the end zone: a touchdown for the defense, and the play's outcome.
    Rusher->SetActorLocation(FVector(-50.f, 300.f, 100.f));
    Loose->UpdateLooseBall(0.1f);
    const FPSTelemetryLooseBallEvent* Dead = FindKind(Events, EPSLooseBallEventKind::Dead);
    TestTrue(TEXT("Scored: dead in the end zone"), Dead && Dead->bTouchdown && Dead->YardLine == 0 && !Dead->bKickingTeam);
    TestEqual(TEXT("The play is over"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);
    TestTrue(TEXT("...a blocked-kick touchdown"), Sim->GetLastSpecialTeamsOutcome().Result == EPSSpecialTeamsResult::BlockedTouchdown);
    const int32 AwayBefore = Sim->GetPlayState().AwayScore;
    Sim->EndPlayAndPrepareNext();
    TestTrue(TEXT("The defense's points are on the board"), Sim->GetPlayState().AwayScore >= AwayBefore + 6);

    Bus->OnLooseBallMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- A blocked field goal, fallen on
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLooseBallBlockedFieldGoalTest,
    "PlaySports.BrokenPlay.BlockedFieldGoalFallenOn",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLooseBallBlockedFieldGoalTest::RunTest(const FString& Parameters)
{
    using namespace PSLooseBallTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSLooseBallSubsystem* Loose = World ? World->GetSubsystem<UPSLooseBallSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Loose-ball subsystem"), Loose))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    const int32 HoldYards = Loose->GetTuning().BlockedFieldGoalYards;
    APSPlayerPawn* Holder = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("H"), FVector(LineX - HoldYards * 100.f, 0.f, 100.f));
    APSPlayerPawn* Wing = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("W"), FVector(LineX - HoldYards * 100.f + 100.f, -150.f, 100.f));
    APSPlayerPawn* Rusher = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL"), FVector(LineX + 100.f, 100.f, 100.f));
    APSBall* Ball = Holder ? GiveBall(World, Holder) : nullptr;
    if (!TestNotNull(TEXT("Holder"), Holder) || !TestNotNull(TEXT("Wing"), Wing) || !TestNotNull(TEXT("Rusher"), Rusher) || !TestNotNull(TEXT("Ball"), Ball))
    {
        DestroyTestWorld(World);
        return false;
    }
    TArray<FPSTelemetryLooseBallEvent> Events;
    const FDelegateHandle Handle = Bus->OnLooseBallMC.AddLambda([&Events](const FPSTelemetryLooseBallEvent& Event) { Events.Add(Event); });

    UPSPlaySimulation* Sim = MakeBlockingSim(World);
    SnapAndKick(Bus, Sim, EPlayPhase::FieldGoal);
    TestTrue(TEXT("The field goal is blocked and comes loose"), Loose->IsLoose() && Sim->GetPlayState().Phase == EPlayPhase::BallCarrierMovement);
    const FVector HoldSpot(LineX - HoldYards * 100.f, 0.f, Ball->GetActorLocation().Z);
    TestTrue(TEXT("...at the hold"), Ball->GetActorLocation().Equals(HoldSpot, 1.f));

    // The rusher gets to it with the wing on him: he falls on it.
    SetRecoveryChance(Ball, 1.f);
    Holder->SetActorLocation(FVector(LineX - 2500.f, 0.f, 100.f));
    Rusher->SetActorLocation(HoldSpot + FVector(0.f, 50.f, 0.f));
    Loose->UpdateLooseBall(0.1f);
    const FPSTelemetryLooseBallEvent* Recovery = FindKind(Events, EPSLooseBallEventKind::Recovered);
    TestTrue(TEXT("He recovers it, but with a blocker on him he falls on it"), Recovery && Recovery->PlayerName == TEXT("DL") && !Recovery->bScooped);
    TestTrue(TEXT("...and has it"), Rusher->HasPossession());
    const FPSTelemetryLooseBallEvent* Dead = FindKind(Events, EPSLooseBallEventKind::Dead);
    const int32 Spot = LineYards - HoldYards;
    TestTrue(TEXT("Dead where he fell on it, the defense's ball"), Dead && !Dead->bTouchdown && !Dead->bKickingTeam && Dead->YardLine == Spot);
    TestEqual(TEXT("The play is over"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);

    // The ball goes over there.
    const bool bHomeKicked = Sim->GetPlayState().bHomeHasPossession;
    Sim->EndPlayAndPrepareNext();
    TestTrue(TEXT("The defense has the ball"), Sim->GetPlayState().bHomeHasPossession != bHomeKicked);
    TestEqual(TEXT("...at the spot, from its own goal line"), Sim->GetPlayState().YardLine, 100 - Spot);
    TestEqual(TEXT("...1st down"), Sim->GetPlayState().Down, 1);

    Bus->OnLooseBallMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The kicking team falls on it; no ball to play
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLooseBallKickingTeamTest,
    "PlaySports.BrokenPlay.BlockedKickKickingTeamRecovers",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLooseBallKickingTeamTest::RunTest(const FString& Parameters)
{
    using namespace PSLooseBallTests;

    {
        UWorld* World = CreateTestWorld();
        UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
        UPSLooseBallSubsystem* Loose = World ? World->GetSubsystem<UPSLooseBallSubsystem>() : nullptr;
        if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Loose-ball subsystem"), Loose))
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
            return false;
        }
        APSPlayerPawn* Punter = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("P"), FVector(LineX - 1400.f, 0.f, 100.f));
        APSBall* Ball = Punter ? GiveBall(World, Punter) : nullptr;
        if (!TestNotNull(TEXT("Punter"), Punter) || !TestNotNull(TEXT("Ball"), Ball))
        {
            DestroyTestWorld(World);
            return false;
        }
        TArray<FPSTelemetryLooseBallEvent> Events;
        const FDelegateHandle Handle = Bus->OnLooseBallMC.AddLambda([&Events](const FPSTelemetryLooseBallEvent& Event) { Events.Add(Event); });

        UPSPlaySimulation* Sim = MakeBlockingSim(World);
        const int32 Recoil = Sim->GetSpecialTeams()->GetTuning().BlockedPuntRecoilYards;
        SnapAndKick(Bus, Sim, EPlayPhase::Punt);
        TestTrue(TEXT("The blocked punt is loose"), Loose->IsLoose());

        // The punter gets back to it first and falls on it: the kicking team's, short of the line.
        SetRecoveryChance(Ball, 1.f);
        Punter->SetActorLocation(Ball->GetActorLocation() + FVector(-50.f, 0.f, 0.f));
        Loose->UpdateLooseBall(0.1f);
        const FPSTelemetryLooseBallEvent* Recovery = FindKind(Events, EPSLooseBallEventKind::Recovered);
        TestTrue(TEXT("The punter recovers it"), Recovery && Recovery->PlayerName == TEXT("P") && Recovery->bKickingTeam && !Recovery->bScooped);
        const FPSTelemetryLooseBallEvent* Dead = FindKind(Events, EPSLooseBallEventKind::Dead);
        const int32 Spot = LineYards - Recoil;
        TestTrue(TEXT("Dead at the spot, the kicking team on it"), Dead && Dead->bKickingTeam && !Dead->bTouchdown && FMath::Abs(Dead->YardLine - Spot) <= 1);

        // On the kicking down, short of the line: the ball goes over on downs there.
        const bool bHomeKicked = Sim->GetPlayState().bHomeHasPossession;
        const int32 DeadSpot = Dead ? Dead->YardLine : Spot;
        Sim->EndPlayAndPrepareNext();
        TestTrue(TEXT("Turned over on downs"), Sim->GetPlayState().bHomeHasPossession != bHomeKicked);
        TestEqual(TEXT("...at the spot"), Sim->GetPlayState().YardLine, 100 - DeadSpot);

        Bus->OnLooseBallMC.Remove(Handle);
        DestroyTestWorld(World);
    }

    // No ball on the field to play (the headless simulation): the block ends the play on the
    // special-teams model's roll, as before.
    {
        UWorld* World = CreateTestWorld();
        UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
        UPSLooseBallSubsystem* Loose = World ? World->GetSubsystem<UPSLooseBallSubsystem>() : nullptr;
        if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Loose-ball subsystem"), Loose))
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
            return false;
        }
        UPSPlaySimulation* Sim = MakeBlockingSim(World);
        SnapAndKick(Bus, Sim, EPlayPhase::Punt);
        TestFalse(TEXT("Nothing to play loose"), Loose->IsLoose());
        TestEqual(TEXT("The play ends at once"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);
        const EPSSpecialTeamsResult Result = Sim->GetLastSpecialTeamsOutcome().Result;
        TestTrue(TEXT("...on the model's block"), Result == EPSSpecialTeamsResult::Blocked || Result == EPSSpecialTeamsResult::BlockedTouchdown);
        DestroyTestWorld(World);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The shipped tuning
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLooseBallTuningTest,
    "PlaySports.BrokenPlay.LooseBallTuning",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLooseBallTuningTest::RunTest(const FString& Parameters)
{
    using namespace PSLooseBallTests;

    UWorld* World = CreateTestWorld();
    UPSLooseBallSubsystem* Loose = World ? World->GetSubsystem<UPSLooseBallSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Loose-ball subsystem"), Loose))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    TestTrue(TEXT("Data/loose_ball.json loads and is sound"), Loose->LoadTuningFromJson(UPSLooseBallSubsystem::GetDefaultTuningPath()));
    const FPSLooseBallTuning Defaults;
    TestEqual(TEXT("The struct's defaults are the file's: chase radius"), Loose->GetTuning().ChaseRadius, Defaults.ChaseRadius);
    TestEqual(TEXT("...field goal hold"), Loose->GetTuning().BlockedFieldGoalYards, Defaults.BlockedFieldGoalYards);
    FPSLooseBallTuning Broken = Defaults;
    Broken.RecoverRadius = Broken.ChaseRadius + 1.f;
    Broken.MaxLooseSeconds = 0.f;
    TestEqual(TEXT("Bad tuning is caught"), UPSLooseBallSubsystem::ValidateTuning(Broken).Num(), 2);
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
