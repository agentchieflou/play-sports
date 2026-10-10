// PSPlayerAging.h - Epic 94: aging curves per role and retirements
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSLegacyData.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerProgression.h"
#include "PSPlayerAging.generated.h"

class UPSContractManager;
class UPSLeagueHistory;
class UPSLockerRoom;
class UPSRoster;
class UPSStatsEngine;
class UPSWeeklyPreparation;

/**
 * UPSPlayerAging runs the off-season's turn of the years (Epic 94), tuned in Data/legacy.json. It
 * keeps no facts of its own: it reads the authorities and changes the rosters.
 *
 *  - Retirements: a veteran's chance grows with his age past MinAge, and rises when his rating has
 *    fallen under LowRating, when he ends the season hurt (Epic 90's preparation) or unhappy (Epic
 *    91's locker room); at ForcedAge he goes. At most MaxRetirementShare of a roster retires a
 *    season, the likeliest first, so the draft (Epic 86) can refill it. A retiree leaves his roster,
 *    his contract ends (UPSContractManager::RetirePlayer: the guarantees he hasn't earned end with
 *    it, the bonus already paid stays as dead money), and the league's history records his career
 *    (UPSLeagueHistory).
 *  - Aging: everyone else ages a year along his role's curve, Core 19's progression model
 *    (UPSPlayerProgression) at that role's ages: a running back peaks early and falls fast, a
 *    quarterback late. A player's snap share is his place on the depth chart (1 for the first, a
 *    half for the second, ...), so buried backups grow slower.
 *
 * A player's age is his FPlayerAttributes::Age (Epic 122), or the contract manager's default for
 * one without (UPSContractManager::GetPlayerAge); without either he neither ages nor retires.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSPlayerAging : public UObject
{
    GENERATED_BODY()

public:
    /** The legacy tuning (Data/legacy.json) and Core 19's age curve (Data/player_progression.json)
     *  for the roles it doesn't list. */
    UFUNCTION(BlueprintCallable, Category = "Legacy")
    bool LoadDefaults();

    void SetTuning(const FPSLegacyTuning& InTuning) { Tuning = InTuning; }

    void SetBaseCurve(const FPSProgressionTuning& InCurve) { BaseCurve = InCurve; }

    UFUNCTION(BlueprintPure, Category = "Legacy")
    const FPSLegacyTuning& GetTuning() const { return Tuning; }

    /** Role's age curve: its own, else Core 19's. */
    UFUNCTION(BlueprintPure, Category = "Legacy")
    FPSProgressionTuning GetCurve(EPlayerRole Role) const;

    /** Player's share of the snaps as Roster's depth chart has it: 1 / (1 + his place at his role);
     *  0 off the chart. */
    UFUNCTION(BlueprintPure, Category = "Legacy")
    static float GetSnapShare(const UPSRoster* Roster, const FPlayerAttributes& Player);

    /** Player's chance of retiring at Age with Morale (0-1), hurt or not, and its biggest reason. */
    UFUNCTION(BlueprintPure, Category = "Legacy")
    float GetRetirementChance(const FPlayerAttributes& Player, int32 Age, float Morale, bool bInjured, FString& OutReason) const;

    /** TeamId's off-season after Season: its retirements (rolled, capped, recorded in History, their
     *  deals ended in Contracts when given), then everyone else a year older along his curve. Stats, the
     *  preparation and the locker room may be null. Returns the retirements. */
    UFUNCTION(BlueprintCallable, Category = "Legacy")
    TArray<FPSRetirementDecision> RunOffseason(FName TeamId, UPSRoster* Roster, int32 Season, UPSContractManager* Contracts, const UPSStatsEngine* Stats,
        const UPSWeeklyPreparation* Preparation, const UPSLockerRoom* LockerRoom, UPSLeagueHistory* History);

private:
    UPROPERTY(Transient)
    FPSLegacyTuning Tuning;

    UPROPERTY(Transient)
    FPSProgressionTuning BaseCurve;

    UPROPERTY(Transient)
    UPSPlayerProgression* Progression = nullptr;
};
