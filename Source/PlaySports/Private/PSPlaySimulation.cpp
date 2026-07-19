#include "PSPlaySimulation.h"
#include "PSGameMode.h"
#include "PSPlayerPawn.h"
#include "PSBall.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/FloatingPawnMovement.h"

UPSPlaySimulation::UPSPlaySimulation()
{
    CurrentState.Phase = EPlayPhase::PreSnap;
    CurrentState.GameTimeSeconds = 0.0f;
    CurrentState.Down = 1;
    CurrentState.Distance = 10;
    CurrentState.YardLine = 20;
    CurrentState.YardLineToGain = 30;
    CurrentState.Quarter = 1;
    CurrentState.GameClockSeconds = 900.f;
    CurrentState.PlayClockSeconds = 40.f;
    CurrentState.bHomeHasPossession = true;
    CurrentState.HomeScore = 0;
    CurrentState.AwayScore = 0;
    CurrentPlayResult.YardsGained = 0;
    CurrentPlayResult.ResultType = EPlayResultType::Incomplete;
    ActivePenalty = EPSPenaltyType::None;
    bPenaltyDeclined = false;
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
    CurrentState.YardLine = 20;
    CurrentState.YardLineToGain = 30;
    CurrentState.Quarter = 1;
    CurrentState.GameClockSeconds = 900.f;
    CurrentState.PlayClockSeconds = 40.f;
    CurrentState.bHomeHasPossession = true;
    CurrentState.HomeScore = 0;
    CurrentState.AwayScore = 0;
    CurrentPlayResult.YardsGained = 0;
    CurrentPlayResult.ResultType = EPlayResultType::Incomplete;
    ActivePenalty = EPSPenaltyType::None;
    bPenaltyDeclined = false;
    PhaseTimer = 0.f;
}

