// AgenticLinkToolProviders.h - Epic 25: other modules' tools on AgenticLink's one MCP server
#pragma once

#include "CoreMinimal.h"
#include "AgenticLinkMcpServer.h"

/**
 * FAgenticLinkToolProviders lets another module serve its tools through AgenticLink's MCP server
 * instead of opening a second one (Autonomix's T3D import and Python escape hatch, Core 25.1 and
 * 25.2). A provider is a function that registers tools on a server:
 *
 *  - Add: the provider registers its tools on the server serving now, if one is, and on every
 *    server that starts serving after.
 *  - Remove: its tools come off the serving server (a module removes its provider before it
 *    unloads, so no handler outlives its code).
 *
 * The AgenticLink module runs the providers on its server before it starts listening
 * (RegisterAll) and names it the serving server (SetServingServer) while it listens. Everything
 * runs on the game thread.
 */
class AGENTICLINK_API FAgenticLinkToolProviders
{
public:
    using FProvider = TFunction<void(FAgenticLinkMcpServer& /* Server */)>;

    static void Add(FName ProviderName, FProvider Provider);

    static void Remove(FName ProviderName);

    static bool Has(FName ProviderName);

    /** Runs every provider on Server. */
    static void RegisterAll(FAgenticLinkMcpServer& Server);

    /** The server serving now (null while none is). */
    static void SetServingServer(const TSharedPtr<FAgenticLinkMcpServer>& Server);

    static TSharedPtr<FAgenticLinkMcpServer> GetServingServer();

private:
    /** Runs one provider on Server, noting the tools it added. */
    static void RunProvider(FName ProviderName, const FProvider& Provider, FAgenticLinkMcpServer& Server);
};
