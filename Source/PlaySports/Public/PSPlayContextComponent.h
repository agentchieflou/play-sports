// PSPlayContextComponent.h - Epic 104: the input context follows the moment of the play
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSTelemetryBus.h"
#include "PSPlayContextComponent.generated.h"

class APSPlayerController;

/**
 * UPSPlayContextComponent keeps one gameplay-depth input context on its APSPlayerController's
 * stack, above the gameplay (OnField) context, matching what the controlled player is doing
 * (Epic 104.1):
 *
 *   PreSnap      before the snap and once the play is over
 *   Passing      the controlled QB holds the ball behind the line of scrimmage
 *   BallCarrier  the controlled player holds the ball anywhere else
 *   Defense      the controlled player is on defense during the play
 *   (none)       an offensive player without the ball during the play
 *
 * The contexts and their bindings are catalog data (Data/input_actions.json); this only
 * decides which is on. The snap and the end of the play come from UPSTelemetryBus (rule 5);
 * possession is the pawn's own (rule 6). It re-evaluates every tick; headless tests call
 * Refresh.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSPlayContextComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSPlayContextComponent();

    /** Listens for the snap and the end of the play. Idempotent. */
    void BindToBus();

    void UnbindFromBus();

    /** The depth context the moment calls for, or NAME_None. */
    UFUNCTION(BlueprintPure, Category = "Input")
    FName ComputeContext() const;

    /** Puts ComputeContext on the owning controller's stack. */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void Refresh();

    /** True from the snap to the end of the play. */
    UFUNCTION(BlueprintPure, Category = "Input")
    bool IsPlayLive() const { return bPlayLive; }

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    FName PreSnapContextId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    FName PassingContextId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    FName BallCarrierContextId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    FName DefenseContextId;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    APSPlayerController* GetPlayerController() const;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    FVector LineOfScrimmage = FVector::ZeroVector;
    bool bPlayLive = false;
};
