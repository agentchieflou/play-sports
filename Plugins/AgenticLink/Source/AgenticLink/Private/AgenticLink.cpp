#include "AgenticLink.h"
#include "AgenticLinkEngineTools.h"
#include "AgenticLinkToolProviders.h"
#include "Features/IModularFeatures.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FAgenticLinkModule"

FName FAgenticLinkModule::GetBridgeFeatureName()
{
    return FName(TEXT("AgenticLinkBridge"));
}

void FAgenticLinkModule::StartupModule()
{
    UE_LOG(LogTemp, Display, TEXT("AgenticLink module started."));

    uint32 Port = FAgenticLinkHttpTransport::DefaultPort;
    const bool bPortGiven = FParse::Value(FCommandLine::Get(), TEXT("AgenticLinkMcpPort="), Port);
    if (!bPortGiven && !FParse::Param(FCommandLine::Get(), TEXT("AgenticLinkMcp")))
    {
        return;
    }

    McpServer = MakeShared<FAgenticLinkMcpServer>();
    FAgenticLinkEngineTools::Register(*McpServer);
    // Other modules' tools (Autonomix's, opt-in by their own switches) join before anyone connects.
    FAgenticLinkToolProviders::RegisterAll(*McpServer);
    Transport = MakeUnique<FAgenticLinkHttpTransport>(McpServer.ToSharedRef());
    if (!Transport->Start(Port))
    {
        Transport.Reset();
        return;
    }
    FAgenticLinkToolProviders::SetServingServer(McpServer);

    // The bridge is online: game code gates its model hooks on this name (Epic 82).
    IModularFeatures::Get().RegisterModularFeature(GetBridgeFeatureName(), this);
    bBridgeFeatureRegistered = true;
}

void FAgenticLinkModule::ShutdownModule()
{
    if (bBridgeFeatureRegistered)
    {
        IModularFeatures::Get().UnregisterModularFeature(GetBridgeFeatureName(), this);
        bBridgeFeatureRegistered = false;
    }
    if (McpServer.IsValid() && FAgenticLinkToolProviders::GetServingServer() == McpServer)
    {
        FAgenticLinkToolProviders::SetServingServer(nullptr);
    }
    Transport.Reset();
    McpServer.Reset();
    UE_LOG(LogTemp, Display, TEXT("AgenticLink module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAgenticLinkModule, AgenticLink)
