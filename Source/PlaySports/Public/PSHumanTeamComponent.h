// PSHumanTeamComponent.h - a single human plays for one team, on offense and on defense
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "PSHumanTeamComponent.generated.h"

class APSPlayerController;
class UPSMatchSetup;

/**
 * UPSHumanTeamComponent keeps a single human on their own team when the ball changes hands.
 * The pawns keep their sides (offense attacks +X); UPSFieldSides moves each team's players
 * onto the side its possession calls for. This component moves the human with their team:
 * offense while their team has the ball, defense while the other team has it.
 *
 *  - The human's team is the match's (UPSMatchSetup, the one authority on it, handed over by
 *    the game mode as it is for the versus seats): the away team when the player's team is the
 *    visitor, otherwise the home team. Kept as a reference, never copied.
 *  - Who has the ball is the play simulation's, from its GameState on the bus (rule 6); this
 *    keeps no copy beyond the last event it heard.
 *  - On a change of side it sets the controller's HumanSide and DefaultControlRole (the
 *    side's control role from Data/control_handoff.json). A human holding a pawn of the other
 *    side lets go and takes that role's player; a human holding none (a CPU-only game, or
 *    before the first take at kickoff) is only pointed at the right side.
 *  - A controller seated in a head-to-head game (UPSVersusSubsystem) is left alone: the versus
 *    subsystem owns its seats' sides.
 *
 * APSPlayerController owns one; it binds at BeginPlay. Headless tests call BindToBus and
 * SetMatchSetup themselves and publish GameState events.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSHumanTeamComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSHumanTeamComponent();

    /** Follows the possession authority's GameState. Idempotent. */
    void BindToBus();

    void UnbindFromBus();

    /** The match whose team this human plays for. Puts the human on that team's side of the
     *  ball for the possession last heard (the home team's ball before any). */
    void SetMatchSetup(const UPSMatchSetup* InMatchSetup);

    /** True when this human plays for the home team. */
    UFUNCTION(BlueprintPure, Category = "Possession")
    bool PlaysForHome() const;

    /** The side this human's team is on for the possession last heard. */
    UFUNCTION(BlueprintPure, Category = "Possession")
    EPSTeamSide GetTeamSide() const;

    /** Puts the owning controller on GetTeamSide: its HumanSide and control role, and, when it
     *  holds a pawn of the other side, that side's control-role player instead. Nothing for a
     *  controller seated in a head-to-head game. */
    UFUNCTION(BlueprintCallable, Category = "Possession")
    void ApplyTeamSide();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void HandleGameState(const FPSTelemetryGameStateEvent& Event);

    APSPlayerController* GetPlayerController() const;

    /** The game mode's match setup: where this human's team is read. */
    TWeakObjectPtr<const UPSMatchSetup> MatchSetup;

    /** Who had the ball in the last GameState heard (the home team's before any). */
    bool bHomeHasPossession = true;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
};
