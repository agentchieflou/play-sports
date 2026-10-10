// PSPlayRecognitionTests.cpp -- Epic 80 (formation and play recognition)
//
// Tests covered:
//   1. The formation classifier: personnel, splits and strength, the quarterback's alignment and
//      the backfield set, named by the first matching class. The game's own 11-personnel lineup
//      reads as trips; a man in motion changes the read; an I-formation, shotgun trips, an empty
//      set (before anything else) and an unmatched look. In a world the read is made at the snap
//      and announced on the first look after it, never inside the snap's broadcast.
//   2. Key-reading: the keys each look shows (a drop but not a shotgun snap, a hand-off, backs
//      flowing downhill, the line firing off or setting);
//      a defender's diagnosis over time (flow, then the drop; a draw; a true key over flow). In a
//      world a linebacker who reads run from the flow fills his gap before the hand-off, then
//      reads the drop and drops, each diagnosis on the bus.
//   3. Latency: high Awareness reads sooner than low on every read, and expectation stretches a
//      read against it. In a world the aware linebacker drops first and the aware corner breaks
//      on the throw first (jumping it); a ball hawk breaks on throws sooner and sees through
//      fakes later than a blanket corner rated the same.
//   4. The play-action bite through the recognition model (Epic 72's rules): against a running
//      offense the average and the raw linebacker bite and the elite one doesn't; against a
//      passing one only the raw one does; a ball hawk bites where a blanket defender doesn't.
//      Each bite holds for the rest of his read.
//   5. The shipped tuning loads and is sound, every offensive personnel package's lineup is a
//      known formation, the DNA catalog binds the recognition reads, and broken tuning is caught.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSAIFieldSnapshot.h"
#include "PSBall.h"
#include "PSDataIngestion.h"
#include "PSDeceptionSubsystem.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenderGapSubsystem.h"
#include "PSDefenseController.h"
#include "PSFieldGrid.h"
#include "PSOffenseController.h"
#include "PSPersonnelManager.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayRecognitionSubsystem.h"
#include "PSPlayerDNA.h"
#include "PSPlayerPawn.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/FloatingPawnMovement.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPlayRecognitionTests
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
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const FString& PlayerId, const FVector& Location, float Awareness = 50.f, float BallHawk = 0.f)
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
        Attributes.DNA.BallHawk = BallHawk;
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

    static UPSDefenderAIComponent* AIOf(const APSPlayerPawn* Pawn)
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

    /** The snap at the origin (a snap outside a call window hands out CPU plays first, so a test
     *  assigns its own jobs after it). */
    static void Snap(UPSTelemetryBus* Bus)
    {
        FPSTelemetrySnapEvent Event;
        Event.LineOfScrimmage = FVector::ZeroVector;
        Bus->PublishSnap(Event);
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

    /** Stands in for the movement tick, which a headless world doesn't run. */
    static void SetVelocity(APSPlayerPawn* Pawn, const FVector& Velocity)
    {
        if (UFloatingPawnMovement* Movement = Pawn ? Pawn->GetFloatingMovementComponent() : nullptr)
        {
            Movement->Velocity = Velocity;
        }
    }

    static bool PointsToward(const FVector& Direction, const FVector& From, const FVector& To)
    {
        FVector Expected = To - From;
        Expected.Z = 0.f;
        FVector Actual = Direction;
        Actual.Z = 0.f;
        return !Actual.IsNearlyZero() && Actual.GetSafeNormal().Equals(Expected.GetSafeNormal(), 0.01f);
    }

    static FPSAlignedPlayer Aligned(EPlayerRole Role, float X, float Y)
    {
        FPSAlignedPlayer Player;
        Player.Role = Role;
        Player.Location = FVector(X, Y, 100.f);
        return Player;
    }

    /** Five linemen on the line, as the game lines them up. */
    static void AddLine(TArray<FPSAlignedPlayer>& Offense)
    {
        for (int32 Index = 0; Index < 5; ++Index)
        {
            Offense.Add(Aligned(EPlayerRole::OffensiveLineman, -50.f, (Index - 2) * 150.f));
        }
    }

    static FPSFormationClassDef MakeClass(const TCHAR* ClassId, float RunLean, const TCHAR* QBAlignment = nullptr, const TCHAR* Backfield = nullptr,
        int32 MinStrongSide = 0, int32 MaxWeakSide = -1, int32 MinTightEnds = 0)
    {
        FPSFormationClassDef Def;
        Def.ClassId = FName(ClassId);
        Def.RunLean = RunLean;
        Def.QBAlignment = QBAlignment ? FName(QBAlignment) : NAME_None;
        Def.Backfield = Backfield ? FName(Backfield) : NAME_None;
        Def.MinStrongSide = MinStrongSide;
        Def.MaxWeakSide = MaxWeakSide;
        Def.MinTightEnds = MinTightEnds;
        return Def;
    }

    /** The test's own classes, most specific first. */
    static FPSPlayRecognitionTuning ClassifierTuning()
    {
        FPSPlayRecognitionTuning Tuning;
        Tuning.DefaultRunLean = 0.5f;
        Tuning.FormationClasses = {
            MakeClass(TEXT("Empty"), 0.15f, nullptr, TEXT("Empty")),
            MakeClass(TEXT("IForm"), 0.65f, TEXT("UnderCenter"), TEXT("I")),
            MakeClass(TEXT("ShotgunTrips"), 0.25f, TEXT("Shotgun"), nullptr, 3),
            MakeClass(TEXT("Trips"), 0.4f, nullptr, nullptr, 3, 1),
            MakeClass(TEXT("Heavy"), 0.6f, TEXT("UnderCenter"), nullptr, 0, -1, 2),
            MakeClass(TEXT("Singleback"), 0.5f, TEXT("UnderCenter")) };
        return Tuning;
    }

    static int32 CountRecognition(const TArray<FPSTelemetryRecognitionEvent>& Events, EPSRecognitionEventKind Kind, const TCHAR* PlayerName = nullptr)
    {
        int32 Found = 0;
        for (const FPSTelemetryRecognitionEvent& Event : Events)
        {
            Found += (Event.Kind == Kind && (!PlayerName || Event.PlayerName == PlayerName)) ? 1 : 0;
        }
        return Found;
    }

    static const FPSTelemetryRecognitionEvent* LastDiagnosis(const TArray<FPSTelemetryRecognitionEvent>& Events, const TCHAR* PlayerName)
    {
        for (int32 Index = Events.Num() - 1; Index >= 0; --Index)
        {
            if (Events[Index].Kind == EPSRecognitionEventKind::Diagnosis && Events[Index].PlayerName == PlayerName)
            {
                return &Events[Index];
            }
        }
        return nullptr;
    }

    static const FPSTelemetryDeceptionEvent* FindBite(const TArray<FPSTelemetryDeceptionEvent>& Events, const TCHAR* PlayerName)
    {
        return Events.FindByPredicate([PlayerName](const FPSTelemetryDeceptionEvent& Event)
        {
            return Event.Kind == EPSDeceptionEventKind::Bite && Event.PlayerName == PlayerName;
        });
    }

    static FPSKeySighting Sighted(const TCHAR* Key, EPSPlayRead Read, bool bFakeable, float ShownAt)
    {
        FPSKeySighting Sighting;
        Sighting.Key.Key = FName(Key);
        Sighting.Key.Read = Read;
        Sighting.Key.bFakeable = bFakeable;
        Sighting.ShownAt = ShownAt;
        return Sighting;
    }

    static bool HasKey(const TArray<FPSKeyRead>& Keys, const TCHAR* Key, EPSPlayRead Read, bool bFakeable)
    {
        return Keys.ContainsByPredicate([Key, Read, bFakeable](const FPSKeyRead& Shown)
        {
            return Shown.Key == FName(Key) && Shown.Read == Read && Shown.bFakeable == bFakeable;
        });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The formation classifier
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayRecognitionFormationTest,
    "PlaySports.AI.Recognition.FormationClassifier",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayRecognitionFormationTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayRecognitionTests;
    const FPSPlayRecognitionTuning Classes = ClassifierTuning();
    const FVector Line = FVector::ZeroVector;

    // The game's own 11-personnel lineup: two receivers and the inline tight end right, one
    // receiver left, the back behind a quarterback under center.
    const TArray<EPlayerRole> Personnel11 = {
        EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman,
        EPlayerRole::OffensiveLineman, EPlayerRole::Quarterback, EPlayerRole::RunningBack, EPlayerRole::WideReceiver,
        EPlayerRole::WideReceiver, EPlayerRole::WideReceiver, EPlayerRole::TightEnd };
    const TArray<FVector> Lineup = APSFieldGrid::ComputeLineup(Personnel11, 0.f);
    TArray<FPSAlignedPlayer> Offense;
    for (int32 Index = 0; Index < Personnel11.Num(); ++Index)
    {
        FPSAlignedPlayer& Player = Offense.AddDefaulted_GetRef();
        Player.Role = Personnel11[Index];
        Player.Location = Lineup[Index];
    }
    FPSFormationRead Read = PSPlayRecognition::ClassifyFormation(Offense, Line, Classes);
    TestTrue(TEXT("The game's lineup is read"), Read.bValid);
    TestEqual(TEXT("...11 personnel"), Read.Personnel, FString(TEXT("11")));
    TestEqual(TEXT("...the quarterback under center"), Read.QBAlignment, FName(TEXT("UnderCenter")));
    TestEqual(TEXT("...one back, behind him"), Read.Backfield, FName(TEXT("Single")));
    TestEqual(TEXT("...strong right"), Read.StrongSide, 1);
    TestTrue(TEXT("...three to the strength, one away"), Read.StrongSideReceivers == 3 && Read.WeakSideReceivers == 1);
    TestTrue(TEXT("...the tight end inline, three split wide"), Read.InlineTightEnds == 1 && Read.SplitReceivers == 3);
    TestEqual(TEXT("...trips"), Read.ClassId, FName(TEXT("Trips")));
    TestEqual(TEXT("...with trips' run lean"), Read.RunLean, 0.4f);

    // Motion: the outside receiver comes across. Two and two, the inline tight end makes the
    // right the strength, and it is no longer trips.
    for (FPSAlignedPlayer& Player : Offense)
    {
        if (Player.Role == EPlayerRole::WideReceiver && Player.Location.Y > 1000.f)
        {
            Player.Location.Y = -Player.Location.Y;
        }
    }
    Read = PSPlayRecognition::ClassifyFormation(Offense, Line, Classes);
    TestTrue(TEXT("A man in motion changes the read: two and two"), Read.StrongSideReceivers == 2 && Read.WeakSideReceivers == 2);
    TestEqual(TEXT("...strong to the tight end"), Read.StrongSide, 1);
    TestEqual(TEXT("...a singleback look now"), Read.ClassId, FName(TEXT("Singleback")));

    // The I: a fullback and a tailback stacked behind the quarterback, 21 personnel.
    Offense.Reset();
    AddLine(Offense);
    Offense.Add(Aligned(EPlayerRole::Quarterback, -100.f, 0.f));
    Offense.Add(Aligned(EPlayerRole::RunningBack, -350.f, 0.f));
    Offense.Add(Aligned(EPlayerRole::RunningBack, -600.f, 20.f));
    Offense.Add(Aligned(EPlayerRole::TightEnd, -50.f, 450.f));
    Offense.Add(Aligned(EPlayerRole::WideReceiver, -50.f, -900.f));
    Offense.Add(Aligned(EPlayerRole::WideReceiver, -50.f, 900.f));
    Read = PSPlayRecognition::ClassifyFormation(Offense, Line, Classes);
    TestEqual(TEXT("The I: 21 personnel"), Read.Personnel, FString(TEXT("21")));
    TestTrue(TEXT("...two backs stacked"), Read.Backs == 2 && Read.Backfield == FName(TEXT("I")));
    TestEqual(TEXT("...the I-formation"), Read.ClassId, FName(TEXT("IForm")));
    TestEqual(TEXT("...strong to the tight end's side"), Read.StrongSide, 1);

    // Shotgun trips left, the back offset beside the quarterback.
    Offense.Reset();
    AddLine(Offense);
    Offense.Add(Aligned(EPlayerRole::Quarterback, -500.f, 0.f));
    Offense.Add(Aligned(EPlayerRole::RunningBack, -500.f, 150.f));
    Offense.Add(Aligned(EPlayerRole::WideReceiver, -50.f, -600.f));
    Offense.Add(Aligned(EPlayerRole::WideReceiver, -50.f, -900.f));
    Offense.Add(Aligned(EPlayerRole::WideReceiver, -50.f, -1200.f));
    Offense.Add(Aligned(EPlayerRole::WideReceiver, -50.f, 900.f));
    Read = PSPlayRecognition::ClassifyFormation(Offense, Line, Classes);
    TestEqual(TEXT("Shotgun"), Read.QBAlignment, FName(TEXT("Shotgun")));
    TestEqual(TEXT("...the back offset"), Read.Backfield, FName(TEXT("Offset")));
    TestEqual(TEXT("...strong left"), Read.StrongSide, -1);
    TestEqual(TEXT("...shotgun trips"), Read.ClassId, FName(TEXT("ShotgunTrips")));

    // The back splits out wide: an empty backfield, which is read before anything else.
    for (FPSAlignedPlayer& Player : Offense)
    {
        if (Player.Role == EPlayerRole::RunningBack)
        {
            Player.Location = FVector(-50.f, 1500.f, 100.f);
        }
    }
    Read = PSPlayRecognition::ClassifyFormation(Offense, Line, Classes);
    TestTrue(TEXT("Nobody in the backfield: empty"), Read.Backs == 0 && Read.Backfield == FName(TEXT("Empty")));
    TestEqual(TEXT("...the first class that matches names it"), Read.ClassId, FName(TEXT("Empty")));
    TestEqual(TEXT("...still 10 personnel"), Read.Personnel, FString(TEXT("10")));

    // A pistol two-by-two matches no class here.
    Offense.Reset();
    AddLine(Offense);
    Offense.Add(Aligned(EPlayerRole::Quarterback, -350.f, 0.f));
    Offense.Add(Aligned(EPlayerRole::RunningBack, -650.f, 0.f));
    Offense.Add(Aligned(EPlayerRole::WideReceiver, -50.f, 900.f));
    Offense.Add(Aligned(EPlayerRole::WideReceiver, -50.f, -900.f));
    Read = PSPlayRecognition::ClassifyFormation(Offense, Line, Classes);
    TestEqual(TEXT("The pistol"), Read.QBAlignment, FName(TEXT("Pistol")));
    TestEqual(TEXT("...matches no class: unknown"), Read.ClassId, PSPlayRecognition::UnknownClassId());
    TestEqual(TEXT("...at the default lean"), Read.RunLean, Classes.DefaultRunLean);

    TArray<FPSAlignedPlayer> NoPasser;
    AddLine(NoPasser);
    TestFalse(TEXT("No quarterback, no read"), PSPlayRecognition::ClassifyFormation(NoPasser, Line, Classes).bValid);

    // In a world: the read is made at the snap and announced on the first look after it.
    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayRecognitionSubsystem* Recognition = UPSPlayRecognitionSubsystem::Get(World);
    UPSAIFieldSnapshot* Field = World ? World->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Recognition"), Recognition) || !TestNotNull(TEXT("Field snapshot"), Field))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    Recognition->SetTuning(Classes);
    for (int32 Index = 0; Index < Personnel11.Num(); ++Index)
    {
        if (!SpawnPlayer(World, Personnel11[Index], FString::Printf(TEXT("O%02d"), Index), Lineup[Index]))
        {
            AddError(TEXT("An offensive player didn't spawn"));
            DestroyTestWorld(World);
            return false;
        }
    }
    SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB"), FVector(450.f, 0.f, 100.f));
    Field->Invalidate();

    TArray<FPSTelemetryRecognitionEvent> Events;
    Bus->OnRecognitionMC.AddLambda([&Events](const FPSTelemetryRecognitionEvent& Event) { Events.Add(Event); });
    Snap(Bus);
    TestEqual(TEXT("At the snap the defense reads trips"), Recognition->GetFormationRead().ClassId, FName(TEXT("Trips")));
    TestEqual(TEXT("...11 personnel"), Recognition->GetFormationRead().Personnel, FString(TEXT("11")));
    TestEqual(TEXT("Nothing is announced inside the snap's broadcast"), Events.Num(), 0);
    Recognition->Observe(0.1f);
    Recognition->Observe(0.2f);
    TestEqual(TEXT("The first look announces the formation, once"), CountRecognition(Events, EPSRecognitionEventKind::Formation), 1);
    const FPSTelemetryRecognitionEvent* Announced = Events.FindByPredicate([](const FPSTelemetryRecognitionEvent& Event)
    {
        return Event.Kind == EPSRecognitionEventKind::Formation;
    });
    TestTrue(TEXT("...as trips in 11 personnel"), Announced && Announced->Formation == FName(TEXT("Trips")) && Announced->Personnel == TEXT("11")
        && FMath::IsNearlyEqual(Announced->RunLean, 0.4f, 0.001f));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Key-reading: the keys and the diagnosis
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayRecognitionKeyReadTest,
    "PlaySports.AI.Recognition.KeyRead",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayRecognitionKeyReadTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayRecognitionTests;
    const FPSPlayRecognitionTuning Keys;

    // The keys a look shows.
    TestEqual(TEXT("Nothing moving shows nothing"), PSPlayRecognition::ReadKeys(FPSKeyLook(), Keys).Num(), 0);
    FPSKeyLook Look;
    Look.bPasserHasBall = true;
    Look.PasserDepth = 400.f;
    Look.PasserRetreat = 300.f;
    TestTrue(TEXT("The quarterback dropping with the ball: a pass"), HasKey(PSPlayRecognition::ReadKeys(Look, Keys), TEXT("Drop"), EPSPlayRead::Pass, false));
    Look.PasserDepth = 500.f;
    Look.PasserRetreat = 0.f;
    TestEqual(TEXT("...a shotgun snap alone is no drop"), PSPlayRecognition::ReadKeys(Look, Keys).Num(), 0);
    Look.PasserRetreat = 300.f;
    Look.bPasserHasBall = false;
    TestEqual(TEXT("...nor is a quarterback without the ball"), PSPlayRecognition::ReadKeys(Look, Keys).Num(), 0);

    Look = FPSKeyLook();
    Look.bHandedOff = true;
    Look.BackDownhillSpeed = 500.f;
    TArray<FPSKeyRead> Shown = PSPlayRecognition::ReadKeys(Look, Keys);
    TestTrue(TEXT("A hand-off: a run, not a fake"), Shown.Num() == 1 && HasKey(Shown, TEXT("Handoff"), EPSPlayRead::Run, false));

    Look = FPSKeyLook();
    Look.BackDownhillSpeed = Keys.FlowMinSpeed + 100.f;
    TestTrue(TEXT("A back flowing downhill: a run a fake can show"), HasKey(PSPlayRecognition::ReadKeys(Look, Keys), TEXT("Flow"), EPSPlayRead::Run, true));
    Look.BackDownhillSpeed = Keys.FlowMinSpeed * 0.5f;
    TestEqual(TEXT("...not jogging"), PSPlayRecognition::ReadKeys(Look, Keys).Num(), 0);

    Look = FPSKeyLook();
    Look.bHasLine = true;
    Look.LineAdvance = Keys.LineKeyDistance + 50.f;
    TestTrue(TEXT("The line firing off: a run"), HasKey(PSPlayRecognition::ReadKeys(Look, Keys), TEXT("LineFire"), EPSPlayRead::Run, false));
    Look.LineAdvance = -(Keys.LineKeyDistance + 50.f);
    TestTrue(TEXT("The line setting back: a pass"), HasKey(PSPlayRecognition::ReadKeys(Look, Keys), TEXT("PassSet"), EPSPlayRead::Pass, false));
    Look.LineAdvance = Keys.LineKeyDistance * 0.5f;
    TestEqual(TEXT("...a step either way is nothing"), PSPlayRecognition::ReadKeys(Look, Keys).Num(), 0);

    // The diagnosis over time: flow at 0.1, the drop at 0.5; he reads a run key in 0.2, a pass key
    // in 0.3.
    const TArray<FPSKeySighting> PlayAction = { Sighted(TEXT("Flow"), EPSPlayRead::Run, true, 0.1f), Sighted(TEXT("Drop"), EPSPlayRead::Pass, false, 0.5f) };
    TestTrue(TEXT("Before he has read anything: none"), PSPlayRecognition::Diagnose(PlayAction, 0.3f, 0.2f, 0.25f).Read == EPSPlayRead::None);
    FPSPlayDiagnosis Diagnosis = PSPlayRecognition::Diagnose(PlayAction, 0.3f, 0.2f, 0.35f);
    TestTrue(TEXT("He reads the flow: run"), Diagnosis.Read == EPSPlayRead::Run && Diagnosis.Key == FName(TEXT("Flow")) && Diagnosis.bFakeable);
    TestTrue(TEXT("...still run before he has read the drop"), PSPlayRecognition::Diagnose(PlayAction, 0.3f, 0.2f, 0.75f).Read == EPSPlayRead::Run);
    Diagnosis = PSPlayRecognition::Diagnose(PlayAction, 0.3f, 0.2f, 0.85f);
    TestTrue(TEXT("...then the drop: pass"), Diagnosis.Read == EPSPlayRead::Pass && Diagnosis.Key == FName(TEXT("Drop")));
    TestTrue(TEXT("...read when it showed plus his pass read"), FMath::IsNearlyEqual(Diagnosis.ReadAt, 0.8f, 0.001f));

    const TArray<FPSKeySighting> Draw = { Sighted(TEXT("Drop"), EPSPlayRead::Pass, false, 0.3f), Sighted(TEXT("Handoff"), EPSPlayRead::Run, false, 0.9f) };
    TestTrue(TEXT("A draw: the drop reads pass"), PSPlayRecognition::Diagnose(Draw, 0.2f, 0.2f, 0.6f).Read == EPSPlayRead::Pass);
    TestTrue(TEXT("...until the hand-off reads run"), PSPlayRecognition::Diagnose(Draw, 0.2f, 0.2f, 1.2f).Key == FName(TEXT("Handoff")));
    const TArray<FPSKeySighting> Late = { Sighted(TEXT("Drop"), EPSPlayRead::Pass, false, 0.3f), Sighted(TEXT("Flow"), EPSPlayRead::Run, true, 0.6f) };
    TestTrue(TEXT("A true key beats later flow"), PSPlayRecognition::Diagnose(Late, 0.2f, 0.2f, 1.f).Read == EPSPlayRead::Pass);

    // In a world: the line, the quarterback under center with the ball, the back behind him and
    // a 4-3 front whose middle linebacker reads at once.
    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayRecognitionSubsystem* Recognition = UPSPlayRecognitionSubsystem::Get(World);
    UPSDefenderGapSubsystem* Gaps = World ? World->GetSubsystem<UPSDefenderGapSubsystem>() : nullptr;
    UPSAIFieldSnapshot* Field = World ? World->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Recognition"), Recognition) || !TestNotNull(TEXT("Gaps"), Gaps) || !TestNotNull(TEXT("Field snapshot"), Field))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    TArray<APSPlayerPawn*> Linemen;
    for (int32 Index = 0; Index < 5; ++Index)
    {
        Linemen.Add(SpawnPlayer(World, EPlayerRole::OffensiveLineman, FString::Printf(TEXT("OL_%d"), Index), FVector(-50.f, (Index - 2) * 150.f, 100.f)));
    }
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-100.f, 0.f, 100.f));
    APSPlayerPawn* RB = SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-500.f, 0.f, 100.f));
    TArray<APSPlayerPawn*> Front;
    const float LinemanY[] = { -300.f, -100.f, 100.f, 300.f };
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Front.Add(SpawnPlayer(World, EPlayerRole::DefensiveLineman, FString::Printf(TEXT("DL_%d"), Index), FVector(100.f, LinemanY[Index], 100.f), 100.f));
    }
    const float LinebackerY[] = { -400.f, 0.f, 400.f };
    for (int32 Index = 0; Index < 3; ++Index)
    {
        Front.Add(SpawnPlayer(World, EPlayerRole::Linebacker, FString::Printf(TEXT("LB_%d"), Index), FVector(450.f, LinebackerY[Index], 100.f), 100.f));
    }
    if (!TestFalse(TEXT("Everyone spawned"), Linemen.Contains(nullptr) || Front.Contains(nullptr) || !QB || !RB) || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }
    Field->Invalidate();
    TArray<FPSTelemetryRecognitionEvent> Events;
    Bus->OnRecognitionMC.AddLambda([&Events](const FPSTelemetryRecognitionEvent& Event) { Events.Add(Event); });

    Snap(Bus);
    for (APSPlayerPawn* Defender : Front)
    {
        Assign(Defender, Defender->GetAttributes().Role == EPlayerRole::DefensiveLineman ? EPSDefensiveAssignmentType::PassRush : EPSDefensiveAssignmentType::RunFit);
    }
    Gaps->AssignGaps(TEXT("4-3"));
    APSPlayerPawn* Mike = Front[5];
    UPSDefenderAIComponent* MikeAI = AIOf(Mike);
    MikeAI->TickAI(0.05f);
    TestTrue(TEXT("At the snap the linebacker reads, holding"), MikeAI->GetAction() == EPSDefenderAction::Read && MikeAI->GetDesiredDirection().IsNearlyZero());
    TestEqual(TEXT("...no key yet"), Recognition->GetSightings().Num(), 0);

    // The back flows downhill: the linebacker reads run and fills his gap before the hand-off.
    SetVelocity(RB, FVector(400.f, 0.f, 0.f));
    MikeAI->TickAI(0.05f);
    FVector GapTarget;
    TestTrue(TEXT("(He has a gap against the back)"), Gaps->GetFitTarget(Mike, RB, GapTarget));
    TestTrue(TEXT("Reading run from the flow he fills his gap"), MikeAI->GetAction() == EPSDefenderAction::Read
        && PointsToward(MikeAI->GetDesiredDirection(), Mike->GetActorLocation(), GapTarget));
    const FPSTelemetryRecognitionEvent* Read = LastDiagnosis(Events, TEXT("LB_1"));
    TestTrue(TEXT("...announced: run, from the flow"), Read && Read->Read == FName(TEXT("Run")) && Read->Key == FName(TEXT("Flow")));
    TestTrue(TEXT("The formation went out with the first look"), CountRecognition(Events, EPSRecognitionEventKind::Formation) == 1);

    // The quarterback drops: a true key, which beats the flow. He drops into coverage.
    QB->SetActorLocation(FVector(-450.f, 0.f, 100.f));
    MikeAI->TickAI(0.05f);
    TestEqual(TEXT("Reading the drop, he drops"), MikeAI->GetAction(), EPSDefenderAction::Zone);
    TestTrue(TEXT("...to his drop depth"), PointsToward(MikeAI->GetDesiredDirection(), Mike->GetActorLocation(),
        FVector(MikeAI->GetTuning().PassDropDepth, Mike->GetActorLocation().Y, 100.f)));
    Read = LastDiagnosis(Events, TEXT("LB_1"));
    TestTrue(TEXT("...announced: pass, from the drop"), Read && Read->Read == FName(TEXT("Pass")) && Read->Key == FName(TEXT("Drop")));
    const TArray<FPSKeySighting>& Seen = Recognition->GetSightings();
    TestTrue(TEXT("The play showed the flow, then the drop"), Seen.Num() == 2 && Seen[0].Key.Key == FName(TEXT("Flow")) && Seen[1].Key.Key == FName(TEXT("Drop"))
        && Seen[0].ShownAt < Seen[1].ShownAt && Seen[0].Player.Get() == RB);

    // The line: firing off its stance is a run key; setting back from it, a pass key.
    QB->SetActorLocation(FVector(-100.f, 0.f, 100.f));
    SetVelocity(RB, FVector::ZeroVector);
    Snap(Bus);
    for (APSPlayerPawn* Lineman : Linemen)
    {
        Lineman->SetActorLocation(Lineman->GetActorLocation() + FVector(Recognition->GetTuning().LineKeyDistance + 50.f, 0.f, 0.f));
    }
    Recognition->Observe(0.1f);
    TestTrue(TEXT("The line firing off shows a run"), Recognition->GetSightings().Num() == 1 && Recognition->GetSightings()[0].Key.Key == FName(TEXT("LineFire")));
    Snap(Bus);
    for (APSPlayerPawn* Lineman : Linemen)
    {
        Lineman->SetActorLocation(Lineman->GetActorLocation() - FVector(2.f * (Recognition->GetTuning().LineKeyDistance + 50.f), 0.f, 0.f));
    }
    Recognition->Observe(0.1f);
    TestTrue(TEXT("...setting back from where it lined up, a pass"), Recognition->GetSightings().Num() == 1 && Recognition->GetSightings()[0].Key.Key == FName(TEXT("PassSet")));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Latency: Awareness, expectation and style
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayRecognitionLatencyTest,
    "PlaySports.AI.Recognition.LatencyOrdering",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayRecognitionLatencyTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayRecognitionTests;

    // The rule: a sharper reaction reads everything sooner.
    const FPSPlayRecognitionTuning Rules;
    const FPSDefenderReadTimes Sharp = PSPlayRecognition::ReadTimes(0.06f, 0.5f, 0.f, Rules);
    const FPSDefenderReadTimes Slow = PSPlayRecognition::ReadTimes(0.42f, 0.5f, 0.f, Rules);
    TestTrue(TEXT("High Awareness reads a pass sooner than low"), Sharp.PassSeconds < Slow.PassSeconds);
    TestTrue(TEXT("...a run"), Sharp.RunSeconds < Slow.RunSeconds);
    TestTrue(TEXT("...a throw"), Sharp.ThrowSeconds < Slow.ThrowSeconds);
    TestTrue(TEXT("...and through a fake"), Sharp.FakeSeconds < Slow.FakeSeconds);
    TestTrue(TEXT("Expecting nothing, a key read is his reaction times its scale"), FMath::IsNearlyEqual(Slow.PassSeconds, 0.42f * Rules.PassReadScale, 0.0001f)
        && FMath::IsNearlyEqual(Slow.RunSeconds, 0.42f * Rules.RunReadScale, 0.0001f));
    const FPSDefenderReadTimes ExpectsRun = PSPlayRecognition::ReadTimes(0.42f, 0.9f, 0.f, Rules);
    const FPSDefenderReadTimes ExpectsPass = PSPlayRecognition::ReadTimes(0.42f, 0.1f, 0.f, Rules);
    TestTrue(TEXT("Expecting the run, he reads the pass later"), ExpectsRun.PassSeconds > Slow.PassSeconds && FMath::IsNearlyEqual(ExpectsRun.RunSeconds, Slow.RunSeconds, 0.0001f));
    TestTrue(TEXT("...expecting the pass, the run later"), ExpectsPass.RunSeconds > Slow.RunSeconds && FMath::IsNearlyEqual(ExpectsPass.PassSeconds, Slow.PassSeconds, 0.0001f));
    TestTrue(TEXT("...and the fake takes longer to see through the more he expects the run"), ExpectsRun.FakeSeconds > Slow.FakeSeconds && Slow.FakeSeconds > ExpectsPass.FakeSeconds);
    const float Spread = Rules.LatencyJitter;
    TestTrue(TEXT("The fake read varies within the jitter"), FMath::IsNearlyEqual(PSPlayRecognition::ReadTimes(0.42f, 0.5f, 1.f, Rules).FakeSeconds, Slow.FakeSeconds * (1.f + Spread), 0.0001f)
        && FMath::IsNearlyEqual(PSPlayRecognition::ReadTimes(0.42f, 0.5f, -1.f, Rules).FakeSeconds, Slow.FakeSeconds * (1.f - Spread), 0.0001f));

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayRecognitionSubsystem* Recognition = UPSPlayRecognitionSubsystem::Get(World);
    UPSAIFieldSnapshot* Field = World ? World->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Recognition"), Recognition) || !TestNotNull(TEXT("Field snapshot"), Field))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    FPSPlayRecognitionTuning Exact = Recognition->GetTuning();
    Exact.LatencyJitter = 0.f;
    Recognition->SetTuning(Exact);

    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-100.f, 0.f, 100.f));
    SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-500.f, 0.f, 100.f));
    APSPlayerPawn* Aware = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB_AWARE"), FVector(450.f, -200.f, 100.f), 90.f);
    APSPlayerPawn* Unaware = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB_UNAWARE"), FVector(450.f, 200.f, 100.f), 30.f);
    const FVector Landing(1500.f, 0.f, 100.f);
    APSPlayerPawn* AwareCorner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_AWARE"), Landing + FVector(0.f, 300.f, 0.f), 90.f);
    APSPlayerPawn* UnawareCorner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_UNAWARE"), Landing - FVector(0.f, 300.f, 0.f), 30.f);
    APSPlayerPawn* Hawk = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_HAWK"), FVector(1500.f, 2500.f, 100.f), 70.f, 1.f);
    APSPlayerPawn* Blanket = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_BLANKET"), FVector(1500.f, -2500.f, 100.f), 70.f, -1.f);
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Aware LB"), Aware) || !TestNotNull(TEXT("Unaware LB"), Unaware) || !TestNotNull(TEXT("Aware CB"), AwareCorner)
        || !TestNotNull(TEXT("Unaware CB"), UnawareCorner) || !TestNotNull(TEXT("Hawk"), Hawk) || !TestNotNull(TEXT("Blanket"), Blanket) || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }
    Field->Invalidate();

    // A drop-back: both linebackers drop, the aware one first, each when the drop showed plus
    // his pass read.
    Snap(Bus);
    Assign(Aware, EPSDefensiveAssignmentType::RunFit);
    Assign(Unaware, EPSDefensiveAssignmentType::RunFit);
    UPSDefenderAIComponent* AwareAI = AIOf(Aware);
    UPSDefenderAIComponent* UnawareAI = AIOf(Unaware);
    AwareAI->TickAI(0.05f);
    UnawareAI->TickAI(0.05f);
    const FPSDefenderReadTimes AwareTimes = Recognition->GetReadTimes(Aware);
    const FPSDefenderReadTimes UnawareTimes = Recognition->GetReadTimes(Unaware);
    TestTrue(TEXT("The aware linebacker reads every key sooner"), AwareTimes.PassSeconds < UnawareTimes.PassSeconds && AwareTimes.RunSeconds < UnawareTimes.RunSeconds
        && AwareTimes.ThrowSeconds < UnawareTimes.ThrowSeconds && AwareTimes.FakeSeconds < UnawareTimes.FakeSeconds);
    QB->SetActorLocation(FVector(-450.f, 0.f, 100.f));
    const float Step = 0.05f;
    float Clock = 0.05f;
    float AwareDropped = -1.f;
    float UnawareDropped = -1.f;
    for (int32 Tick = 0; Tick < 40 && (AwareDropped < 0.f || UnawareDropped < 0.f); ++Tick)
    {
        Clock += Step;
        AwareAI->TickAI(Step);
        UnawareAI->TickAI(Step);
        if (AwareDropped < 0.f && AwareAI->GetAction() == EPSDefenderAction::Zone)
        {
            AwareDropped = Clock;
        }
        if (UnawareDropped < 0.f && UnawareAI->GetAction() == EPSDefenderAction::Zone)
        {
            UnawareDropped = Clock;
        }
    }
    const float DropShown = Recognition->GetSightings().Num() > 0 ? Recognition->GetSightings()[0].ShownAt : -1.f;
    TestTrue(TEXT("Both read the drop"), AwareDropped > 0.f && UnawareDropped > 0.f && DropShown > 0.f);
    TestTrue(TEXT("...the aware one first"), AwareDropped < UnawareDropped);
    TestTrue(TEXT("...the unaware one when the drop showed plus his pass read"), UnawareDropped >= DropShown + UnawareTimes.PassSeconds - 0.001f
        && UnawareDropped < DropShown + UnawareTimes.PassSeconds + Step + 0.001f);

    // A throw between the corners: the aware one breaks on it first -- he jumps it.
    Snap(Bus);
    Assign(AwareCorner, EPSDefensiveAssignmentType::ZoneCoverage);
    Assign(UnawareCorner, EPSDefensiveAssignmentType::ZoneCoverage);
    UPSDefenderAIComponent* AwareCornerAI = AIOf(AwareCorner);
    UPSDefenderAIComponent* UnawareCornerAI = AIOf(UnawareCorner);
    AwareCornerAI->TickAI(Step);
    UnawareCornerAI->TickAI(Step);
    FPSTelemetryThrowEvent Throw;
    Throw.PasserName = TEXT("QB");
    Throw.TargetLocation = Landing;
    Throw.LandingLocation = Landing;
    Bus->PublishThrow(Throw);
    Clock = Step;
    float AwareBroke = -1.f;
    float UnawareBroke = -1.f;
    for (int32 Tick = 0; Tick < 40 && (AwareBroke < 0.f || UnawareBroke < 0.f); ++Tick)
    {
        Clock += Step;
        AwareCornerAI->TickAI(Step);
        UnawareCornerAI->TickAI(Step);
        if (AwareBroke < 0.f && AwareCornerAI->GetAction() == EPSDefenderAction::BallHawk)
        {
            AwareBroke = Clock;
        }
        if (UnawareBroke < 0.f && UnawareCornerAI->GetAction() == EPSDefenderAction::BallHawk)
        {
            UnawareBroke = Clock;
        }
    }
    TestTrue(TEXT("Both corners break on the throw"), AwareBroke > 0.f && UnawareBroke > 0.f);
    TestTrue(TEXT("...the aware one first: he jumps it"), AwareBroke < UnawareBroke);
    TestTrue(TEXT("...the unaware one after his throw read"), UnawareBroke >= Step + Recognition->GetReadTimes(UnawareCorner).ThrowSeconds - 0.001f);

    // Style, rated alike (Epic 79): a ball hawk breaks on throws sooner and sees through fakes
    // later than a blanket corner.
    const FPSDefenderReadTimes HawkTimes = Recognition->GetReadTimes(Hawk);
    const FPSDefenderReadTimes BlanketTimes = Recognition->GetReadTimes(Blanket);
    TestTrue(TEXT("The ball hawk jumps throws sooner"), HawkTimes.ThrowSeconds < BlanketTimes.ThrowSeconds);
    TestTrue(TEXT("...and bites on fakes longer"), HawkTimes.FakeSeconds > BlanketTimes.FakeSeconds);
    TestTrue(TEXT("...reading the run and the pass alike"), FMath::IsNearlyEqual(HawkTimes.PassSeconds, BlanketTimes.PassSeconds, 0.0001f)
        && FMath::IsNearlyEqual(HawkTimes.RunSeconds, BlanketTimes.RunSeconds, 0.0001f));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The play-action bite, through the recognition model
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayRecognitionBiteTest,
    "PlaySports.AI.Recognition.PlayActionBite",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayRecognitionBiteTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayRecognitionTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSDeceptionSubsystem* Deception = World ? World->GetSubsystem<UPSDeceptionSubsystem>() : nullptr;
    UPSPlayRecognitionSubsystem* Recognition = UPSPlayRecognitionSubsystem::Get(World);
    UPSAIFieldSnapshot* Field = World ? World->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Deception"), Deception) || !TestNotNull(TEXT("Recognition"), Recognition) || !TestNotNull(TEXT("Field snapshot"), Field))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    // Every look leans neither way here, so the offense's calls alone set what the defense
    // expects; no jitter, so each defender's read is exact.
    FPSPlayRecognitionTuning Reads = Recognition->GetTuning();
    Reads.FormationClasses = { MakeClass(TEXT("AnyLook"), 0.5f) };
    Reads.FakeReadScale = 2.f;
    Reads.ExpectationWeight = 0.5f;
    Reads.TendencyWeight = 0.5f;
    Reads.LatencyJitter = 0.f;
    Reads.MaxBiteSeconds = 1.f;
    Recognition->SetTuning(Reads);
    const FPSDeceptionTuning Fakes = Deception->GetTuning();
    const int32 Window = FMath::Max(1, Fakes.TendencyWindow);

    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f));
    SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(-500.f, 150.f, 100.f));
    SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(0.f, 1500.f, 100.f));
    const TCHAR* Names[] = { TEXT("LB_ELITE"), TEXT("LB_AVERAGE"), TEXT("LB_RAW"), TEXT("LB_HAWK"), TEXT("LB_BLANKET") };
    const float Awareness[] = { 95.f, 50.f, 0.f, 50.f, 50.f };
    const float Style[] = { 0.f, 0.f, 0.f, 1.f, -1.f };
    TArray<APSPlayerPawn*> Linebackers;
    for (int32 Index = 0; Index < 5; ++Index)
    {
        Linebackers.Add(SpawnPlayer(World, EPlayerRole::Linebacker, Names[Index], FVector(500.f, -400.f + 200.f * Index, 100.f), Awareness[Index], Style[Index]));
    }
    if (!TestNotNull(TEXT("QB"), QB) || !TestFalse(TEXT("The linebackers spawned"), Linebackers.Contains(nullptr)) || !TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
    {
        DestroyTestWorld(World);
        return false;
    }
    Field->Invalidate();
    TArray<FPSTelemetryDeceptionEvent> Events;
    Bus->OnDeceptionMC.AddLambda([&Events](const FPSTelemetryDeceptionEvent& Event) { Events.Add(Event); });

    // Calls Window plays of Category's PlayId, then play-action, snaps it, puts every linebacker
    // in the run fit and sells the fake.
    auto RunPlayAction = [&](const TCHAR* TendencyPlay)
    {
        for (int32 Index = 0; Index < Window; ++Index)
        {
            CallOffense(World, TendencyPlay);
            Snap(Bus);
        }
        CallOffense(World, TEXT("Offense_PlayActionPost"));
        Snap(Bus);
        for (APSPlayerPawn* Linebacker : Linebackers)
        {
            Assign(Linebacker, EPSDefensiveAssignmentType::RunFit);
        }
        Events.Reset();
        Deception->UpdateFake(QB, 0.1f);
        Deception->UpdateFake(QB, Fakes.FakeSeconds);
    };

    // A running offense: he expects the run.
    RunPlayAction(TEXT("Offense_InsideZone"));
    TestTrue(TEXT("Against a running offense the defense expects the run"), Recognition->GetExpectedRunShare() > 0.5f);
    TestNull(TEXT("The elite linebacker sees through it"), FindBite(Events, TEXT("LB_ELITE")));
    TestNotNull(TEXT("...the average one bites"), FindBite(Events, TEXT("LB_AVERAGE")));
    const FPSTelemetryDeceptionEvent* RawBite = FindBite(Events, TEXT("LB_RAW"));
    const FPSTelemetryDeceptionEvent* AverageBite = FindBite(Events, TEXT("LB_AVERAGE"));
    TestTrue(TEXT("...the raw one bites, and holds longer"), RawBite && AverageBite && RawBite->Seconds > AverageBite->Seconds);
    TestTrue(TEXT("...each for the rest of his read"), RawBite && FMath::IsNearlyEqual(RawBite->Seconds, Recognition->GetBiteSeconds(Linebackers[2], Fakes.FakeSeconds), 0.0001f)
        && AverageBite && FMath::IsNearlyEqual(AverageBite->Seconds, Recognition->GetReadTimes(Linebackers[1]).FakeSeconds - Fakes.FakeSeconds, 0.0001f));
    TestTrue(TEXT("...and holds instead of dropping"), AIOf(Linebackers[2])->IsFrozen() && !AIOf(Linebackers[0])->IsFrozen());
    TestNotNull(TEXT("The ball hawk bites"), FindBite(Events, TEXT("LB_HAWK")));
    TestNull(TEXT("...the blanket defender, rated the same, doesn't"), FindBite(Events, TEXT("LB_BLANKET")));

    // A passing offense: he expects the pass, and only the raw one bites.
    RunPlayAction(TEXT("Offense_SlantFlat"));
    TestTrue(TEXT("Against a passing offense the defense expects the pass"), Recognition->GetExpectedRunShare() < 0.5f);
    TestNull(TEXT("The elite linebacker sees through it"), FindBite(Events, TEXT("LB_ELITE")));
    TestNull(TEXT("...so does the average one now"), FindBite(Events, TEXT("LB_AVERAGE")));
    TestNotNull(TEXT("...the raw one still bites"), FindBite(Events, TEXT("LB_RAW")));
    TestNotNull(TEXT("...and the ball hawk"), FindBite(Events, TEXT("LB_HAWK")));
    TestNull(TEXT("...not the blanket defender"), FindBite(Events, TEXT("LB_BLANKET")));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The shipped tuning, the lineups, the DNA bindings
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayRecognitionDataTest,
    "PlaySports.AI.Recognition.TuningAndData",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayRecognitionDataTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSPlayRecognitionTuning Shipped;
    if (!TestTrue(TEXT("The shipped tuning loads"), Ingestion->LoadPlayRecognitionTuningFromJson(UPSPlayRecognitionSubsystem::GetDefaultTuningPath(), Shipped)))
    {
        return false;
    }
    const TArray<FString> Problems = PSPlayRecognition::ValidateTuning(Shipped);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    TestEqual(TEXT("...and is sound"), Problems.Num(), 0);
    TestTrue(TEXT("...with formation classes"), Shipped.FormationClasses.Num() > 0);
    TestTrue(TEXT("...a fake takes longer to see through than a plain key"), Shipped.FakeReadScale > Shipped.PassReadScale);

    // Every offensive personnel package, lined up as the game lines it up, is a formation the
    // defense knows.
    FPSPersonnelCatalog Personnel;
    if (!TestTrue(TEXT("The personnel packages load"), Ingestion->LoadPersonnelCatalogFromJson(UPSPersonnelManager::GetDefaultCatalogPath(), Personnel)))
    {
        return false;
    }
    const UEnum* RoleEnum = StaticEnum<EPlayerRole>();
    for (const FPSPersonnelPackage& Package : Personnel.Packages)
    {
        if (!Package.bOffense)
        {
            continue;
        }
        TArray<EPlayerRole> Roles;
        for (const TPair<FString, int32>& Count : Package.RoleCounts)
        {
            const int64 Value = RoleEnum->GetValueByNameString(Count.Key);
            for (int32 Index = 0; Value != INDEX_NONE && Index < Count.Value; ++Index)
            {
                Roles.Add(static_cast<EPlayerRole>(Value));
            }
        }
        const TArray<FVector> Lineup = APSFieldGrid::ComputeLineup(Roles, 0.f);
        TArray<FPSAlignedPlayer> Offense;
        for (int32 Index = 0; Index < Roles.Num(); ++Index)
        {
            FPSAlignedPlayer& Player = Offense.AddDefaulted_GetRef();
            Player.Role = Roles[Index];
            Player.Location = Lineup[Index];
        }
        const FPSFormationRead Read = PSPlayRecognition::ClassifyFormation(Offense, FVector::ZeroVector, Shipped);
        TestTrue(FString::Printf(TEXT("%s lines up in a known formation (%s)"), *Package.PackageId.ToString(), *Read.ClassId.ToString()),
            Read.bValid && Read.ClassId != PSPlayRecognition::UnknownClassId());
    }

    // The style catalog binds the recognition reads (Epic 79) and stays sound.
    FPSPlayerDNACatalog Catalog;
    if (TestTrue(TEXT("The DNA catalog loads"), Ingestion->LoadPlayerDNACatalogFromJson(UPSPlayerDNASubsystem::GetDefaultCatalogPath(), Catalog)))
    {
        TestEqual(TEXT("...and is sound with its recognition bindings"), PSPlayerDNA::ValidateCatalog(Catalog).Num(), 0);
        TestTrue(TEXT("...which bind the recognition tuning"), PSPlayerDNA::FindTargetStruct(TEXT("Recognition")) == FPSPlayRecognitionTuning::StaticStruct()
            && Catalog.Bindings.ContainsByPredicate([](const FPSDNABinding& Binding) { return Binding.Target == FName(TEXT("Recognition")); }));
    }

    FPSPlayRecognitionTuning Broken = Shipped;
    Broken.UnderCenterMaxDepth = -1.f;
    Broken.FlowMinSpeed = 0.f;
    Broken.TendencyWeight = 2.f;
    Broken.LatencyJitter = 1.f;
    Broken.FormationClasses.Add(Shipped.FormationClasses[0]);
    FPSFormationClassDef Odd;
    Odd.ClassId = FName(TEXT("Wildcat"));
    Odd.QBAlignment = FName(TEXT("Direct"));
    Odd.RunLean = 1.5f;
    Broken.FormationClasses.Add(Odd);
    TestEqual(TEXT("Broken tuning: a negative depth, no flow speed, a weight over 1, a jitter of 1, a repeated class, a bad alignment, a lean over 1"),
        PSPlayRecognition::ValidateTuning(Broken).Num(), 7);
    FPSPlayRecognitionTuning NoClasses = Shipped;
    NoClasses.FormationClasses.Reset();
    TestEqual(TEXT("No classes at all is caught"), PSPlayRecognition::ValidateTuning(NoClasses).Num(), 1);
    return true;
}

#endif
