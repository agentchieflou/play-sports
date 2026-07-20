// PSCombatRulesModel.h - Epic 139: deterministic tackle-damage resolution
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSArchetypeTuning.h"
#include "PSCombatRulesModel.generated.h"

/** Rolls deterministic tackle damage against a ball carrier's hitpoints (Epic 139).
 *  A tackle is a damage event, not an automatic play-ender -- the caller applies the
 *  returned amount to the carrier's UPSHealthComponent and only ends the play if that
 *  brings hitpoints to 0. */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSCombatRulesModel : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Combat")
    void SeedDeterminism(int32 Seed);

    /** Base damage scales with the tackler's Strength relative to the carrier's
     *  Strength+Agility, then is scaled by the carrier's archetype damage multiplier
     *  (linemen are tankier, skill players are squishier) and a small random spread.
     *  Deterministic given the same seed + call order (replay-safe). */
    UFUNCTION(BlueprintCallable, Category = "Combat")
    float ResolveTackleDamage(const FPlayerAttributes& Carrier, const FPlayerAttributes& Tackler, const FPSArchetypeTuning& Tuning);

private:
    UPROPERTY(Transient)
    FRandomStream DeterminismStream;
};
