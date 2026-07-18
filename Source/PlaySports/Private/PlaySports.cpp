#include "PlaySports.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FPlaySportsModule"

void FPlaySportsModule::StartupModule()
{
    UE_LOG(LogTemp, Display, TEXT("PlaySports module started."));
}

void FPlaySportsModule::ShutdownModule()
{
    UE_LOG(LogTemp, Display, TEXT("PlaySports module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_PRIMARY_GAME_MODULE(FPlaySportsModule, PlaySports, "PlaySports");
