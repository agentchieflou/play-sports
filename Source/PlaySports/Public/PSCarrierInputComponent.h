// PSCarrierInputComponent.h - Epic 104.2: the human ball carrier's move buttons
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSCarrierInputComponent.generated.h"

class APSPlayerController;

/**
 * UPSCarrierInputComponent turns the BallCarrier context's move buttons into moves for the
 * pawn its APSPlayerController controls (Epic 104.2). Which button does which move is the move
 * set's data (Data/carrier_moves.json, each move's ActionId); the move itself -- gating,
 * stamina, physics, tackle odds -- is the pawn's UPSCarrierMoveComponent. The Move stick picks
 * a juke's side.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSCarrierInputComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSCarrierInputComponent();

    /** Listens to the owning controller's catalog actions. Idempotent. */
    void BindToController();

    /** Does the move ActionId names for the controlled pawn. False when it names no move or the
     *  move can't be done now. Public so headless tests can press a button. */
    UFUNCTION(BlueprintCallable, Category = "Moves")
    bool PressMoveAction(FName ActionId);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleActionStarted(FName ActionId);

    APSPlayerController* GetPlayerController() const;
};