void UPSPlaySimulation::TriggerSnap()
{
    if (CurrentState.Phase == EPlayPhase::PreSnap)
    {
        if (FMath::FRand() < 0.05f)
        {
            ActivePenalty = EPSPenaltyType::Offsides;
            bPenaltyDeclined = false;
            UE_LOG(LogTemp, Warning, TEXT("UPSPlaySimulation: FLAG! Offsides penalty called at the snap!"));
        }

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

    if (CurrentState.Phase == EPlayPhase::PreSnap)
    {
        CurrentState.PlayClockSeconds -= DeltaSeconds;
        if (CurrentState.PlayClockSeconds <= 0.f)
        {
            CurrentState.PlayClockSeconds = 40.f;
            CurrentState.YardLine = FMath::Max(1, CurrentState.YardLine - 5);
            CurrentState.Distance = CurrentState.YardLineToGain - CurrentState.YardLine;
            UE_LOG(LogTemp, Warning, TEXT("UPSPlaySimulation: DELAY OF GAME penalty! 5 yards loss."));
        }
    }
    else
    {
        if (ActivePenalty == EPSPenaltyType::None && FMath::FRand() < 0.03f * DeltaSeconds)
        {
            ActivePenalty = EPSPenaltyType::Holding;
            bPenaltyDeclined = false;
            UE_LOG(LogTemp, Warning, TEXT("UPSPlaySimulation: FLAG! Offensive Holding penalty called during play!"));
        }

        CurrentState.GameClockSeconds -= DeltaSeconds;
        if (CurrentState.GameClockSeconds <= 0.f)
        {
            CurrentState.GameClockSeconds = 900.f;
            CurrentState.Quarter++;
            if (CurrentState.Quarter > 4)
            {
                UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: GAME OVER! Final Score: Home %d - Away %d"), 
                    CurrentState.HomeScore, CurrentState.AwayScore);
            }
            else
            {
                UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: End of Quarter. Transitioning to Quarter %d"), CurrentState.Quarter);
            }
        }
    }

    switch (CurrentState.Phase)
    {
    case EPlayPhase::PreSnap:
        break;
    case EPlayPhase::Snap:
        if (PhaseTimer >= 0.5f)
        {
            CurrentState.Phase = EPlayPhase::PassRush;
            PhaseTimer = 0.f;
        }
        break;
    case EPlayPhase::PassRush:
        if (PhaseTimer >= 2.0f)
        {
            CurrentState.Phase = EPlayPhase::BallCarrierMovement;
            PhaseTimer = 0.f;
        }
        break;
    case EPlayPhase::BallCarrierMovement:
        if (PhaseTimer >= 3.0f)
        {
            ResolvePlayResult();
            CurrentState.Phase = EPlayPhase::Scoring;
            PhaseTimer = 0.f;
        }
        break;
    case EPlayPhase::Scoring:
        if (PhaseTimer >= 5.0f)
        {
            EndPlayAndPrepareNext();
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

void UPSPlaySimulation::EndPlayAndPrepareNext()
{
    // 1. Safety detection
    if (CurrentPlayResult.ResultType == EPlayResultType::Tackle && CurrentState.YardLine + CurrentPlayResult.YardsGained <= 0)
    {
        CurrentPlayResult.ResultType = EPlayResultType::Safety;
        CurrentPlayResult.YardsGained = -CurrentState.YardLine; // Loss to the goal line
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: SAFETY DETECTED in the end zone!"));
    }

    // 2. Penalty Accept/Decline Resolution
    if (ActivePenalty != EPSPenaltyType::None)
    {
        if (ActivePenalty == EPSPenaltyType::Offsides)
        {
            if (CurrentPlayResult.YardsGained < 5)
            {
                CurrentPlayResult.YardsGained = 5;
                CurrentPlayResult.ResultType = EPlayResultType::Tackle;
                CurrentState.Down = FMath::Max(1, CurrentState.Down - 1);
                UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Offsides penalty ACCEPTED (5 yards, repeat down)."));
            }
            else
            {
                UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Offsides penalty DECLINED. Result stands."));
            }
        }
        else if (ActivePenalty == EPSPenaltyType::Holding)
        {
            if (CurrentPlayResult.YardsGained > 0 || CurrentPlayResult.ResultType == EPlayResultType::Touchdown)
            {
                CurrentPlayResult.YardsGained = -10;
                CurrentPlayResult.ResultType = EPlayResultType::Tackle;
                CurrentState.Down = FMath::Max(1, CurrentState.Down - 1);
                UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Holding penalty ACCEPTED (10 yards, repeat down)."));
            }
            else
            {
                UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Holding penalty DECLINED. Result stands."));
            }
        }

        ActivePenalty = EPSPenaltyType::None;
    }

    // Update drive summary tracking
    CurrentDriveSummary.Plays++;
    CurrentDriveSummary.Yards += CurrentPlayResult.YardsGained;

    // Runoff play game clock
    if (CurrentPlayResult.ResultType == EPlayResultType::Tackle)
    {
        CurrentState.GameClockSeconds -= 30.f; // Runoff 30 seconds on tackled plays
    }
    
    // Play clock resets to 40
    CurrentState.PlayClockSeconds = 40.f;

    // Update yard line
    CurrentState.YardLine += CurrentPlayResult.YardsGained;

    if (CurrentState.YardLine >= 100)
    {
        CurrentState.YardLine = 100;
        CurrentPlayResult.ResultType = EPlayResultType::Touchdown;
    }
    else if (CurrentState.YardLine <= 0)
    {
        CurrentState.YardLine = 1;
    }

    bool bTurnover = false;

    // Touchdown Score Tracking
    if (CurrentPlayResult.ResultType == EPlayResultType::Touchdown)
    {
        int32 TouchdownPoints = 6;
        int32 PatPoints = 0;

        if (FMath::FRand() < 0.94f)
        {
            PatPoints = 1;
            UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: PAT kick is GOOD!"));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSPlaySimulation: PAT kick is MISSED!"));
        }

        int32 TotalPoints = TouchdownPoints + PatPoints;

        if (CurrentState.bHomeHasPossession)
        {
            CurrentState.HomeScore += TotalPoints;
        }
        else
        {
            CurrentState.AwayScore += TotalPoints;
        }

        CurrentDriveSummary.Result = TEXT("Touchdown");
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: TOUCHDOWN! Score: Home %d - Away %d. Drive resets."), 
            CurrentState.HomeScore, CurrentState.AwayScore);

        CurrentState.YardLine = 20;
        CurrentState.Down = 1;
        CurrentState.Distance = 10;
        CurrentState.YardLineToGain = 30;
        bTurnover = true;
    }
    // Safety Score Tracking
    else if (CurrentPlayResult.ResultType == EPlayResultType::Safety)
    {
        if (CurrentState.bHomeHasPossession)
        {
            CurrentState.AwayScore += 2;
        }
        else
        {
            CurrentState.HomeScore += 2;
        }

        CurrentDriveSummary.Result = TEXT("Safety");
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: SAFETY! Score: Home %d - Away %d. Drive resets via kickoff."), 
            CurrentState.HomeScore, CurrentState.AwayScore);

        CurrentState.YardLine = 20;
        CurrentState.Down = 1;
        CurrentState.Distance = 10;
        CurrentState.YardLineToGain = 30;
        bTurnover = true;
    }
    else
    {
        // First down calculations
        if (CurrentState.YardLine >= CurrentState.YardLineToGain)
        {
            UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: FIRST DOWN! YardLine: %d, GainTarget: %d"), CurrentState.YardLine, CurrentState.YardLineToGain);
            CurrentState.Down = 1;
            CurrentState.Distance = 10;
            CurrentState.YardLineToGain = CurrentState.YardLine + 10;
        }
        else
        {
            CurrentState.Down++;
            if (CurrentState.Down > 4)
            {
                CurrentDriveSummary.Result = TEXT("Turnover on Downs");
                UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: TURNOVER ON DOWNS!"));
                bTurnover = true;
            }
            else
            {
                CurrentState.Distance = CurrentState.YardLineToGain - CurrentState.YardLine;
            }
        }
    }

    if (bTurnover)
    {
        // Log final drive summary before reset
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Drive complete. Plays: %d, Yards: %d, Result: %s"), 
            CurrentDriveSummary.Plays, CurrentDriveSummary.Yards, *CurrentDriveSummary.Result);
            
        // Reset drive summary for next drive
        CurrentDriveSummary.Plays = 0;
        CurrentDriveSummary.Yards = 0;
        CurrentDriveSummary.Result = TEXT("");

        TArray<FPlayerAttributes> Temp = OffenseRoster;
        OffenseRoster = DefenseRoster;
        DefenseRoster = Temp;

        CurrentState.bHomeHasPossession = !CurrentState.bHomeHasPossession;

        CurrentState.YardLine = 100 - CurrentState.YardLine;
        CurrentState.YardLine = FMath::Clamp(CurrentState.YardLine, 1, 99);
        CurrentState.Down = 1;
        CurrentState.Distance = 10;
        CurrentState.YardLineToGain = CurrentState.YardLine + 10;
    }

    CurrentState.YardLineToGain = FMath::Min(CurrentState.YardLineToGain, 100);
    CurrentState.Distance = CurrentState.YardLineToGain - CurrentState.YardLine;

    SetPlayPhase(EPlayPhase::PreSnap);
    CurrentPlayResult.YardsGained = 0;
    CurrentPlayResult.ResultType = EPlayResultType::Incomplete;

    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Play resolved. New State: Down %d, Distance %d, YardLine %d, YardToGain %d"), 
        CurrentState.Down, CurrentState.Distance, CurrentState.YardLine, CurrentState.YardLineToGain);

    // Notify GameMode to reset physical pawns at new line of scrimmage
    APSGameMode* GM = Cast<APSGameMode>(GetOuter());
    if (!GM)
    {
        GM = Cast<APSGameMode>(GetWorld()->GetAuthGameMode());
    }
    if (GM)
    {
        GM->ResetPawnPositions();
    }
}

void UPSPlaySimulation::RecordTouchdown()
{
    CurrentPlayResult.ResultType = EPlayResultType::Touchdown;
    CurrentPlayResult.YardsGained = 100;
    SetPlayPhase(EPlayPhase::Scoring);
    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Touchdown recorded."));
}
