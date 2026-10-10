#include "PSGameStateEvents.h"
#include "PSFieldDimensions.h"
#include "PSLocalization.h"

bool PSGameStateEvents::IsGameClockRunning(const FPlayState& State)
{
    return (State.Phase != EPlayPhase::PreSnap || State.bIsClockRunning) && State.Phase != EPlayPhase::Scoring;
}

bool PSGameStateEvents::IsPlayClockRunning(const FPlayState& State)
{
    return State.Phase == EPlayPhase::PreSnap;
}

FVector PSGameStateEvents::LineOfScrimmageFor(int32 YardLine)
{
    // The field's one frame (PSField): the offense always drives toward +X from its goal line at
    // X = 0, in the middle of the field.
    return PSField::YardLineToWorld(static_cast<float>(YardLine));
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
    Event.LineOfScrimmage = LineOfScrimmageFor(State.YardLine);
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
    // The ordinals are each language's own (Data/ui_text.csv).
    if (Quarter > 4)
    {
        return UPSLocalization::GetText(TEXT("Broadcast.Overtime")).ToString();
    }
    switch (Quarter)
    {
    case 2:  return UPSLocalization::GetText(TEXT("Broadcast.Quarter2")).ToString();
    case 3:  return UPSLocalization::GetText(TEXT("Broadcast.Quarter3")).ToString();
    case 4:  return UPSLocalization::GetText(TEXT("Broadcast.Quarter4")).ToString();
    default: return UPSLocalization::GetText(TEXT("Broadcast.Quarter1")).ToString();
    }
}

FString PSGameStateEvents::ClockText(float Seconds)
{
    // A clock reads the whole seconds left, so 0.4 s left still shows 0:01 until it runs out.
    // The HUD clock's pattern, in the culture's digits.
    const int32 Whole = FMath::Max(0, FMath::CeilToInt(Seconds));
    FNumberFormattingOptions TwoDigits = FNumberFormattingOptions::DefaultNoGrouping();
    TwoDigits.SetMinimumIntegralDigits(2);
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Minutes"), FText::AsNumber(Whole / 60, &FNumberFormattingOptions::DefaultNoGrouping()));
    Arguments.Add(TEXT("Seconds"), FText::AsNumber(Whole % 60, &TwoDigits));
    return UPSLocalization::Format(TEXT("HUD.GameClock"), Arguments).ToString();
}
