// PSGameIntelligenceTests.cpp -- Epic 82 (LLM game-intelligence hooks)
//
// No model is called: the tests stand in for the agent, polling and answering the requests as
// one would through AgenticLink's call_function, and stand in for the bridge by registering the
// modular feature AgenticLink registers while its MCP server serves.
//
// Tests covered:
//   1. Data/game_intelligence.json loads and validates; bad tunings are refused.
//   2. The game-state contract: the situation, personnel, tendencies and statistics from their
//      authorities, never over its budget (down to MinBudgetChars), trimmed least-needed first.
//   3. The bridge gate: with no bridge every hook is refused and the CPU calls at once.
//   4. Play-call consultation: a CPU side's call waits for an outside model's answer, which must
//      be one of its plays; the answer is run (or overruled by special teams); with no answer
//      the CPU calls its own at the timeout; a bound listener answers in process.
//   5. Post-game analysis: the drives from the game state, the key plays from Epic 42's
//      highlights, the summary and analysis requests at the final whistle, their answers kept;
//      the analysis also fits its budget.
//   6. Model-slot routing: each request names its task in tools/orchestrator/routing.json, the
//      play call's at least as capable as the summary's, and the tasks are data.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PSDataIngestion.h"
#include "PSFieldGrid.h"
#include "PSGameIntelligenceSubsystem.h"
#include "PSGameStateSerializer.h"
#include "PSHighlightSubsystem.h"
#include "PSOpponentModel.h"
#include "PSPersonnelManager.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayerPawn.h"
#include "PSRoster.h"
#include "PSStatsEngine.h"
#include "PSTelemetryBus.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSGameIntelligenceTests
{
    UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    void DestroyTestWorld(UWorld* World)
    {
        if (World)
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    }

    /** What AgenticLink registers while its MCP server serves. */
    struct FPSFakeAgentBridge : public IModularFeature
    {
    };

    /** The bridge online for as long as it lives. */
    struct FScopedBridgeOnline
    {
        FPSFakeAgentBridge Feature;

        FScopedBridgeOnline()
        {
            IModularFeatures::Get().RegisterModularFeature(UPSGameIntelligenceSubsystem::GetBridgeFeatureName(), &Feature);
        }

        ~FScopedBridgeOnline()
        {
            IModularFeatures::Get().UnregisterModularFeature(UPSGameIntelligenceSubsystem::GetBridgeFeatureName(), &Feature);
        }
    };

    /** The shipped roster (Data/sample_players.json) with its default depth chart. */
    UPSRoster* LoadSampleRoster()
    {
        UDataTable* Table = NewObject<UDataTable>();
        Table->RowStruct = FPlayerAttributes::StaticStruct();
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        if (!Ingestion->LoadPlayerAttributesFromJson(FPaths::ProjectDir() / TEXT("Data/sample_players.json"), Table))
        {
            return nullptr;
        }
        TArray<FPlayerAttributes*> Rows;
        Table->GetAllRows<FPlayerAttributes>(TEXT("PSGameIntelligenceTests"), Rows);
        TArray<FPlayerAttributes> Players;
        for (const FPlayerAttributes* Row : Rows)
        {
            if (Row)
            {
                Players.Add(*Row);
            }
        }
        UPSRoster* Roster = NewObject<UPSRoster>();
        Roster->InitializeRoster(Players);
        Roster->BuildDefaultDepthChart();
        return Roster;
    }

    FPSTelemetryGameStateEvent MakeState(const TCHAR* Phase, int32 Quarter, int32 YardLine, bool bHomeBall, int32 HomeScore, int32 AwayScore, int32 CompletedDrives)
    {
        FPSTelemetryGameStateEvent State;
        State.Phase = Phase;
        State.Quarter = Quarter;
        State.GameClockSeconds = 600.f;
        State.Down = 1;
        State.Distance = 10;
        State.YardLine = YardLine;
        State.YardLineToGain = YardLine + 10;
        State.bHomeHasPossession = bHomeBall;
        State.HomeScore = HomeScore;
        State.AwayScore = AwayScore;
        State.CompletedDrives = CompletedDrives;
        return State;
    }

    FPSSituationContext MakeSituation(int32 Down, int32 Distance, int32 YardLine)
    {
        FPSSituationContext Situation;
        Situation.Down = Down;
        Situation.Distance = Distance;
        Situation.YardLine = YardLine;
        Situation.Quarter = 2;
        Situation.GameClockSeconds = 600.f;
        Situation.bHomeHasPossession = true;
        return Situation;
    }

    /** The side's PlayCall request, most recent first; null when there is none. */
    const FPSIntelRequest* FindPlayCall(const UPSGameIntelligenceSubsystem& Intelligence, bool bOffense)
    {
        const TArray<FPSIntelRequest>& All = Intelligence.GetRequests();
        for (int32 Index = All.Num() - 1; Index >= 0; --Index)
        {
            if (All[Index].Kind == EPSIntelRequestKind::PlayCall && All[Index].bOffense == bOffense)
            {
                return &All[Index];
            }
        }
        return nullptr;
    }

    EPSIntelRequestState StateOf(const UPSGameIntelligenceSubsystem& Intelligence, int32 RequestId)
    {
        FPSIntelRequest Request;
        return Intelligence.FindRequest(RequestId, Request) ? Request.State : EPSIntelRequestState::Closed;
    }

    /** The first of Request's choices whose note starts with Category (e.g. "Run,"). */
    FName FindChoice(const FPSIntelRequest& Request, const TCHAR* Category)
    {
        for (const FPSIntelChoice& Choice : Request.Choices)
        {
            if (Choice.Note.StartsWith(Category))
            {
                return Choice.Id;
            }
        }
        return NAME_None;
    }

    TSharedPtr<FJsonObject> GetObject(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Field)
    {
        const TSharedPtr<FJsonObject>* Child = nullptr;
        return Parent.IsValid() && Parent->TryGetObjectField(Field, Child) && Child ? *Child : TSharedPtr<FJsonObject>();
    }

    int32 ArrayLength(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Field)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        return Parent.IsValid() && Parent->TryGetArrayField(Field, Values) && Values ? Values->Num() : -1;
    }

    FString StringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        FString Value;
        return Object.IsValid() && Object->TryGetStringField(Field, Value) ? Value : FString();
    }

    TArray<FString> TrimmedOf(const FString& Json)
    {
        TArray<FString> Names;
        const TSharedPtr<FJsonObject> Root = PSGameStateSerializer::ParseObject(Json);
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (Root.IsValid() && Root->TryGetArrayField(TEXT("trimmed"), Values) && Values)
        {
            for (const TSharedPtr<FJsonValue>& Value : *Values)
            {
                Names.Add(Value->AsString());
            }
        }
        return Names;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The tuning
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSGameIntelligenceTuningTest,
    "PlaySports.GameIntelligence.Tuning",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSGameIntelligenceTuningTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSGameIntelligenceTuning Loaded;
    if (!TestTrue(TEXT("Data/game_intelligence.json loads"), Ingestion->LoadGameIntelligenceTuningFromJson(UPSGameIntelligenceSubsystem::GetDefaultTuningPath(), Loaded)))
    {
        return false;
    }
    for (const FString& Problem : UPSGameIntelligenceSubsystem::ValidateTuning(Loaded))
    {
        AddError(Problem);
    }
    const FPSGameIntelligenceTuning Defaults;
    TestEqual(TEXT("The file's budget is the struct's default"), Loaded.ContextBudgetChars, Defaults.ContextBudgetChars);
    TestEqual(TEXT("...and its play-call timeout"), Loaded.PlayCallTimeoutSeconds, Defaults.PlayCallTimeoutSeconds);
    TestEqual(TEXT("...and its play-call task"), Loaded.PlayCallTask, Defaults.PlayCallTask);
    TestEqual(TEXT("...and its instructions"), Loaded.PlayCallInstructions, Defaults.PlayCallInstructions);

    FPSGameIntelligenceTuning Small = Loaded;
    Small.ContextBudgetChars = PSGameStateSerializer::MinBudgetChars - 1;
    TestEqual(TEXT("A budget under the contract's least is refused"), UPSGameIntelligenceSubsystem::ValidateTuning(Small).Num(), 1);
    FPSGameIntelligenceTuning NoTask = Loaded;
    NoTask.DriveSummaryTask.Reset();
    TestEqual(TEXT("A request without a task is refused"), UPSGameIntelligenceSubsystem::ValidateTuning(NoTask).Num(), 1);
    FPSGameIntelligenceTuning NoWait = Loaded;
    NoWait.PlayCallTimeoutSeconds = 0.f;
    TestEqual(TEXT("A play call that can't wait is refused"), UPSGameIntelligenceSubsystem::ValidateTuning(NoWait).Num(), 1);

    UWorld* World = PSGameIntelligenceTests::CreateTestWorld();
    UPSGameIntelligenceSubsystem* Intelligence = World ? World->GetSubsystem<UPSGameIntelligenceSubsystem>() : nullptr;
    if (TestNotNull(TEXT("A game world has the subsystem"), Intelligence))
    {
        TestFalse(TEXT("The subsystem refuses a bad tuning"), Intelligence->SetTuning(Small));
        TestEqual(TEXT("...keeping the shipped one"), Intelligence->GetTuning().ContextBudgetChars, Loaded.ContextBudgetChars);
    }
    PSGameIntelligenceTests::DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The game-state contract and its size bound
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSGameStateContractTest,
    "PlaySports.GameIntelligence.StateContractSizeBound",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSGameStateContractTest::RunTest(const FString& Parameters)
{
    using namespace PSGameIntelligenceTests;

    UWorld* World = CreateTestWorld();
    UPSOpponentModel* Model = World ? World->GetSubsystem<UPSOpponentModel>() : nullptr;
    UPSRoster* Roster = LoadSampleRoster();
    if (!TestNotNull(TEXT("World"), World) || !TestNotNull(TEXT("Opponent model"), Model) || !TestNotNull(TEXT("Roster"), Roster))
    {
        DestroyTestWorld(World);
        return false;
    }

    // Personnel: the shipped roster's starting packages on the field.
    UPSPersonnelManager* Manager = NewObject<UPSPersonnelManager>();
    Manager->Initialize(Roster);
    Manager->LoadCatalogFromJson(UPSPersonnelManager::GetDefaultCatalogPath());
    const float ScrimmageX = 2000.f;
    Manager->BindPawns(APSFieldGrid::SpawnPlayersFromRoster(Manager->GetStartingLineup(), ScrimmageX, World));
    Manager->BeginNewPlay(ScrimmageX, 1);
    const FName OffensePackage = Manager->GetCurrentPackage(true);

    // Tendencies: the human's earlier games, run-heavy on 1st and long.
    const int32 Bucket = PSOpponentModel::GetDistanceBucket(10, Model->GetTuning());
    TArray<FPSTendencyCell> Cells;
    const TPair<const TCHAR*, int32> Calls[] = { { TEXT("Run"), 30 }, { TEXT("ShortPass"), 12 }, { TEXT("DeepPass"), 6 }, { TEXT("PlayAction"), 4 } };
    for (const TPair<const TCHAR*, int32>& Called : Calls)
    {
        FPSTendencyCell& Cell = Cells.AddDefaulted_GetRef();
        Cell.bOffense = true;
        Cell.Down = 1;
        Cell.DistanceBucket = Bucket;
        Cell.Personnel = OffensePackage;
        Cell.Category = Called.Key;
        Cell.Count = Called.Value;
    }
    Model->SetCareerCells(Cells);

    // Statistics: a busy game, many players with numbers.
    UPSStatsEngine* GameStats = NewObject<UPSStatsEngine>();
    GameStats->BeginGame(3, TEXT("HOME_TEAM"), TEXT("AWAY_TEAM"));
    for (int32 Play = 0; Play < 160; ++Play)
    {
        FPSTelemetryPlayResultEvent Event;
        Event.PlayNumber = Play + 1;
        Event.bHomeOffense = Play % 2 == 0;
        Event.Result = TEXT("Tackle");
        Event.YardsGained = 3 + Play % 17;
        Event.bPass = Play % 3 != 0;
        Event.bComplete = Event.bPass;
        Event.PasserId = FName(*FString::Printf(TEXT("QB_%02d"), Play % 20));
        Event.ReceiverId = Event.bPass ? FName(*FString::Printf(TEXT("WR_%02d"), Play % 40)) : NAME_None;
        Event.RusherId = Event.bPass ? NAME_None : FName(*FString::Printf(TEXT("RB_%02d"), (Play / 3) % 30));
        Event.TacklerId = FName(*FString::Printf(TEXT("LB_%02d"), Play % 50));
        GameStats->RecordPlay(Event);
    }

    FPSGameStateSources Sources;
    Sources.State = MakeState(TEXT("PreSnap"), 2, 35, true, 14, 10, 5);
    Sources.State.Down = 1;
    Sources.State.Distance = 10;
    Sources.State.LastDrivePlays = 8;
    Sources.State.LastDriveYards = 75;
    Sources.State.LastDriveResult = TEXT("Touchdown");
    Sources.bHumanOffense = true;
    Sources.Personnel = Manager;
    Sources.Opponent = Model;
    Sources.Stats = GameStats;
    Sources.LeadersPerCategory = 100;

    // Everything, with room to spare.
    const FString Full = PSGameStateSerializer::Serialize(Sources, 1000000);
    AddInfo(FString::Printf(TEXT("The full game state is %d characters."), Full.Len()));
    const TSharedPtr<FJsonObject> Root = PSGameStateSerializer::ParseObject(Full);
    if (!TestTrue(TEXT("The game state is a JSON object"), Root.IsValid()))
    {
        DestroyTestWorld(World);
        return false;
    }
    TestEqual(TEXT("...under the contract's name"), StringField(Root, TEXT("contract")), FString(TEXT("play-sports.game-state/1")));
    const TSharedPtr<FJsonObject> Situation = GetObject(Root, TEXT("situation"));
    int32 Number = 0;
    TestTrue(TEXT("The situation is the simulation's: 2nd quarter"), Situation.IsValid() && Situation->TryGetNumberField(TEXT("quarter"), Number) && Number == 2);
    TestTrue(TEXT("...65 yards to go to the goal"), Situation.IsValid() && Situation->TryGetNumberField(TEXT("yardsToGoal"), Number) && Number == 65);
    TestTrue(TEXT("...the offense up by 4"), Situation.IsValid() && Situation->TryGetNumberField(TEXT("offenseMargin"), Number) && Number == 4);
    TestEqual(TEXT("...the clock as a broadcast shows it"), StringField(Situation, TEXT("clock")), FString(TEXT("10:00")));
    TestEqual(TEXT("...and the last drive's result"), StringField(GetObject(Situation, TEXT("lastDrive")), TEXT("result")), FString(TEXT("Touchdown")));
    TestEqual(TEXT("The human's tendency on offense is read"), ArrayLength(Root, TEXT("tendencies")), 1);
    TestTrue(TEXT("...as the opponent model reads it: mostly runs"), Full.Contains(TEXT("\"top\":\"Run\"")) && Full.Contains(TEXT("\"pct\":{\"Run\":")));
    const TSharedPtr<FJsonObject> Offense = GetObject(GetObject(Root, TEXT("personnel")), TEXT("offense"));
    TestEqual(TEXT("The personnel is the manager's package"), StringField(Offense, TEXT("package")), OffensePackage.ToString());
    TestEqual(TEXT("...with its eleven on the field"), ArrayLength(Offense, TEXT("players")), 11);
    TestEqual(TEXT("The statistics are the stats engine's"), StringField(GetObject(GetObject(Root, TEXT("stats")), TEXT("home")), TEXT("team")), FString(TEXT("HOME_TEAM")));
    TestTrue(TEXT("...with the game's leaders"), ArrayLength(GetObject(Root, TEXT("stats")), TEXT("leaders")) > 100);
    TestFalse(TEXT("Nothing is trimmed with room to spare"), Root->HasField(TEXT("trimmed")));
    TestTrue(TEXT("The full state is bigger than the shipped budget (so the bound is exercised)"), Full.Len() > FPSGameIntelligenceTuning().ContextBudgetChars);

    // Just under the full size: the leaders go first, nothing else.
    TArray<FString> Trimmed;
    const FString JustUnder = PSGameStateSerializer::Serialize(Sources, Full.Len() - 1, &Trimmed);
    TestTrue(TEXT("Just under the full size it fits"), JustUnder.Len() <= Full.Len() - 1);
    TestTrue(TEXT("...by leaving the leaders out first"), Trimmed.Num() == 1 && Trimmed[0] == TEXT("stats.leaders"));
    TestTrue(TEXT("...and says so in the state"), TrimmedOf(JustUnder) == Trimmed);

    // Every budget from the least up is met, and the situation always stays.
    for (const int32 Budget : { PSGameStateSerializer::MinBudgetChars, 1100, 1300, 1600, 2000, 3000, 4500, 6000, 9000 })
    {
        const FString Bounded = PSGameStateSerializer::Serialize(Sources, Budget);
        const TSharedPtr<FJsonObject> BoundedRoot = PSGameStateSerializer::ParseObject(Bounded);
        TestTrue(*FString::Printf(TEXT("At %d characters the state fits (%d)"), Budget, Bounded.Len()), Bounded.Len() <= Budget);
        TestTrue(*FString::Printf(TEXT("At %d characters it is still JSON with the situation"), Budget), GetObject(BoundedRoot, TEXT("situation")).IsValid());
    }
    const TArray<FString> AtLeast = TrimmedOf(PSGameStateSerializer::Serialize(Sources, PSGameStateSerializer::MinBudgetChars));
    TestTrue(TEXT("At the least budget the leaders and then the players on the field went first"),
        AtLeast.Num() >= 2 && AtLeast[0] == TEXT("stats.leaders") && AtLeast[1] == TEXT("personnel.players"));

    // Under any budget at all, detail goes in the contract's order and the situation stays.
    TArray<FString> AllTrimmed;
    const FString Starved = PSGameStateSerializer::Serialize(Sources, 200, &AllTrimmed);
    const TArray<FString> Order = { TEXT("stats.leaders"), TEXT("personnel.players"), TEXT("tendencies.pct"), TEXT("stats"), TEXT("personnel"), TEXT("tendencies") };
    TestTrue(TEXT("Starved, every section goes, in the contract's order"), AllTrimmed == Order);
    TestTrue(TEXT("...and the situation stays"), GetObject(PSGameStateSerializer::ParseObject(Starved), TEXT("situation")).IsValid());

    // A missing source leaves its section out.
    FPSGameStateSources Bare;
    Bare.State = Sources.State;
    const TSharedPtr<FJsonObject> BareRoot = PSGameStateSerializer::ParseObject(PSGameStateSerializer::Serialize(Bare, 6000));
    TestTrue(TEXT("With only the game state, only the situation is told"), BareRoot.IsValid() && BareRoot->HasField(TEXT("situation"))
        && !BareRoot->HasField(TEXT("personnel")) && !BareRoot->HasField(TEXT("stats")) && !BareRoot->HasField(TEXT("tendencies")));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The bridge gate
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSGameIntelligenceBridgeGateTest,
    "PlaySports.GameIntelligence.BridgeGate",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSGameIntelligenceBridgeGateTest::RunTest(const FString& Parameters)
{
    using namespace PSGameIntelligenceTests;

    if (UPSGameIntelligenceSubsystem::IsBridgeOnline())
    {
        AddInfo(TEXT("The agent bridge is serving in this session; the offline gate can't be checked."));
        return true;
    }
    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSGameIntelligenceSubsystem* Intelligence = World ? World->GetSubsystem<UPSGameIntelligenceSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play calls"), PlayCall) || !TestNotNull(TEXT("Game intelligence"), Intelligence))
    {
        DestroyTestWorld(World);
        return false;
    }

    TestFalse(TEXT("Consultation is refused with no bridge"), Intelligence->SetConsultation(true, true));
    TestFalse(TEXT("...so no side is consulted"), Intelligence->IsConsulting(true) || Intelligence->IsConsulting(false));
    TestEqual(TEXT("No play-call request opens"), Intelligence->OpenPlayConsultation(true, MakeSituation(1, 10, 30), PlayCall->GetPlays(true)), 0);

    // The CPU calls both sides on the first poll, as it always has.
    PlayCall->OpenPlayCall(MakeSituation(1, 10, 30));
    PlayCall->PollReadyToSnap(0.f);
    TestTrue(TEXT("The CPU calls both sides at once"), PlayCall->GetCall(true).IsSet() && PlayCall->GetCall(false).IsSet());
    TestEqual(TEXT("...and nothing was asked"), Intelligence->GetRequests().Num(), 0);
    TestEqual(TEXT("The coaching AI's provider suggests nothing"), Intelligence->GetAnsweredPlay(true), FName());

    const TSharedPtr<FJsonObject> Pending = PSGameStateSerializer::ParseObject(Intelligence->GetPendingRequestsJson());
    bool bBridge = true;
    TestTrue(TEXT("An agent polling hears the bridge is offline"), Pending.IsValid() && Pending->TryGetBoolField(TEXT("bridge"), bBridge) && !bBridge);
    TestEqual(TEXT("...with nothing to answer"), ArrayLength(Pending, TEXT("requests")), 0);
    FString Reason;
    TestFalse(TEXT("An answer is refused"), Intelligence->AnswerRequest(1, TEXT("Offense_InsideZone"), Reason));
    TestTrue(TEXT("...saying why"), Reason.Contains(TEXT("offline")));

    // The final whistle: the analysis is built, but nothing is asked of a model.
    Bus->PublishGameState(MakeState(TEXT("PreSnap"), 4, 40, true, 3, 0, 0));
    Bus->PublishGameState(MakeState(TEXT("PreSnap"), 5, 40, true, 3, 0, 0));
    TestTrue(TEXT("The game is over"), Intelligence->IsGameOver());
    TestTrue(TEXT("...its analysis is built"), Intelligence->GetPostGameAnalysis().bFinal && Intelligence->GetPostGameAnalysis().HomeScore == 3);
    TestEqual(TEXT("...and no request opened"), Intelligence->GetRequests().Num(), 0);
    TestTrue(TEXT("The game state is still there to read"), Intelligence->GetGameStateJson().Contains(TEXT("play-sports.game-state/1")));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Play-call consultation
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayCallConsultationTest,
    "PlaySports.GameIntelligence.PlayCallConsultation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayCallConsultationTest::RunTest(const FString& Parameters)
{
    using namespace PSGameIntelligenceTests;

    FScopedBridgeOnline Bridge;
    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSGameIntelligenceSubsystem* Intelligence = World ? World->GetSubsystem<UPSGameIntelligenceSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play calls"), PlayCall) || !TestNotNull(TEXT("Game intelligence"), Intelligence))
    {
        DestroyTestWorld(World);
        return false;
    }
    TestTrue(TEXT("The bridge is online"), UPSGameIntelligenceSubsystem::IsBridgeOnline());
    TestTrue(TEXT("An agent takes both CPU sides"), Intelligence->SetConsultation(true, true));
    const float WaitLimit = Intelligence->GetTuning().PlayCallTimeoutSeconds;
    Bus->PublishGameState(MakeState(TEXT("PreSnap"), 2, 30, true, 7, 3, 2));

    // The window opens: each CPU side asks, with its plays as the only answers.
    PlayCall->OpenPlayCall(MakeSituation(1, 10, 30));
    const FPSIntelRequest* OffensePtr = FindPlayCall(*Intelligence, true);
    const FPSIntelRequest* DefensePtr = FindPlayCall(*Intelligence, false);
    if (!TestNotNull(TEXT("The offense asks"), OffensePtr) || !TestNotNull(TEXT("The defense asks"), DefensePtr))
    {
        DestroyTestWorld(World);
        return false;
    }
    const FPSIntelRequest OffenseAsk = *OffensePtr;
    const FPSIntelRequest DefenseAsk = *DefensePtr;
    const int32 OffenseId = OffenseAsk.RequestId;
    const int32 DefenseId = DefenseAsk.RequestId;
    TestEqual(TEXT("A play call is a strategy task"), OffenseAsk.Task, FString(TEXT("strategy")));
    TestEqual(TEXT("...offering the offense's plays this down"), OffenseAsk.Choices.Num(), PlayCall->GetPlays(true).Num());
    TestEqual(TEXT("...and the defense's"), DefenseAsk.Choices.Num(), PlayCall->GetPlays(false).Num());
    TestTrue(TEXT("...with the game state as context"), OffenseAsk.Context.Contains(TEXT("\"quarter\":2")));
    const FName RunPlay = FindChoice(OffenseAsk, TEXT("Run,"));
    const FName BaseDefense = FindChoice(DefenseAsk, TEXT("Base,"));
    TestFalse(TEXT("A run is among the choices"), RunPlay.IsNone());

    // The calls wait for the answers.
    PlayCall->PollReadyToSnap(0.5f);
    TestFalse(TEXT("The offense waits for its answer"), PlayCall->GetCall(true).IsSet());
    TestFalse(TEXT("...and so does the defense"), PlayCall->GetCall(false).IsSet());

    // The agent polls: two requests, with their context as JSON.
    const TSharedPtr<FJsonObject> Pending = PSGameStateSerializer::ParseObject(Intelligence->GetPendingRequestsJson());
    TestEqual(TEXT("The agent sees both requests"), ArrayLength(Pending, TEXT("requests")), 2);
    TestTrue(TEXT("...each with the game-state contract"), Intelligence->GetPendingRequestsJson().Contains(TEXT("\"context\":{\"contract\":\"play-sports.game-state/1\"")));

    // A play that isn't offered is refused; the request stays open.
    FString Reason;
    TestFalse(TEXT("A play not offered is refused"), Intelligence->AnswerRequest(OffenseId, TEXT("Offense_NotAPlay"), Reason));
    TestTrue(TEXT("...saying it isn't one of the choices"), Reason.Contains(TEXT("not one of")));
    TestTrue(TEXT("...and the request stays open"), StateOf(*Intelligence, OffenseId) == EPSIntelRequestState::Open);
    TestFalse(TEXT("An empty answer is refused"), Intelligence->AnswerRequest(OffenseId, TEXT("   "), Reason));
    const FString TooLong = FString::ChrN(Intelligence->GetTuning().MaxAnswerChars + 1, TEXT('x'));
    TestFalse(TEXT("An answer over the limit is refused"), Intelligence->AnswerRequest(OffenseId, TooLong, Reason));

    // The offense's answer, in any case, is taken and run.
    TestTrue(TEXT("The run is taken"), Intelligence->AnswerRequest(OffenseId, RunPlay.ToString().ToLower(), Reason));
    TestTrue(TEXT("...as answered"), StateOf(*Intelligence, OffenseId) == EPSIntelRequestState::Answered);
    TestEqual(TEXT("...by its id"), Intelligence->GetAnsweredPlay(true), RunPlay);
    PlayCall->PollReadyToSnap(0.5f);
    TestEqual(TEXT("The offense runs the answered play"), PlayCall->GetCall(true).PlayId, RunPlay);
    TestTrue(TEXT("...as the CPU's call"), PlayCall->GetCall(true).Caller == EPSPlayCaller::CPU);
    TestFalse(TEXT("The defense still waits"), PlayCall->GetCall(false).IsSet());

    // No answer for the defense: at the timeout the CPU calls its own.
    PlayCall->PollReadyToSnap(WaitLimit);
    TestTrue(TEXT("At the timeout the defense calls its own"), PlayCall->GetCall(false).IsSet());
    TestTrue(TEXT("...and its request timed out"), StateOf(*Intelligence, DefenseId) == EPSIntelRequestState::TimedOut);
    TestFalse(TEXT("A late answer is refused"), Intelligence->AnswerRequest(DefenseId, BaseDefense.ToString(), Reason));
    TestTrue(TEXT("...the request being no longer open"), Reason.Contains(TEXT("no longer open")));

    // The snap settles them.
    Bus->PublishSnap(FPSTelemetrySnapEvent());
    TestTrue(TEXT("At the snap the offense's answer was used"), StateOf(*Intelligence, OffenseId) == EPSIntelRequestState::Used);
    TestTrue(TEXT("...and the defense's stays timed out"), StateOf(*Intelligence, DefenseId) == EPSIntelRequestState::TimedOut);
    TestEqual(TEXT("...and no side has an answered play left"), Intelligence->GetAnsweredPlay(true), FName());

    // 4th and long deep in its own end: special teams call the punt outright, over the answer.
    TestTrue(TEXT("The agent keeps only the offense"), Intelligence->SetConsultation(true, false));
    PlayCall->OpenPlayCall(MakeSituation(4, 10, 20));
    const FPSIntelRequest* DefenseNow = FindPlayCall(*Intelligence, false);
    TestTrue(TEXT("The defense no longer asks"), !DefenseNow || DefenseNow->RequestId == DefenseId);
    const FPSIntelRequest* FourthDown = FindPlayCall(*Intelligence, true);
    const int32 FourthDownId = FourthDown ? FourthDown->RequestId : 0;
    TestTrue(TEXT("The offense asks again"), FourthDownId > OffenseId);
    TestTrue(TEXT("The run is taken on 4th down"), Intelligence->AnswerRequest(FourthDownId, RunPlay.ToString(), Reason));
    PlayCall->PollReadyToSnap(0.1f);
    TestTrue(TEXT("The offense calls a kick instead"), PlayCall->GetCall(true).IsSet() && PlayCall->GetCall(true).PlayId != RunPlay);
    Bus->PublishSnap(FPSTelemetrySnapEvent());
    TestTrue(TEXT("...so the answer was overruled"), StateOf(*Intelligence, FourthDownId) == EPSIntelRequestState::Overruled);

    // A bound listener answers in process, as the request opens: the call doesn't wait at all.
    FName Heard;
    const FDelegateHandle Listening = Intelligence->OnRequestOpenedMC.AddLambda([Intelligence, &Heard](const FPSIntelRequest& Opened)
    {
        const FName Pick = FindChoice(Opened, TEXT("ShortPass,"));
        FString Why;
        if (Opened.Kind == EPSIntelRequestKind::PlayCall && !Pick.IsNone() && Intelligence->AnswerRequest(Opened.RequestId, Pick.ToString(), Why))
        {
            Heard = Pick;
        }
    });
    PlayCall->OpenPlayCall(MakeSituation(2, 6, 45));
    PlayCall->PollReadyToSnap(0.f);
    Intelligence->OnRequestOpenedMC.Remove(Listening);
    TestFalse(TEXT("The listener answered"), Heard.IsNone());
    TestEqual(TEXT("...and the offense runs its play on the first poll"), PlayCall->GetCall(true).PlayId, Heard);
    Bus->PublishSnap(FPSTelemetrySnapEvent());

    // The agent lets go: the CPU calls at once again.
    TestTrue(TEXT("The agent lets go of the offense"), Intelligence->SetConsultation(false, false));
    const int32 Before = Intelligence->GetRequests().Num();
    PlayCall->OpenPlayCall(MakeSituation(1, 10, 50));
    PlayCall->PollReadyToSnap(0.f);
    TestTrue(TEXT("...so both sides call on the first poll"), PlayCall->GetCall(true).IsSet() && PlayCall->GetCall(false).IsSet());
    TestEqual(TEXT("...with nothing asked"), Intelligence->GetRequests().Num(), Before);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Post-game analysis
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPostGameAnalysisTest,
    "PlaySports.GameIntelligence.PostGameAnalysis",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPostGameAnalysisTest::RunTest(const FString& Parameters)
{
    using namespace PSGameIntelligenceTests;

    FScopedBridgeOnline Bridge;
    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSHighlightSubsystem* Highlights = World ? World->GetSubsystem<UPSHighlightSubsystem>() : nullptr;
    UPSGameIntelligenceSubsystem* Intelligence = World ? World->GetSubsystem<UPSGameIntelligenceSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Highlights"), Highlights) || !TestNotNull(TEXT("Game intelligence"), Intelligence))
    {
        DestroyTestWorld(World);
        return false;
    }

    auto Snap = [Bus]() { Bus->PublishSnap(FPSTelemetrySnapEvent()); };
    auto Whistle = [Bus]()
    {
        FPSTelemetryPhaseChangeEvent Phase;
        Phase.OldPhase = TEXT("BallCarrierMovement");
        Phase.NewPhase = TEXT("Scoring");
        Bus->PublishPhaseChange(Phase);
    };
    auto Catch = [Bus](int32 Yards, bool bInterception)
    {
        FPSTelemetryCatchEvent Event;
        Event.ReceiverName = bInterception ? TEXT("DB_01") : TEXT("WR_01");
        Event.YardsGained = Yards;
        Event.bIsInterception = bInterception;
        Bus->PublishCatch(Event);
    };
    auto Tackle = [Bus](int32 Yards)
    {
        FPSTelemetryTackleEvent Event;
        Event.YardsGained = Yards;
        Bus->PublishTackle(Event);
    };

    // Home's opening drive: a 45-yard catch and run, then a touchdown pass.
    Bus->PublishGameState(MakeState(TEXT("PreSnap"), 1, 20, true, 0, 0, 0));
    Snap();
    Catch(12, false);
    Tackle(45);
    Whistle();
    Bus->PublishGameState(MakeState(TEXT("PreSnap"), 1, 65, true, 0, 0, 0));
    Snap();
    Catch(35, false);
    Whistle();
    Bus->PublishGameState(MakeState(TEXT("Scoring"), 1, 65, true, 0, 0, 0));
    FPSTelemetryGameStateEvent Scored = MakeState(TEXT("PreSnap"), 1, 35, true, 7, 0, 1);
    Scored.bKickoff = true;
    Scored.LastDrivePlays = 2;
    Scored.LastDriveYards = 80;
    Scored.LastDriveResult = TEXT("Touchdown");
    Bus->PublishGameState(Scored);

    // Away's drive ends in an interception.
    Bus->PublishGameState(MakeState(TEXT("PreSnap"), 2, 30, false, 7, 0, 1));
    Snap();
    Catch(0, true);
    Whistle();
    FPSTelemetryGameStateEvent Picked = MakeState(TEXT("PreSnap"), 2, 60, true, 7, 0, 2);
    Picked.LastDrivePlays = 1;
    Picked.LastDriveYards = 0;
    Picked.LastDriveResult = TEXT("Interception");
    Bus->PublishGameState(Picked);

    TestFalse(TEXT("No analysis is asked for before the end"), Intelligence->IsGameOver() || Intelligence->GetRequests().Num() > 0);
    TestEqual(TEXT("Two drives so far"), Intelligence->GetDrives().Num(), 2);

    // The final whistle.
    Bus->PublishGameState(MakeState(TEXT("PreSnap"), 5, 60, true, 7, 0, 2));
    const FPSGameAnalysis& Final = Intelligence->GetPostGameAnalysis();
    TestTrue(TEXT("The game is over and analysed"), Intelligence->IsGameOver() && Final.bFinal);
    TestTrue(TEXT("...7-0"), Final.HomeScore == 7 && Final.AwayScore == 0);
    if (TestEqual(TEXT("...with both drives"), Final.Drives.Num(), 2))
    {
        TestTrue(TEXT("Home's touchdown drive"), Final.Drives[0].bHomeTeam && Final.Drives[0].Result == TEXT("Touchdown") && Final.Drives[0].Yards == 80);
        TestTrue(TEXT("Away's interception"), !Final.Drives[1].bHomeTeam && Final.Drives[1].Result == TEXT("Interception") && Final.Drives[1].Quarter == 2);
    }
    TestEqual(TEXT("The key plays are the highlights' reel"), Final.KeyPlays.Num(), Highlights->GetReel().Num());
    if (TestEqual(TEXT("...three of them"), Final.KeyPlays.Num(), 3))
    {
        TestTrue(TEXT("...the most important first"), Final.KeyPlays[0].Importance >= Final.KeyPlays[1].Importance && Final.KeyPlays[1].Importance >= Final.KeyPlays[2].Importance);
        TestTrue(TEXT("...the touchdown among them"), Final.KeyPlays.ContainsByPredicate([](const FPSKeyPlay& Key) { return Key.Kind == EPSHighlightKind::Score && Key.Points == 7; }));
        TestTrue(TEXT("...and the interception among them"), Final.KeyPlays.ContainsByPredicate([](const FPSKeyPlay& Key) { return Key.bTurnover && !Key.bHomeOffense; }));
    }

    // Two requests: the drive summary and the analysis, each its own router task.
    FPSIntelRequest Summary;
    FPSIntelRequest Explained;
    TestTrue(TEXT("A drive summary is asked for"), Intelligence->FindRequest(Final.DriveSummaryRequestId, Summary) && Summary.Kind == EPSIntelRequestKind::DriveSummary);
    TestTrue(TEXT("...and a game analysis"), Intelligence->FindRequest(Final.GameAnalysisRequestId, Explained) && Explained.Kind == EPSIntelRequestKind::GameAnalysis);
    TestEqual(TEXT("The summary is a summary task"), Summary.Task, FString(TEXT("summary")));
    TestEqual(TEXT("The analysis an analysis task"), Explained.Task, FString(TEXT("analysis")));
    const TSharedPtr<FJsonObject> Context = PSGameStateSerializer::ParseObject(Explained.Context);
    TestEqual(TEXT("Its context is the post-game contract"), StringField(Context, TEXT("contract")), FString(TEXT("play-sports.post-game/1")));
    TestEqual(TEXT("...with the winner"), StringField(GetObject(Context, TEXT("final")), TEXT("winner")), FString(TEXT("home")));
    TestEqual(TEXT("...the drives"), ArrayLength(Context, TEXT("drives")), 2);
    TestEqual(TEXT("...and the key plays"), ArrayLength(Context, TEXT("keyPlays")), 3);
    TestTrue(TEXT("Both are free text: no choices"), Summary.Choices.Num() == 0 && Explained.Choices.Num() == 0);

    // The model's answers are kept with the analysis.
    FString Reason;
    TestTrue(TEXT("The summary is taken"), Intelligence->AnswerRequest(Summary.RequestId, TEXT("Home drove 80 yards in two plays. Away threw it away."), Reason));
    TestTrue(TEXT("The analysis is taken"), Intelligence->AnswerRequest(Explained.RequestId, TEXT("The opening touchdown held up."), Reason));
    TestEqual(TEXT("...kept as the drive summary"), Intelligence->GetPostGameAnalysis().DriveSummaryText, FString(TEXT("Home drove 80 yards in two plays. Away threw it away.")));
    TestEqual(TEXT("...and the game analysis"), Intelligence->GetPostGameAnalysis().GameAnalysisText, FString(TEXT("The opening touchdown held up.")));
    TestEqual(TEXT("...through a rebuild"), Intelligence->BuildPostGameAnalysis().GameAnalysisText, FString(TEXT("The opening touchdown held up.")));
    TestFalse(TEXT("An answer to an answered request is refused"), Intelligence->AnswerRequest(Explained.RequestId, TEXT("Again"), Reason));

    // A long season's analysis still fits its budget: the oldest drives go first.
    FPSGameAnalysis Long = Final;
    for (int32 Index = 0; Index < 60; ++Index)
    {
        FPSDriveRecap& Drive = Long.Drives.AddDefaulted_GetRef();
        Drive.bHomeTeam = Index % 2 == 0;
        Drive.Quarter = 1 + Index / 15;
        Drive.Plays = 6;
        Drive.Yards = 31;
        Drive.Result = TEXT("Punt");
    }
    const FString Fitted = PSGameStateSerializer::SerializeAnalysis(Long, PSGameStateSerializer::MinBudgetChars);
    const TSharedPtr<FJsonObject> FittedRoot = PSGameStateSerializer::ParseObject(Fitted);
    TestTrue(TEXT("A long analysis fits the least budget"), Fitted.Len() <= PSGameStateSerializer::MinBudgetChars);
    int32 OmittedDrives = 0;
    TestTrue(TEXT("...saying how many drives it left out"), GetObject(FittedRoot, TEXT("omitted")).IsValid()
        && GetObject(FittedRoot, TEXT("omitted"))->TryGetNumberField(TEXT("drives"), OmittedDrives) && OmittedDrives > 0);
    TestEqual(TEXT("...keeping every key play"), ArrayLength(FittedRoot, TEXT("keyPlays")), 3);
    TestFalse(TEXT("With room, nothing is left out"), PSGameStateSerializer::SerializeAnalysis(Long, 100000).Contains(TEXT("omitted")));

    // A new game starts over.
    Bus->PublishGameState(MakeState(TEXT("PreSnap"), 1, 20, true, 0, 0, 0));
    TestFalse(TEXT("A new game isn't over"), Intelligence->IsGameOver());
    TestEqual(TEXT("...and has no drives yet"), Intelligence->GetDrives().Num(), 0);

    Highlights->StopReel();
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- Model-slot routing
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSModelRoutingTest,
    "PlaySports.GameIntelligence.ModelRouting",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSModelRoutingTest::RunTest(const FString& Parameters)
{
    using namespace PSGameIntelligenceTests;

    // The router's table (Epic 119): every task a request names is one of its tasks.
    FString RoutingText;
    const FString RoutingPath = FPaths::ProjectDir() / TEXT("tools/orchestrator/routing.json");
    if (!TestTrue(TEXT("tools/orchestrator/routing.json reads"), FFileHelper::LoadFileToString(RoutingText, *RoutingPath)))
    {
        return false;
    }
    const TSharedPtr<FJsonObject> Tasks = GetObject(PSGameStateSerializer::ParseObject(RoutingText), TEXT("tasks"));
    if (!TestTrue(TEXT("...with its tasks"), Tasks.IsValid()))
    {
        return false;
    }
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSGameIntelligenceTuning Shipped;
    Ingestion->LoadGameIntelligenceTuningFromJson(UPSGameIntelligenceSubsystem::GetDefaultTuningPath(), Shipped);
    auto Capability = [&Tasks](const FString& Task)
    {
        int32 Value = -1;
        const TSharedPtr<FJsonObject> Route = GetObject(Tasks, *Task);
        return Route.IsValid() && Route->TryGetNumberField(TEXT("min_capability"), Value) ? Value : -1;
    };
    for (const FString& Task : { Shipped.PlayCallTask, Shipped.DriveSummaryTask, Shipped.GameAnalysisTask })
    {
        TestTrue(*FString::Printf(TEXT("'%s' is a routed task"), *Task), Tasks->HasField(Task));
    }
    TestTrue(TEXT("A play call routes to a model at least as capable as a summary's"), Capability(Shipped.PlayCallTask) >= Capability(Shipped.DriveSummaryTask));
    TestTrue(TEXT("...and an analysis too"), Capability(Shipped.GameAnalysisTask) >= Capability(Shipped.DriveSummaryTask));

    // The task is data: a request names whatever the tuning says.
    FScopedBridgeOnline Bridge;
    UWorld* World = CreateTestWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSGameIntelligenceSubsystem* Intelligence = World ? World->GetSubsystem<UPSGameIntelligenceSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Play calls"), PlayCall) || !TestNotNull(TEXT("Game intelligence"), Intelligence))
    {
        DestroyTestWorld(World);
        return false;
    }
    FPSGameIntelligenceTuning Delegated = Shipped;
    Delegated.PlayCallTask = TEXT("delegate");
    TestTrue(TEXT("A tuning naming another task is taken"), Intelligence->SetTuning(Delegated));
    TestTrue(TEXT("The agent takes the offense"), Intelligence->SetConsultation(true, false));
    const int32 RequestId = Intelligence->OpenPlayConsultation(true, MakeSituation(3, 4, 55), PlayCall->GetPlays(true));
    FPSIntelRequest Request;
    TestTrue(TEXT("The request names the tuning's task"), Intelligence->FindRequest(RequestId, Request) && Request.Task == TEXT("delegate"));
    const FString Pending = Intelligence->GetPendingRequestsJson();
    TestTrue(TEXT("...as the agent sees it"), Pending.Contains(TEXT("\"task\":\"delegate\"")));
    TestTrue(TEXT("...with the call's own situation when the bus told none"), Pending.Contains(TEXT("\"down\":3")) && Pending.Contains(TEXT("\"distance\":4")));

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
