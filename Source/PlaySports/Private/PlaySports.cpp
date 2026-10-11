#include "PlaySports.h"
#include "PSPackagedSmokeTest.h"
#include "PSRenderCapture.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FPlaySportsModule"

void FPlaySportsModule::StartupModule()
{
    UE_LOG(LogTemp, Display, TEXT("PlaySports module started."));

    // Epic 145.3: -PSSmokeTest plays a scripted full game once the engine is up, then exits.
    UPSPackagedSmokeTest::RegisterCommandLineHook();

    // Lane V1: -PSRenderCapture renders the game scene from fixed views to PNGs, then exits.
    UPSRenderCapture::RegisterCommandLineHook();
}

void FPlaySportsModule::ShutdownModule()
{
    UE_LOG(LogTemp, Display, TEXT("PlaySports module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_PRIMARY_GAME_MODULE(FPlaySportsModule, PlaySports, "PlaySports");
