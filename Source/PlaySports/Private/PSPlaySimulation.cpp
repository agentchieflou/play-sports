#include "PSPlaySimulation.h"

UPSPlaySimulation::UPSPlaySimulation()
{
    CurrentState.Phase = EPlayPhase::PreSnap;
    CurrentState.GameTimeSeconds = 0.0f;
    CurrentState.Down = 1;
    CurrentState.Distance = 10;
    CurrentPlayResult.YardsGained = 0;
    CurrentPlayResult.ResultType = EPlayResultType::Incomplete;
    PhaseTimer = 0.f;
}

void UPSPlaySimulation::InitializePlay(const TArray<FPlayerAttributes>& Offense, const TArray<FPlayerAttributes>& Defense)
{
    OffenseRoster = Offense;
    DefenseRoster = Defense;
    CurrentState.Phase = EPlayPhase::PreSnap;
    CurrentState.GameTimeSeconds = 0.0f;
    CurrentState.Down = 1;
    CurrentState.Distance = 10;
    CurrentPlayResult.YardsGained = 0;
    CurrentPlayResult.ResultType = EPlayResultType::Incomplete;
    PhaseTimer = 0.f;
}

void UPSPlaySimulation::TriggerSnap()
{
    if (CurrentState.Phase == EPlayPhase::PreSnap)
    {
        CurrentState.Phase = EPlayPhase::Snap;
        PhaseTimer = 0.f;
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Snap triggered. Phase transitioned to Snap."));
    }
}

void UPSPlaySimulation::SetPlayPhase(EPlayPhase NewPhase)
{
    CurrentState.Phase = NewPhase;
    PhaseTimer = 0.f;
    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Play phase overridden. Transited to: %s"), *UEnum::GetValueAsString(NewPhase));
}

void UPSPlaySimulation::RecordTackle(int32 YardsGained)
{
    CurrentPlayResult.ResultType = EPlayResultType::Tackle;
    CurrentPlayResult.YardsGained = YardsGained;
    SetPlayPhase(EPlayPhase::Scoring);
    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Tackle recorded. Yards Gained: %d"), YardsGained);
}

void UPSPlaySimulation::AdvancePlay(float DeltaSeconds)
{
    CurrentState.GameTimeSeconds += DeltaSeconds;
    PhaseTimer += DeltaSeconds;

    switch (CurrentState.Phase)
    {
    case EPlayPhase::PreSnap:
        // Do not advance automatically; wait for TriggerSnap()
        break;
    case EPlayPhase::Snap:
        if (PhaseTimer >= 0.5f) // Snap phase lasts 0.5 seconds
        {
            CurrentState.Phase = EPlayPhase::PassRush;
            PhaseTimer = 0.f;
        }
        break;
    case EPlayPhase::PassRush:
        if (PhaseTimer >= 2.0f) // PassRush phase lasts 2.0 seconds
        {
            CurrentState.Phase = EPlayPhase::BallCarrierMovement;
            PhaseTimer = 0.f;
        }
        break;
    case EPlayPhase::BallCarrierMovement:
        if (PhaseTimer >= 3.0f) // BallCarrierMovement phase lasts 3.0 seconds
        {
            ResolvePlayResult();
            CurrentState.Phase = EPlayPhase::Scoring;
            PhaseTimer = 0.f;
        }
        break;
    default:
        break;
    }
}

FPlayState UPSPlaySimulation::GetPlayState() const
{
    return CurrentState;
}

FPlayResult UPSPlaySimulation::GetPlayResult() const
{
    return CurrentPlayResult;
}

void UPSPlaySimulation::ResolvePlayResult()
{
    // Find representative players to resolve outcomes
    FPlayerAttributes Passer;
    FPlayerAttributes Receiver;
    FPlayerAttributes Defender;

    // Set fallback attributes in case rosters are empty
    Passer.Awareness = 80.0f;
    Receiver.Speed = 80.0f;
    Receiver.Agility = 80.0f;
    Defender.Speed = 80.0f;
    Defender.Agility = 80.0f;
    Defender.Awareness = 80.0f;

    for (const FPlayerAttributes& Player : OffenseRoster)
    {
        if (Player.Role == EPlayerRole::Quarterback)
        {
            Passer = Player;
        }
        else if (Player.Role == EPlayerRole::WideReceiver || Player.Role == EPlayerRole::TightEnd)
        {
            Receiver = Player;
        }
    }

    for (const FPlayerAttributes& Player : DefenseRoster)
    {
        if (Player.Role == EPlayerRole::DefensiveBack || Player.Role == EPlayerRole::Linebacker)
        {
            Defender = Player;
        }
    }

    // Resolve Pass Completion (Incomplete vs Complete)
    float CompletionChance = 0.60f + (Passer.Awareness + Receiver.Agility - Defender.Awareness - Defender.Agility) * 0.005f;
    CompletionChance = FMath::Clamp(CompletionChance, 0.10f, 0.95f);

    float RandomRoll = FMath::FRand();
    if (RandomRoll > CompletionChance)
    {
        CurrentPlayResult.ResultType = EPlayResultType::Incomplete;
        CurrentPlayResult.YardsGained = 0;
    }
    else
    {
        // Resolved as complete, calculate yards gained
        float BaseYards = 6.0f + (Receiver.Speed - Defender.Speed) * 0.25f;
        BaseYards += FMath::FRandRange(-4.0f, 16.0f);
        int32 Yards = FMath::Clamp(FMath::RoundToInt(BaseYards), -5, 99);

        CurrentPlayResult.YardsGained = Yards;

        // Determine if it was a Touchdown or a Tackle
        float TouchdownChance = 0.05f + (Receiver.Speed - Defender.Speed) * 0.01f + (Yards * 0.005f);
        TouchdownChance = FMath::Clamp(TouchdownChance, 0.0f, 0.85f);

        if (FMath::FRand() < TouchdownChance || Yards >= 50)
        {
            CurrentPlayResult.ResultType = EPlayResultType::Touchdown;
        }
        else
        {
            CurrentPlayResult.ResultType = EPlayResultType::Tackle;
        }
    }
}
