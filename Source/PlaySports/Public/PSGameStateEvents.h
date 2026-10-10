// PSGameStateEvents.h - Epic 33: the game state the broadcast shows, as the play simulation states it
#pragma once

#include "CoreMinimal.h"
#include "PSPlaySimulation.h"
#include "PSTelemetryBus.h"

/**
 * One definition of the game-state facts the play simulation (the authority, rule 6) announces
 * on the bus and the broadcast overlay shows: whether the clocks run, the event built from the
 * sim's state, and how the broadcast writes a quarter and a clock. No world access; pure.
 */
namespace PSGameStateEvents
{
    /** The game clock runs during the play, and before the snap while the clock was left
     *  running; never while a play is being scored. UPSPlaySimulation::AdvancePlay ticks it by
     *  this rule. */
    PLAYSPORTS_API bool IsGameClockRunning(const FPlayState& State);

    /** The play clock runs before the snap. */
    PLAYSPORTS_API bool IsPlayClockRunning(const FPlayState& State);

    /** The GameState event for State, with the last finished drive. */
    PLAYSPORTS_API FPSTelemetryGameStateEvent MakeEvent(const FPlayState& State, const FDriveSummary& LastDrive, int32 CompletedDrives, int32 MaxTimeouts);

    /** "1st", "2nd", "3rd", "4th"; "OT" after the 4th. */
    PLAYSPORTS_API FString QuarterLabel(int32 Quarter);

    /** A game clock as a broadcast shows it: "12:05", "0:42". Never negative. */
    PLAYSPORTS_API FString ClockText(float Seconds);
}
