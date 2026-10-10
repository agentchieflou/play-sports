// PSPreSnapInputComponent.h - Epic 66: the human offense's pre-snap buttons
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSPreSnapInputComponent.generated.h"

class APSPlayerController;
class APSPlayerPawn;
class UPSPreSnapSubsystem;

/**
 * UPSPreSnapInputComponent turns the PreSnap context's buttons into the offense's pre-snap
 * calls on UPSPreSnapSubsystem (Epic 66), while the player controls an offensive pawn:
 *
 *   Audible      the next play in the formation
 *   Select       the next eligible receiver, left to right across the field
 *   HotRoute     the selected receiver's next allowed route
 *   Motion       the selected receiver goes in motion
 *   Slide        the line's slide: none, left, right
 *   Protection   the selected back or tight end: kept in, released, or as called
 *
 * Which buttons these are is data (FPreSnapTuningRow's actions, bound in
 * Data/input_actions.json's PreSnap context). The calls themselves, their rules and their
 * announcement are the subsystem's.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSPreSnapInputComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSPreSnapInputComponent();

    /** Listens to the owning controller's catalog actions. Idempotent. */
    void BindToController();

    /** Does what ActionId asks. False when it is no pre-snap action, the player isn't on
     *  offense, or the call can't be made now. Public so headless tests can press a button. */
    UFUNCTION(BlueprintCallable, Category = "PreSnap")
    bool PressAction(FName ActionId);

    /** The receivers Select runs through: the controlled player's eligible teammates (not
     *  himself), left to right across the field. */
    UFUNCTION(BlueprintPure, Category = "PreSnap")
    TArray<APSPlayerPawn*> GetSelectablePlayers() const;

    /** The receiver HotRoute, Motion and Protection act on; the first selectable one until
     *  Select is pressed. */
    UFUNCTION(BlueprintPure, Category = "PreSnap")
    APSPlayerPawn* GetSelectedPlayer() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleActionStarted(FName ActionId);

    APSPlayerController* GetPlayerController() const;
    UPSPreSnapSubsystem* GetPreSnap() const;

    int32 SelectedIndex = 0;
};
