// PSDeceptionTests.cpp -- Epic 72 (deception plays: play-action, RPO and the option)
//
// Tests covered:
//   1. Play-action: the QB carries out a fake hand-off toward his back, then drops; when it is
//      sold, run-fit defenders bite (and hold) against an offense that has been running and not
//      against one that has been throwing; a defender in man doesn't. The tendency is the
//      offense's recent calls, an audible replacing the call it changes. A drop-back has no fake;
//      a plain run gives at once.
//   2. RPO: the QB rides the mesh, then reads the conflict linebacker (not the nearer corner):
//      fitting the run, he pulls it and throws to the pass option; dropping to the pass, he gives;
//      standing still, he is read by whom he is nearer.
//   3. Zone read: the end man on the line on the play side is the key: crashing on the back, the
//      QB keeps it; staying home on the QB, he gives.
//   4. Triple option: the dive read, then, keeping it, the pitch key: on the pitch man the QB
//      keeps it; on the QB he pitches -- but not once the QB is past the line.
//   5. Defensive integrity: the defense sees the mesh and gives out option jobs, and the defenders
//      play them. Disciplined, the read gives the ball to the back a lineman has; undisciplined,
//      the end crashes, the QB keeps it into the scraping linebacker, and the pitch key on the QB
//      leaves the pitch man free.
//   6. The playbook's deception plays load with their mechanics; the shipped tuning loads and is
//      sound, and broken tuning is caught.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSAIFieldSnapshot.h"
#include "PSBall.h"
#include "PSDataIngestion.h"
#include "PSDeceptionSubsystem.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlaybookData.h"
#include "PSPlaybookIngestion.h"
#include "PSPlayerPawn.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSDeceptionTests
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
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location, float Awareness = 50.f)
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
        Attributes.Strength = 70.f;
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

    static APSDefenseController* DefenseControllerOf(const APSPlayerPawn* Pawn)
    {
        return Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr;
    }

    static UPSDefenderAIComponent* DefenseOf(const APSPlayerPawn* Pawn)
    {
        const APSDefenseController* Controller = DefenseControllerOf(Pawn);
        return Controller ? Controller->GetDefenderAI() : nullptr;
    }

    static UPSSkillPlayerAIComponent* OffenseOf(const APSPlayerPawn* Pawn)
    {
        const APSOffenseController* Controller = Pawn ? Cast<APSOffenseController>(Pawn->GetController()) : nullptr;
        return Controller ? Controller->GetSkillAI() : nullptr;
    }

    static APSBall* GiveBall(UWorld* World, APSPlayerPawn* Carrier)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), Carrier->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);
        if (Ball)
        {
            Ball->AttachToCarrier(Carrier, TEXT("HandSocket"));
            Carrier->GainPossession();
        }
        return Ball;
    }

    /** The offense calls PlayId in an open call window (the defense is left to the CPU). */
    static bool CallOffense(UWorld* World, const TCHAR* PlayId)
    {
        UPSPlayCallSubsystem* PlayCall = World->GetSubsystem<UPSPlayCallSubsystem>();
        if (!PlayCall)
        {
            return false;
        }
        PlayCall->OpenPlayCall(FPSSituationContext());
        return PlayCall->CallPlay(FName(PlayId), EPSPlayCaller::CPU);
    }

    /** The snap at the origin: the play-call subsystem hands both calls out on it. */
    static void Snap(UPSTelemetryBus* Bus)
    {
        FPSTelemetrySnapEvent Event;
        Event.LineOfScrimmage = FVector::ZeroVector;
        Bus->PublishSnap(Event);
    }

    /** Stands in for the movement tick, which a headless world doesn't run: Pawn moving at Speed
     *  along Direction. */
    static void SetVelocity(APSPlayerPawn* Pawn, const FVector& Direction, float Speed)
    {
        if (UFloatingPawnMovement* Movement = Pawn ? Pawn->GetFloatingMovementComponent() : nullptr)
        {
            Movement->Velocity = Direction.GetSafeNormal2D() * Speed;
        }
    }

    static void SetHeading(APSPlayerPawn* Pawn, const APSPlayerPawn* Target, float Speed)
    {
        if (Pawn && Target)
        {
            SetVelocity(Pawn, Target->GetActorLocation() - Pawn->GetActorLocation(), Speed);
        }
    }

    static int32 CountEvents(const TArray<FPSTelemetryDeceptionEvent>& Events, EPSDeceptionEventKind Kind, const TCHAR* PlayerName = nullptr)
    {
        int32 Found = 0;
        for (const FPSTelemetryDeceptionEvent& Event : Events)
        {
            Found += (Event.Kind == Kind && (!PlayerName || Event.PlayerName == PlayerName)) ? 1 : 0;
        }
        return Found;
    }

    static const FPSTelemetryDeceptionEvent* FindEvent(const TArray<FPSTelemetryDeceptionEvent>& Events, EPSDeceptionEventKind Kind, const TCHAR* Outcome = nullptr)
    {
        return Events.FindByPredicate([Kind, Outcome](const FPSTelemetryDeceptionEvent& Event)
        {
            return Event.Kind == Kind && (!Outcome || Event.Outcome == FName(Outcome));
        });
    }

    static bool PointsToward(const FVector& Direction, const FVector& From, const FVector& To)
    {
        FVector Expected = To - From;
        Expected.Z = 0.f;
        FVector Actual = Direction;
        Actual.Z = 0.f;
        return !Actual.IsNearlyZero() && Actual.GetSafeNormal().Equals(Expected.GetSafeNormal(), 0.01f);
    }

    /** How the RPO's conflict linebacker plays it. */
    enum class EConflictLook : uint8
    {
        FitsTheRun,
        DropsToThePass,
        StandsInTheBox
    };

    /** What the quarterback did with a run option. */
    struct FOptionResult
    {
        FString Outcome;
        FString Key;
        bool bRodeTheMesh = false;
        bool bThrownToOption = false;
        bool bHandedOff = false;
        bool bKept = false;
    };

    /** The QB's first two decisions of a run option: riding the mesh, then the read. */
    static void RideAndRead(UPSSkillPlayerAIComponent* Passer, UPSDeceptionSubsystem* Deception, const TArray<FPSTelemetryDeceptionEvent>& Events,
        const APSPlayerPawn* QB, FOptionResult& Result)
    {
        Passer->TickAI(0.1f);
        Result.bRodeTheMesh = QB->HasPossession() && CountEvents(Events, EPSDeceptionEventKind::Read) == 0;
        Passer->TickAI(Deception->GetTuning().MeshRideSeconds);
        const FPSTelemetryDeceptionEvent* Read = FindEvent(Events, EPSDeceptionEventKind::Read);
        Result.Outcome = Read ? Read->Outcome.ToString() : FString();
        Result.Key = Read ? Read->OtherName : FString();
    }

    /** An RPO slant from the gun, the conflict linebacker playing it Look's way. */
    static FOptionResult RunRPO(FAutomationTestBase& Test, EConflictLook Look)
    {
        FOptionResult Result;
        UWorld* World = CreateTestWorld();
        UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
        UPSDeceptionSubsystem* Deception = World ? World->GetSubsystem<UPSDeceptionSubsystem>() : nullptr;
        UPSAIFieldSnapshot* Field = World ? World->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
        if (!Test.TestNotNull(TEXT("Bus"), Bus) || !Test.TestNotNull(TEXT("Deception"), Deception) || !Test.TestNotNull(TEXT("Field snapshot"), Field))
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
            return Result;
        }

        APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-500.f, 0.f, 100.f));
        APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-450.f, 100.f, 100.f));
        APSPlayerPawn* WR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(0.f, 1500.f, 100.f));
        // The conflict defender is the linebacker, though the corner is nearer the slant.
        const FVector ConflictSpot = Look == EConflictLook::StandsInTheBox ? FVector(-150.f, 150.f, 100.f) : FVector(300.f, 600.f, 100.f);
        APSPlayerPawn* Conflict = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("Conflict"), ConflictSpot);
        SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("Corner"), FVector(100.f, 1600.f, 100.f));
        APSBall* Ball = QB ? GiveBall(World, QB) : nullptr;
        if (!Test.TestNotNull(TEXT("QB"), QB) || !Test.TestNotNull(TEXT("RB"), RB) || !Test.TestNotNull(TEXT("WR"), WR)
            || !Test.TestNotNull(TEXT("Conflict"), Conflict) || !Test.TestNotNull(TEXT("Ball"), Ball))
        {
            DestroyTestWorld(World);
            return Result;
        }
        Field->Invalidate();

        TArray<FPSTelemetryDeceptionEvent> Events;
        Bus->OnDeceptionMC.AddLambda([&Events](const FPSTelemetryDeceptionEvent& Event) { Events.Add(Event); });
        TArray<FString> Targets;
        Bus->OnThrowMC.AddLambda([&Targets](const FPSTelemetryThrowEvent& Event) { Targets.Add(Event.TargetReceiverName); });

        Test.TestTrue(TEXT("The offense calls the RPO"), CallOffense(World, TEXT("Offense_ShotgunRPOSlant")));
        Snap(Bus);
        if (Look == EConflictLook::FitsTheRun)
        {
            SetHeading(Conflict, RB, 500.f);
        }
        else if (Look == EConflictLook::DropsToThePass)
        {
            SetHeading(Conflict, WR, 500.f);
        }

        RideAndRead(OffenseOf(QB), Deception, Events, QB, Result);
        Result.bThrownToOption = Targets.Contains(TEXT("WR")) && !QB->HasPossession();
        Result.bHandedOff = RB->HasPossession();
        Result.bKept = QB->HasPossession();

        DestroyTestWorld(World);
        return Result;
    }

    /** A zone read from the gun, the end man on the line crashing on the back or staying home. */
    static FOptionResult RunZoneRead(FAutomationTestBase& Test, bool bEndCrashes)
    {
        FOptionResult Result;
        UWorld* World = CreateTestWorld();
        UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
        UPSDeceptionSubsystem* Deception = World ? World->GetSubsystem<UPSDeceptionSubsystem>() : nullptr;
        UPSAIFieldSnapshot* Field = World ? World->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
        if (!Test.TestNotNull(TEXT("Bus"), Bus) || !Test.TestNotNull(TEXT("Deception"), Deception) || !Test.TestNotNull(TEXT("Field snapshot"), Field))
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
            return Result;
        }

        APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-500.f, 0.f, 100.f));
        APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-500.f, 140.f, 100.f));
        // The key is the widest man on the line on the play side (right): not the tackle inside
        // him, nor the end on the other side.
        APSPlayerPawn* End = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("End"), FVector(0.f, 300.f, 100.f));
        SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("Tackle"), FVector(0.f, 100.f, 100.f));
        SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("Backside"), FVector(0.f, -400.f, 100.f));
        APSBall* Ball = QB ? GiveBall(World, QB) : nullptr;
        if (!Test.TestNotNull(TEXT("QB"), QB) || !Test.TestNotNull(TEXT("RB"), RB) || !Test.TestNotNull(TEXT("End"), End) || !Test.TestNotNull(TEXT("Ball"), Ball))
        {
            DestroyTestWorld(World);
            return Result;
        }
        Field->Invalidate();

        TArray<FPSTelemetryDeceptionEvent> Events;
        Bus->OnDeceptionMC.AddLambda([&Events](const FPSTelemetryDeceptionEvent& Event) { Events.Add(Event); });

        Test.TestTrue(TEXT("The offense calls the zone read"), CallOffense(World, TEXT("Offense_ShotgunZoneRead")));
        Snap(Bus);
        SetHeading(End, bEndCrashes ? RB : QB, 500.f);

        UPSSkillPlayerAIComponent* Passer = OffenseOf(QB);
        RideAndRead(Passer, Deception, Events, QB, Result);
        Result.bHandedOff = RB->HasPossession();
        Result.bKept = QB->HasPossession() && Passer->GetAction() == EPSSkillPlayerAction::CarryBall;

        DestroyTestWorld(World);
        return Result;
    }

    /** The triple option's field from the I: the QB and the back at the mesh, the tight end the
     *  pitch man, the end on the line the dive key, a safety the pitch key. */
    struct FTripleField
    {
        UWorld* World = nullptr;
        UPSTelemetryBus* Bus = nullptr;
        UPSDeceptionSubsystem* Deception = nullptr;
        APSPlayerPawn* QB = nullptr;
        APSPlayerPawn* RB = nullptr;
        APSPlayerPawn* TE = nullptr;
        APSPlayerPawn* End = nullptr;
        APSPlayerPawn* Tackle = nullptr;
        APSPlayerPawn* Mike = nullptr;
        APSPlayerPawn* Force = nullptr;
        TArray<FPSTelemetryDeceptionEvent> Events;

        bool Setup(FAutomationTestBase& Test, float EndAwareness, float ForceAwareness, bool bFullDefense)
        {
            World = CreateTestWorld();
            Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
            Deception = World ? World->GetSubsystem<UPSDeceptionSubsystem>() : nullptr;
            UPSAIFieldSnapshot* Field = World ? World->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
            if (!Test.TestNotNull(TEXT("Bus"), Bus) || !Test.TestNotNull(TEXT("Deception"), Deception) || !Test.TestNotNull(TEXT("Field snapshot"), Field))
            {
                return false;
            }
            QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-100.f, 0.f, 100.f));
            RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-200.f, 100.f, 100.f));
            TE = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE"), FVector(-100.f, 600.f, 100.f));
            End = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("End"), FVector(100.f, 300.f, 100.f), EndAwareness);
            Force = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("Force"), FVector(400.f, 800.f, 100.f), ForceAwareness);
            if (bFullDefense)
            {
                Tackle = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("Tackle"), FVector(100.f, 50.f, 100.f));
                Mike = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("Mike"), FVector(500.f, 100.f, 100.f), 90.f);
            }
            APSBall* Ball = QB ? GiveBall(World, QB) : nullptr;
            if (!Test.TestNotNull(TEXT("QB"), QB) || !Test.TestNotNull(TEXT("RB"), RB) || !Test.TestNotNull(TEXT("TE"), TE)
                || !Test.TestNotNull(TEXT("End"), End) || !Test.TestNotNull(TEXT("Force"), Force) || !Test.TestNotNull(TEXT("Ball"), Ball)
                || (bFullDefense && (!Test.TestNotNull(TEXT("Tackle"), Tackle) || !Test.TestNotNull(TEXT("Mike"), Mike))))
            {
                return false;
            }
            Field->Invalidate();
            Bus->OnDeceptionMC.AddLambda([this](const FPSTelemetryDeceptionEvent& Event) { Events.Add(Event); });
            if (!Test.TestTrue(TEXT("The offense calls the triple option"), CallOffense(World, TEXT("Offense_IFormTripleOption"))))
            {
                return false;
            }
            Snap(Bus);
            return true;
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
}

