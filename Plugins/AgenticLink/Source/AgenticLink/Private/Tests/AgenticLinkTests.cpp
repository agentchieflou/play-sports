// AgenticLinkTests.cpp -- Epic 25 (AgenticLink: MCP server over engine reflection)
//
// Tests covered:
//   1. The MCP protocol: initialize and version negotiation, notifications, ping, tools/list,
//      tools/call results and tool errors, and the JSON-RPC errors for malformed messages.
//   2. The engine tools on a headless world: list_actors, get_property, set_property,
//      call_function and spawn_actor, with their refusals.
//   3. In the editor, an agent's set_property is one transaction that Undo reverts.
//   4. The HTTP transport: POST answers, 202 for notifications, 405 for GET, 403 for a foreign
//      Origin.

#include "CoreMinimal.h"
#include "AgenticLinkEngineTools.h"
#include "AgenticLinkHttpTransport.h"
#include "AgenticLinkMcpServer.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HttpServerResponse.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS

namespace AgenticLinkTests
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

    static ATargetPoint* SpawnPoint(UWorld* World, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<ATargetPoint>(ATargetPoint::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
    }

    static TSharedPtr<FJsonObject> ParseObject(const FString& Text)
    {
        TSharedPtr<FJsonObject> Object;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        FJsonSerializer::Deserialize(Reader, Object);
        return Object;
    }

    static TSharedPtr<FJsonObject> GetObject(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Field)
    {
        const TSharedPtr<FJsonObject>* Child = nullptr;
        return Parent.IsValid() && Parent->TryGetObjectField(Field, Child) && Child ? *Child : TSharedPtr<FJsonObject>();
    }

    static int32 ErrorCode(const TSharedPtr<FJsonObject>& Response)
    {
        int32 Code = 0;
        const TSharedPtr<FJsonObject> Error = GetObject(Response, TEXT("error"));
        return Error.IsValid() && Error->TryGetNumberField(TEXT("code"), Code) ? Code : 0;
    }

    /** tools/call Tool with ArgumentsJson; returns the call's result object. */
    static TSharedPtr<FJsonObject> CallTool(FAgenticLinkMcpServer& Server, const TCHAR* Tool, const FString& ArgumentsJson)
    {
        const FString Message = FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":%s}}"), Tool, *ArgumentsJson);
        return GetObject(ParseObject(Server.HandleMessage(Message)), TEXT("result"));
    }

    static bool IsToolError(const TSharedPtr<FJsonObject>& Result)
    {
        bool bError = false;
        return Result.IsValid() && Result->TryGetBoolField(TEXT("isError"), bError) && bError;
    }

    /** The first text block of a tool result. */
    static FString ResultText(const TSharedPtr<FJsonObject>& Result)
    {
        const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
        if (!Result.IsValid() || !Result->TryGetArrayField(TEXT("content"), Content) || !Content || Content->Num() == 0)
        {
            return FString();
        }
        const TSharedPtr<FJsonObject>* Block = nullptr;
        FString Text;
        return (*Content)[0]->TryGetObject(Block) && Block && (*Block)->TryGetStringField(TEXT("text"), Text) ? Text : FString();
    }

    static FString StructuredString(const TSharedPtr<FJsonObject>& Result, const TCHAR* Field)
    {
        FString Value;
        const TSharedPtr<FJsonObject> Structured = GetObject(Result, TEXT("structuredContent"));
        return Structured.IsValid() && Structured->TryGetStringField(Field, Value) ? Value : FString();
    }

    static int32 CountActors(const TSharedPtr<FJsonObject>& Result)
    {
        const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
        const TSharedPtr<FJsonObject> Structured = GetObject(Result, TEXT("structuredContent"));
        return Structured.IsValid() && Structured->TryGetArrayField(TEXT("actors"), Actors) && Actors ? Actors->Num() : -1;
    }

    static FString BodyText(const FHttpServerResponse& Response)
    {
        if (Response.Body.Num() == 0)
        {
            return FString();
        }
        FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Response.Body.GetData()), Response.Body.Num());
        return FString(Converted.Length(), Converted.Get());
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The MCP protocol
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAgenticLinkProtocolTest,
    "PlaySports.AgenticLink.McpProtocol",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAgenticLinkProtocolTest::RunTest(const FString& Parameters)
{
    using namespace AgenticLinkTests;

    FAgenticLinkMcpServer Server;
    FAgenticLinkTool Echo;
    Echo.Name = TEXT("echo");
    Echo.Description = TEXT("Echoes text.");
    Echo.Handler = [](const TSharedPtr<FJsonObject>& Arguments)
    {
        FString Text;
        if (!Arguments->TryGetStringField(TEXT("text"), Text))
        {
            return FAgenticLinkToolResult::Failure(TEXT("Missing 'text'."));
        }
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("echo"), Text);
        return FAgenticLinkToolResult::Success(Result);
    };
    Server.RegisterTool(Echo);

    // initialize: the client's version when supported, else the newest.
    TestFalse(TEXT("Not initialized before initialize"), Server.IsInitialized());
    const TSharedPtr<FJsonObject> Init = ParseObject(Server.HandleMessage(TEXT(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})")));
    const TSharedPtr<FJsonObject> InitResult = GetObject(Init, TEXT("result"));
    FString Version;
    TestTrue(TEXT("initialize answers with a result"), InitResult.IsValid());
    TestTrue(TEXT("...agreeing to the client's supported version"), InitResult.IsValid() && InitResult->TryGetStringField(TEXT("protocolVersion"), Version) && Version == TEXT("2025-03-26"));
    TestTrue(TEXT("...offering tools"), GetObject(GetObject(InitResult, TEXT("capabilities")), TEXT("tools")).IsValid());
    FString ServerName;
    TestTrue(TEXT("...as AgenticLink"), GetObject(InitResult, TEXT("serverInfo")).IsValid() && GetObject(InitResult, TEXT("serverInfo"))->TryGetStringField(TEXT("name"), ServerName) && ServerName == TEXT("AgenticLink"));
    TestTrue(TEXT("The server is initialized"), Server.IsInitialized());

    const TSharedPtr<FJsonObject> Future = GetObject(ParseObject(Server.HandleMessage(TEXT(R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"1999-01-01"}})"))), TEXT("result"));
    TestTrue(TEXT("An unknown version gets the newest supported one"), Future.IsValid() && Future->TryGetStringField(TEXT("protocolVersion"), Version)
        && Version == FAgenticLinkMcpServer::GetSupportedProtocolVersions()[0]);

    // Notifications and client responses get no answer.
    TestTrue(TEXT("notifications/initialized gets no answer"), Server.HandleMessage(TEXT(R"({"jsonrpc":"2.0","method":"notifications/initialized"})")).IsEmpty());
    TestTrue(TEXT("A client's response gets no answer"), Server.HandleMessage(TEXT(R"({"jsonrpc":"2.0","id":9,"result":{}})")).IsEmpty());

    // ping keeps a string id.
    const TSharedPtr<FJsonObject> Ping = ParseObject(Server.HandleMessage(TEXT(R"({"jsonrpc":"2.0","id":"abc","method":"ping"})")));
    FString PingId;
    TestTrue(TEXT("ping answers with the same string id"), Ping.IsValid() && Ping->TryGetStringField(TEXT("id"), PingId) && PingId == TEXT("abc") && GetObject(Ping, TEXT("result")).IsValid());

    // tools/list
    const TSharedPtr<FJsonObject> List = GetObject(ParseObject(Server.HandleMessage(TEXT(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})"))), TEXT("result"));
    const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
    TestTrue(TEXT("tools/list lists the one tool"), List.IsValid() && List->TryGetArrayField(TEXT("tools"), Tools) && Tools && Tools->Num() == 1);
    if (Tools && Tools->Num() == 1)
    {
        const TSharedPtr<FJsonObject> Tool = (*Tools)[0]->AsObject();
        FString ToolName;
        FString SchemaType;
        TestTrue(TEXT("...by name"), Tool.IsValid() && Tool->TryGetStringField(TEXT("name"), ToolName) && ToolName == TEXT("echo"));
        TestTrue(TEXT("...with an object input schema"), GetObject(Tool, TEXT("inputSchema")).IsValid()
            && GetObject(Tool, TEXT("inputSchema"))->TryGetStringField(TEXT("type"), SchemaType) && SchemaType == TEXT("object"));
    }

    // tools/call: a result, a tool error, an unknown tool.
    const TSharedPtr<FJsonObject> Echoed = CallTool(Server, TEXT("echo"), TEXT(R"({"text":"hi"})"));
    TestFalse(TEXT("A good call is not an error"), IsToolError(Echoed));
    TestEqual(TEXT("...its structured result"), StructuredString(Echoed, TEXT("echo")), FString(TEXT("hi")));
    TestTrue(TEXT("...and the same as text"), ResultText(Echoed).Contains(TEXT("\"echo\":\"hi\"")));
    const TSharedPtr<FJsonObject> Refused = CallTool(Server, TEXT("echo"), TEXT("{}"));
    TestTrue(TEXT("A failing tool answers isError"), IsToolError(Refused));
    TestEqual(TEXT("...with its message"), ResultText(Refused), FString(TEXT("Missing 'text'.")));
    TestEqual(TEXT("An unknown tool is invalid params"),
        ErrorCode(ParseObject(Server.HandleMessage(TEXT(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"nope"}})")))), FAgenticLinkMcpServer::InvalidParams);

    // Malformed messages.
    TestEqual(TEXT("An unknown method"), ErrorCode(ParseObject(Server.HandleMessage(TEXT(R"({"jsonrpc":"2.0","id":5,"method":"resources/list"})")))), FAgenticLinkMcpServer::MethodNotFound);
    TestEqual(TEXT("Not JSON"), ErrorCode(ParseObject(Server.HandleMessage(TEXT("not json")))), FAgenticLinkMcpServer::ParseError);
    TestEqual(TEXT("A batch"), ErrorCode(ParseObject(Server.HandleMessage(TEXT(R"([{"jsonrpc":"2.0","id":6,"method":"ping"}])")))), FAgenticLinkMcpServer::InvalidRequest);
    TestEqual(TEXT("No method"), ErrorCode(ParseObject(Server.HandleMessage(TEXT(R"({"jsonrpc":"2.0","id":8})")))), FAgenticLinkMcpServer::InvalidRequest);
    TestEqual(TEXT("Not JSON-RPC 2.0"), ErrorCode(ParseObject(Server.HandleMessage(TEXT(R"({"jsonrpc":"1.0","id":10,"method":"ping"})")))), FAgenticLinkMcpServer::InvalidRequest);

    // JSON arguments in Unreal's text format.
    FString Text;
    TestTrue(TEXT("A string argument is used as it is"), FAgenticLinkEngineTools::JsonValueToText(MakeShared<FJsonValueString>(TEXT("(X=1)")), Text) && Text == TEXT("(X=1)"));
    TestTrue(TEXT("A whole number has no fraction"), FAgenticLinkEngineTools::JsonValueToText(MakeShared<FJsonValueNumber>(3.0), Text) && Text == TEXT("3"));
    TestTrue(TEXT("A fraction is kept"), FAgenticLinkEngineTools::JsonValueToText(MakeShared<FJsonValueNumber>(2.5), Text) && Text == TEXT("2.5"));
    TestTrue(TEXT("A boolean is True/False"), FAgenticLinkEngineTools::JsonValueToText(MakeShared<FJsonValueBoolean>(true), Text) && Text == TEXT("True"));
    TestFalse(TEXT("An array is refused"), FAgenticLinkEngineTools::JsonValueToText(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>()), Text));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The engine tools on a headless world
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAgenticLinkEngineToolsTest,
    "PlaySports.AgenticLink.EngineReflection",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAgenticLinkEngineToolsTest::RunTest(const FString& Parameters)
{
    using namespace AgenticLinkTests;

    UWorld* World = CreateTestWorld();
    ATargetPoint* First = World ? SpawnPoint(World, FVector(100.f, 0.f, 0.f)) : nullptr;
    ATargetPoint* Second = World ? SpawnPoint(World, FVector(200.f, 0.f, 0.f)) : nullptr;
    if (!TestNotNull(TEXT("First target point"), First) || !TestNotNull(TEXT("Second target point"), Second))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    FAgenticLinkMcpServer Server;
    FAgenticLinkEngineTools::Register(Server, [World]() { return World; });
    TestEqual(TEXT("Five engine tools are registered"), Server.GetTools().Num(), 5);
    const FString FirstName = First->GetName();

    // list_actors
    TestEqual(TEXT("Both target points are listed"), CountActors(CallTool(Server, TEXT("list_actors"), TEXT(R"({"class":"TargetPoint"})"))), 2);
    TestTrue(TEXT("Listing everything includes them"), CountActors(CallTool(Server, TEXT("list_actors"), TEXT("{}"))) >= 2);
    const TSharedPtr<FJsonObject> Limited = CallTool(Server, TEXT("list_actors"), TEXT(R"({"class":"TargetPoint","limit":1})"));
    bool bTruncated = false;
    TestTrue(TEXT("A limit truncates"), CountActors(Limited) == 1 && GetObject(Limited, TEXT("structuredContent"))->TryGetBoolField(TEXT("truncated"), bTruncated) && bTruncated);
    TestTrue(TEXT("A listed actor carries its name"), ResultText(CallTool(Server, TEXT("list_actors"), TEXT(R"({"class":"TargetPoint"})"))).Contains(FirstName));

    // get_property / set_property
    const TSharedPtr<FJsonObject> Tags = CallTool(Server, TEXT("get_property"), FString::Printf(TEXT(R"({"actor":"%s","property":"Tags"})"), *FirstName));
    TestFalse(TEXT("Tags can be read"), IsToolError(Tags));
    TestTrue(TEXT("...as a TArray"), StructuredString(Tags, TEXT("type")).StartsWith(TEXT("TArray")));
    const TSharedPtr<FJsonObject> Tagged = CallTool(Server, TEXT("set_property"), FString::Printf(TEXT(R"json({"actor":"%s","property":"Tags","value":"(\"AgentTag\")"})json"), *FirstName));
    TestFalse(TEXT("Tags can be set"), IsToolError(Tagged));
    TestTrue(TEXT("...on the actor"), First->Tags.Contains(FName(TEXT("AgentTag"))));
    TestTrue(TEXT("...and the new value is echoed"), StructuredString(Tagged, TEXT("value")).Contains(TEXT("AgentTag")));
    TestFalse(TEXT("...only on that actor"), Second->Tags.Contains(FName(TEXT("AgentTag"))));

    TestTrue(TEXT("A defaults-only property can't be set on a placed actor"),
        IsToolError(CallTool(Server, TEXT("set_property"), FString::Printf(TEXT(R"json({"actor":"%s","property":"PrimaryActorTick","value":"()"})json"), *FirstName))));
    TestTrue(TEXT("A hidden property can't be read"),
        IsToolError(CallTool(Server, TEXT("get_property"), FString::Printf(TEXT(R"({"actor":"%s","property":"Owner"})"), *FirstName))));
    TestTrue(TEXT("An unknown property is refused"),
        ResultText(CallTool(Server, TEXT("get_property"), FString::Printf(TEXT(R"({"actor":"%s","property":"NoSuchThing"})"), *FirstName))).Contains(TEXT("no property")));
    TestTrue(TEXT("An unknown actor is refused"),
        ResultText(CallTool(Server, TEXT("get_property"), TEXT(R"({"actor":"Nobody","property":"Tags"})"))).Contains(TEXT("No actor 'Nobody'")));
    TestTrue(TEXT("set_property needs a value"),
        IsToolError(CallTool(Server, TEXT("set_property"), FString::Printf(TEXT(R"({"actor":"%s","property":"Tags"})"), *FirstName))));

    // call_function
    const TSharedPtr<FJsonObject> Location = CallTool(Server, TEXT("call_function"), FString::Printf(TEXT(R"({"actor":"%s","function":"K2_GetActorLocation"})"), *FirstName));
    TestFalse(TEXT("K2_GetActorLocation can be called"), IsToolError(Location));
    FString ReturnValue;
    const TSharedPtr<FJsonObject> Outputs = GetObject(GetObject(Location, TEXT("structuredContent")), TEXT("outputs"));
    const bool bHasReturnValue = Outputs.IsValid() && Outputs->TryGetStringField(TEXT("ReturnValue"), ReturnValue);
    AddInfo(FString::Printf(TEXT("K2_GetActorLocation answered %s; the actor stands at %s"), *ResultText(Location), *First->GetActorLocation().ToString()));
    FVector Returned = FVector::ZeroVector;
    TestTrue(TEXT("...and returns its location as text"), bHasReturnValue && Returned.InitFromString(ReturnValue));
    TestTrue(TEXT("...which is where the actor stands"), Returned.Equals(First->GetActorLocation(), 0.1f));
    TestTrue(TEXT("...where it was spawned"), First->GetActorLocation().Equals(FVector(100.f, 0.f, 0.f), 0.1f));
    const TSharedPtr<FJsonObject> Hidden = CallTool(Server, TEXT("call_function"), FString::Printf(TEXT(R"({"actor":"%s","function":"SetActorHiddenInGame","arguments":{"bNewHidden":true}})"), *FirstName));
    TestFalse(TEXT("SetActorHiddenInGame can be called with a JSON boolean"), IsToolError(Hidden));
    TestTrue(TEXT("...and hides the actor"), First->IsHidden());
    TestTrue(TEXT("A missing argument is refused"),
        ResultText(CallTool(Server, TEXT("call_function"), FString::Printf(TEXT(R"({"actor":"%s","function":"SetActorHiddenInGame"})"), *FirstName))).Contains(TEXT("bNewHidden")));
    TestTrue(TEXT("A function Blueprints can't call is refused"),
        ResultText(CallTool(Server, TEXT("call_function"), FString::Printf(TEXT(R"({"actor":"%s","function":"ReceiveBeginPlay"})"), *FirstName))).Contains(TEXT("not BlueprintCallable")));

    // spawn_actor
    const TSharedPtr<FJsonObject> Spawned = CallTool(Server, TEXT("spawn_actor"), TEXT(R"({"class":"TargetPoint","location":[10,20,30]})"));
    TestFalse(TEXT("A target point can be spawned by class name"), IsToolError(Spawned));
    AActor* SpawnedActor = FAgenticLinkEngineTools::FindActor(World, StructuredString(Spawned, TEXT("name")));
    TestTrue(TEXT("...where asked"), SpawnedActor && SpawnedActor->GetActorLocation().Equals(FVector(10.f, 20.f, 30.f), 0.1f));
    TestFalse(TEXT("A target point can be spawned by path"), IsToolError(CallTool(Server, TEXT("spawn_actor"), TEXT(R"({"class":"/Script/Engine.TargetPoint"})"))));
    TestEqual(TEXT("Four target points now"), CountActors(CallTool(Server, TEXT("list_actors"), TEXT(R"({"class":"TargetPoint"})"))), 4);
    TestTrue(TEXT("A component class is not an actor"), IsToolError(CallTool(Server, TEXT("spawn_actor"), TEXT(R"({"class":"StaticMeshComponent"})"))));
    TestTrue(TEXT("An unknown class is refused"), IsToolError(CallTool(Server, TEXT("spawn_actor"), TEXT(R"({"class":"NoSuchActorClass"})"))));
    TestTrue(TEXT("A location needs three numbers"), IsToolError(CallTool(Server, TEXT("spawn_actor"), TEXT(R"({"class":"TargetPoint","location":[1,2]})"))));

    // No world: every tool says so.
    FAgenticLinkMcpServer Worldless;
    FAgenticLinkEngineTools::Register(Worldless, []() { return static_cast<UWorld*>(nullptr); });
    TestTrue(TEXT("Without a world, list_actors says to open a level"), ResultText(CallTool(Worldless, TEXT("list_actors"), TEXT("{}"))).Contains(TEXT("No world")));

#if WITH_EDITOR
    if (GEditor)
    {
        GEditor->ResetTransaction(FText::FromString(TEXT("AgenticLink engine-tools test")));
    }
#endif
    DestroyTestWorld(World);
    return true;
}

#if WITH_EDITOR
// ---------------------------------------------------------------------------
// Test 3 -- An agent's edit is one undoable transaction
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAgenticLinkUndoTest,
    "PlaySports.AgenticLink.UndoableEdits",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAgenticLinkUndoTest::RunTest(const FString& Parameters)
{
    using namespace AgenticLinkTests;

    if (!TestNotNull(TEXT("The editor is running"), GEditor))
    {
        return false;
    }
    UWorld* World = CreateTestWorld();
    ATargetPoint* Point = World ? SpawnPoint(World, FVector::ZeroVector) : nullptr;
    if (!TestNotNull(TEXT("Target point"), Point))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    FAgenticLinkMcpServer Server;
    FAgenticLinkEngineTools::Register(Server, [World]() { return World; });
    GEditor->ResetTransaction(FText::FromString(TEXT("AgenticLink undo test")));

    const TSharedPtr<FJsonObject> Tagged = CallTool(Server, TEXT("set_property"), FString::Printf(TEXT(R"json({"actor":"%s","property":"Tags","value":"(\"UndoMe\")"})json"), *Point->GetName()));
    bool bUndoable = false;
    const TSharedPtr<FJsonObject> Structured = GetObject(Tagged, TEXT("structuredContent"));
    TestTrue(TEXT("The edit reports itself undoable"), Structured.IsValid() && Structured->TryGetBoolField(TEXT("undoable"), bUndoable) && bUndoable);
    TestTrue(TEXT("The tag is set"), Point->Tags.Contains(FName(TEXT("UndoMe"))));
    TestTrue(TEXT("Undo runs"), GEditor->UndoTransaction());
    TestFalse(TEXT("Undo takes the agent's tag off again"), Point->Tags.Contains(FName(TEXT("UndoMe"))));

    GEditor->ResetTransaction(FText::FromString(TEXT("AgenticLink undo test done")));
    DestroyTestWorld(World);
    return true;
}
#endif // WITH_EDITOR

// ---------------------------------------------------------------------------
// Test 4 -- The HTTP transport
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAgenticLinkHttpTransportTest,
    "PlaySports.AgenticLink.HttpTransport",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAgenticLinkHttpTransportTest::RunTest(const FString& Parameters)
{
    using namespace AgenticLinkTests;

    TSharedRef<FAgenticLinkMcpServer> Server = MakeShared<FAgenticLinkMcpServer>();
    FAgenticLinkHttpTransport Transport(Server);
    TestFalse(TEXT("A transport that hasn't started isn't listening"), Transport.IsListening());

    auto Send = [&Transport](EHttpServerRequestVerbs Verb, const FString& Body, const FString& Origin)
    {
        FHttpServerRequest Request;
        Request.Verb = Verb;
        FTCHARToUTF8 Utf8(*Body);
        Request.Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
        if (!Origin.IsEmpty())
        {
            TArray<FString> Values;
            Values.Add(Origin);
            Request.Headers.Add(TEXT("Origin"), Values);
        }
        TUniquePtr<FHttpServerResponse> Captured;
        Transport.HandleRequest(Request, [&Captured](TUniquePtr<FHttpServerResponse>&& Response) { Captured = MoveTemp(Response); });
        return Captured;
    };

    const FString Initialize = TEXT(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})");
    TUniquePtr<FHttpServerResponse> Answer = Send(EHttpServerRequestVerbs::VERB_POST, Initialize, FString());
    TestTrue(TEXT("POST initialize is answered 200"), Answer.IsValid() && Answer->Code == EHttpServerResponseCodes::Ok);
    TestTrue(TEXT("...with the JSON-RPC result"), Answer.IsValid() && BodyText(*Answer).Contains(TEXT("\"protocolVersion\":\"2025-06-18\"")));
    TestTrue(TEXT("...as application/json"), Answer.IsValid() && Answer->Headers.Contains(TEXT("content-type"))
        && Answer->Headers[TEXT("content-type")].Num() > 0 && Answer->Headers[TEXT("content-type")][0].Contains(TEXT("application/json")));

    TUniquePtr<FHttpServerResponse> Notified = Send(EHttpServerRequestVerbs::VERB_POST, TEXT(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"), FString());
    TestTrue(TEXT("A notification is accepted with 202 and no body"), Notified.IsValid() && Notified->Code == EHttpServerResponseCodes::Accepted && Notified->Body.Num() == 0);

    TUniquePtr<FHttpServerResponse> Stream = Send(EHttpServerRequestVerbs::VERB_GET, FString(), FString());
    TestTrue(TEXT("GET (an event stream) is refused with 405"), Stream.IsValid() && Stream->Code == EHttpServerResponseCodes::BadMethod);

    TUniquePtr<FHttpServerResponse> Foreign = Send(EHttpServerRequestVerbs::VERB_POST, Initialize, TEXT("http://evil.example"));
    TestTrue(TEXT("A page from another origin is refused with 403"), Foreign.IsValid() && Foreign->Code == EHttpServerResponseCodes::Forbidden);
    TUniquePtr<FHttpServerResponse> Local = Send(EHttpServerRequestVerbs::VERB_POST, Initialize, TEXT("http://localhost:5173"));
    TestTrue(TEXT("A page on localhost is answered"), Local.IsValid() && Local->Code == EHttpServerResponseCodes::Ok);

    TestTrue(TEXT("No Origin is allowed (a CLI client)"), FAgenticLinkHttpTransport::IsAllowedOrigin(FString()));
    TestTrue(TEXT("127.0.0.1 is allowed"), FAgenticLinkHttpTransport::IsAllowedOrigin(TEXT("http://127.0.0.1:8080")));
    TestTrue(TEXT("https://localhost is allowed"), FAgenticLinkHttpTransport::IsAllowedOrigin(TEXT("https://localhost")));
    TestTrue(TEXT("[::1] is allowed"), FAgenticLinkHttpTransport::IsAllowedOrigin(TEXT("http://[::1]:3000")));
    TestFalse(TEXT("A lookalike host is refused"), FAgenticLinkHttpTransport::IsAllowedOrigin(TEXT("http://localhost.evil.com")));
    TestFalse(TEXT("The null origin is refused"), FAgenticLinkHttpTransport::IsAllowedOrigin(TEXT("null")));
    TestFalse(TEXT("A file origin is refused"), FAgenticLinkHttpTransport::IsAllowedOrigin(TEXT("file://")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
