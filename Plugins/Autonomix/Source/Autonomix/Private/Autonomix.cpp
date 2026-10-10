#include "Autonomix.h"
#include "AgenticLinkToolProviders.h"
#include "AutonomixTools.h"
#include "Misc/CommandLine.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FAutonomixModule"

void FAutonomixModule::StartupModule()
{
    UE_LOG(LogTemp, Display, TEXT("Autonomix module started."));

#if !UE_BUILD_SHIPPING
    // Opt-in per tool; AgenticLink serves them (localhost only) when it serves at all.
    const FAutonomixSwitches Switches = FAutonomixTools::ReadSwitches(FCommandLine::Get());
    if (Switches.Any())
    {
        FAgenticLinkToolProviders::Add(FAutonomixTools::GetProviderName(), [Switches](FAgenticLinkMcpServer& Server)
        {
            FAutonomixTools::Register(Server, Switches);
        });
        bProvidingTools = true;
        UE_LOG(LogTemp, Display, TEXT("Autonomix: agent tools on (T3D import: %s, Python: %s); AgenticLink serves them with -AgenticLinkMcp."),
            Switches.bT3D ? TEXT("yes") : TEXT("no"), Switches.bPython ? TEXT("yes") : TEXT("no"));
    }
#endif
}

void FAutonomixModule::ShutdownModule()
{
    if (bProvidingTools)
    {
        FAgenticLinkToolProviders::Remove(FAutonomixTools::GetProviderName());
        bProvidingTools = false;
    }
    UE_LOG(LogTemp, Display, TEXT("Autonomix module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAutonomixModule, Autonomix)
