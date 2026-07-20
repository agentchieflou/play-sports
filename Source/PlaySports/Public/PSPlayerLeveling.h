// PSPlayerLeveling.h - Epic 141: player leveling/XP progression
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSLevelingTuning.h"
#include "PSRoster.h"
#include "PSPlayerLeveling.generated.h"

/** Awards XP for a play and applies level-up attribute growth, modeled on
 *  UPSPlayerProgression's attribute-mutation shape but driven by per-play XP
 *  accrual rather than an offseason age curve. Reads/writes through UPSRoster's
 *  FPSPlayerLiveState, since UPSRoster remains the single authority for player
 *  state (Architecture rule 6). No monetization/store hooks -- gameplay only. */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSPlayerLeveling : public UObject
{
    GENERATED_BODY()

public:
    /** XP earned for one play: base participation XP, plus a touchdown bonus if
     *  bScoredTouchdown is set for this player. */
    UFUNCTION(BlueprintPure, Category = "Leveling")
    float ComputeXpForPlay(bool bScoredTouchdown, const FPSLevelingTuning& Tuning) const;

    /** Awards XpAmount to PlayerId via Roster and applies any level-ups the XP
     *  crosses (each level-up grows the player's stored attributes in place via
     *  Roster's mutable roster). Returns true if the player leveled up at least once. */
    UFUNCTION(BlueprintCallable, Category = "Leveling")
    bool AwardXpForPlay(UPSRoster* Roster, FName PlayerId, float XpAmount, const FPSLevelingTuning& Tuning);

private:
    void ApplyLevelUpGrowth(FPlayerAttributes& Attributes, const FPSLevelingTuning& Tuning) const;
};
