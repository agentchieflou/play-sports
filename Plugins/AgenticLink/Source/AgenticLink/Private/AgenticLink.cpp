#include "AgenticLink.h"
#include "AgenticLinkEngineTools.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FAgenticLinkModule"

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
    Transport = MakeUnique<FAgenticLinkHttpTransport>(McpServer.ToSharedRef());
    if (!Transport->Start(Port))
    {
        Transport.Reset();
    }
}

void FAgenticLinkModule::ShutdownModule()
{
    Transport.Reset();
    McpServer.Reset();
    UE_LOG(LogTemp, Display, TEXT("AgenticLink module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAgenticLinkModule, AgenticLink)
