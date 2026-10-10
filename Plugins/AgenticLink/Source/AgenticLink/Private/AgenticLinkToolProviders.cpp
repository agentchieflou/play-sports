// AgenticLinkToolProviders.cpp - Epic 25: other modules' tools on AgenticLink's one MCP server
#include "AgenticLinkToolProviders.h"

namespace AgenticLinkProviders
{
    TMap<FName, FAgenticLinkToolProviders::FProvider>& Providers()
    {
        static TMap<FName, FAgenticLinkToolProviders::FProvider> Registered;
        return Registered;
    }

    /** The tools each provider added to the serving server. */
    TMap<FName, TArray<FString>>& ProvidedTools()
    {
        static TMap<FName, TArray<FString>> Tools;
        return Tools;
    }

    TWeakPtr<FAgenticLinkMcpServer>& Serving()
    {
        static TWeakPtr<FAgenticLinkMcpServer> Server;
        return Server;
    }

    TSet<FString> ToolNames(const FAgenticLinkMcpServer& Server)
    {
        TSet<FString> Names;
        for (const FAgenticLinkTool& Tool : Server.GetTools())
        {
            Names.Add(Tool.Name);
        }
        return Names;
    }
}

void FAgenticLinkToolProviders::RunProvider(FName ProviderName, const FProvider& Provider, FAgenticLinkMcpServer& Server)
{
    using namespace AgenticLinkProviders;

    const TSet<FString> Before = ToolNames(Server);
    Provider(Server);
    TArray<FString>& Added = ProvidedTools().FindOrAdd(ProviderName);
    for (const FAgenticLinkTool& Tool : Server.GetTools())
    {
        if (!Before.Contains(Tool.Name))
        {
            Added.AddUnique(Tool.Name);
        }
    }
}

void FAgenticLinkToolProviders::Add(FName ProviderName, FProvider Provider)
{
    using namespace AgenticLinkProviders;

    Remove(ProviderName);
    Providers().Add(ProviderName, Provider);
    if (const TSharedPtr<FAgenticLinkMcpServer> Server = Serving().Pin())
    {
        RunProvider(ProviderName, Provider, *Server);
    }
}

void FAgenticLinkToolProviders::Remove(FName ProviderName)
{
    using namespace AgenticLinkProviders;

    Providers().Remove(ProviderName);
    TArray<FString> Added;
    ProvidedTools().RemoveAndCopyValue(ProviderName, Added);
    if (const TSharedPtr<FAgenticLinkMcpServer> Server = Serving().Pin())
    {
        for (const FString& ToolName : Added)
        {
            Server->UnregisterTool(ToolName);
        }
    }
}

bool FAgenticLinkToolProviders::Has(FName ProviderName)
{
    return AgenticLinkProviders::Providers().Contains(ProviderName);
}

void FAgenticLinkToolProviders::RegisterAll(FAgenticLinkMcpServer& Server)
{
    using namespace AgenticLinkProviders;

    ProvidedTools().Reset();
    for (const TPair<FName, FProvider>& Entry : Providers())
    {
        RunProvider(Entry.Key, Entry.Value, Server);
    }
}

void FAgenticLinkToolProviders::SetServingServer(const TSharedPtr<FAgenticLinkMcpServer>& Server)
{
    AgenticLinkProviders::Serving() = Server;
}

TSharedPtr<FAgenticLinkMcpServer> FAgenticLinkToolProviders::GetServingServer()
{
    return AgenticLinkProviders::Serving().Pin();
}
