#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "AgenticLinkHttpTransport.h"
#include "AgenticLinkMcpServer.h"

/**
 * AgenticLink: the external-agent bridge (Epic 25). Started with -AgenticLinkMcp (port
 * FAgenticLinkHttpTransport::DefaultPort) or -AgenticLinkMcpPort=<port> on the editor's command
 * line, it serves the engine-reflection tools (FAgenticLinkEngineTools) over MCP at
 * http://127.0.0.1:<port>/mcp. Without either switch it only logs that it loaded.
 */
class FAgenticLinkModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedPtr<FAgenticLinkMcpServer> McpServer;
    TUniquePtr<FAgenticLinkHttpTransport> Transport;
};
