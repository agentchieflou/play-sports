// PSDefenderPreSnapInputComponent.h - Epic 67: the human defense's pre-snap buttons
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSDefenderPreSnapInputComponent.generated.h"

class APSPlayerController;
class APSPlayerPawn;
class UPSDefenderPreSnapSubsystem;

/**
 * UPSDefenderPreSnapInputComponent turns the DefensePreSnap context's buttons into the
 * defense's pre-snap calls on UPSDefenderPreSnapSubsystem (Epic 67). UPSPlayContextComponent
 * puts that context on the stack while the player controls a defender before the snap; the
 * offense's PreSnap buttons (UPSPreSnapInputComponent) are off then. The calls:
 *
 *   DefenseAudible  the next play of the front
 *   ShadowSelect    the next offensive receiver, left to right across the field
 *   Shadow          the AI defensive back nearest the selected receiver shadows him (again:
 *                   lets him go)
 *   ShowBlitz       linebackers walk up to show a blitz that isn't coming (toggles)
 *   DisguiseShell   the safeties show the other shell (toggles)
 *   Creep           the blitzers line up in coverage and creep up late (toggles)
 *
 * Which actions these are is data (FPSDefensivePreSnapTuning, Data/defensive_presnap.json);
 * their keys are the input catalog's. The calls, their rules and their announcement are the
 * subsystem's.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSDefenderPreSnapInputComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSDefenderPreSnapInputComponent();

    /** Listens to the owning controller's catalog actions. Idempotent. */
    void BindToController();

    /** Does what ActionId asks. False when it is no defensive pre-snap action, the player
     *  isn't on defense, or the call can't be made now. Public so headless tests can press. */
    UFUNCTION(BlueprintCallable, Category = "PreSnap")
    bool PressAction(FName ActionId);

    /** The receivers Select runs through: the offense's receivers, tight ends and backs, left
     *  to right across the field. Empty unless the player controls a defender. */
    UFUNCTION(BlueprintPure, Category = "PreSnap")
    TArray<APSPlayerPawn*> GetSelectableReceivers() const;

    /** The receiver Shadow acts on; the first selectable one until Select is pressed. */
    UFUNCTION(BlueprintPure, Category = "PreSnap")
    APSPlayerPawn* GetSelectedReceiver() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleActionStarted(FName ActionId);

    APSPlayerController* GetPlayerController() const;
    UPSDefenderPreSnapSubsystem* GetDefensePreSnap() const;

    /** The AI defensive back lined up nearest Receiver across the field. */
    APSPlayerPawn* FindNearestBack(const APSPlayerPawn* Receiver) const;

    int32 SelectedIndex = 0;
};
