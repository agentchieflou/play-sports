#include "PSFunctionalGym.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

APSFunctionalGym::APSFunctionalGym()
{
    bIsEnabled = true;
}

void APSFunctionalGym::BeginPlay()
{
    Super::BeginPlay();
    RunBlockSimulationTest();
}

void APSFunctionalGym::RunBlockSimulationTest()
{
    UE_LOG(LogTemp, Display, TEXT("Running block simulation test gym."));
    FinishTest(EFunctionalTestResult::Succeeded, TEXT("Block simulation gym completed."));
}
