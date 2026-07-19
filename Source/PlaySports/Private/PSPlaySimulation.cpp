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
    CurrentState.bIsClockRunning = false;
    CurrentPlayResult.YardsGained = 0;
    CurrentPlayResult.ResultType = EPlayResultType::Incomplete;
    ActivePenalty = EPSPenaltyType::None;
    bPenaltyDeclined = false;
    PhaseTimer = 0.f;
    bQuickSimMode = false;
    CachedWorld = nullptr;
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
    CurrentState.bIsClockRunning = false;
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
        CurrentState.bIsClockRunning = true;
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

    bool bShouldTickGameClock = (CurrentState.Phase != EPlayPhase::PreSnap) || CurrentState.bIsClockRunning;
    if (bShouldTickGameClock && CurrentState.Phase != EPlayPhase::Scoring)
    {
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

    if (CurrentState.Phase == EPlayPhase::PreSnap)
    {
        CurrentState.PlayClockSeconds -= DeltaSeconds;
        if (CurrentState.PlayClockSeconds <= 0.f)
        {
            CurrentState.PlayClockSeconds = 25.f;
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
            // In quick-sim mode the statistical resolver drives the outcome;
            // in physical-play mode outcomes arrive via bus events (OnBusCatch/OnBusTackle).
            if (bQuickSimMode)
            {
                ResolvePlayResult();
            }
            CurrentState.Phase = EPlayPhase::Scoring;
            PhaseTimer = 0.f;
        }
        break;
    case EPlayPhase::Kickoff:
        if (PhaseTimer >= 2.0f)
        {
            CurrentPlayResult.ResultType = EPlayResultType::KickoffResult;
            if (FMath::FRand() < 0.60f)
            {
                CurrentPlayResult.YardsGained = 25;
                UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Kickoff resulted in Touchback."));
            }
            else
            {
                CurrentPlayResult.YardsGained = FMath::RandRange(15, 30);
                UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Kickoff returned to own %d yard line."), CurrentPlayResult.YardsGained);
            }
            CurrentState.Phase = EPlayPhase::Scoring;
            PhaseTimer = 0.f;
        }
        break;
    case EPlayPhase::Punt:
        if (PhaseTimer >= 2.0f)
        {
            CurrentPlayResult.ResultType = EPlayResultType::PuntResult;
            CurrentPlayResult.YardsGained = FMath::RandRange(35, 45);
            UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Punt net distance: %d yards."), CurrentPlayResult.YardsGained);
            CurrentState.Phase = EPlayPhase::Scoring;
            PhaseTimer = 0.f;
        }
        break;
    case EPlayPhase::FieldGoal:
        if (PhaseTimer >= 2.0f)
        {
            float DistToGoal = 100.f - CurrentState.YardLine + 17.f;
            float SuccessChance = 0.95f;
            if (DistToGoal > 50.f) SuccessChance = 0.30f;
            else if (DistToGoal > 40.f) SuccessChance = 0.70f;
            else if (DistToGoal > 30.f) SuccessChance = 0.85f;

            if (FMath::FRand() < SuccessChance)
            {
                CurrentPlayResult.ResultType = EPlayResultType::FieldGoalGood;
                UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Field Goal is GOOD from %.1f yards!"), DistToGoal);
            }
            else
            {
                CurrentPlayResult.ResultType = EPlayResultType::FieldGoalMissed;
                UE_LOG(LogTemp, Warning, TEXT("UPSPlaySimulation: Field Goal is MISSED from %.1f yards!"), DistToGoal);
            }
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

    // Game clock and play clock status update based on play type
    if (CurrentPlayResult.ResultType == EPlayResultType::Tackle)
    {
        CurrentState.GameClockSeconds -= 30.f; // Runoff 30 seconds on tackled plays
        CurrentState.PlayClockSeconds = 40.f;
        CurrentState.bIsClockRunning = true;
    }
    else
    {
        CurrentState.PlayClockSeconds = 25.f;
        CurrentState.bIsClockRunning = false;
    }

    // Update yard line
    if (CurrentPlayResult.ResultType != EPlayResultType::KickoffResult && 
        CurrentPlayResult.ResultType != EPlayResultType::PuntResult &&
        CurrentPlayResult.ResultType != EPlayResultType::FieldGoalGood &&
        CurrentPlayResult.ResultType != EPlayResultType::FieldGoalMissed)
    {
        CurrentState.YardLine += CurrentPlayResult.YardsGained;
    }

    if (CurrentState.YardLine >= 100 && 
        CurrentPlayResult.ResultType != EPlayResultType::KickoffResult &&
        CurrentPlayResult.ResultType != EPlayResultType::PuntResult)
    {
        CurrentState.YardLine = 100;
        CurrentPlayResult.ResultType = EPlayResultType::Touchdown;
    }
    else if (CurrentState.YardLine <= 0 && 
             CurrentPlayResult.ResultType != EPlayResultType::KickoffResult &&
             CurrentPlayResult.ResultType != EPlayResultType::PuntResult)
    {
        CurrentState.YardLine = 1;
    }

    bool bTurnover = false;
    EPlayPhase NextPhase = EPlayPhase::PreSnap;

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
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: TOUCHDOWN! Score: Home %d - Away %d. Next play: Kickoff."), 
            CurrentState.HomeScore, CurrentState.AwayScore);

        NextPhase = EPlayPhase::Kickoff;
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
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: SAFETY! Score: Home %d - Away %d. Next play: Kickoff."), 
            CurrentState.HomeScore, CurrentState.AwayScore);

        NextPhase = EPlayPhase::Kickoff;
        bTurnover = true;
    }
    // Kickoff Outcome Resolution
    else if (CurrentPlayResult.ResultType == EPlayResultType::KickoffResult)
    {
        CurrentState.YardLine = CurrentPlayResult.YardsGained;
        CurrentState.Down = 1;
        CurrentState.Distance = 10;
        CurrentState.YardLineToGain = CurrentState.YardLine + 10;
        bTurnover = true; // Swap possession to receiving team
        NextPhase = EPlayPhase::PreSnap;
    }
    // Punt Outcome Resolution
    else if (CurrentPlayResult.ResultType == EPlayResultType::PuntResult)
    {
        CurrentState.YardLine = FMath::Clamp(100 - (CurrentState.YardLine + CurrentPlayResult.YardsGained), 1, 99);
        CurrentState.Down = 1;
        CurrentState.Distance = 10;
        CurrentState.YardLineToGain = CurrentState.YardLine + 10;
        bTurnover = true; // Swap possession to receiving team
        NextPhase = EPlayPhase::PreSnap;
    }
    // Field Goal Good Resolution
    else if (CurrentPlayResult.ResultType == EPlayResultType::FieldGoalGood)
    {
        if (CurrentState.bHomeHasPossession)
        {
            CurrentState.HomeScore += 3;
        }
        else
        {
            CurrentState.AwayScore += 3;
        }
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Field Goal points recorded. Next play: Kickoff."));
        NextPhase = EPlayPhase::Kickoff;
        bTurnover = true;
    }
    // Field Goal Missed Resolution
    else if (CurrentPlayResult.ResultType == EPlayResultType::FieldGoalMissed)
    {
        CurrentState.YardLine = FMath::Clamp(100 - CurrentState.YardLine, 20, 80);
        CurrentState.Down = 1;
        CurrentState.Distance = 10;
        CurrentState.YardLineToGain = CurrentState.YardLine + 10;
        bTurnover = true;
        NextPhase = EPlayPhase::PreSnap;
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
            if (CurrentState.Down == 3) // If next down is 4th down
            {
                CurrentState.Down = 4;
                if (CurrentState.YardLine >= 60)
                {
                    NextPhase = EPlayPhase::FieldGoal;
                }
                else
                {
                    NextPhase = EPlayPhase::Punt;
                }
                UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: 4th Down. Offense chooses Special Teams: %s"), *UEnum::GetValueAsString(NextPhase));
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

        // If it was a normal turnover (not Kickoff/Punt/FG/Touchdown which handled this already), flip perspective
        if (CurrentPlayResult.ResultType != EPlayResultType::KickoffResult && 
            CurrentPlayResult.ResultType != EPlayResultType::PuntResult &&
            CurrentPlayResult.ResultType != EPlayResultType::FieldGoalGood &&
            CurrentPlayResult.ResultType != EPlayResultType::FieldGoalMissed &&
            CurrentPlayResult.ResultType != EPlayResultType::Touchdown &&
            CurrentPlayResult.ResultType != EPlayResultType::Safety)
        {
            CurrentState.YardLine = 100 - CurrentState.YardLine;
            CurrentState.YardLine = FMath::Clamp(CurrentState.YardLine, 1, 99);
            CurrentState.Down = 1;
            CurrentState.Distance = 10;
            CurrentState.YardLineToGain = CurrentState.YardLine + 10;
        }
    }

    CurrentState.YardLineToGain = FMath::Min(CurrentState.YardLineToGain, 100);
    CurrentState.Distance = CurrentState.YardLineToGain - CurrentState.YardLine;

    SetPlayPhase(NextPhase);
    CurrentPlayResult.YardsGained = 0;
    CurrentPlayResult.ResultType = EPlayResultType::Incomplete;

    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Play resolved. New State: Down %d, Distance %d, YardLine %d, YardToGain %d"), 
        CurrentState.Down, CurrentState.Distance, CurrentState.YardLine, CurrentState.YardLineToGain);

    // Notify GameMode to reset physical pawns at new line of scrimmage
    UWorld* World = CachedWorld ? CachedWorld : GetWorld();
    APSGameMode* GM = nullptr;
    if (World)
    {
        GM = Cast<APSGameMode>(World->GetAuthGameMode());
    }
    if (!GM)
    {
        GM = Cast<APSGameMode>(GetOuter());
    }
    if (GM)
    {
        GM->ResetPawnPositions();
    }
}

