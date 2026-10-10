// AutonomixPython.h - Core 25.2: agent-supplied Python run in the editor, with what it said
#pragma once

#include "CoreMinimal.h"

/** How a script is run: as a file of statements, one statement, or an expression whose value is
 *  the result. */
enum class EAutonomixPythonMode : uint8
{
    File,
    Statement,
    Evaluate
};

/** One line the script logged (print, unreal.log, an error). */
struct AUTONOMIX_API FAutonomixPythonLogLine
{
    /** "Info", "Warning" or "Error". */
    FString Type;
    FString Output;
};

/** What a run did. */
struct AUTONOMIX_API FAutonomixPythonResult
{
    /** False when the script raised, or Python couldn't run it (Error says why). */
    bool bSuccess = false;

    /** The expression's value (Evaluate), or the traceback when the script raised. */
    FString Result;

    TArray<FAutonomixPythonLogLine> Log;

    /** Set when the script couldn't be run at all (no Python, an empty script). */
    FString Error;

    /** True when the run is one editor transaction (Ctrl+Z undoes what it changed). */
    bool bUndoable = false;
};

/**
 * FAutonomixPython runs an agent's Python in the editor through the Python Editor Script Plugin
 * (IPythonScriptPlugin) and captures its result and log (Core 25.2). The run is one editor
 * transaction. Without the plugin, or before Python has initialized, nothing runs and Error says
 * so.
 */
class AUTONOMIX_API FAutonomixPython
{
public:
    /** True when the Python plugin is loaded and its interpreter is up. */
    static bool IsAvailable();

    static FAutonomixPythonResult Run(const FString& Script, EAutonomixPythonMode Mode);

    /** "file", "statement" or "evaluate" (any case); false for anything else. */
    static bool ParseMode(const FString& Text, EAutonomixPythonMode& OutMode);
};
