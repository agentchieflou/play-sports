#include "PSPlaySimulation.h"
#include "PSPerfBudget.h"
#include "PSGameStateEvents.h"
#include "PSFieldDimensions.h"
#include "PSRulesConfig.h"
#include "PSSpecialTeamsModel.h"
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
    CurrentState.HomeTimeoutsRemaining = 3;
    CurrentState.AwayTimeoutsRemaining = 3;
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
    CurrentState.HomeTimeoutsRemaining = RulesConfig ? RulesConfig->MaxTimeoutsPerHalf : 3;
    CurrentState.AwayTimeoutsRemaining = RulesConfig ? RulesConfig->MaxTimeoutsPerHalf : 3;
    CurrentState.bKickoff = false;
    CurrentPlayResult = FPlayResult();
    PendingClockPlay = EPSClockPlay::None;
    PendingSnapPlayClock = -1.f;
    PendingSpecialTeams = FPSSpecialTeamsCall();
    ActivePenalty = EPSPenaltyType::None;
    bPenaltyDeclined = false;
    PhaseTimer = 0.f;
    PlayLog = FPSTelemetryPlayResultEvent();
    bPlayLogOpen = false;
    PlaysAnnounced = 0;
    PublishGameStateIfChanged();
}

void UPSPlaySimulation::TriggerSnap()
{
    if (CurrentState.Phase == EPlayPhase::PreSnap)
    {
        OpenPlayLog();
        // The snap comes at the call's tempo mark (Epic 76): a running game clock also runs
        // off what the play clock had left above it.
        if (CurrentState.bIsClockRunning && PendingSnapPlayClock >= 0.f && CurrentState.PlayClockSeconds > PendingSnapPlayClock)
        {
            CurrentState.GameClockSeconds = FMath::Max(0.f, CurrentState.GameClockSeconds - (CurrentState.PlayClockSeconds - PendingSnapPlayClock));
            CurrentState.PlayClockSeconds = PendingSnapPlayClock;
        }
        PendingSnapPlayClock = -1.f;

        if (FMath::FRand() < 0.05f)
        {
            ActivePenalty = EPSPenaltyType::Offsides;
            bPenaltyDeclined = false;
            UE_LOG(LogTemp, Warning, TEXT("UPSPlaySimulation: FLAG! Offsides penalty called at the snap!"));
        }

        // A kickoff, or a punt or field-goal call, snaps into its kick (Epic 75).
        CurrentState.Phase = CurrentState.bKickoff ? EPlayPhase::Kickoff
            : PendingSpecialTeams.Kicking == EPSSpecialTeamsPlay::Punt ? EPlayPhase::Punt
            : PendingSpecialTeams.Kicking == EPSSpecialTeamsPlay::FieldGoal ? EPlayPhase::FieldGoal
            : EPlayPhase::Snap;
        CurrentState.bIsClockRunning = true;
        PhaseTimer = 0.f;
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Snap triggered. Phase transitioned to Snap."));
    }
    PublishGameStateIfChanged();
}

void UPSPlaySimulation::SetPlayPhase(EPlayPhase NewPhase)
{
    CurrentState.Phase = NewPhase;
    PhaseTimer = 0.f;
    bHumanKickLinedUp = false;
    HumanKickRoll = -1.f;
    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Play phase overridden. Transited to: %s"), *UEnum::GetValueAsString(NewPhase));
    PublishGameStateIfChanged();
}

void UPSPlaySimulation::RecordTackle(int32 YardsGained)
{
    if (IsBallDead())
    {
        return;
    }
    // A downed interceptor only ends the return; the turnover stands.
    if (CurrentPlayResult.ResultType != EPlayResultType::Interception)
    {
        CurrentPlayResult.ResultType = EPlayResultType::Tackle;
        CurrentPlayResult.YardsGained = YardsGained;
    }
    SetPlayPhase(EPlayPhase::Scoring);
    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Tackle recorded. Yards Gained: %d"), YardsGained);
}

void UPSPlaySimulation::RecordOutOfBounds(int32 YardsGained)
{
    if (IsBallDead())
    {
        return;
    }
    RecordTackle(YardsGained);
    CurrentPlayResult.bOutOfBounds = true;
}

void UPSPlaySimulation::ResolveClockPlay()
{
    const UPSRulesConfig* Rules = RulesConfig ? RulesConfig : GetDefault<UPSRulesConfig>();
    const bool bKneel = PendingClockPlay == EPSClockPlay::Kneel;
    PendingClockPlay = EPSClockPlay::None;

    // A kneel is down where he stands; a spike is an incompletion.
    CurrentPlayResult.ResultType = bKneel ? EPlayResultType::Tackle : EPlayResultType::Incomplete;
    CurrentPlayResult.YardsGained = bKneel ? Rules->KneelYardage : 0;
    CurrentState.GameClockSeconds = FMath::Max(0.f, CurrentState.GameClockSeconds - (bKneel ? Rules->KneelPlaySeconds : Rules->SpikePlaySeconds));
    SetPlayPhase(EPlayPhase::Scoring);
    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: %s at the snap."), bKneel ? TEXT("Kneel") : TEXT("Spike"));
}

