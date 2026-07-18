#include "AgenticLink.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FAgenticLinkModule"

void FAgenticLinkModule::StartupModule()
{
    UE_LOG(LogTemp, Display, TEXT("AgenticLink module started."));
}

void FAgenticLinkModule::ShutdownModule()
{
    UE_LOG(LogTemp, Display, TEXT("AgenticLink module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAgenticLinkModule, AgenticLink)