void UPSPlaySimulation::InitializeWithWorld(UWorld* InWorld)
{
    CachedWorld = InWorld;
    if (!InWorld)
    {
        return;
    }

    UPSTelemetryBus* Bus = InWorld->GetSubsystem<UPSTelemetryBus>();
    if (!Bus)
    {
        return;
    }

    // Subscribe to physical play events so bus-driven outcomes advance state
    Bus->OnCatch.AddDynamic(this, &UPSPlaySimulation::OnBusCatchEvent);
    Bus->OnTackle.AddDynamic(this, &UPSPlaySimulation::OnBusTackleEvent);
    Bus->OnScore.AddDynamic(this, &UPSPlaySimulation::OnBusScoreEvent);

    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Subscribed to TelemetryBus (C2)."));
}

void UPSPlaySimulation::OnBusCatchEvent(const FPSTelemetryCatchEvent& Event)
{
    // A physical catch (or interception) occurred — transition to ball-carrier movement
    // unless we are already past that phase or in quick-sim mode.
    if (bQuickSimMode)
    {
        return;
    }

    if (CurrentState.Phase == EPlayPhase::PassRush || CurrentState.Phase == EPlayPhase::Snap)
    {
        SetPlayPhase(EPlayPhase::BallCarrierMovement);
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: BusCatch — transitioning to BallCarrierMovement (interception=%d)."), (int32)Event.bIsInterception);
    }
}