void UPSPlaySimulation::AdvancePlay(float DeltaSeconds)
{
    PS_PERF_SCOPE(Simulation);
    CurrentState.GameTimeSeconds += DeltaSeconds;
    PhaseTimer += DeltaSeconds;

    if (PSGameStateEvents::IsGameClockRunning(CurrentState))
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
                if (CurrentState.Quarter == 3)
                {
                    CurrentState.HomeTimeoutsRemaining = RulesConfig ? RulesConfig->MaxTimeoutsPerHalf : 3;
                    CurrentState.AwayTimeoutsRemaining = RulesConfig ? RulesConfig->MaxTimeoutsPerHalf : 3;
                    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Timeouts reset for Second Half."));
                }
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
    else if (IsBallLive())
    {
        // Holding is called while the ball is live, never after the whistle or on a kick.
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
        if (PendingClockPlay != EPSClockPlay::None)
        {
            ResolveClockPlay();
        }
        else if (PendingSpecialTeams.Kicking == EPSSpecialTeamsPlay::FakePunt || PendingSpecialTeams.Kicking == EPSSpecialTeamsPlay::FakeFieldGoal)
        {
            // A fake is decided at the snap: converted or stopped (Epic 75).
            const int32 Gain = GetSpecialTeams()->ResolveFake(PendingSpecialTeams, CurrentState.Distance);
            PendingSpecialTeams = FPSSpecialTeamsCall();
            RecordTackle(Gain);
        }
        else if (PhaseTimer >= 0.5f)
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
        // A blocked kick's loose ball ends when it is blown dead (Epic 17.4).
        if (PhaseTimer >= 3.0f && !bLooseBallLive)
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
    case EPlayPhase::Punt:
    case EPlayPhase::FieldGoal:
        // The kick resolves through the special-teams model when the kicker is ready: the
        // human's meter roll (Epic 104.5), or the CPU's after its two seconds.
        if (IsKickReady())
        {
            ResolveKick(ConsumeKickRoll());
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
    PublishGameStateIfChanged();
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

    // The starters take the snaps: the first of each role in roster order, which is the depth
    // chart's (UPSRoster::BuildDefaultDepthChart), never a backup further down.
    const auto FindStarter = [](const TArray<FPlayerAttributes>& Roster, EPlayerRole Role, EPlayerRole OrRole, FPlayerAttributes& OutPlayer)
    {
        const FPlayerAttributes* Starter = Roster.FindByPredicate([Role](const FPlayerAttributes& Candidate) { return Candidate.Role == Role; });
        if (!Starter)
        {
            Starter = Roster.FindByPredicate([OrRole](const FPlayerAttributes& Candidate) { return Candidate.Role == OrRole; });
        }
        if (Starter)
        {
            OutPlayer = *Starter;
        }
    };
    FindStarter(OffenseRoster, EPlayerRole::Quarterback, EPlayerRole::Quarterback, Passer);
    FindStarter(OffenseRoster, EPlayerRole::WideReceiver, EPlayerRole::TightEnd, Receiver);
    FindStarter(DefenseRoster, EPlayerRole::DefensiveBack, EPlayerRole::Linebacker, Defender);

    // Resolve Pass Completion (Incomplete vs Complete)
    float CompletionChance = 0.60f + (Passer.Awareness + Receiver.Agility - Defender.Awareness - Defender.Agility) * 0.005f;
    CompletionChance = FMath::Clamp(CompletionChance, 0.10f, 0.95f);

    float RandomRoll = FMath::FRand();
    PlayLog.bPass = true;
    PlayLog.PasserId = Passer.PlayerId;
    PlayLog.ReceiverId = Receiver.PlayerId;
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
        PlayLog.bComplete = true;

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
            PlayLog.TacklerId = Defender.PlayerId;
        }
    }
}