// ---------------------------------------------------------------------------
// Test 1 -- Play-action: the fake, the bites and the offense's tendency
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDeceptionPlayActionTest,
    "PlaySports.Deception.PlayActionFakeAndBite",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDeceptionPlayActionTest::RunTest(const FString& Parameters)
{
    using namespace PSDeceptionTests;

    // The bite, on the header's tuning: the less aware and the more run-heavy the offense, the
    // likelier, held between the floor and the ceiling.
    const FPSDeceptionTuning Formula;
    TestTrue(TEXT("An unaware defender bites more than an aware one"),
        UPSDeceptionSubsystem::BiteChance(0.f, 0.5f, Formula) > UPSDeceptionSubsystem::BiteChance(100.f, 0.5f, Formula));
    TestTrue(TEXT("...and more against an offense that has been running"),
        UPSDeceptionSubsystem::BiteChance(50.f, 1.f, Formula) > UPSDeceptionSubsystem::BiteChance(50.f, 0.f, Formula));
    TestEqual(TEXT("...never above the ceiling"), UPSDeceptionSubsystem::BiteChance(0.f, 1.f, Formula), Formula.BiteMaxChance);
    TestEqual(TEXT("...nor below the floor"), UPSDeceptionSubsystem::BiteChance(100.f, 0.f, Formula), Formula.BiteMinChance);

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSDeceptionSubsystem* Deception = World ? World->GetSubsystem<UPSDeceptionSubsystem>() : nullptr;
    UPSAIFieldSnapshot* Field = World ? World->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Deception"), Deception) || !TestNotNull(TEXT("Field snapshot"), Field))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f));
    APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-500.f, 150.f, 100.f));
    SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(0.f, 1500.f, 100.f));
    APSPlayerPawn* LB = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB"), FVector(500.f, 100.f, 100.f), 0.f);
    APSPlayerPawn* CB = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB"), FVector(600.f, 1500.f, 100.f), 0.f);
    APSBall* Ball = QB ? GiveBall(World, QB) : nullptr;
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("RB"), RB) || !TestNotNull(TEXT("LB"), LB) || !TestNotNull(TEXT("CB"), CB) || !TestNotNull(TEXT("Ball"), Ball))
    {
        DestroyTestWorld(World);
        return false;
    }
    Field->Invalidate();

    TArray<FPSTelemetryDeceptionEvent> Events;
    Bus->OnDeceptionMC.AddLambda([&Events](const FPSTelemetryDeceptionEvent& Event) { Events.Add(Event); });

    // Here the offense's tendency alone decides a bite: always against all runs, never against
    // all passes.
    FPSDeceptionTuning Tuning = Deception->GetTuning();
    Tuning.BiteBaseChance = 0.5f;
    Tuning.BiteRunTendencyWeight = 1.f;
    Tuning.BiteAwarenessWeight = 0.f;
    Tuning.BiteMinChance = 0.f;
    Tuning.BiteMaxChance = 1.f;
    Deception->SetTuning(Tuning);
    const int32 Window = FMath::Max(1, Tuning.TendencyWindow);

    for (int32 Index = 0; Index < Window; ++Index)
    {
        CallOffense(World, TEXT("Offense_InsideZone"));
        Snap(Bus);
    }
    TestEqual(TEXT("All runs: a running offense"), Deception->GetRunShare(), 1.f);
    TestTrue(TEXT("A plain run gives it at once"), Deception->ReadMesh(QB, RB, 0.f) == EPSOptionChoice::Give);

    TestTrue(TEXT("The offense calls play-action"), CallOffense(World, TEXT("Offense_PlayActionPost")));
    TestTrue(TEXT("...a play with a fake"), Deception->GetPlayDeception().Type == EPSDeception::PlayAction);
    Snap(Bus);
    // The CPU's defense came with the snap: the linebacker plays the run, the corner his man.
    DefenseControllerOf(LB)->SetAssignment(EPSDefensiveAssignmentType::RunFit, nullptr, LB->GetActorLocation());
    DefenseControllerOf(CB)->SetAssignment(EPSDefensiveAssignmentType::ManCoverage, nullptr, CB->GetActorLocation());
    Events.Reset();

    UPSSkillPlayerAIComponent* Passer = OffenseOf(QB);
    TestFalse(TEXT("Play-action isn't a run"), Passer->IsRunPlay());
    Passer->TickAI(0.1f);
    TestTrue(TEXT("The QB carries out the fake"), Passer->GetAction() == EPSSkillPlayerAction::Fake);
    TestTrue(TEXT("...toward his back"), PointsToward(Passer->GetDesiredDirection(), QB->GetActorLocation(), RB->GetActorLocation()));
    TestTrue(TEXT("...keeping the ball"), QB->HasPossession());
    TestEqual(TEXT("Nobody bites before it's sold"), CountEvents(Events, EPSDeceptionEventKind::Bite), 0);

    Passer->TickAI(Tuning.FakeSeconds);
    TestTrue(TEXT("Then he drops"), Passer->GetAction() != EPSSkillPlayerAction::Fake);
    TestEqual(TEXT("The fake is sold once"), CountEvents(Events, EPSDeceptionEventKind::Fake), 1);
    TestEqual(TEXT("The run-fit linebacker bites"), CountEvents(Events, EPSDeceptionEventKind::Bite, TEXT("LB")), 1);
    TestEqual(TEXT("...the corner in man doesn't"), CountEvents(Events, EPSDeceptionEventKind::Bite, TEXT("CB")), 0);
    const FPSTelemetryDeceptionEvent* Bite = FindEvent(Events, EPSDeceptionEventKind::Bite);
    TestTrue(TEXT("...and is held for the freeze"), Bite && FMath::IsNearlyEqual(Bite->Seconds, Tuning.BiteFreezeSeconds));
    TestTrue(TEXT("The linebacker holds instead of dropping"), DefenseOf(LB)->IsFrozen());
    TestFalse(TEXT("...the corner doesn't"), DefenseOf(CB)->IsFrozen());

    // A passing offense: the same fake, and nobody bites.
    for (int32 Index = 0; Index < Window; ++Index)
    {
        CallOffense(World, TEXT("Offense_SlantFlat"));
        Snap(Bus);
    }
    TestEqual(TEXT("All passes: a passing offense"), Deception->GetRunShare(), 0.f);
    CallOffense(World, TEXT("Offense_PlayActionPost"));
    Snap(Bus);
    DefenseControllerOf(LB)->SetAssignment(EPSDefensiveAssignmentType::RunFit, nullptr, LB->GetActorLocation());
    Events.Reset();
    TestTrue(TEXT("The fake takes its time"), Deception->UpdateFake(QB, 0.1f));
    TestFalse(TEXT("...then it's sold"), Deception->UpdateFake(QB, Tuning.FakeSeconds));
    TestEqual(TEXT("Sold"), CountEvents(Events, EPSDeceptionEventKind::Fake), 1);
    TestEqual(TEXT("...but against a passing offense nobody bites"), CountEvents(Events, EPSDeceptionEventKind::Bite), 0);
    TestFalse(TEXT("The linebacker drops"), DefenseOf(LB)->IsFrozen());

    // A straight drop-back has no fake.
    CallOffense(World, TEXT("Offense_SlantFlat"));
    Snap(Bus);
    TestTrue(TEXT("A drop-back pass has no deception"), Deception->GetPlayDeception().Type == EPSDeception::None);
    TestFalse(TEXT("...and no fake"), Deception->UpdateFake(QB, 0.1f));

    // The tendency counts one call a snap: an audible replaces the call it changes.
    CallOffense(World, TEXT("Offense_InsideZone"));
    TestEqual(TEXT("A run call counts"), Deception->GetRunShare(), 1.f / Window);
    CallOffense(World, TEXT("Offense_SlantFlat"));
    TestEqual(TEXT("...until the audible to a pass replaces it"), Deception->GetRunShare(), 0.f);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- RPO: the conflict defender's read
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDeceptionRPOTest,
    "PlaySports.Deception.RPOReadsTheConflictDefender",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDeceptionRPOTest::RunTest(const FString& Parameters)
{
    using namespace PSDeceptionTests;

    const FOptionResult Fits = RunRPO(*this, EConflictLook::FitsTheRun);
    TestTrue(TEXT("The QB rides the mesh before he reads"), Fits.bRodeTheMesh);
    TestEqual(TEXT("He reads the conflict linebacker, not the nearer corner"), Fits.Key, FString(TEXT("Conflict")));
    TestEqual(TEXT("The linebacker fits the run: pull it and throw"), Fits.Outcome, FString(TEXT("Throw")));
    TestTrue(TEXT("...to the slant behind him"), Fits.bThrownToOption);
    TestFalse(TEXT("...not to the back"), Fits.bHandedOff);

    const FOptionResult Drops = RunRPO(*this, EConflictLook::DropsToThePass);
    TestEqual(TEXT("The linebacker drops to the slant: give it"), Drops.Outcome, FString(TEXT("Give")));
    TestTrue(TEXT("...the back has it"), Drops.bHandedOff);
    TestFalse(TEXT("...and nothing is thrown"), Drops.bThrownToOption);

    const FOptionResult Stands = RunRPO(*this, EConflictLook::StandsInTheBox);
    TestEqual(TEXT("Standing still in the box, he is read by whom he is nearer: the run, so throw"), Stands.Outcome, FString(TEXT("Throw")));
    TestTrue(TEXT("...to the slant"), Stands.bThrownToOption);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Zone read: give or keep on the end man
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDeceptionZoneReadTest,
    "PlaySports.Deception.ZoneReadGiveOrKeep",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDeceptionZoneReadTest::RunTest(const FString& Parameters)
{
    using namespace PSDeceptionTests;

    const FOptionResult Crash = RunZoneRead(*this, true);
    TestTrue(TEXT("The QB rides the mesh before he reads"), Crash.bRodeTheMesh);
    TestEqual(TEXT("The key is the end man on the line on the play side"), Crash.Key, FString(TEXT("End")));
    TestEqual(TEXT("The end crashes on the back: keep it"), Crash.Outcome, FString(TEXT("Keep")));
    TestTrue(TEXT("...the QB runs with it"), Crash.bKept);
    TestFalse(TEXT("...the back doesn't have it"), Crash.bHandedOff);

    const FOptionResult Home = RunZoneRead(*this, false);
    TestEqual(TEXT("The end stays home on the QB: give it"), Home.Outcome, FString(TEXT("Give")));
    TestTrue(TEXT("...the back has it"), Home.bHandedOff);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Triple option: the dive read, then the pitch key
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDeceptionTripleOptionTest,
    "PlaySports.Deception.TripleOptionPitch",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDeceptionTripleOptionTest::RunTest(const FString& Parameters)
{
    using namespace PSDeceptionTests;

    FTripleField Triple;
    if (!Triple.Setup(*this, 50.f, 50.f, false))
    {
        Triple.Teardown();
        return false;
    }
    UPSSkillPlayerAIComponent* Passer = OffenseOf(Triple.QB);
    const FPSDeceptionTuning& Tuning = Triple.Deception->GetTuning();

    // The dive key crashes on the back: keep it.
    SetHeading(Triple.End, Triple.RB, 500.f);
    FOptionResult Dive;
    RideAndRead(Passer, Triple.Deception, Triple.Events, Triple.QB, Dive);
    TestEqual(TEXT("The dive key is the end man on the line"), Dive.Key, FString(TEXT("End")));
    TestEqual(TEXT("He crashes on the dive: the QB keeps it"), Dive.Outcome, FString(TEXT("Keep")));
    TestTrue(TEXT("...and runs with it"), Triple.QB->HasPossession() && Passer->GetAction() == EPSSkillPlayerAction::CarryBall);

    // The pitch key, far off: keep running.
    Passer->TickAI(0.1f);
    TestTrue(TEXT("The pitch key is far off: he keeps it"), Triple.QB->HasPossession());
    TestTrue(TEXT("...no pitch"), FindEvent(Triple.Events, EPSDeceptionEventKind::Read, TEXT("Pitch")) == nullptr);

    // The pitch key comes up but stays on the pitch man: keep it.
    Triple.Force->SetActorLocation(FVector(50.f, 250.f, 100.f));
    SetHeading(Triple.Force, Triple.TE, 500.f);
    Passer->TickAI(0.1f);
    TestTrue(TEXT("The pitch key stays on the pitch man: he keeps it"), Triple.QB->HasPossession());

    // Past the line the QB keeps it for good, whoever comes at him.
    const FVector AtTheMesh = Triple.QB->GetActorLocation();
    Triple.QB->SetActorLocation(FVector(Tuning.PitchWindowDepth + 100.f, 0.f, 100.f));
    SetHeading(Triple.Force, Triple.QB, 500.f);
    TestNull(TEXT("Past the line there is no pitch"), Triple.Deception->ReadPitch(Triple.QB));

    // Behind the line, the pitch key takes the QB: pitch it.
    Triple.QB->SetActorLocation(AtTheMesh);
    SetHeading(Triple.Force, Triple.QB, 500.f);
    Passer->TickAI(0.1f);
    const FPSTelemetryDeceptionEvent* Pitch = FindEvent(Triple.Events, EPSDeceptionEventKind::Read, TEXT("Pitch"));
    TestTrue(TEXT("The pitch key takes the QB: he pitches"), Pitch != nullptr);
    TestTrue(TEXT("...reading the pitch key"), Pitch && Pitch->OtherName == TEXT("Force"));
    TestFalse(TEXT("...and the ball is gone to the pitch man"), Triple.QB->HasPossession());

    Triple.Teardown();
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Defensive integrity: option jobs, played by the defenders
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDeceptionIntegrityTest,
    "PlaySports.Deception.OptionIntegrity",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDeceptionIntegrityTest::RunTest(const FString& Parameters)
{
    using namespace PSDeceptionTests;

    // A disciplined defense: the end takes the QB, the tackle the dive, the safety the pitch.
    {
        FTripleField Disciplined;
        if (!Disciplined.Setup(*this, 90.f, 90.f, true))
        {
            Disciplined.Teardown();
            return false;
        }
        UPSDeceptionSubsystem* Deception = Disciplined.Deception;
        TestFalse(TEXT("Before the mesh nobody has an option job"), Deception->HasRecognizedOption());
        Deception->UpdateDefense();
        TestTrue(TEXT("The defense sees the mesh"), Deception->HasRecognizedOption());
        TestTrue(TEXT("The disciplined end takes the QB"), Deception->GetOptionMan(Disciplined.End) == Disciplined.QB);
        TestTrue(TEXT("The tackle takes the dive"), Deception->GetOptionMan(Disciplined.Tackle) == Disciplined.RB);
        TestTrue(TEXT("The safety takes the pitch man"), Deception->GetOptionMan(Disciplined.Force) == Disciplined.TE);
        TestNull(TEXT("The linebacker has no job"), Deception->GetOptionMan(Disciplined.Mike));
        TestEqual(TEXT("Three jobs on the bus"), CountEvents(Disciplined.Events, EPSDeceptionEventKind::Assignment), 3);

        // The defenders play them; the end comes at the QB.
        for (APSPlayerPawn* Defender : { Disciplined.End, Disciplined.Tackle, Disciplined.Force })
        {
            UPSDefenderAIComponent* AI = DefenseOf(Defender);
            AI->TickAI(0.1f);
            SetVelocity(Defender, AI->GetDesiredDirection(), 500.f);
        }
        TestTrue(TEXT("The end goes for the QB"), PointsToward(DefenseOf(Disciplined.End)->GetDesiredDirection(), Disciplined.End->GetActorLocation(), Disciplined.QB->GetActorLocation()));
        TestTrue(TEXT("The tackle goes for the back"), PointsToward(DefenseOf(Disciplined.Tackle)->GetDesiredDirection(), Disciplined.Tackle->GetActorLocation(), Disciplined.RB->GetActorLocation()));
        TestTrue(TEXT("The safety goes for the pitch man"), PointsToward(DefenseOf(Disciplined.Force)->GetDesiredDirection(), Disciplined.Force->GetActorLocation(), Disciplined.TE->GetActorLocation()));

        // The read gives it to the back, whom the tackle has.
        FOptionResult Read;
        RideAndRead(OffenseOf(Disciplined.QB), Deception, Disciplined.Events, Disciplined.QB, Read);
        TestEqual(TEXT("The end on the QB: give it"), Read.Outcome, FString(TEXT("Give")));
        TestTrue(TEXT("...to the back the tackle has"), Disciplined.RB->HasPossession() && Deception->GetOptionMan(Disciplined.Tackle) == Disciplined.RB);
        FVector Target;
        TestFalse(TEXT("With the ball given, the option is over: everyone pursues"), Deception->GetOptionTarget(Disciplined.End, Target));
        Disciplined.Teardown();
    }

    // An undisciplined end and safety: the end crashes, the linebacker scrapes to the QB, the
    // safety goes for the QB and the pitch man is free.
    {
        FTripleField Loose;
        if (!Loose.Setup(*this, 30.f, 30.f, true))
        {
            Loose.Teardown();
            return false;
        }
        UPSDeceptionSubsystem* Deception = Loose.Deception;
        Deception->UpdateDefense();
        TestTrue(TEXT("The undisciplined end crashes on the dive"), Deception->GetOptionMan(Loose.End) == Loose.RB);
        TestTrue(TEXT("The aware linebacker scrapes over to the QB"), Deception->GetOptionMan(Loose.Mike) == Loose.QB);
        TestNull(TEXT("The dive is taken, so the tackle has no job"), Deception->GetOptionMan(Loose.Tackle));
        TestTrue(TEXT("The undisciplined safety goes for the QB"), Deception->GetOptionMan(Loose.Force) == Loose.QB);
        bool bPitchManCovered = false;
        for (const APSPlayerPawn* Defender : { Loose.End, Loose.Tackle, Loose.Mike, Loose.Force })
        {
            bPitchManCovered |= Deception->GetOptionMan(Defender) == Loose.TE;
        }
        TestFalse(TEXT("...nobody has the pitch man"), bPitchManCovered);

        // The end goes for the back: the QB keeps it, into the scraping linebacker.
        UPSDefenderAIComponent* EndAI = DefenseOf(Loose.End);
        EndAI->TickAI(0.1f);
        TestTrue(TEXT("The end goes for the back"), PointsToward(EndAI->GetDesiredDirection(), Loose.End->GetActorLocation(), Loose.RB->GetActorLocation()));
        SetVelocity(Loose.End, EndAI->GetDesiredDirection(), 500.f);
        UPSSkillPlayerAIComponent* Passer = OffenseOf(Loose.QB);
        FOptionResult Read;
        RideAndRead(Passer, Deception, Loose.Events, Loose.QB, Read);
        TestEqual(TEXT("The end on the dive: keep it"), Read.Outcome, FString(TEXT("Keep")));
        UPSDefenderAIComponent* MikeAI = DefenseOf(Loose.Mike);
        MikeAI->TickAI(0.1f);
        TestTrue(TEXT("...the linebacker comes for the QB who kept it"), PointsToward(MikeAI->GetDesiredDirection(), Loose.Mike->GetActorLocation(), Loose.QB->GetActorLocation()));

        // The safety comes at the QB too: he pitches it to the free man.
        Loose.Force->SetActorLocation(FVector(50.f, 250.f, 100.f));
        UPSDefenderAIComponent* ForceAI = DefenseOf(Loose.Force);
        ForceAI->TickAI(0.1f);
        SetVelocity(Loose.Force, ForceAI->GetDesiredDirection(), 500.f);
        Passer->TickAI(0.1f);
        TestTrue(TEXT("The safety on the QB: he pitches"), FindEvent(Loose.Events, EPSDeceptionEventKind::Read, TEXT("Pitch")) != nullptr);
        TestFalse(TEXT("...to the free pitch man"), Loose.QB->HasPossession());
        Loose.Teardown();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- The playbook's deception plays and the tuning
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDeceptionDataTest,
    "PlaySports.Deception.PlaybookAndTuning",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDeceptionDataTest::RunTest(const FString& Parameters)
{
    UPSPlaybookIngestion* Playbook = NewObject<UPSPlaybookIngestion>();
    UDataTable* Plays = NewObject<UDataTable>();
    Plays->RowStruct = FPSPlayDefinition::StaticStruct();
    if (!TestTrue(TEXT("The playbook loads"), Playbook->LoadPlaysFromJson(FPaths::ProjectDir() / TEXT("Data/sample_playbook.json"), Plays)))
    {
        return false;
    }
    const FPSPlayDefinition* PlayAction = Plays->FindRow<FPSPlayDefinition>(FName(TEXT("Offense_PlayActionPost")), TEXT("Test"));
    const FPSPlayDefinition* RPO = Plays->FindRow<FPSPlayDefinition>(FName(TEXT("Offense_ShotgunRPOSlant")), TEXT("Test"));
    const FPSPlayDefinition* ZoneRead = Plays->FindRow<FPSPlayDefinition>(FName(TEXT("Offense_ShotgunZoneRead")), TEXT("Test"));
    const FPSPlayDefinition* Triple = Plays->FindRow<FPSPlayDefinition>(FName(TEXT("Offense_IFormTripleOption")), TEXT("Test"));
    const FPSPlayDefinition* Plain = Plays->FindRow<FPSPlayDefinition>(FName(TEXT("Offense_InsideZone")), TEXT("Test"));
    if (!TestNotNull(TEXT("Play-action post"), PlayAction) || !TestNotNull(TEXT("RPO slant"), RPO) || !TestNotNull(TEXT("Zone read"), ZoneRead)
        || !TestNotNull(TEXT("Triple option"), Triple) || !TestNotNull(TEXT("Inside zone"), Plain))
    {
        return false;
    }
    TestTrue(TEXT("Play-action post fakes"), PlayAction->Deception.Type == EPSDeception::PlayAction);
    TestTrue(TEXT("The RPO is a run with a pass option"), RPO->Deception.Type == EPSDeception::RPO && RPO->PlayCategory == TEXT("Run")
        && RPO->Deception.PassRole == EPlayerRole::WideReceiver);
    TestTrue(TEXT("The zone read goes right"), ZoneRead->Deception.Type == EPSDeception::ZoneRead && ZoneRead->Deception.PlaySide == 1);
    TestTrue(TEXT("The triple option pitches to the tight end"), Triple->Deception.Type == EPSDeception::TripleOption && Triple->Deception.PitchRole == EPlayerRole::TightEnd);
    TestTrue(TEXT("A plain run has no deception"), Plain->Deception.Type == EPSDeception::None);

    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSDeceptionTuning Shipped;
    if (!TestTrue(TEXT("The shipped tuning loads"), Ingestion->LoadDeceptionTuningFromJson(UPSDeceptionSubsystem::GetDefaultTuningPath(), Shipped)))
    {
        return false;
    }
    TestEqual(TEXT("...and is sound"), UPSDeceptionSubsystem::ValidateTuning(Shipped).Num(), 0);
    TestTrue(TEXT("...the QB rides the mesh"), Shipped.MeshRideSeconds > 0.f);
    TestTrue(TEXT("...and the fake takes time"), Shipped.FakeSeconds > 0.f);

    FPSDeceptionTuning Broken = Shipped;
    Broken.FakeSeconds = -1.f;
    Broken.BiteMinChance = 0.8f;
    Broken.BiteMaxChance = 0.5f;
    Broken.DisciplineAwareness = 150.f;
    Broken.TendencyWindow = 0;
    TestEqual(TEXT("Broken tuning: a negative time, the floor over the ceiling, a rating over 100, no window"),
        UPSDeceptionSubsystem::ValidateTuning(Broken).Num(), 4);
    return true;
}

#endif
