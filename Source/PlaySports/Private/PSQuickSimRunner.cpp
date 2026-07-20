#include "PSQuickSimRunner.h"

FPSQuickSimResult UPSQuickSimRunner::SimulateGame(const TArray<FPlayerAttributes>& HomeRoster, const TArray<FPlayerAttributes>& AwayRoster)
{
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    Sim->bQuickSimMode = true;
    Sim->InitializePlay(HomeRoster, AwayRoster);

    int32 TickCount = 0;
    while (TickCount < MaxPlaysPerGame)
    {
        const FPlayState State = Sim->GetPlayState();
        if (State.Quarter > 4)
        {
            break;
        }

        if (State.Phase == EPlayPhase::PreSnap)
        {
            Sim->TriggerSnap();
        }

        Sim->AdvancePlay(SecondsPerPlayAdvance);
        ++TickCount;
    }

    const FPlayState FinalState = Sim->GetPlayState();
    FPSQuickSimResult Result;
    Result.HomeScore = FinalState.HomeScore;
    Result.AwayScore = FinalState.AwayScore;
    return Result;
}