void UPSPlaySimulation::EndPlayAndPrepareNext()
{
    // The state the play is resolved from: its result is announced against it (Epic 92).
    const FPlayState AtSnap = CurrentState;

    // A kick's outcome is the special-teams model's (Epic 75): no penalty or yardage rule changes it.
    const bool bKickResult = CurrentPlayResult.ResultType == EPlayResultType::KickoffResult || CurrentPlayResult.ResultType == EPlayResultType::PuntResult
        || CurrentPlayResult.ResultType == EPlayResultType::FieldGoalGood || CurrentPlayResult.ResultType == EPlayResultType::FieldGoalMissed;
    if (bKickResult)
    {
        ActivePenalty = EPSPenaltyType::None;
    }

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
        else if (ActivePenalty == EPSPenaltyType::PassInterference)
        {
            // A spot foul (Epic 69): the ball at the spot, never past the 1, and a first down --
            // declined when the play itself gained more.
            const int32 SpotYards = FMath::Min(PassInterferenceYards, 99 - CurrentState.YardLine);
            if (CurrentPlayResult.ResultType != EPlayResultType::Touchdown && CurrentPlayResult.YardsGained < SpotYards)
            {
                CurrentPlayResult.YardsGained = SpotYards;
                CurrentPlayResult.ResultType = EPlayResultType::Tackle;
                CurrentPlayResult.bOutOfBounds = false;
                CurrentState.YardLineToGain = FMath::Min(CurrentState.YardLineToGain, CurrentState.YardLine + SpotYards);
                UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Pass interference ACCEPTED (%d yards, first down)."), SpotYards);
            }
            else
            {
                UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Pass interference DECLINED. Result stands."));
            }
        }

        ActivePenalty = EPSPenaltyType::None;
    }

    // Update drive summary tracking
    CurrentDriveSummary.Plays++;
    CurrentDriveSummary.Yards += CurrentPlayResult.YardsGained;

    // Game clock and play clock status update based on play type. Out of bounds late in a
    // half keeps the clock stopped (Epic 76). A played game runs the real play clock before the
    // snap, at the call's tempo; quick sim, which has no pre-snap time, runs off a fixed amount.
    const bool bClockKeepsRunning = CurrentPlayResult.ResultType == EPlayResultType::Tackle
        && !(CurrentPlayResult.bOutOfBounds && DoesOutOfBoundsStopClock(CurrentState.Quarter, CurrentState.GameClockSeconds, RulesConfig));
    if (bClockKeepsRunning)
    {
        if (bQuickSimMode)
        {
            CurrentState.GameClockSeconds -= (RulesConfig ? RulesConfig : GetDefault<UPSRulesConfig>())->QuickSimTackleRunoffSeconds;
        }
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
    bool bReturnTouchdown = false;
    EPlayPhase NextPhase = EPlayPhase::PreSnap;

    // Touchdown Score Tracking
    if (CurrentPlayResult.ResultType == EPlayResultType::Touchdown)
    {
        CurrentDriveSummary.Result = TEXT("Touchdown");
        ScoreTouchdown();
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

        // The team scored on free-kicks from its own 20 (Epic 75).
        CurrentState.bKickoff = true;
        CurrentState.YardLine = GetSpecialTeams()->GetTuning().SafetyKickYardLine;
        CurrentState.Down = 1;
        CurrentState.YardLineToGain = CurrentState.YardLine + 10;
    }
    // Special teams (Epic 75): the kick's outcome sets the score, the spot and who has the ball.
    else if (bKickResult)
    {
        const UPSRulesConfig* Rules = RulesConfig ? RulesConfig : GetDefault<UPSRulesConfig>();
        bTurnover = GetSpecialTeams()->ApplyOutcome(CurrentState, LastSpecialTeamsOutcome, Rules->TouchdownPoints, Rules->FieldGoalPoints, Rules->PATSuccessChance);
        CurrentDriveSummary.Result = UEnum::GetValueAsString(LastSpecialTeamsOutcome.Result);
    }
    // An interception (this simulation owns possession, Epic C2/C3): the defense takes the ball
    // where the return ended; down in the end zone it defends, at the touchback line (the 20,
    // the non-kickoff touchback in Data/special_teams.json).
    else if (CurrentPlayResult.ResultType == EPlayResultType::Interception)
    {
        CurrentState.YardLine = InterceptionSpot >= 100 ? 100 - GetSpecialTeams()->GetTuning().PuntTouchbackYardLine : InterceptionSpot;
        CurrentDriveSummary.Result = TEXT("Interception");
        bTurnover = true;
        // Returned to the offense's goal line: a touchdown for the defense, scored once the
        // ball is its (below).
        bReturnTouchdown = InterceptionSpot <= 0;
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: INTERCEPTION! The return ended at the offense's %d."), InterceptionSpot);
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
                const bool bPuntingAllowed = RulesConfig ? RulesConfig->bAllowPunting : true;
                if (!bQuickSimMode)
                {
                    // A played game's 4th down is a call like any other: punt, field goal, fake
                    // or go for it (Epic 75). Quick sim has no calls, so it kicks by rule.
                }
                else if (CurrentState.YardLine >= 60)
                {
                    NextPhase = EPlayPhase::FieldGoal;
                    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: 4th Down. Offense chooses Special Teams: %s"), *UEnum::GetValueAsString(NextPhase));
                }
                else if (bPuntingAllowed)
                {
                    NextPhase = EPlayPhase::Punt;
                    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: 4th Down. Offense chooses Special Teams: %s"), *UEnum::GetValueAsString(NextPhase));
                }
                else
                {
                    // No-punting ruleset (Epic 140): out of field-goal range with punting
                    // disallowed means the offense has no kicking option -- they must go
                    // for it. NextPhase stays PreSnap for a normal 4th-down snap attempt.
                    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: 4th Down, punting disallowed and out of FG range. Offense must go for it."));
                }
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

    if (bTurnover || CurrentState.bKickoff)
    {
        // Log final drive summary before reset
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Drive complete. Plays: %d, Yards: %d, Result: %s"), 
            CurrentDriveSummary.Plays, CurrentDriveSummary.Yards, *CurrentDriveSummary.Result);
            
        LastCompletedDrive = CurrentDriveSummary;
        ++CompletedDrives;

        // Reset drive summary for next drive
        CurrentDriveSummary.Plays = 0;
        CurrentDriveSummary.Yards = 0;
        CurrentDriveSummary.Result = TEXT("");
    }

    if (bTurnover)
    {
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

    // A pick-six: the defense, the team with the ball now, scores and kicks off.
    if (bReturnTouchdown)
    {
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: The interception is returned for a touchdown."));
        ScoreTouchdown();
    }

    CurrentState.YardLineToGain = FMath::Min(CurrentState.YardLineToGain, 100);
    CurrentState.Distance = CurrentState.YardLineToGain - CurrentState.YardLine;

    // Quick sim has no calls: a kickoff goes straight to the kick (Epic 75).
    if (bQuickSimMode && CurrentState.bKickoff)
    {
        NextPhase = EPlayPhase::Kickoff;
    }
    AnnouncePlayResult(AtSnap, bTurnover);
    SetPlayPhase(NextPhase);
    CurrentPlayResult.YardsGained = 0;
    CurrentPlayResult.ResultType = EPlayResultType::Incomplete;
    CurrentPlayResult.bOutOfBounds = false;
    PendingClockPlay = EPSClockPlay::None;
    PendingSnapPlayClock = -1.f;
    PendingSpecialTeams = FPSSpecialTeamsCall();

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

void UPSPlaySimulation::ScoreTouchdown()
{
    const UPSRulesConfig* Rules = RulesConfig ? RulesConfig : GetDefault<UPSRulesConfig>();
    int32 Points = Rules->TouchdownPoints;
    if (FMath::FRand() < Rules->PATSuccessChance)
    {
        ++Points;
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: PAT kick is GOOD!"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlaySimulation: PAT kick is MISSED!"));
    }
    if (CurrentState.bHomeHasPossession)
    {
        CurrentState.HomeScore += Points;
    }
    else
    {
        CurrentState.AwayScore += Points;
    }
    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: TOUCHDOWN! Score: Home %d - Away %d. Next play: Kickoff."),
        CurrentState.HomeScore, CurrentState.AwayScore);

    // The scoring team kicks off (Epic 75).
    CurrentState.bKickoff = true;
    CurrentState.YardLine = GetSpecialTeams()->GetTuning().KickoffYardLine;
    CurrentState.Down = 1;
    CurrentState.YardLineToGain = CurrentState.YardLine + 10;
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
    Bus->OnThrow.AddDynamic(this, &UPSPlaySimulation::OnBusThrowEvent);
    // Epic 104.5: the human kicker's meter and the human defender's jump at the snap
    Bus->OnKick.AddDynamic(this, &UPSPlaySimulation::OnBusKickEvent);
    Bus->OnJumpSnap.AddDynamic(this, &UPSPlaySimulation::OnBusJumpSnapEvent);
    // Epic 69: the coverage contest's pass interference
    Bus->OnCoverage.AddDynamic(this, &UPSPlaySimulation::OnBusCoverageEvent);
    // Epic 17.4: a blocked kick's loose ball, played out on the field
    Bus->OnLooseBall.AddDynamic(this, &UPSPlaySimulation::OnBusLooseBallEvent);
    Bus->OnPlayCall.AddDynamic(this, &UPSPlaySimulation::OnBusPlayCallEvent);
    Bus->OnTimeout.AddDynamic(this, &UPSPlaySimulation::OnBusTimeoutEvent);
    // The field's volumes: out of bounds and the end zones.
    Bus->OnBoundaryCrossed.AddDynamic(this, &UPSPlaySimulation::OnBusBoundaryCrossedEvent);

    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Subscribed to TelemetryBus (C2)."));
    PublishGameStateIfChanged();
}

