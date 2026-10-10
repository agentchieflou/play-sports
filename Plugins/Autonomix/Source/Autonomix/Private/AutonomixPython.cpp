// AutonomixPython.cpp - Core 25.2: agent-supplied Python run in the editor, with what it said
#include "AutonomixPython.h"
#include "IPythonScriptPlugin.h"
#include "PythonScriptTypes.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

bool FAutonomixPython::IsAvailable()
{
    const IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
    return Python && Python->IsPythonAvailable() && Python->IsPythonInitialized();
}

bool FAutonomixPython::ParseMode(const FString& Text, EAutonomixPythonMode& OutMode)
{
    if (Text.IsEmpty() || Text.Equals(TEXT("file"), ESearchCase::IgnoreCase))
    {
        OutMode = EAutonomixPythonMode::File;
        return true;
    }
    if (Text.Equals(TEXT("statement"), ESearchCase::IgnoreCase))
    {
        OutMode = EAutonomixPythonMode::Statement;
        return true;
    }
    if (Text.Equals(TEXT("evaluate"), ESearchCase::IgnoreCase))
    {
        OutMode = EAutonomixPythonMode::Evaluate;
        return true;
    }
    return false;
}

FAutonomixPythonResult FAutonomixPython::Run(const FString& Script, EAutonomixPythonMode Mode)
{
    FAutonomixPythonResult Result;
    if (Script.TrimStartAndEnd().IsEmpty())
    {
        Result.Error = TEXT("The script is empty.");
        return Result;
    }
    IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
    if (!Python || !Python->IsPythonAvailable())
    {
        Result.Error = TEXT("Python isn't available: enable the Python Editor Script Plugin for this project and restart the editor.");
        return Result;
    }
    if (!Python->IsPythonInitialized())
    {
        Result.Error = TEXT("Python hasn't finished starting; retry in a moment.");
        return Result;
    }

    FPythonCommandEx Command;
    Command.Command = Script;
    Command.ExecutionMode = Mode == EAutonomixPythonMode::Evaluate ? EPythonCommandExecutionMode::EvaluateStatement
        : Mode == EAutonomixPythonMode::Statement ? EPythonCommandExecutionMode::ExecuteStatement
        : EPythonCommandExecutionMode::ExecuteFile;
    Command.FileExecutionScope = EPythonFileExecutionScope::Private;
    Command.Flags = EPythonCommandFlags::Unattended;
    {
#if WITH_EDITOR
        FScopedTransaction Transaction(TEXT("Autonomix"), FText::FromString(TEXT("Agent runs Python")), nullptr);
#endif
        Result.bSuccess = Python->ExecPythonCommandEx(Command);
#if WITH_EDITOR
        Result.bUndoable = Transaction.IsOutstanding();
#endif
    }

    Result.Result = Command.CommandResult;
    for (const FPythonLogOutputEntry& Entry : Command.LogOutput)
    {
        FAutonomixPythonLogLine& Line = Result.Log.AddDefaulted_GetRef();
        Line.Type = Entry.Type == EPythonLogOutputType::Error ? TEXT("Error")
            : Entry.Type == EPythonLogOutputType::Warning ? TEXT("Warning") : TEXT("Info");
        Line.Output = Entry.Output;
    }
    return Result;
}