void UPSPlaySimulation::OnBusTackleEvent(const FPSTelemetryTackleEvent& Event)
{
    if (bQuickSimMode)
    {
        return;
    }

    // Physical tackle resolves the play
    CurrentPlayResult.ResultType = EPlayResultType::Tackle;
    CurrentPlayResult.YardsGained = Event.YardsGained;
    SetPlayPhase(EPlayPhase::Scoring);
    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: BusTackle — %s tackled by %s for %d yards."),
        *Event.BallCarrierName, *Event.TacklerName, Event.YardsGained);
}

void UPSPlaySimulation::OnBusScoreEvent(const FPSTelemetryScoreEvent& Event)
{
    // Keep the FPlayState score fields in sync with bus-driven score events
    // (GameMode maintains its own HomeScore/AwayScore; this keeps the sim's
    // FPlayState in agreement so GetPlayState() callers see the right values).
    CurrentState.HomeScore = Event.HomeScore;
    CurrentState.AwayScore = Event.AwayScore;
}

void UPSPlaySimulation::RecordTouchdown()
{
    CurrentPlayResult.ResultType = EPlayResultType::Touchdown;
    CurrentPlayResult.YardsGained = 100;
    SetPlayPhase(EPlayPhase::Scoring);
    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Touchdown recorded."));
}

FString UPSPlaySimulation::GetFormattedGameClock() const
{
    int32 TotalSeconds = FMath::Max(0, FMath::RoundToInt(CurrentState.GameClockSeconds));
    int32 Minutes = TotalSeconds / 60;
    int32 Seconds = TotalSeconds % 60;
    return FString::Printf(TEXT("%02d:%02d"), Minutes, Seconds);
}