void UPSPlaySimulation::OnBusCatchEvent(const FPSTelemetryCatchEvent& Event)
{
    // A physical catch (or interception) occurred — transition to ball-carrier movement
    // unless we are already past that phase or in quick-sim mode.
    if (bQuickSimMode)
    {
        return;
    }

    if (!IsBallDead())
    {
        const FName CatcherId = FindPlayerIdByName(Event.ReceiverName);
        PlayLog.bInterception = Event.bIsInterception;
        PlayLog.bComplete = !Event.bIsInterception;
        if (Event.bIsInterception)
        {
            PlayLog.InterceptorId = CatcherId;
            // Only a pass is intercepted. The play is now a turnover: the offense gains nothing,
            // and the defense gets the ball where the return ends (where he caught it, until a
            // tackle or the end zone says otherwise).
            PlayLog.bPass = true;
            CurrentPlayResult.ResultType = EPlayResultType::Interception;
            CurrentPlayResult.YardsGained = 0;
            InterceptionSpot = PSField::WorldToSpot(Event.CatchLocation);
        }
        else
        {
            PlayLog.ReceiverId = CatcherId;
        }
    }

    if (CurrentState.Phase == EPlayPhase::PassRush || CurrentState.Phase == EPlayPhase::Snap)
    {
        SetPlayPhase(EPlayPhase::BallCarrierMovement);
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: BusCatch — transitioning to BallCarrierMovement (interception=%d)."), (int32)Event.bIsInterception);
    }
}

