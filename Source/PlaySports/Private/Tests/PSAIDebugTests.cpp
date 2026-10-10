// PSAIDebugTests.cpp -- AI observability and debug tooling (Epic 85)
//
// Tests covered:
//   1. The decision log: off, it records nothing; on, the quarterback's read (each receiver he
//      weighed, the one he threw to and why), a corner's coverage, a rusher's moves and the CPU's
//      play calls are recorded per player and per play, and the overlay's text says who did
//      what at whom.
//   2. The play post-mortem: one JSON file per play with the snap, the calls, the bus events
//      from the snap on and every player's decision stream, written when the play ends and kept
//      to the newest MaxPostMortemFiles.
//   3. The scenario runner (in Epic 24's gym): every shipped scenario places its players, runs a
//      decision cycle and gets the decisions it expects; a wrong expectation fails, and so does a
//      scenario naming a player it doesn't place.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSAIDecisionLog.h"
#include "PSAIScenarioRunner.h"
#include "PSBall.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayerPawn.h"
#include "PSRouteRunnerComponent.h"
#include "PSRushMoveComponent.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSAIDebugTests
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

    /** A pawn rated Rating at everything under his side's AI, bound to the bus. */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location, float Rating = 70.f)
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
        Attributes.Speed = Rating;
        Attributes.Agility = Rating;
        Attributes.Strength = Rating;
        Attributes.Acceleration = Rating;
        Attributes.Awareness = Rating;
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
            AI->GetSkillAI()->BindToBus();
        }
        return Pawn;
    }

    static APSOffenseController* OffenseOf(const APSPlayerPawn* Pawn)
    {
        return Pawn ? Cast<APSOffenseController>(Pawn->GetController()) : nullptr;
    }

    static APSDefenseController* DefenseOf(const APSPlayerPawn* Pawn)
    {
        return Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr;
    }

    static void GiveBall(UWorld* World, APSPlayerPawn* Carrier)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if (APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), Carrier->GetActorLocation(), FRotator::ZeroRotator, SpawnParams))
        {
            Ball->AttachToCarrier(Carrier, TEXT("HandSocket"));
            Carrier->GainPossession();
        }
    }

    /** A snap (the CPU's plays go out on it), then the test's own: no routes, a pass call, and
     *  the corner on the receiver. */
    static void StartPassPlay(UWorld* World, UPSTelemetryBus* Bus, APSPlayerPawn* Corner, APSPlayerPawn* Receiver)
    {
        FPSTelemetrySnapEvent Snap;
        Bus->PublishSnap(Snap);
        for (TActorIterator<APSOffenseController> It(World); It; ++It)
        {
            It->SetAssignedRoute(TArray<FVector>());
            It->GetRouteRunner()->ClearRoutePlan();
        }
        if (Corner)
        {
            DefenseOf(Corner)->SetAssignment(EPSDefensiveAssignmentType::ManCoverage, Receiver);
        }
        FPSTelemetryPlayCallEvent Call;
        Call.bOffense = true;
        Call.PlayCategory = TEXT("ShortPass");
        Bus->PublishPlayCall(Call);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The decision log and the overlay's text
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSAIDecisionLogTest,
    "PlaySports.AI.Debug.DecisionLog",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSAIDecisionLogTest::RunTest(const FString& Parameters)
{
    using namespace PSAIDebugTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSAIDecisionLog* Log = UPSAIDecisionLog::Get(World);
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    APSPlayerPawn* QB = World ? SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f)) : nullptr;
    APSPlayerPawn* WR = World ? SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(800.f, 1200.f, 100.f)) : nullptr;
    APSPlayerPawn* CB = World ? SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB"), FVector(900.f, 2000.f, 100.f)) : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Decision log"), Log) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall)
        || !TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("WR"), WR) || !TestNotNull(TEXT("CB"), CB))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    Log->SetTuning(FPSAIDebugTuning());
    GiveBall(World, QB);
    UPSSkillPlayerAIComponent* Passer = OffenseOf(QB)->GetSkillAI();
    UPSDefenderAIComponent* Corner = DefenseOf(CB)->GetDefenderAI();

    // Off by default: nothing is recorded.
    TestFalse(TEXT("The log is off by default"), Log->IsLogging());
    StartPassPlay(World, Bus, CB, WR);
    Corner->TickAI(0.1f);
    TestEqual(TEXT("...and records nothing"), Log->GetPlayRecords().Num(), 0);

    // On: the next play's decisions are recorded.
    Log->SetLogging(true);
    const int32 FirstPlay = Log->GetPlayIndex();
    StartPassPlay(World, Bus, CB, WR);
    TestEqual(TEXT("A snap starts a new play"), Log->GetPlayIndex(), FirstPlay + 1);
    Corner->TickAI(0.1f);
    Passer->TickAI(0.7f);
    Corner->TickAI(0.1f);

    FPSAIDecisionRecord Throw;
    if (TestTrue(TEXT("The quarterback's decision is recorded"), Log->GetLatest(TEXT("QB"), Throw)))
    {
        TestEqual(TEXT("...by the skill AI"), Throw.System, FString(TEXT("SkillAI")));
        TestEqual(TEXT("...a throw"), Throw.Action, FString(TEXT("Throw")));
        TestEqual(TEXT("...to the open receiver"), Throw.Target, FString(TEXT("WR")));
        TestTrue(TEXT("...because he was open"), Throw.Reason.StartsWith(TEXT("Open: WR")));
        TestEqual(TEXT("...in this play"), Throw.PlayIndex, Log->GetPlayIndex());
        const FPSAIDecisionOption* Read = Throw.Options.FindByPredicate([](const FPSAIDecisionOption& Option) { return Option.Option == TEXT("WR"); });
        if (TestNotNull(TEXT("...with the receiver he read among the options"), Read))
        {
            TestTrue(TEXT("...chosen"), Read->bChosen);
            TestTrue(TEXT("...weighed by his separation"), FMath::IsNearlyEqual(Read->Score, FVector::Dist2D(WR->GetActorLocation(), CB->GetActorLocation()), 1.f));
        }
    }
    FPSAIDecisionRecord Cover;
    if (TestTrue(TEXT("The corner's decision is recorded"), Log->GetLatest(TEXT("CB"), Cover)))
    {
        TestEqual(TEXT("...by the defender AI"), Cover.System, FString(TEXT("DefenderAI")));
        TestEqual(TEXT("...with his assignment"), Cover.Assignment, FString(TEXT("ManCoverage")));
        TestEqual(TEXT("...at his man"), Cover.Target, FString(TEXT("WR")));
        const FString Overlay = UPSAIDecisionLog::DescribeForOverlay(Cover);
        TestTrue(TEXT("The overlay says who, his assignment, what and at whom"), Overlay.StartsWith(TEXT("CB [ManCoverage] ")) && Overlay.Contains(TEXT("-> WR"))
            && Overlay.Contains(Cover.Action) && Overlay.Contains(Cover.Reason));
    }
    TestEqual(TEXT("Each decision tick is in the corner's stream"), Log->GetAgentStream(TEXT("CB")).Num(), 2);

    // A rusher's moves.
    APSPlayerPawn* DL = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL"), FVector(0.f, -3000.f, 100.f), 80.f);
    APSPlayerPawn* OL = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL"), FVector(-100.f, -3000.f, 100.f));
    if (TestNotNull(TEXT("DL"), DL) && TestNotNull(TEXT("OL"), OL))
    {
        DL->bIsEngaged = true;
        DL->EngagedOpponent = OL;
        OL->bIsEngaged = true;
        OL->EngagedOpponent = DL;
        const EPSRushMove Move = DefenseOf(DL)->GetRushMoves()->ChooseMove();
        FPSAIDecisionRecord Rush;
        if (TestTrue(TEXT("The rusher's move choice is recorded"), Log->GetLatest(TEXT("DL"), Rush)))
        {
            TestEqual(TEXT("...by the rush plan"), Rush.System, FString(TEXT("RushMove")));
            TestEqual(TEXT("...the move he picked"), Rush.Action, StaticEnum<EPSRushMove>()->GetNameStringByValue(static_cast<int64>(Move)));
            TestEqual(TEXT("...against his blocker"), Rush.Target, FString(TEXT("OL")));
            TestTrue(TEXT("...with every move he weighed"), Rush.Options.Num() >= 3);
            TestEqual(TEXT("...one of them chosen"), Rush.Options.FilterByPredicate([](const FPSAIDecisionOption& Option) { return Option.bChosen; }).Num(), 1);
        }
    }

    // The CPU's play calls belong to the play they are snapped into.
    FPSSituationContext Situation;
    Situation.Down = 2;
    Situation.Distance = 6;
    PlayCall->OpenPlayCall(Situation);
    PlayCall->PollReadyToSnap(0.f);
    const int32 PlayBeforeSnap = Log->GetPlayIndex();
    FPSTelemetrySnapEvent Snap;
    Snap.Down = 2;
    Snap.Distance = 6;
    Bus->PublishSnap(Snap);
    FPSAIDecisionRecord Called;
    if (TestTrue(TEXT("Snapped, the CPU defense's call is the new play's"), Log->GetLatest(TEXT("CPUDefense"), Called)))
    {
        TestEqual(TEXT("...the play it called"), Called.Action, PlayCall->GetCall(false).PlayId.ToString());
        TestEqual(TEXT("...in this play"), Called.PlayIndex, PlayBeforeSnap + 1);
        TestEqual(TEXT("...once: the call made in the window, held for its snap"), Log->GetAgentStream(TEXT("CPUDefense")).Num(), 1);
        TestTrue(TEXT("...weighing the playbook's defensive calls"), Called.Options.Num() > 1);
        TestEqual(TEXT("...one of them chosen"), Called.Options.FilterByPredicate([](const FPSAIDecisionOption& Option) { return Option.bChosen; }).Num(), 1);
        TestFalse(TEXT("...with its reasons"), Called.Reason.IsEmpty());
    }
    TestFalse(TEXT("The last play's decisions are gone"), Log->GetLatest(TEXT("CB"), Cover));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The play post-mortem
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSAIPostMortemTest,
    "PlaySports.AI.Debug.PostMortem",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSAIPostMortemTest::RunTest(const FString& Parameters)
{
    using namespace PSAIDebugTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSAIDecisionLog* Log = UPSAIDecisionLog::Get(World);
    APSPlayerPawn* QB = World ? SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f)) : nullptr;
    APSPlayerPawn* WR = World ? SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(800.f, 1200.f, 100.f)) : nullptr;
    APSPlayerPawn* CB = World ? SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB"), FVector(900.f, 1500.f, 100.f)) : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Decision log"), Log) || !TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("WR"), WR) || !TestNotNull(TEXT("CB"), CB))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    FPSAIDebugTuning Tuning;
    Tuning.PostMortemDirectory = TEXT("Automation/PSAIPostMortems");
    Tuning.MaxPostMortemFiles = 2;
    Log->SetTuning(Tuning);
    const FString Directory = Log->GetPostMortemDirectory();
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    Log->SetWritePostMortems(true);
    TestTrue(TEXT("Writing post-mortems turns the log on"), Log->IsLogging());

    // A play: the snap, the corner and the quarterback decide, and the play ends.
    StartPassPlay(World, Bus, CB, WR);
    OffenseOf(QB)->GetSkillAI()->TickAI(0.1f);
    DefenseOf(CB)->GetDefenderAI()->TickAI(0.1f);
    DefenseOf(CB)->GetDefenderAI()->TickAI(0.1f);
    FPSTelemetryPhaseChangeEvent End;
    End.NewPhase = TEXT("Scoring");
    Bus->PublishPhaseChange(End);

    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(Directory / TEXT("Play_*.json")), true, false);
    if (TestEqual(TEXT("The play's end writes one post-mortem"), Files.Num(), 1))
    {
        FString Json;
        TSharedPtr<FJsonObject> Root;
        if (TestTrue(TEXT("...that reads back as JSON"), FFileHelper::LoadFileToString(Json, *(Directory / Files[0]))
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid()))
        {
            TestEqual(TEXT("...for this play"), static_cast<int32>(Root->GetNumberField(TEXT("PlayIndex"))), Log->GetPlayIndex());
            const int32 FirstSequence = static_cast<int32>(Root->GetNumberField(TEXT("SnapSequence")));
            TestTrue(TEXT("...linked to the bus from the snap on"), FirstSequence > 0 && Root->GetNumberField(TEXT("EndSequence")) >= FirstSequence);
            const TArray<TSharedPtr<FJsonValue>>& Events = Root->GetArrayField(TEXT("BusEvents"));
            TestTrue(TEXT("...starting with the snap"), Events.Num() > 0 && Events[0]->AsObject()->GetStringField(TEXT("EventType")) == TEXT("Snap")
                && static_cast<int32>(Events[0]->AsObject()->GetNumberField(TEXT("Sequence"))) == FirstSequence);
            TestTrue(TEXT("...with the calls"), Root->GetArrayField(TEXT("Calls")).Num() > 0);
            TestTrue(TEXT("...and the snap's situation"), Root->HasField(TEXT("Snap")));
            TMap<FString, int32> Streams;
            for (const TSharedPtr<FJsonValue>& Agent : Root->GetArrayField(TEXT("Agents")))
            {
                Streams.Add(Agent->AsObject()->GetStringField(TEXT("AgentId")), Agent->AsObject()->GetArrayField(TEXT("Decisions")).Num());
            }
            TestEqual(TEXT("...the quarterback's stream"), Streams.FindRef(TEXT("QB")), 1);
            TestEqual(TEXT("...and the corner's, every tick"), Streams.FindRef(TEXT("CB")), 2);
        }
    }

    // Three more plays, ended by the next snap: only the newest two files are kept.
    for (int32 Index = 0; Index < 3; ++Index)
    {
        StartPassPlay(World, Bus, CB, WR);
        DefenseOf(CB)->GetDefenderAI()->TickAI(0.1f);
    }
    Files.Reset();
    IFileManager::Get().FindFiles(Files, *(Directory / TEXT("Play_*.json")), true, false);
    TestEqual(TEXT("Post-mortems are kept to MaxPostMortemFiles"), Files.Num(), 2);

    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The scenario runner
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSAIScenarioGymTest,
    "PlaySports.Gym.AIScenarios",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSAIScenarioGymTest::RunTest(const FString& Parameters)
{
    UPSAIScenarioRunner* Runner = NewObject<UPSAIScenarioRunner>();
    if (!TestTrue(TEXT("The shipped scenarios load"), Runner->LoadScenariosFromJson(UPSAIScenarioRunner::GetDefaultScenariosPath())))
    {
        return false;
    }
    const TArray<FPSAIScenario>& Scenarios = Runner->GetScenarios();
    TestTrue(TEXT("There are scenarios to run"), Scenarios.Num() >= 5);
    for (const FPSAIScenario& Scenario : Scenarios)
    {
        for (const FString& Problem : UPSAIScenarioRunner::ValidateScenario(Scenario))
        {
            AddError(Problem);
        }
        const FPSAIScenarioResult Result = Runner->RunInNewWorld(Scenario);
        TestTrue(FString::Printf(TEXT("Scenario %s"), *Scenario.ScenarioId.ToString()), Result.bPassed);
        for (const FString& Failure : Result.Failures)
        {
            AddError(FString::Printf(TEXT("%s: %s"), *Scenario.ScenarioId.ToString(), *Failure));
        }
    }

    // The runner asserts: a wrong expectation fails, and says how.
    if (Scenarios.Num() > 0 && Scenarios[0].Expectations.Num() > 0)
    {
        FPSAIScenario Wrong = Scenarios[0];
        Wrong.Expectations[0].Action = TEXT("Kneel");
        const FPSAIScenarioResult Result = Runner->RunInNewWorld(Wrong);
        TestFalse(TEXT("A wrong expectation fails"), Result.bPassed);
        TestTrue(TEXT("...saying what was expected"), Result.Failures.Num() > 0 && Result.Failures[0].Contains(TEXT("expected Kneel")));
        TestTrue(TEXT("...and reporting what each player decided"), Result.Decisions.Num() > 0);
    }

    // A scenario that names a player it doesn't place is refused.
    FPSAIScenario Broken;
    Broken.ScenarioId = TEXT("Broken");
    FPSAIScenarioPlayer Corner;
    Corner.PlayerId = TEXT("CB");
    Corner.Role = EPlayerRole::DefensiveBack;
    Corner.Assignment = EPSDefensiveAssignmentType::ManCoverage;
    Corner.CoverTarget = TEXT("Nobody");
    Broken.Players.Add(Corner);
    FPSAIScenarioExpectation Expectation;
    Expectation.PlayerId = TEXT("Ghost");
    Expectation.Action = TEXT("Cover");
    Broken.Expectations.Add(Expectation);
    const FString Problems = FString::Join(UPSAIScenarioRunner::ValidateScenario(Broken), TEXT("\n"));
    TestTrue(TEXT("A cover target nobody plays is refused"), Problems.Contains(TEXT("covers 'Nobody'")));
    TestTrue(TEXT("...as is an expectation of nobody"), Problems.Contains(TEXT("'Ghost'")));
    TestFalse(TEXT("...and it isn't run"), Runner->RunInNewWorld(Broken).bPassed);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
