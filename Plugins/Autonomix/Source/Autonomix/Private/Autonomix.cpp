#include "Autonomix.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FAutonomixModule"

void FAutonomixModule::StartupModule()
{
    UE_LOG(LogTemp, Display, TEXT("Autonomix module started."));
}

void FAutonomixModule::ShutdownModule()
{
    UE_LOG(LogTemp, Display, TEXT("Autonomix module shut down."));
}

#undef LOCTEXT_NAMESPACE