void UPSPlaySimulation::OnBusTackleEvent(const FPSTelemetryTackleEvent& Event)
{
    // Physical tackle resolves the play, unless the whistle already blew. A loose ball's return
    // ends through its own dead ball (Epic 17.4).
    if (bQuickSimMode || IsBallDead() || bLooseBallLive)
    {
        return;
    }

    // The interceptor is down: his return ends at the tackle's spot. Tackling him is no
    // defensive stat, so the play's tackler stays empty.
    if (CurrentPlayResult.ResultType == EPlayResultType::Interception)
    {
        InterceptionSpot = FMath::Clamp(Event.YardLine, 0, 100);
        SetPlayPhase(EPlayPhase::Scoring);
        UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: BusTackle -- interceptor %s down at the %d."), *Event.BallCarrierName, Event.YardLine);
        return;
    }

    // The play's yards run from the line of scrimmage, this simulation's spot, to where the
    // carrier went down (the event's spot, in the offense's yard lines). This is their one
    // measure: the overlay, the highlights and the stats read it from the play's result.
    CurrentPlayResult.ResultType = EPlayResultType::Tackle;
    CurrentPlayResult.YardsGained = FMath::Clamp(Event.YardLine, 0, 100) - CurrentState.YardLine;
    const FName CarrierId = FindPlayerIdByName(Event.BallCarrierName);
    PlayLog.TacklerId = FindPlayerIdByName(Event.TacklerName);
    if (Event.bIsSack)
    {
        PlayLog.bSack = true;
        PlayLog.bPass = true;
        PlayLog.PasserId = CarrierId;
    }
    else if (!PlayLog.bPass)
    {
        PlayLog.RusherId = CarrierId;
    }
    SetPlayPhase(EPlayPhase::Scoring);
    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: BusTackle -- %s tackled by %s at the %d, %d yards from the line."),
        *Event.BallCarrierName, *Event.TacklerName, Event.YardLine, CurrentPlayResult.YardsGained);
}

void UPSPlaySimulation::OnBusScoreEvent(const FPSTelemetryScoreEvent& Event)
{
    // Keep the FPlayState score fields in sync with bus-driven score events
    // (GameMode maintains its own HomeScore/AwayScore; this keeps the sim's
    // FPlayState in agreement so GetPlayState() callers see the right values).
    CurrentState.HomeScore = Event.HomeScore;
    CurrentState.AwayScore = Event.AwayScore;
    PublishGameStateIfChanged();
}

void UPSPlaySimulation::OnBusThrowEvent(const FPSTelemetryThrowEvent& Event)
{
    if (bQuickSimMode || IsBallDead())
    {
        return;
    }
    PlayLog.bPass = true;
    PlayLog.PasserId = FindPlayerIdByName(Event.PasserName);
    PlayLog.ReceiverId = FindPlayerIdByName(Event.TargetReceiverName);
}

void UPSPlaySimulation::OpenPlayLog()
{
    PlayLog = FPSTelemetryPlayResultEvent();
    PlayLog.bHomeOffense = CurrentState.bHomeHasPossession;
    PlayLog.Quarter = CurrentState.Quarter;
    PlayLog.GameClockSeconds = CurrentState.GameClockSeconds;
    PlayLog.Down = CurrentState.Down;
    PlayLog.Distance = CurrentState.Distance;
    PlayLog.YardLine = CurrentState.YardLine;
    bPlayLogOpen = true;
}

FName UPSPlaySimulation::FindPlayerIdByName(const FString& DisplayName) const
{
    if (DisplayName.IsEmpty())
    {
        return NAME_None;
    }
    const auto Named = [&DisplayName](const FPlayerAttributes& Player) { return Player.DisplayName == DisplayName; };
    const FPlayerAttributes* Found = OffenseRoster.FindByPredicate(Named);
    if (!Found)
    {
        Found = DefenseRoster.FindByPredicate(Named);
    }
    return Found ? Found->PlayerId : NAME_None;
}

