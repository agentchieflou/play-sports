#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerPawn.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class UFloatingPawnMovement;

UENUM(BlueprintType)
enum class EPSTeamSide : uint8
{
    Offense UMETA(DisplayName = "Offense"),
    Defense UMETA(DisplayName = "Defense")
};

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

    // Give the ball to this player pawn
    UFUNCTION(BlueprintCallable, Category = "Player")
    void GainPossession();

    // Take the ball away from this player pawn
    UFUNCTION(BlueprintCallable, Category = "Player")
    void LosePossession();

    // Transfer the ball from this player pawn to a target player pawn
    UFUNCTION(BlueprintCallable, Category = "Player")
    bool TransferPossessionTo(APSPlayerPawn* TargetPlayerPawn);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player")
    EPSTeamSide TeamSide;

    // Called to bind functionality to input
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    // Input handlers
    void MoveForward(float Value);
    void MoveRight(float Value);

    // Called every frame
    virtual void Tick(float DeltaSeconds) override;

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

    UPROPERTY(BlueprintReadOnly, Category = "Player")
    bool bHasPossession;

    UPROPERTY(BlueprintReadOnly, Category = "Player|Movement")
    float BaseAcceleration;
};
