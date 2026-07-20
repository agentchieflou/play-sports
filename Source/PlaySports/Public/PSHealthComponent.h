// PSHealthComponent.h - Epic 139: per-pawn hitpoint tracking for the combat rules layer
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSHealthComponent.generated.h"

/**
 * UPSHealthComponent tracks a player pawn's hitpoints for the current play. A tackle
 * (or other contact) deals damage rather than unconditionally ending the play -- the
 * snap isn't over until the ball carrier is downed (HP reaches 0). Cross-play
 * availability (sit-out-next-play, respawn) is tracked separately by UPSRoster, which
 * remains the single authority for "who's available" (Architecture rule 6); this
 * component only owns the live in-play HP pool, mirroring how CurrentStamina/MaxStamina
 * are tracked locally on APSPlayerPawn.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSHealthComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSHealthComponent();

    /** Sets MaxHitPoints and fully heals CurrentHitPoints to match. */
    UFUNCTION(BlueprintCallable, Category = "Health")
    void Initialize(float InMaxHitPoints);

    UFUNCTION(BlueprintPure, Category = "Health")
    float GetCurrentHitPoints() const { return CurrentHitPoints; }

    UFUNCTION(BlueprintPure, Category = "Health")
    float GetMaxHitPoints() const { return MaxHitPoints; }

    UFUNCTION(BlueprintPure, Category = "Health")
    bool IsDowned() const { return bIsDowned; }

    /** Reduces CurrentHitPoints by Amount. Returns true if this damage brought the
     *  pawn's hitpoints to 0 or below (i.e. it just became downed). */
    UFUNCTION(BlueprintCallable, Category = "Health")
    bool ApplyDamage(float Amount);

    /** Immediately zeroes hitpoints and marks the pawn downed, regardless of current HP
     *  (used for rule-driven kills, e.g. the interception auto-kill). */
    UFUNCTION(BlueprintCallable, Category = "Health")
    void Kill();

    /** Restores hitpoints to MaxHitPoints and clears the downed flag. */
    UFUNCTION(BlueprintCallable, Category = "Health")
    void Respawn();

private:
    UPROPERTY(BlueprintReadOnly, Category = "Health", meta = (AllowPrivateAccess = "true"))
    float CurrentHitPoints = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Health", meta = (AllowPrivateAccess = "true"))
    float MaxHitPoints = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Health", meta = (AllowPrivateAccess = "true"))
    bool bIsDowned = false;
};
