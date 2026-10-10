// AutonomixTests.cpp -- Core 25.1 (T3D import) and 25.2 (Python escape hatch)
//
// Tests covered:
//   1. T3D parsing: actor blocks inside Begin Map/Begin Level, subobject declarations told from
//      definitions, and the refusals of a block left open or an End Object without a Begin.
//   2. T3D import on a headless world: a block spawns its actor where its root is placed, with its
//      tags; the same name changes it (an array written by element starts over); an unknown
//      class or property is a warning while the rest imports; text with no actor imports nothing.
//   3. In the editor, an import is one transaction that Undo reverts.
//   4. The tools on AgenticLink's MCP server: off unless their switch is on, import_t3d over MCP,
//      run_python (executed when this session has Python; otherwise its clean refusal), and the
//      provider registry adding and removing them on the serving server.

#include "CoreMinimal.h"
#include "AgenticLinkEngineTools.h"
#include "AgenticLinkMcpServer.h"
#include "AgenticLinkToolProviders.h"
#include "AutonomixPython.h"
#include "AutonomixT3D.h"
#include "AutonomixTools.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS

namespace AutonomixTests
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

    /** The subobject name of a target point's root, as its class makes it. */
    FString RootName()
    {
        const ATargetPoint* Defaults = GetDefault<ATargetPoint>();
        return Defaults && Defaults->GetRootComponent() ? Defaults->GetRootComponent()->GetName() : FString(TEXT("SceneComp"));
    }

    /** A target point named Name at (X, Y, 0) with Tag, as the editor copies one. */
    FString TargetPointT3D(const TCHAR* Name, float X, float Y, const TCHAR* Tag)
    {
        const FString Root = RootName();
        return FString::Printf(TEXT(
            "Begin Map\r\n"
            "   Begin Level\r\n"
            "      Begin Actor Class=/Script/Engine.TargetPoint Name=%s Archetype=/Script/Engine.TargetPoint'/Script/Engine.Default__TargetPoint'\r\n"
            "         Begin Object Class=/Script/Engine.SceneComponent Name=\"%s\" Archetype=SceneComponent'/Script/Engine.Default__TargetPoint:%s'\r\n"
            "         End Object\r\n"
            "         Begin Object Name=\"%s\"\r\n"
            "            RelativeLocation=(X=%f,Y=%f,Z=0.000000)\r\n"
            "         End Object\r\n"
            "         RootComponent=\"%s\"\r\n"
            "         Tags(0)=\"%s\"\r\n"
            "         ActorLabel=\"Agent %s\"\r\n"
            "      End Actor\r\n"
            "   End Level\r\n"
            "End Map\r\n"), Name, *Root, *Root, *Root, X, Y, *Root, Tag, Name);
    }

    int32 CountTargetPoints(UWorld* World)
    {
        int32 Count = 0;
        for (TActorIterator<ATargetPoint> It(World); It; ++It)
        {
            ++Count;
        }
        return Count;
    }

    TSharedPtr<FJsonObject> ParseObject(const FString& Text)
    {
        TSharedPtr<FJsonObject> Object;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        FJsonSerializer::Deserialize(Reader, Object);
        return Object;
    }

    TSharedPtr<FJsonObject> GetObject(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Field)
    {
        const TSharedPtr<FJsonObject>* Child = nullptr;
        return Parent.IsValid() && Parent->TryGetObjectField(Field, Child) && Child ? *Child : TSharedPtr<FJsonObject>();
    }

    /** tools/call Tool with Arguments (a JSON object's text); the call's result. */
    TSharedPtr<FJsonObject> CallTool(FAgenticLinkMcpServer& Server, const TCHAR* Tool, const FString& Arguments)
    {
        const FString Message = FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":%s}}"), Tool, *Arguments);
        return GetObject(ParseObject(Server.HandleMessage(Message)), TEXT("result"));
    }

    bool IsToolError(const TSharedPtr<FJsonObject>& Result)
    {
        bool bError = false;
        return Result.IsValid() && Result->TryGetBoolField(TEXT("isError"), bError) && bError;
    }

    FString ResultText(const TSharedPtr<FJsonObject>& Result)
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

    bool HasTool(const FAgenticLinkMcpServer& Server, const TCHAR* Name)
    {
        return Server.GetTools().ContainsByPredicate([Name](const FAgenticLinkTool& Tool) { return Tool.Name == Name; });
    }

    /** {"Field": Value} as JSON text. */
    FString ArgumentsJson(const TCHAR* Field, const FString& Value)
    {
        TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
        Arguments->SetStringField(Field, Value);
        FString Out;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
        FJsonSerializer::Serialize(Arguments, Writer);
        return Out;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- T3D parsing
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAutonomixT3DParseTest,
    "PlaySports.Autonomix.T3DParse",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAutonomixT3DParseTest::RunTest(const FString& Parameters)
{
    using namespace AutonomixTests;

    TArray<FAutonomixT3DActor> Actors;
    FString Error;
    TestTrue(TEXT("Copied T3D parses"), FAutonomixT3D::Parse(TargetPointT3D(TEXT("AgentPoint"), 100.f, 200.f, TEXT("FromT3D")), Actors, Error));
    if (TestEqual(TEXT("...into one actor block"), Actors.Num(), 1))
    {
        const FAutonomixT3DActor& Block = Actors[0];
        TestEqual(TEXT("...of its class"), Block.ClassName, FString(TEXT("/Script/Engine.TargetPoint")));
        TestEqual(TEXT("...and name"), Block.Name, FString(TEXT("AgentPoint")));
        TestEqual(TEXT("...with its own lines"), Block.PropertyLines.Num(), 3);
        TestTrue(TEXT("...and the root's definition, not its declaration"), Block.Subobjects.Num() == 1 && Block.Subobjects[0].Key == RootName()
            && Block.Subobjects[0].Value.Num() == 1 && Block.Subobjects[0].Value[0].StartsWith(TEXT("RelativeLocation=")));
        TestEqual(TEXT("...found at its line"), Block.LineNumber, 3);
    }

    TestTrue(TEXT("Text with no actor parses to none"), FAutonomixT3D::Parse(TEXT("Begin Map\nEnd Map\n"), Actors, Error) && Actors.Num() == 0);
    TestFalse(TEXT("A block left open is refused"), FAutonomixT3D::Parse(TEXT("Begin Actor Class=/Script/Engine.TargetPoint Name=Open\n  Tags(0)=\"X\"\n"), Actors, Error));
    TestTrue(TEXT("...saying where"), Error.Contains(TEXT("Line 1")) && Error.Contains(TEXT("End Actor")));
    TestFalse(TEXT("An End Object without a Begin is refused"), FAutonomixT3D::Parse(TEXT("Begin Actor Class=/Script/Engine.TargetPoint\nEnd Object\nEnd Actor\n"), Actors, Error));
    TestFalse(TEXT("A Begin Actor with no class or name is refused"), FAutonomixT3D::Parse(TEXT("Begin Actor\nEnd Actor\n"), Actors, Error));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- T3D import on a headless world
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAutonomixT3DImportTest,
    "PlaySports.Autonomix.T3DImport",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAutonomixT3DImportTest::RunTest(const FString& Parameters)
{
    using namespace AutonomixTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("World"), World))
    {
        return false;
    }

    // A new actor.
    const FAutonomixT3DResult Spawned = FAutonomixT3D::Import(World, TargetPointT3D(TEXT("AgentPoint"), 100.f, 200.f, TEXT("FromT3D")));
    TestFalse(TEXT("The T3D imports"), Spawned.IsError());
    for (const FString& Warning : Spawned.Warnings)
    {
        AddError(FString::Printf(TEXT("Unexpected warning: %s"), *Warning));
    }
    TestTrue(TEXT("...spawning AgentPoint"), Spawned.Spawned.Num() == 1 && Spawned.Spawned[0] == TEXT("AgentPoint") && Spawned.Changed.Num() == 0);
    AActor* Point = FAgenticLinkEngineTools::FindActor(World, TEXT("AgentPoint"));
    if (!TestNotNull(TEXT("AgentPoint is in the level"), Point))
    {
        DestroyTestWorld(World);
        return false;
    }
    TestTrue(TEXT("...a target point"), Point->IsA<ATargetPoint>());
    TestTrue(TEXT("...where its root was placed"), Point->GetActorLocation().Equals(FVector(100.f, 200.f, 0.f), 0.1f));
    TestTrue(TEXT("...with its tag"), Point->Tags.Num() == 1 && Point->Tags[0] == FName(TEXT("FromT3D")));
#if WITH_EDITOR
    TestEqual(TEXT("...and its label"), Point->GetActorLabel(), FString(TEXT("Agent AgentPoint")));
#endif

    // The same name changes it.
    const FAutonomixT3DResult Changed = FAutonomixT3D::Import(World, TargetPointT3D(TEXT("AgentPoint"), 300.f, 0.f, TEXT("Moved")));
    TestTrue(TEXT("Importing the same name changes the actor"), Changed.Changed.Num() == 1 && Changed.Spawned.Num() == 0);
    TestEqual(TEXT("...without a second one"), CountTargetPoints(World), 1);
    TestTrue(TEXT("...moving it"), Point->GetActorLocation().Equals(FVector(300.f, 0.f, 0.f), 0.1f));
    TestTrue(TEXT("...and its tags start over"), Point->Tags.Num() == 1 && Point->Tags[0] == FName(TEXT("Moved")));

    // What can't be applied is a warning; the rest imports.
    const FString Mixed = TEXT(
        "Begin Actor Class=/Script/Engine.TargetPoint Name=SecondPoint\n"
        "   NoSuchProperty=1\n"
        "   Tags(0)=\"Second\"\n"
        "End Actor\n"
        "Begin Actor Class=/Script/Engine.NoSuchActorClass Name=Nobody\n"
        "End Actor\n");
    const FAutonomixT3DResult Partial = FAutonomixT3D::Import(World, Mixed);
    TestFalse(TEXT("A partly bad import still imports"), Partial.IsError());
    TestTrue(TEXT("...the good actor"), Partial.Spawned.Num() == 1 && Partial.Spawned[0] == TEXT("SecondPoint"));
    TestTrue(TEXT("...warning of the unknown property"), Partial.Warnings.ContainsByPredicate([](const FString& Warning) { return Warning.Contains(TEXT("no property 'NoSuchProperty'")); }));
    TestTrue(TEXT("...and of the unknown class"), Partial.Warnings.ContainsByPredicate([](const FString& Warning) { return Warning.Contains(TEXT("not a spawnable actor class")); }));
    TestEqual(TEXT("Two target points now"), CountTargetPoints(World), 2);

    // Nothing to import.
    TestTrue(TEXT("Text with no actor imports nothing"), FAutonomixT3D::Import(World, TEXT("Begin Map\nEnd Map\n")).Error.Contains(TEXT("no Begin Actor")));
    TestTrue(TEXT("Only an unknown class imports nothing"), FAutonomixT3D::Import(World, TEXT("Begin Actor Class=NoSuchActorClass Name=Nobody\nEnd Actor\n")).IsError());
    TestTrue(TEXT("Without a world, it says so"), FAutonomixT3D::Import(nullptr, TargetPointT3D(TEXT("X"), 0.f, 0.f, TEXT("X"))).Error.Contains(TEXT("No world")));
    TestEqual(TEXT("...and the level is as it was"), CountTargetPoints(World), 2);

    // One line on its own.
    FString Warning;
    TestTrue(TEXT("A property line applies to any object"), FAutonomixT3D::ApplyPropertyLine(Point, TEXT("bHidden=True"), Warning) && Point->IsHidden());
    TestFalse(TEXT("A line without '=' is refused"), FAutonomixT3D::ApplyPropertyLine(Point, TEXT("JustText"), Warning));
    TestFalse(TEXT("A value of the wrong type is refused"), FAutonomixT3D::ApplyPropertyLine(Point, TEXT("bHidden=(X=1)"), Warning));

#if WITH_EDITOR
    if (GEditor)
    {
        GEditor->ResetTransaction(FText::FromString(TEXT("Autonomix T3D import test")));
    }
#endif
    DestroyTestWorld(World);
    return true;
}

