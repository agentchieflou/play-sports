#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSBallResolutionHelpers.h"
#include "PSTelemetryBus.h"
#include "PSBall.generated.h"

class APSPlayerPawn;

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

    // Is the ball currently fumbled/live on the ground
    UPROPERTY(BlueprintReadOnly, Category = "Ball")
    bool bIsFumbled;

    // Fumble the ball and launch it with a specific velocity
    UFUNCTION(BlueprintCallable, Category = "Ball")
    void Fumble(const FVector& LaunchVelocity);

    /** Catch/interception/fumble-recovery probability tuning (Epic C4: extracted
     *  out of OnBallOverlap's inline formulas per AGENTS.md rule 4). DataTable
     *  takes priority; falls back to CatchTuningJsonPath; falls back to the
     *  struct's built-in defaults (which match the original hardcoded values). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tuning")
    UDataTable* CatchTuningTable;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tuning")
    FString CatchTuningJsonPath;

    UPROPERTY(BlueprintReadOnly, Category = "Tuning")
    FCatchTuningRow CatchTuningSettings;

    /**
     * A player reached the ball while it was loose or in the air (the overlap calls this): a
     * fumble's recovery roll, else a pass's catch roll for the offense or interception roll for
     * the defense. True when he took the ball. Each outcome goes out on the bus (Fumble, Catch;
     * an interception also downs the pass's intended receiver, Epic 140) and the play
     * simulation, the outcome authority, moves the play on from there (rules 5 and 6): the ball
     * never reaches into the game mode or the simulation. A ball held, or at rest and not
     * fumbled, can't be taken.
     */
    UFUNCTION(BlueprintCallable, Category = "Ball")
    bool ResolveTouch(APSPlayerPawn* PlayerPawn);

    /** The ball came down (the bounce calls this): a thrown or kicked ball nobody holds, not a
     *  fumble, is reported on the bus (BallGrounded) once per flight; the play simulation rules
     *  on it. True when reported. */
    UFUNCTION(BlueprintCallable, Category = "Ball")
    bool ReportGrounded();

    /** Hears the passes on the bus: their intended receivers (Epic 140). BeginPlay binds;
     *  headless tests call it. Idempotent. */
    void BindToBus();

    void UnbindFromBus();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USphereComponent* CollisionComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UStaticMeshComponent* MeshComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UProjectileMovementComponent* ProjectileMovement;

private:
    float CurrentRollSpin;

    /** Intended receiver of the most recently thrown pass (Epic 140: an interception
     *  auto-kills this player, not whoever the ball happens to hit). Cleared once
     *  consumed by an interception so it can't leak into a later, unrelated play. */
    FString LastThrowTargetName;

    /** This flight's landing was reported (ReportGrounded); a new launch or a catch clears it. */
    bool bGroundedReported = false;

    UFUNCTION()
    void OnBallOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    UFUNCTION()
    void OnBallBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity);

    void HandleBusThrow(const FPSTelemetryThrowEvent& Event);

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    FDelegateHandle ThrowHandle;
};
