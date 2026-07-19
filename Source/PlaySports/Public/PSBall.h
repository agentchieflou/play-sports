#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSBall.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

/**
 * APSBall represents the physical football in the game world, capable of snapping, throwing, catching, fumbling, and bouncing.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSBall : public AActor
{
    GENERATED_BODY()

public:
    APSBall();

    virtual void Tick(float DeltaTime) override;

    // Returns the Sphere Component used for collision
    USphereComponent* GetCollisionComponent() const { return CollisionComponent; }

    // Returns the Projectile Movement Component
    UProjectileMovementComponent* GetProjectileMovement() const { return ProjectileMovement; }

    // Launch the ball towards a target velocity vector
    UFUNCTION(BlueprintCallable, Category = "Ball")
    void Launch(const FVector& Velocity);

    // Stop ball physics and attach it to a pawn (carrier hand socket)
    UFUNCTION(BlueprintCallable, Category = "Ball")
    void AttachToCarrier(class APawn* Carrier, FName SocketName = TEXT("HandSocket"));

    // Detach ball from any parent and enable physics/bouncing
    UFUNCTION(BlueprintCallable, Category = "Ball")
    void DetachFromCarrier();

    // Spin rate (degrees/sec) for visual spiral effect
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball")
    float SpiralSpinRate;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USphereComponent* CollisionComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UStaticMeshComponent* MeshComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UProjectileMovementComponent* ProjectileMovement;

private:
    float CurrentRollSpin;
};
