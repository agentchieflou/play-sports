#include "PSFunctionalGym.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "PSGameMode.h"

APSFunctionalGym::APSFunctionalGym()
{
    bIsEnabled = true;
    InitialPhase = EPlayPhase::PreSnap;
}

void APSFunctionalGym::BeginPlay()
{
    Super::BeginPlay();
    RunBlockSimulationTest();
}

void APSFunctionalGym::RunBlockSimulationTest()
{
    UE_LOG(LogTemp, Display, TEXT("Running block simulation test gym."));

    APSGameMode* GameMode = Cast<APSGameMode>(UGameplayStatics::GetGameMode(this));
    if (!AssertIsValid(GameMode, TEXT("APSGameMode not found in the gym level")))
    {
        return;
    }
    if (!AssertIsValid(GameMode->PlaySimulation, TEXT("APSGameMode::PlaySimulation was not initialized")))
    {
        FinishTest(EFunctionalTestResult::Failed, TEXT("PlaySimulation not initialized."));
        return;
    }

    InitialPhase = GameMode->PlaySimulation->GetPlayState().Phase;
    AssertTrue(InitialPhase == EPlayPhase::PreSnap, TEXT("Gym starts in PreSnap before the snap"));

    // Drive the outcome deterministically via the quick-sim statistical resolver
    // (Epic C2) instead of depending on a real ball-catch overlap firing, which the
    // gym level's actor placement can't guarantee.
    GameMode->PlaySimulation->bQuickSimMode = true;
    GameMode->ExecuteSnap();

    // Snap->PassRush at 0.5s, PassRush->BallCarrierMovement at +2.0s, BallCarrierMovement
    // ->Scoring at +3.0s (see UPSPlaySimulation::AdvancePlay) = 5.5s minimum; give it a
    // generous margin for frame-timing jitter.
    GetWorldTimerManager().SetTimer(CheckProgressTimerHandle, this, &APSFunctionalGym::CheckPlayProgress, 7.0f, false);
}

void APSFunctionalGym::CheckPlayProgress()
{
    APSGameMode* GameMode = Cast<APSGameMode>(UGameplayStatics::GetGameMode(this));
    if (!GameMode || !GameMode->PlaySimulation)
    {
        FinishTest(EFunctionalTestResult::Failed, TEXT("GameMode/PlaySimulation missing during progress check."));
        return;
    }

    const EPlayPhase CurrentPhase = GameMode->PlaySimulation->GetPlayState().Phase;
    const bool bPhaseAdvanced = AssertTrue(CurrentPhase != InitialPhase, TEXT("Phase advanced past PreSnap after the snap"));
    const bool bReachedScoring = AssertTrue(CurrentPhase == EPlayPhase::Scoring, TEXT("Quick-sim play reached the Scoring phase within the time limit"));

    if (bPhaseAdvanced && bReachedScoring)
    {
        FinishTest(EFunctionalTestResult::Succeeded, TEXT("Block simulation gym: play progressed through phases and reached Scoring."));
    }
    else
    {
        FinishTest(EFunctionalTestResult::Failed, TEXT("Block simulation gym: play did not progress/score as expected."));
    }
}