#if WITH_EDITOR
// ---------------------------------------------------------------------------
// Test 3 -- An import is one undoable transaction
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAutonomixT3DUndoTest,
    "PlaySports.Autonomix.T3DUndo",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAutonomixT3DUndoTest::RunTest(const FString& Parameters)
{
    using namespace AutonomixTests;

    if (!TestNotNull(TEXT("The editor is running"), GEditor))
    {
        return false;
    }
    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("World"), World))
    {
        return false;
    }
    FAutonomixT3D::Import(World, TargetPointT3D(TEXT("UndoPoint"), 0.f, 0.f, TEXT("Before")));
    AActor* Point = FAgenticLinkEngineTools::FindActor(World, TEXT("UndoPoint"));
    GEditor->ResetTransaction(FText::FromString(TEXT("Autonomix undo test")));

    const FAutonomixT3DResult Changed = FAutonomixT3D::Import(World, TargetPointT3D(TEXT("UndoPoint"), 0.f, 0.f, TEXT("After")));
    TestTrue(TEXT("The import reports itself undoable"), Changed.bUndoable);
    TestTrue(TEXT("The tag changed"), Point && Point->Tags.Num() == 1 && Point->Tags[0] == FName(TEXT("After")));
    TestTrue(TEXT("Undo runs"), GEditor->UndoTransaction());
    TestTrue(TEXT("Undo puts the tag back"), Point && Point->Tags.Num() == 1 && Point->Tags[0] == FName(TEXT("Before")));

    GEditor->ResetTransaction(FText::FromString(TEXT("Autonomix undo test done")));
    DestroyTestWorld(World);
    return true;
}
#endif // WITH_EDITOR

