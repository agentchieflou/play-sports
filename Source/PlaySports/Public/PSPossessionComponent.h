// PSPossessionComponent.h - Epic C3: extracted possession state from APSPlayerPawn
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSPossessionComponent.generated.h"

class APSPlayerPawn;

/**
 * UPSPossessionComponent encapsulates possession state and the transfer API
 * that was previously inlined in APSPlayerPawn. By extracting this into a
 * component we respect the "new system = new class" rule (AGENTS.md rule 1)
 * and keep APSPlayerPawn's line count down.
 *
 * The component publishes nothing to the bus directly; the caller (PSBall,
 * PSGameMode) publishes after a transfer completes.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSPossessionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSPossessionComponent();

    /** True while this pawn holds the ball. */
    UFUNCTION(BlueprintPure, Category = "Possession")
    bool HasPossession() const { return bHasPossession; }

    /** Mark this pawn as holding the ball. */
    UFUNCTION(BlueprintCallable, Category = "Possession")
    void GainPossession();

    /** Mark this pawn as no longer holding the ball. */
    UFUNCTION(BlueprintCallable, Category = "Possession")
    void LosePossession();

    /**
     * Transfer possession to TargetPawn: this pawn loses it, target gains it.
     * Returns true on success, false if TargetPawn is null.
     * Caller is responsible for updating APSBall attachment.
     */
    UFUNCTION(BlueprintCallable, Category = "Possession")
    bool TransferPossessionTo(APSPlayerPawn* TargetPawn);

private:
    UPROPERTY(BlueprintReadOnly, Category = "Possession", meta = (AllowPrivateAccess = "true"))
    bool bHasPossession = false;
};
