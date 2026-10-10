// PSFieldSides.h - which team's players line up on each side of the ball
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSPlayerAttributes.h"
#include "PSTelemetryBus.h"
#include "PSFieldSides.generated.h"

class UPSRoster;

/**
 * UPSFieldSides puts the team with the ball on offense. The game mode's field roster (UPSRoster)
 * holds both teams' players for the whole match (UPSMatchSetup::LoadFieldPlayers), so every
 * pawn always points at a live roster row; this decides which of them each side draws from,
 * through the roster's depth chart:
 *
 *  - Every offensive role lists only the players of the team with the ball, every defensive
 *    role only the other team's, each in roster order (each team's own depth order). The
 *    personnel manager picks every package, substitution and the 4th-down extra defender from
 *    the depth chart, so it fields only the right team on each side.
 *  - The possession authority is UPSPlaySimulation. When its GameState on the bus says the
 *    ball changed hands (a turnover, a kick), the depth chart is arranged for the new
 *    offense. At the next play's refill (UPSPersonnelManager::BeginNewPlay, which the game mode
 *    calls as it resets the pawns) the new teams come on, role for role on the same pawns.
 *  - A roster with no home players named (one roster playing both sides) is left as it is.
 */
UCLASS()
class PLAYSPORTS_API UPSFieldSides : public UObject
{
    GENERATED_BODY()

public:
    /** The field roster, and which of its players are the home team's (the rest are the away
     *  team's). Arranges the roster for the home team's ball: it has the ball first. */
    void Initialize(UPSRoster* InRoster, const TArray<FName>& InHomePlayerIds);

    /** Orders the roster's depth chart so the team with the ball is the offense (see the
     *  class comment). */
    void ArrangeForPossession(bool bHomeHasBall);

    /** True while the home team's players are the offense. */
    bool IsHomeOnOffense() const { return bHomeOnOffense; }

    /** Follows the possession authority's GameState on Bus. */
    void BindToBus(UPSTelemetryBus* Bus);

    void UnbindFromBus();

    /** The bus's GameState: rearranges the sides when the ball has changed hands. */
    void HandleGameState(const FPSTelemetryGameStateEvent& Event);

private:
    UPROPERTY(Transient)
    UPSRoster* Roster = nullptr;

    TSet<FName> HomePlayerIds;
    bool bHomeOnOffense = true;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    FDelegateHandle GameStateHandle;
};