void UPSPlaySimulation::AnnouncePlayResult(const FPlayState& AtSnap, bool bTurnover)
{
    // A play that began without a snap (a kick the simulation went straight to) is announced
    // against the state it was resolved from.
    if (!bPlayLogOpen)
    {
        PlayLog.bHomeOffense = AtSnap.bHomeHasPossession;
        PlayLog.Quarter = AtSnap.Quarter;
        PlayLog.GameClockSeconds = AtSnap.GameClockSeconds;
        PlayLog.Down = AtSnap.Down;
        PlayLog.Distance = AtSnap.Distance;
        PlayLog.YardLine = AtSnap.YardLine;
    }

    const EPlayResultType Result = CurrentPlayResult.ResultType;
    const bool bKick = Result == EPlayResultType::KickoffResult || Result == EPlayResultType::PuntResult
        || Result == EPlayResultType::FieldGoalGood || Result == EPlayResultType::FieldGoalMissed;
    PlayLog.PlayNumber = ++PlaysAnnounced;
    PlayLog.Result = StaticEnum<EPlayResultType>()->GetNameStringByValue(static_cast<int64>(Result));
    PlayLog.YardsGained = CurrentPlayResult.YardsGained;
    // A defensive flag the offense accepted wiped the interception out.
    PlayLog.bInterception = PlayLog.bInterception && Result == EPlayResultType::Interception;
    PlayLog.HomePoints = CurrentState.HomeScore - AtSnap.HomeScore;
    PlayLog.AwayPoints = CurrentState.AwayScore - AtSnap.AwayScore;
    PlayLog.bTurnoverOnDowns = bTurnover && !bKick && !PlayLog.bInterception;
    PlayLog.bFirstDown = !bKick && (Result == EPlayResultType::Touchdown
        || (Result != EPlayResultType::Safety && !bTurnover && AtSnap.YardLine + CurrentPlayResult.YardsGained >= AtSnap.YardLineToGain));

    OnPlayResolved.Broadcast(PlayLog);
    if (UPSTelemetryBus* Bus = CachedWorld ? CachedWorld->GetSubsystem<UPSTelemetryBus>() : nullptr)
    {
        Bus->PublishPlayResult(PlayLog);
    }
    PlayLog = FPSTelemetryPlayResultEvent();
    bPlayLogOpen = false;
}

void UPSPlaySimulation::OnBusPlayCallEvent(const FPSTelemetryPlayCallEvent& Event)
{
    // Only calls for the coming snap; a call made at or after a snap is not for it.
    if (CurrentState.Phase != EPlayPhase::PreSnap)
    {
        return;
    }
    const EPSSpecialTeamsPlay SpecialTeamsPlay = PSSpecialTeams::FromCategory(Event.PlayCategory);
    if (!Event.bOffense)
    {
        // The receiving team's return or block, and its formation: the return scheme (Epic 75).
        PendingSpecialTeams.Receiving = SpecialTeamsPlay;
        PendingSpecialTeams.ReturnFormation = Event.Formation;
        return;
    }
    PendingSpecialTeams.Kicking = SpecialTeamsPlay;
    PendingClockPlay = PSSituation::ClockPlayFromCategory(Event.PlayCategory);
    PendingSnapPlayClock = Event.SnapAtPlayClockSeconds;
}

void UPSPlaySimulation::OnBusTimeoutEvent(const FPSTelemetryTimeoutEvent& Event)
{
    CallTimeout(Event.bOffense == CurrentState.bHomeHasPossession);
}

void UPSPlaySimulation::OnBusBoundaryCrossedEvent(const FPSTelemetryBoundaryCrossedEvent& Event)
{
    // Before the snap nothing is live; after the whistle the play has its result.
    if (bQuickSimMode || IsBallDead() || CurrentState.Phase == EPlayPhase::PreSnap)
    {
        return;
    }

    const int32 Spot = FMath::Clamp(Event.YardLine, 0, 100);
    if (Event.CarrierName.IsEmpty())
    {
        // The ball alone out of bounds is dead; loose in an end zone it plays on.
        if (!Event.bEndZone)
        {
            SetPlayPhase(EPlayPhase::Scoring);
        }
        return;
    }
    if (CurrentPlayResult.ResultType == EPlayResultType::Interception)
    {
        InterceptionSpot = Spot;
        SetPlayPhase(EPlayPhase::Scoring);
        return;
    }
    if (!Event.bEndZone)
    {
        RecordOutOfBounds(Spot - CurrentState.YardLine);
    }
    else if (Spot >= 100)
    {
        RecordTouchdown();
    }
}

