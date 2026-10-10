// PSDefenseInputComponent.h - Epic 104.5: the human defender's jump at the snap and strip button
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSTelemetryBus.h"
#include "PSDefenseInputComponent.generated.h"

class APSPlayerController;
class APSPlayerPawn;
class UPSDefenderTechniqueComponent;

/**
 * UPSDefenseInputComponent turns the human defender's buttons into technique (Epic 104.5):
 *
 *   - JumpSnap (PreSnap context): the first press before the snap is when he moves. At the snap
 *     it is judged against JumpWindowSeconds: inside it, a clean jump, and the controlled
 *     defender bursts off the line (UPSDefenderTechniqueComponent::GetOff); earlier, he is
 *     offside. Either way the jump goes on the bus (JumpSnap), where UPSPlaySimulation flags
 *     an offside one.
 *   - Strip (Defense context): the controlled defender rips at the ball
 *     (UPSDefenderTechniqueComponent::TryStrip). A press while the last attempt cools down
 *     waits in the input buffer.
 *
 * Which buttons and the timing are the controlled defender's technique data
 * (Data/defensive_techniques.json). Presses arrive through the controller's
 * UPSInputBufferComponent and the snap from UPSTelemetryBus. It keeps its own clock (ticked,
 * or advanced by AdvanceTime in headless tests).
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSDefenseInputComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSDefenseInputComponent();

    /** Listens to the owning controller's input buffer and gives it the strip's busy check.
     *  Idempotent. */
    void BindToController();

    /** Listens for the snap and the start of each play. Idempotent. */
    void BindToBus();

    void UnbindFromBus();

    /** The controlled defender moves now, before the snap. False when the human doesn't control
     *  a defender, the ball is already snapped, or he already moved this play. */
    UFUNCTION(BlueprintCallable, Category = "Defense")
    bool PressJumpSnap();

    /** The controlled defender rips at the ball. False when he can't now. */
    UFUNCTION(BlueprintCallable, Category = "Defense")
    bool PressStrip();

    /** True while ActionId is the strip and the controlled defender's last attempt still cools
     *  down, so the press waits in the input buffer. */
    bool IsDefenseActionBusy(FName ActionId);

    /** True once the human defender has moved this play and the snap hasn't come yet. */
    UFUNCTION(BlueprintPure, Category = "Defense")
    bool HasJumped() const { return JumpPawn.IsValid(); }

    /** Moves the component's clock on. Ticking does this; headless tests call it. */
    void AdvanceTime(float DeltaSeconds);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    UFUNCTION()
    void HandleActionPressed(FName ActionId, float HeldSeconds);

    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    APSPlayerPawn* GetControlledDefender() const;
    UPSDefenderTechniqueComponent* GetControlledTechnique() const;
    APSPlayerController* GetPlayerController() const;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;

    /** The defender who moved before this snap, and when. */
    TWeakObjectPtr<APSPlayerPawn> JumpPawn;
    float JumpPressedAt = 0.f;

    float Clock = 0.f;
    bool bSnapped = false;
};