// ---------------------------------------------------------------------------
// Test 4 -- The tools on AgenticLink's server
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAutonomixMcpToolsTest,
    "PlaySports.Autonomix.McpTools",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAutonomixMcpToolsTest::RunTest(const FString& Parameters)
{
    using namespace AutonomixTests;

    // Off unless switched on.
    TestFalse(TEXT("No switch, no tools"), FAutonomixTools::ReadSwitches(TEXT("-AgenticLinkMcp")).Any());
    const FAutonomixSwitches T3DOnly = FAutonomixTools::ReadSwitches(TEXT("-AgenticLinkMcp -AutonomixT3D"));
    TestTrue(TEXT("-AutonomixT3D turns on the T3D import alone"), T3DOnly.bT3D && !T3DOnly.bPython);
    const FAutonomixSwitches Both = FAutonomixTools::ReadSwitches(TEXT("-AutonomixT3D -AutonomixPython"));
    TestTrue(TEXT("-AutonomixPython turns on Python"), Both.bT3D && Both.bPython);
    FAgenticLinkMcpServer Off;
    FAutonomixTools::Register(Off, FAutonomixSwitches());
    TestEqual(TEXT("Nothing switched on registers nothing"), Off.GetTools().Num(), 0);

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("World"), World))
    {
        return false;
    }
    FAgenticLinkMcpServer Server;
    FAutonomixTools::Register(Server, Both, [World]() { return World; });
    TestTrue(TEXT("import_t3d and run_python are listed"), HasTool(Server, TEXT("import_t3d")) && HasTool(Server, TEXT("run_python")));

    // import_t3d over MCP.
    const TSharedPtr<FJsonObject> Imported = CallTool(Server, TEXT("import_t3d"), ArgumentsJson(TEXT("t3d"), TargetPointT3D(TEXT("McpPoint"), 50.f, 0.f, TEXT("Mcp"))));
    TestFalse(TEXT("import_t3d imports"), IsToolError(Imported));
    TestTrue(TEXT("...McpPoint, as the result says"), ResultText(Imported).Contains(TEXT("McpPoint")) && FAgenticLinkEngineTools::FindActor(World, TEXT("McpPoint")) != nullptr);
    TestTrue(TEXT("import_t3d needs its text"), IsToolError(CallTool(Server, TEXT("import_t3d"), TEXT("{}"))));
    TestTrue(TEXT("Bad T3D is the agent's to fix"), ResultText(CallTool(Server, TEXT("import_t3d"), TEXT("{\"t3d\":\"Begin Actor Class=X\"}"))).Contains(TEXT("End Actor")));

    // run_python.
    TestTrue(TEXT("run_python refuses an unknown mode"), ResultText(CallTool(Server, TEXT("run_python"), TEXT("{\"script\":\"1\",\"mode\":\"compile\"}"))).Contains(TEXT("mode")));
    TestTrue(TEXT("run_python needs a script"), IsToolError(CallTool(Server, TEXT("run_python"), TEXT("{}"))));
    if (FAutonomixPython::IsAvailable())
    {
        const TSharedPtr<FJsonObject> Sum = CallTool(Server, TEXT("run_python"), TEXT("{\"script\":\"1 + 1\",\"mode\":\"evaluate\"}"));
        FString Value;
        TestTrue(TEXT("An expression's value is the result"), GetObject(Sum, TEXT("structuredContent")).IsValid()
            && GetObject(Sum, TEXT("structuredContent"))->TryGetStringField(TEXT("result"), Value) && Value == TEXT("2"));
        TestTrue(TEXT("What a script prints is in its log"), ResultText(CallTool(Server, TEXT("run_python"), TEXT("{\"script\":\"print('autonomix says hi')\"}"))).Contains(TEXT("autonomix says hi")));
        const TSharedPtr<FJsonObject> Raised = CallTool(Server, TEXT("run_python"), TEXT("{\"script\":\"1 / 0\"}"));
        TestTrue(TEXT("A script that raises is an error with its traceback"), IsToolError(Raised) && ResultText(Raised).Contains(TEXT("ZeroDivisionError")));
    }
    else
    {
        AddInfo(TEXT("Python isn't available in this session: run_python's execution is not exercised here, only its refusal."));
        const TSharedPtr<FJsonObject> Refused = CallTool(Server, TEXT("run_python"), TEXT("{\"script\":\"1 + 1\",\"mode\":\"evaluate\"}"));
        TestTrue(TEXT("Without Python, run_python refuses cleanly"), IsToolError(Refused) && ResultText(Refused).Contains(TEXT("Python")));
    }

    // The provider registry, on a stand-in serving server (never over a real one).
    if (!FAgenticLinkToolProviders::GetServingServer().IsValid())
    {
        const FName TestProvider(TEXT("AutonomixTestProvider"));
        TSharedPtr<FAgenticLinkMcpServer> Serving = MakeShared<FAgenticLinkMcpServer>();
        FAgenticLinkToolProviders::SetServingServer(Serving);
        FAgenticLinkToolProviders::Add(TestProvider, [T3DOnly](FAgenticLinkMcpServer& Target) { FAutonomixTools::Register(Target, T3DOnly); });
        TestTrue(TEXT("A provider added while a server serves registers at once"), HasTool(*Serving, TEXT("import_t3d")) && !HasTool(*Serving, TEXT("run_python")));
        FAgenticLinkMcpServer Next;
        FAgenticLinkToolProviders::RegisterAll(Next);
        TestTrue(TEXT("...and on a server that starts later"), HasTool(Next, TEXT("import_t3d")));
        FAgenticLinkToolProviders::Remove(TestProvider);
        TestFalse(TEXT("Removed, its tools come off the serving server"), HasTool(*Serving, TEXT("import_t3d")));
        TestFalse(TEXT("...and it is gone"), FAgenticLinkToolProviders::Has(TestProvider));
        FAgenticLinkToolProviders::SetServingServer(nullptr);
    }
    else
    {
        AddInfo(TEXT("AgenticLink is serving in this session; the provider registry wasn't exercised on a stand-in."));
    }

#if WITH_EDITOR
    if (GEditor)
    {
        GEditor->ResetTransaction(FText::FromString(TEXT("Autonomix MCP tools test")));
    }
#endif
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