void UPSPlaySimulation::RecordTouchdown()
{
    // The end zone the offense attacks is the one the interceptor defends: down in it.
    if (CurrentPlayResult.ResultType == EPlayResultType::Interception)
    {
        InterceptionSpot = 100;
        SetPlayPhase(EPlayPhase::Scoring);
        return;
    }
    // The play gained the rest of the field: from the line of scrimmage to the goal line.
    CurrentPlayResult.ResultType = EPlayResultType::Touchdown;
    CurrentPlayResult.YardsGained = 100 - CurrentState.YardLine;
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

bool UPSPlaySimulation::CallTimeout(bool bHomeTeam)
{
    int32& TimeoutsRemaining = bHomeTeam ? CurrentState.HomeTimeoutsRemaining : CurrentState.AwayTimeoutsRemaining;
    if (TimeoutsRemaining <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlaySimulation: Cannot call timeout. No timeouts remaining for %s team."),
            bHomeTeam ? TEXT("Home") : TEXT("Away"));
        return false;
    }

    if (!CurrentState.bIsClockRunning && CurrentState.Phase != EPlayPhase::PreSnap)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlaySimulation: Cannot call timeout. Game clock is already stopped."));
        return false;
    }

    TimeoutsRemaining--;
    CurrentState.bIsClockRunning = false;
    
    // Reset play clock to 25 seconds when a timeout stops the game/play clock
    CurrentState.PlayClockSeconds = 25.f;

    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Timeout called by %s team. Timeouts remaining: %d"),
        bHomeTeam ? TEXT("Home") : TEXT("Away"), TimeoutsRemaining);
    PublishGameStateIfChanged();
    return true;
}

UPSSpecialTeamsModel* UPSPlaySimulation::GetSpecialTeams()
{
    if (!SpecialTeams)
    {
        SpecialTeams = NewObject<UPSSpecialTeamsModel>(this);
        SpecialTeams->LoadTuningFromJson(UPSSpecialTeamsModel::GetDefaultTuningPath());
    }
    return SpecialTeams;
}

bool UPSPlaySimulation::IsBallLive() const
{
    const EPlayPhase Phase = CurrentState.Phase;
    return Phase == EPlayPhase::Snap || Phase == EPlayPhase::PassRush || Phase == EPlayPhase::BallCarrierMovement;
}

bool UPSPlaySimulation::IsBallDead() const
{
    const EPlayPhase Phase = CurrentState.Phase;
    return Phase == EPlayPhase::Scoring || Phase == EPlayPhase::Kickoff || Phase == EPlayPhase::Punt || Phase == EPlayPhase::FieldGoal;
}

void UPSPlaySimulation::ResolveKick(float KickRoll)
{
    // The kicking team has the ball for the kick; the other side receives (or rushes it).
    UPSSpecialTeamsModel* Model = GetSpecialTeams();
    const FPSSpecialTeamsUnitRatings Kicking = UPSSpecialTeamsModel::RateUnit(OffenseRoster);
    const FPSSpecialTeamsUnitRatings Receiving = UPSSpecialTeamsModel::RateUnit(DefenseRoster);
    switch (CurrentState.Phase)
    {
    case EPlayPhase::Kickoff:
        LastSpecialTeamsOutcome = Model->ResolveKickoff(PendingSpecialTeams, CurrentState.YardLine, Kicking, Receiving, KickRoll);
        CurrentPlayResult.ResultType = EPlayResultType::KickoffResult;
        break;
    case EPlayPhase::Punt:
        LastSpecialTeamsOutcome = Model->ResolvePunt(PendingSpecialTeams, CurrentState.YardLine, Kicking, Receiving, KickRoll);
        CurrentPlayResult.ResultType = EPlayResultType::PuntResult;
        break;
    default:
        LastSpecialTeamsOutcome = Model->ResolveFieldGoal(PendingSpecialTeams, CurrentState.YardLine, Kicking, Receiving, KickRoll);
        CurrentPlayResult.ResultType = LastSpecialTeamsOutcome.Result == EPSSpecialTeamsResult::FieldGoalGood ? EPlayResultType::FieldGoalGood : EPlayResultType::FieldGoalMissed;
        break;
    }
    CurrentPlayResult.YardsGained = 0;
    PendingSpecialTeams = FPSSpecialTeamsCall();
    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: %s: %s (%d yards)."), *UEnum::GetValueAsString(CurrentState.Phase),
        *UEnum::GetValueAsString(LastSpecialTeamsOutcome.Result), LastSpecialTeamsOutcome.Yards);

    // A block on a live field is the players' to finish (Epic 17.4): announced, and if the ball is
    // taken live (a Loose event in answer) the play goes on until it is blown dead.
    bLooseBallLive = false;
    const bool bBlocked = LastSpecialTeamsOutcome.Result == EPSSpecialTeamsResult::Blocked || LastSpecialTeamsOutcome.Result == EPSSpecialTeamsResult::BlockedTouchdown;
    UPSTelemetryBus* Bus = bBlocked && CachedWorld && !bQuickSimMode ? CachedWorld->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (Bus)
    {
        FPSTelemetryLooseBallEvent Block;
        Block.Kind = EPSLooseBallEventKind::Blocked;
        Block.KickType = CurrentState.Phase == EPlayPhase::Punt ? TEXT("Punt") : TEXT("FieldGoal");
        Block.YardsBehindLine = CurrentState.Phase == EPlayPhase::Punt ? Model->GetTuning().BlockedPuntRecoilYards : 0;
        Bus->PublishLooseBall(Block);
    }
    SetPlayPhase(bLooseBallLive ? EPlayPhase::BallCarrierMovement : EPlayPhase::Scoring);
}

