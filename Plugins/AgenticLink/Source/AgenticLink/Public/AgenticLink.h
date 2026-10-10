#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"
#include "Modules/ModuleManager.h"
#include "AgenticLinkHttpTransport.h"
#include "AgenticLinkMcpServer.h"

/**
 * AgenticLink: the external-agent bridge (Epic 25). Started with -AgenticLinkMcp (port
 * FAgenticLinkHttpTransport::DefaultPort) or -AgenticLinkMcpPort=<port> on the editor's command
 * line, it serves the engine-reflection tools (FAgenticLinkEngineTools) over MCP at
 * http://127.0.0.1:<port>/mcp. Without either switch it only logs that it loaded.
 *
 * While the server listens, the module is registered as the modular feature named
 * GetBridgeFeatureName(). Game code tells the bridge is online from that name alone
 * (IModularFeatures::IsModularFeatureAvailable), with no link to this plugin: Epic 82's model
 * hooks are refused while it is absent.
 */
class FAgenticLinkModule : public IModuleInterface, public IModularFeature
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** "AgenticLinkBridge": the modular feature present while the MCP server listens. */
    static FName GetBridgeFeatureName();

private:
    TSharedPtr<FAgenticLinkMcpServer> McpServer;
    TUniquePtr<FAgenticLinkHttpTransport> Transport;
    bool bBridgeFeatureRegistered = false;
};
