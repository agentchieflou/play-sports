#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerPawn.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class UFloatingPawnMovement;

/**
 * APSPlayerPawn represents any physical player on the field, initialized from an FPlayerAttributes row.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSPlayerPawn : public APawn
{
    GENERATED_BODY()

public:
    APSPlayerPawn();

    // Initialize the pawn with specific player attributes
    UFUNCTION(BlueprintCallable, Category = "Player")
    void InitializePlayer(const FPlayerAttributes& InAttributes);

    // Get the player's current attributes
    UFUNCTION(BlueprintPure, Category = "Player")
    FPlayerAttributes GetAttributes() const { return Attributes; }

    // Request the pawn's controller to move to a location
    UFUNCTION(BlueprintCallable, Category = "Movement")
    void MoveToLocation(const FVector& TargetLocation);

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UCapsuleComponent* CapsuleComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UStaticMeshComponent* MeshComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UFloatingPawnMovement* MovementComponent;

    UPROPERTY(BlueprintReadOnly, Category = "Player")
    FPlayerAttributes Attributes;
};