void UPSPlaySimulation::OnBusLooseBallEvent(const FPSTelemetryLooseBallEvent& Event)
{
    // Taken live in answer to the block, while the kick is still being resolved.
    const EPlayPhase Phase = CurrentState.Phase;
    if (Event.Kind == EPSLooseBallEventKind::Loose && (Phase == EPlayPhase::Punt || Phase == EPlayPhase::FieldGoal))
    {
        bLooseBallLive = true;
        return;
    }
    if (Event.Kind != EPSLooseBallEventKind::Dead || !bLooseBallLive)
    {
        return;
    }
    // The dead ball is the kick's outcome: the ball goes over at the spot (the kicking team
    // falling on it behind the line on its kicking down turns it over too), or the defense scored.
    bLooseBallLive = false;
    LastSpecialTeamsOutcome.Result = Event.bTouchdown ? EPSSpecialTeamsResult::BlockedTouchdown : EPSSpecialTeamsResult::Blocked;
    LastSpecialTeamsOutcome.bTouchdown = Event.bTouchdown;
    LastSpecialTeamsOutcome.bPossessionChanges = true;
    LastSpecialTeamsOutcome.NextYardLine = FMath::Clamp(100 - Event.YardLine, 1, 99);
    LastSpecialTeamsOutcome.Yards = Event.YardLine - CurrentState.YardLine;
    UE_LOG(LogTemp, Display, TEXT("UPSPlaySimulation: Blocked kick dead at the %d (%s)."), Event.YardLine, Event.bTouchdown ? TEXT("touchdown") : TEXT("defense's ball"));
    SetPlayPhase(EPlayPhase::Scoring);
}

void UPSPlaySimulation::OnBusKickEvent(const FPSTelemetryKickEvent& Event)
{
    const EPlayPhase Phase = CurrentState.Phase;
    if (Phase != EPlayPhase::Kickoff && Phase != EPlayPhase::Punt && Phase != EPlayPhase::FieldGoal)
    {
        return;
    }
    if (Event.bLiningUp)
    {
        bHumanKickLinedUp = true;
        HumanKickHoldSeconds = Event.HoldSeconds;
    }
    else
    {
        HumanKickRoll = FMath::Clamp(Event.Roll, 0.f, 1.f);
    }
}

void UPSPlaySimulation::OnBusJumpSnapEvent(const FPSTelemetryJumpSnapEvent& Event)
{
    if (Event.bOffside && ActivePenalty == EPSPenaltyType::None)
    {
        ActivePenalty = EPSPenaltyType::Offsides;
        bPenaltyDeclined = false;
        UE_LOG(LogTemp, Warning, TEXT("UPSPlaySimulation: FLAG! %s jumped offside."), *Event.DefenderName);
    }
}

void UPSPlaySimulation::OnBusCoverageEvent(const FPSTelemetryCoverageEvent& Event)
{
    if (Event.Kind == EPSCoverageEventKind::PassInterference && ActivePenalty == EPSPenaltyType::None && !IsBallDead())
    {
        ActivePenalty = EPSPenaltyType::PassInterference;
        bPenaltyDeclined = false;
        PassInterferenceYards = FMath::Max(1, Event.YardsPastLine);
        UE_LOG(LogTemp, Warning, TEXT("UPSPlaySimulation: FLAG! Pass interference on %s, %d yards past the line."), *Event.DefenderName, PassInterferenceYards);
    }
}

bool UPSPlaySimulation::IsKickReady() const
{
    if (HumanKickRoll >= 0.f)
    {
        return true;
    }
    // The CPU kicker kicks after two seconds; a lined-up human gets until his hold runs out.
    return PhaseTimer >= (bHumanKickLinedUp ? FMath::Max(2.0f, HumanKickHoldSeconds) : 2.0f);
}

float UPSPlaySimulation::ConsumeKickRoll()
{
    const float KickRoll = HumanKickRoll >= 0.f ? HumanKickRoll : FMath::FRand();
    bHumanKickLinedUp = false;
    HumanKickHoldSeconds = 0.f;
    HumanKickRoll = -1.f;
    return KickRoll;
}
void UPSPlaySimulation::PublishGameStateIfChanged()
{
    UPSTelemetryBus* Bus = CachedWorld ? CachedWorld->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus)
    {
        return;
    }
    const int32 MaxTimeouts = RulesConfig ? RulesConfig->MaxTimeoutsPerHalf : 3;
    const FPSTelemetryGameStateEvent Event = PSGameStateEvents::MakeEvent(CurrentState, LastCompletedDrive, CompletedDrives, MaxTimeouts);
    if (bHasPublishedGameState && Event.HasSameStateAs(LastPublishedGameState))
    {
        return;
    }
    LastPublishedGameState = Event;
    bHasPublishedGameState = true;
    Bus->PublishGameState(Event);
}

