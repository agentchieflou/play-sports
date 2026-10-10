// PSCarrierInputComponent.h - Epic 104.2: the human ball carrier's move buttons
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSCarrierInputComponent.generated.h"

class APSPlayerController;
class UPSCarrierMoveComponent;

/**
 * UPSCarrierInputComponent turns the BallCarrier context's move buttons into moves for the
 * pawn its APSPlayerController controls (Epic 104.2). Which button does which move is the move
 * set's data (Data/carrier_moves.json, each move's ActionId); the move itself -- gating,
 * stamina, physics, tackle odds -- is the pawn's UPSCarrierMoveComponent. The Move stick picks
 * a juke's side.
 *
 * Presses come through the controller's UPSInputBufferComponent (Epic 104.4), which this
 * component tells when a move is busy: a move pressed while the carrier is committed to
 * another, or while it cools down, waits for the carrier instead of being lost.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSCarrierInputComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSCarrierInputComponent();

    /** Listens to the owning controller's input buffer (binding the buffer to the controller
     *  too) and gives it this component's busy check. Idempotent. */
    void BindToController();

    /** True while the move ActionId names can't start yet only because of timing (the
     *  controlled carrier is committed to a move, or this one cools down). */
    bool IsMoveActionBusy(FName ActionId);

    /** Does the move ActionId names for the controlled pawn. False when it names no move or the
     *  move can't be done now. Public so headless tests can press a button. */
    UFUNCTION(BlueprintCallable, Category = "Moves")
    bool PressMoveAction(FName ActionId);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleActionPressed(FName ActionId, float HeldSeconds);

    UPSCarrierMoveComponent* GetControlledMoves() const;

    APSPlayerController* GetPlayerController() const;
};
