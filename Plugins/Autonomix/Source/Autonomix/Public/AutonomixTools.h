// AutonomixTools.h - Core 25.1/25.2: Autonomix's tools on AgenticLink's MCP server
#pragma once

#include "CoreMinimal.h"

class FAgenticLinkMcpServer;
class UWorld;

/** Which Autonomix tools are on. Both are off unless their switch is on the command line. */
struct AUTONOMIX_API FAutonomixSwitches
{
    /** -AutonomixT3D: import_t3d. */
    bool bT3D = false;

    /** -AutonomixPython: run_python. */
    bool bPython = false;

    bool Any() const { return bT3D || bPython; }
};

/**
 * Autonomix's agent tools, served by AgenticLink's one MCP server (it never opens its own):
 *
 *  - import_t3d {t3d}                 FAutonomixT3D::Import into the open level (the PIE world
 *                                     while playing): spawned and changed actors, warnings,
 *                                     undoable. Switch: -AutonomixT3D.
 *  - run_python {script, mode?}       FAutonomixPython::Run (mode file, statement or evaluate):
 *                                     success, result and log. Switch: -AutonomixPython.
 *
 * Each is opt-in by its own switch, on top of AgenticLink's -AgenticLinkMcp (localhost only), and
 * none exists in a shipping build (Autonomix is a Developer module; the code is compiled out).
 */
class AUTONOMIX_API FAutonomixTools
{
public:
    /** The switches CommandLine turns on. */
    static FAutonomixSwitches ReadSwitches(const TCHAR* CommandLine);

    /** Registers the tools Switches turns on. WorldResolver picks import_t3d's world per call; an
     *  empty one means AgenticLink's default world. */
    static void Register(FAgenticLinkMcpServer& Server, const FAutonomixSwitches& Switches, TFunction<UWorld*()> WorldResolver = nullptr);

    /** The name Autonomix provides its tools under (FAgenticLinkToolProviders). */
    static FName GetProviderName();
};
