// PSFieldReads.h - Epic 14: what a player can read off the field, shared by the game and the AI
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerPawn.h"

/**
 * Pure reads over the pawns on the field: the run lane the line opened, the nearest opponent,
 * how open a receiver is. One copy for the game mode and the AI (Architecture rule 3: no
 * twins), with no world access of their own -- callers pass the pawns.
 */
namespace PSFieldReads
{
    /** Centre of the widest gap between adjacent offensive linemen (sorted across the
     *  field); a lone lineman's spot; zero with none. */
    PLAYSPORTS_API FVector LargestRunLaneGap(const TArray<APSPlayerPawn*>& Pawns);

    /** The nearest pawn on the other side from From, or null. Downed pawns don't count. */
    PLAYSPORTS_API APSPlayerPawn* NearestOpponent(const TArray<APSPlayerPawn*>& Pawns, EPSTeamSide FromSide, const FVector& From, float* OutDistance = nullptr);

    /** How open a receiver is: the distance to the nearest defender (a large number with none). */
    PLAYSPORTS_API float Separation(const TArray<APSPlayerPawn*>& Pawns, const APSPlayerPawn* Receiver);
}
