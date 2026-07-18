#include "PSPlaySimulation.h"

UPSPlaySimulation::UPSPlaySimulation()
{
    CurrentState.Phase = EPlayPhase::PreSnap;
    CurrentState.GameTimeSeconds = 0.0f;
    CurrentState.Down = 1;
    CurrentState.Distance = 10;
}

void UPSPlaySimulation::InitializePlay(const TArray<FPlayerAttributes>& Offense, const TArray<FPlayerAttributes>& Defense)
{
    OffenseRoster = Offense;
    DefenseRoster = Defense;
    CurrentState.Phase = EPlayPhase::PreSnap;
    CurrentState.GameTimeSeconds = 0.0f;
    CurrentState.Down = 1;
    CurrentState.Distance = 10;
}

void UPSPlaySimulation::AdvancePlay(float DeltaSeconds)
{
    CurrentState.GameTimeSeconds += DeltaSeconds;

    switch (CurrentState.Phase)
    {
    case EPlayPhase::PreSnap:
        CurrentState.Phase = EPlayPhase::Snap;
        break;
    case EPlayPhase::Snap:
        CurrentState.Phase = EPlayPhase::PassRush;
        break;
    case EPlayPhase::PassRush:
        CurrentState.Phase = EPlayPhase::BallCarrierMovement;
        break;
    case EPlayPhase::BallCarrierMovement:
        CurrentState.Phase = EPlayPhase::Scoring;
        break;
    default:
        break;
    }
}

FPlayState UPSPlaySimulation::GetPlayState() const
{
    return CurrentState;
}
