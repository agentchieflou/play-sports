#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PSPlayerAttributes.h"
#include "PSPossessionComponent.h"
#include "PSPlayerPawn.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class UFloatingPawnMovement;
class APSGameMode;
class APSOffenseController;

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

    // Initialize the pawn with specific player attributes pointer (Epic C3: single source of truth)
    void InitializePlayerPointer(const FPlayerAttributes* InAttributes);

    // Get the player's current attributes
    UFUNCTION(BlueprintPure, Category = "Player")
    FPlayerAttributes GetAttributes() const { return AttributesPtr ? *AttributesPtr : Attributes; }

    // Request the pawn's controller to move to a location
    UFUNCTION(BlueprintCallable, Category = "Movement")
    void MoveToLocation(const FVector& TargetLocation);

    UFUNCTION(BlueprintPure, Category = "Movement")
    class UFloatingPawnMovement* GetFloatingMovementComponent() const { return MovementComponent; }

    UFUNCTION(BlueprintCallable, Category = "Player")
    void SetStartingLocation(const FVector& NewLoc) { StartingLocation = NewLoc; }

    UFUNCTION(BlueprintPure, Category = "Player")
    FVector GetStartingLocation() const { return StartingLocation; }

    UFUNCTION(BlueprintPure, Category = "Player")
    bool HasPossession() const { return bHasPossession; }

    // Give the ball to this player pawn
    UFUNCTION(BlueprintCallable, Category = "Player")
    void GainPossession();

    // Take the ball away from this player pawn
    UFUNCTION(BlueprintCallable, Category = "Player")
    void LosePossession();

    // Transfer the ball from this player pawn to a target player pawn
    UFUNCTION(BlueprintCallable, Category = "Player")
    bool TransferPossessionTo(APSPlayerPawn* TargetPlayerPawn);

    // Throw the ball to a target location
    UFUNCTION(BlueprintCallable, Category = "Player")
    bool ThrowPass(class APSBall* Ball, const FVector& TargetLocation, bool bHighArc = false);

    // Perform an instant handoff of the ball to a target player pawn
    UFUNCTION(BlueprintCallable, Category = "Player")
    bool ExecuteHandoff(APSPlayerPawn* TargetPlayer);

    // Perform a lateral/pitch toss of the ball to a target player pawn
    UFUNCTION(BlueprintCallable, Category = "Player")
    bool ExecutePitch(APSPlayerPawn* TargetPlayer);

    UFUNCTION(BlueprintCallable, Category = "Player|SpecialTeams")
    bool ExecuteKick(class APSBall* Ball, float KickPower, float LaunchAngle);

    // Fumble the ball, launching it with a pop-out velocity
    UFUNCTION(BlueprintCallable, Category = "Player")
    void FumbleBall();

    // Resolve a physical tackle collision contest against an incoming defender
    UFUNCTION(BlueprintCallable, Category = "Player|Tackling")
    bool ResolveTackle(APSPlayerPawn* Defender);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player")
    EPSTeamSide TeamSide;

    UFUNCTION(BlueprintPure, Category = "Possession")
    UPSPossessionComponent* GetPossessionComponent() const { return PossessionComponent; }

    UFUNCTION(BlueprintPure, Category = "BallAction")
    class UPSBallActionComponent* GetBallActionComponent() const { return BallActionComponent; }

    // Called to bind functionality to input
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    // Input handlers
    void MoveForward(float Value);
    void MoveRight(float Value);

    // Called every frame
    virtual void Tick(float DeltaSeconds) override;

    // Get the player's momentum vector (WeightKg * Velocity in m/s)
    UFUNCTION(BlueprintPure, Category = "Player|Physics")
    FVector GetMomentum() const;

    // Get the magnitude of the player's momentum (WeightKg * Speed in m/s)
    UFUNCTION(BlueprintPure, Category = "Player|Physics")
    float GetMomentumMagnitude() const;

    // Active state of fatigue and burst (stamina system hooks)
    UPROPERTY(BlueprintReadOnly, Category = "Player|Stamina")
    float CurrentStamina;

    UPROPERTY(BlueprintReadOnly, Category = "Player|Stamina")
    float MaxStamina;

    UPROPERTY(BlueprintReadWrite, Category = "Player|Stamina")
    bool bIsBursted;

    // Use burst speed (toggles bIsBursted)
    UFUNCTION(BlueprintCallable, Category = "Player|Stamina")
    void UseBurst(bool bEnable);

    // Apply fatigue (decreases CurrentStamina)
    UFUNCTION(BlueprintCallable, Category = "Player|Stamina")
    void ApplyFatigue(float Amount);

    // Reset fatigue (restores CurrentStamina to MaxStamina)
    UFUNCTION(BlueprintCallable, Category = "Player|Stamina")
    void ResetFatigue();

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UCapsuleComponent* CapsuleComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UStaticMeshComponent* MeshComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UFloatingPawnMovement* MovementComponent;

    /** Extracted possession state component (Epic C3). Callers continue to use
     *  GainPossession/LosePossession/HasPossession/TransferPossessionTo on the pawn;
     *  those methods delegate here. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UPSPossessionComponent* PossessionComponent;

    /** Extracted ball action mechanics component (Epic C3). Methods below delegate here. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class UPSBallActionComponent* BallActionComponent;


    const FPlayerAttributes* AttributesPtr = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "Player")
    FPlayerAttributes Attributes;

    UPROPERTY(BlueprintReadOnly, Category = "Player")
    bool bHasPossession;

    UPROPERTY(BlueprintReadOnly, Category = "Player|Blocking")
    float EngagementTime;

    UPROPERTY(BlueprintReadOnly, Category = "Player")
    FVector StartingLocation;

    UPROPERTY(BlueprintReadOnly, Category = "Player|Movement")
    float BaseAcceleration;

    /** Cached once in BeginPlay so Tick/InitializePlayer don't re-Cast<APSGameMode>
     *  and copy FMovementTuningRow by value every call (Epic C3: no per-frame
     *  tuning copies). */
    UPROPERTY(Transient)
    APSGameMode* CachedGameMode;

public:
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Player|Blocking")
    APSPlayerPawn* EngagedOpponent;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Player|Blocking")
    bool bIsEngaged;

private:
    UFUNCTION()
    void OnPawnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
