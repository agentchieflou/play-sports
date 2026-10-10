// AutonomixTools.cpp - Core 25.1/25.2: Autonomix's tools on AgenticLink's MCP server
#include "AutonomixTools.h"
#include "AgenticLinkEngineTools.h"
#include "AgenticLinkMcpServer.h"
#include "AutonomixPython.h"
#include "AutonomixT3D.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Parse.h"

namespace AutonomixToolsPrivate
{
    TSharedPtr<FJsonObject> StringParam(const TCHAR* Description)
    {
        TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
        Param->SetStringField(TEXT("type"), TEXT("string"));
        Param->SetStringField(TEXT("description"), Description);
        return Param;
    }

    TSharedPtr<FJsonObject> Schema(const TSharedPtr<FJsonObject>& Properties, const TArray<FString>& Required)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("type"), TEXT("object"));
        Result->SetObjectField(TEXT("properties"), Properties);
        TArray<TSharedPtr<FJsonValue>> Names;
        for (const FString& Name : Required)
        {
            Names.Add(MakeShared<FJsonValueString>(Name));
        }
        Result->SetArrayField(TEXT("required"), Names);
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (const FString& Value : Values)
        {
            Out.Add(MakeShared<FJsonValueString>(Value));
        }
        return Out;
    }

    FAgenticLinkToolResult ImportT3D(UWorld* World, const TSharedPtr<FJsonObject>& Arguments)
    {
        FString Text;
        if (!Arguments->TryGetStringField(TEXT("t3d"), Text) || Text.IsEmpty())
        {
            return FAgenticLinkToolResult::Failure(TEXT("Missing 't3d': the actors as T3D text (Begin Actor Class=... Name=... End Actor)."));
        }
        const FAutonomixT3DResult Imported = FAutonomixT3D::Import(World, Text);
        if (Imported.IsError())
        {
            return FAgenticLinkToolResult::Failure(Imported.Error);
        }
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetArrayField(TEXT("spawned"), Strings(Imported.Spawned));
        Result->SetArrayField(TEXT("changed"), Strings(Imported.Changed));
        Result->SetArrayField(TEXT("warnings"), Strings(Imported.Warnings));
        Result->SetBoolField(TEXT("undoable"), Imported.bUndoable);
        return FAgenticLinkToolResult::Success(Result);
    }

    FAgenticLinkToolResult RunPython(const TSharedPtr<FJsonObject>& Arguments)
    {
        FString Script;
        if (!Arguments->TryGetStringField(TEXT("script"), Script) || Script.IsEmpty())
        {
            return FAgenticLinkToolResult::Failure(TEXT("Missing 'script': the Python to run."));
        }
        FString ModeText;
        Arguments->TryGetStringField(TEXT("mode"), ModeText);
        EAutonomixPythonMode Mode = EAutonomixPythonMode::File;
        if (!FAutonomixPython::ParseMode(ModeText, Mode))
        {
            return FAgenticLinkToolResult::Failure(FString::Printf(TEXT("'mode' is file, statement or evaluate, not '%s'."), *ModeText));
        }
        const FAutonomixPythonResult Ran = FAutonomixPython::Run(Script, Mode);
        if (!Ran.Error.IsEmpty())
        {
            return FAgenticLinkToolResult::Failure(Ran.Error);
        }

        TArray<TSharedPtr<FJsonValue>> Log;
        FString Errors;
        for (const FAutonomixPythonLogLine& Line : Ran.Log)
        {
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("type"), Line.Type);
            Entry->SetStringField(TEXT("output"), Line.Output);
            Log.Add(MakeShared<FJsonValueObject>(Entry));
            if (Line.Type == TEXT("Error"))
            {
                Errors += Line.Output + TEXT("\n");
            }
        }
        if (!Ran.bSuccess)
        {
            // A script that raised is the agent's to fix: its traceback and errors as the message.
            return FAgenticLinkToolResult::Failure(FString::Printf(TEXT("The script failed.\n%s\n%s"), *Ran.Result, *Errors).TrimEnd());
        }
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), true);
        Result->SetStringField(TEXT("result"), Ran.Result);
        Result->SetArrayField(TEXT("log"), Log);
        Result->SetBoolField(TEXT("undoable"), Ran.bUndoable);
        return FAgenticLinkToolResult::Success(Result);
    }
}

FAutonomixSwitches FAutonomixTools::ReadSwitches(const TCHAR* CommandLine)
{
    FAutonomixSwitches Switches;
#if !UE_BUILD_SHIPPING
    if (CommandLine)
    {
        Switches.bT3D = FParse::Param(CommandLine, TEXT("AutonomixT3D"));
        Switches.bPython = FParse::Param(CommandLine, TEXT("AutonomixPython"));
    }
#endif
    return Switches;
}

FName FAutonomixTools::GetProviderName()
{
    return FName(TEXT("Autonomix"));
}

void FAutonomixTools::Register(FAgenticLinkMcpServer& Server, const FAutonomixSwitches& Switches, TFunction<UWorld*()> WorldResolver)
{
#if !UE_BUILD_SHIPPING
    using namespace AutonomixToolsPrivate;

    if (Switches.bT3D)
    {
        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        Properties->SetObjectField(TEXT("t3d"), StringParam(TEXT("T3D text: Begin Actor Class=... Name=... blocks (as the editor copies actors). A block naming an actor the level has changes it; the rest spawn.")));
        FAgenticLinkTool Tool;
        Tool.Name = TEXT("import_t3d");
        Tool.Description = TEXT("Imports actors from T3D text into the open level (the PIE world while playing) as one undoable editor transaction: spawns new actors, changes existing ones, reports what couldn't be applied.");
        Tool.InputSchema = Schema(Properties, { TEXT("t3d") });
        Tool.Handler = [WorldResolver](const TSharedPtr<FJsonObject>& Arguments)
        {
            return ImportT3D(WorldResolver ? WorldResolver() : FAgenticLinkEngineTools::FindDefaultWorld(), Arguments);
        };
        Server.RegisterTool(Tool);
    }
    if (Switches.bPython)
    {
        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        Properties->SetObjectField(TEXT("script"), StringParam(TEXT("Python for the editor's interpreter (the unreal module is there).")));
        Properties->SetObjectField(TEXT("mode"), StringParam(TEXT("file (default: statements), statement (one), or evaluate (an expression; its value is the result).")));
        FAgenticLinkTool Tool;
        Tool.Name = TEXT("run_python");
        Tool.Description = TEXT("Runs Python in the editor (Python Editor Script Plugin) as one undoable editor transaction and returns its result and log; a script that raises returns its traceback as the error.");
        Tool.InputSchema = Schema(Properties, { TEXT("script") });
        Tool.Handler = [](const TSharedPtr<FJsonObject>& Arguments)
        {
            return RunPython(Arguments);
        };
        Server.RegisterTool(Tool);
    }
#endif
}
