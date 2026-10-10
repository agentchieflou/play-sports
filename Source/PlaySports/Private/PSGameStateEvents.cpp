#include "PSGameStateEvents.h"

bool PSGameStateEvents::IsGameClockRunning(const FPlayState& State)
{
    return (State.Phase != EPlayPhase::PreSnap || State.bIsClockRunning) && State.Phase != EPlayPhase::Scoring;
}

bool PSGameStateEvents::IsPlayClockRunning(const FPlayState& State)
{
    return State.Phase == EPlayPhase::PreSnap;
}

FPSTelemetryGameStateEvent PSGameStateEvents::MakeEvent(const FPlayState& State, const FDriveSummary& LastDrive, int32 CompletedDrives, int32 MaxTimeouts)
{
    FPSTelemetryGameStateEvent Event;
    const UEnum* PhaseEnum = StaticEnum<EPlayPhase>();
    Event.Phase = PhaseEnum ? PhaseEnum->GetNameStringByValue(static_cast<int64>(State.Phase)) : FString();
    Event.Quarter = State.Quarter;
    Event.GameClockSeconds = State.GameClockSeconds;
    Event.bGameClockRunning = IsGameClockRunning(State);
    Event.PlayClockSeconds = State.PlayClockSeconds;
    Event.bPlayClockRunning = IsPlayClockRunning(State);
    Event.Down = State.Down;
    Event.Distance = State.Distance;
    Event.YardLine = State.YardLine;
    Event.YardLineToGain = State.YardLineToGain;
    Event.bHomeHasPossession = State.bHomeHasPossession;
    Event.bKickoff = State.bKickoff;
    Event.HomeScore = State.HomeScore;
    Event.AwayScore = State.AwayScore;
    Event.HomeTimeoutsRemaining = State.HomeTimeoutsRemaining;
    Event.AwayTimeoutsRemaining = State.AwayTimeoutsRemaining;
    Event.MaxTimeouts = MaxTimeouts;
    Event.CompletedDrives = CompletedDrives;
    Event.LastDrivePlays = LastDrive.Plays;
    Event.LastDriveYards = LastDrive.Yards;
    Event.LastDriveResult = LastDrive.Result;
    return Event;
}

FPlayState PSGameStateEvents::ToPlayState(const FPSTelemetryGameStateEvent& Event)
{
    FPlayState State;
    const UEnum* PhaseEnum = StaticEnum<EPlayPhase>();
    const int64 PhaseValue = PhaseEnum ? PhaseEnum->GetValueByNameString(Event.Phase) : INDEX_NONE;
    State.Phase = PhaseValue == INDEX_NONE ? EPlayPhase::PreSnap : static_cast<EPlayPhase>(PhaseValue);
    State.Quarter = Event.Quarter;
    State.GameClockSeconds = Event.GameClockSeconds;
    State.bIsClockRunning = Event.bGameClockRunning;
    State.PlayClockSeconds = Event.PlayClockSeconds;
    State.Down = Event.Down;
    State.Distance = Event.Distance;
    State.YardLine = Event.YardLine;
    State.YardLineToGain = Event.YardLineToGain;
    State.bHomeHasPossession = Event.bHomeHasPossession;
    State.bKickoff = Event.bKickoff;
    State.HomeScore = Event.HomeScore;
    State.AwayScore = Event.AwayScore;
    State.HomeTimeoutsRemaining = Event.HomeTimeoutsRemaining;
    State.AwayTimeoutsRemaining = Event.AwayTimeoutsRemaining;
    return State;
}

FString PSGameStateEvents::QuarterLabel(int32 Quarter)
{
    switch (Quarter)
    {
    case 1:  return TEXT("1st");
    case 2:  return TEXT("2nd");
    case 3:  return TEXT("3rd");
    case 4:  return TEXT("4th");
    default: return Quarter > 4 ? TEXT("OT") : TEXT("1st");
    }
}

FString PSGameStateEvents::ClockText(float Seconds)
{
    // A clock reads the whole seconds left, so 0.4 s left still shows 0:01 until it runs out.
    const int32 Whole = FMath::Max(0, FMath::CeilToInt(Seconds));
    return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
}
