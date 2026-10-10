// PSSituationAI.h - Epic 76: the coaching AI's read of the game's leverage moments
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PSCoachingData.h"
#include "PSPlaybookData.h"
#include "PSSituationData.h"
#include "PSSituationAI.generated.h"

/**
 * UPSSituationAI reads the clock, the score and the field the way a coach does at the end of
 * a half (Epic 76): it names the situation (two-minute drill, four-minute offense, victory
 * formation), picks the offense's tempo, decides when a running clock needs a timeout, a
 * spike or a kneel, says whether the ball carrier heads for the sideline or stays in bounds,
 * and adjusts play weights for the play caller (UPSCoachingAI) with the reasons.
 *
 * Every situation is read from the possessing team's side (FPSSituationContext); the
 * defense's decisions read the same snapshot. Pure logic over FPSSituationalTuning
 * (Data/situational_tuning.json), so every decision is testable without a world.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSSituationAI : public UObject
{
    GENERATED_BODY()

public:
    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    void SetTuning(const FPSSituationalTuning& InTuning) { Tuning = InTuning; }

    const FPSSituationalTuning& GetTuning() const { return Tuning; }

    UFUNCTION(BlueprintPure, Category = "AI|Situation")
    EPSGameSituation ClassifySituation(const FPSSituationContext& Situation) const;

    /** True when the leading offense can kneel until the half ends (4th quarter), or when an
     *  offense backed up at the end of the 1st half should: every kneel's play time plus the
     *  milked play clock between kneels the defense has no timeout to stop covers the time. */
    UFUNCTION(BlueprintPure, Category = "AI|Situation")
    bool ShouldKneelOut(const FPSSituationContext& Situation) const;

    /** The CPU offense's tempo for the situation. */
    UFUNCTION(BlueprintPure, Category = "AI|Situation")
    EPSTempo ChooseTempo(const FPSSituationContext& Situation) const;

    /** A spike or a kneel takes its own tempo; any other play keeps Tempo. */
    UFUNCTION(BlueprintPure, Category = "AI|Situation")
    EPSTempo GetTempoForPlay(const FPSPlayDefinition& Play, EPSTempo Tempo) const;

    /** The play-clock reading Tempo snaps at (FPSTempoDef::SnapAtPlayClockSeconds). */
    UFUNCTION(BlueprintPure, Category = "AI|Situation")
    float GetSnapPlayClock(EPSTempo Tempo) const;

    /** The tempo's definition, or null when the tuning has none. */
    const FPSTempoDef* FindTempo(EPSTempo Tempo) const;

    /** The tempo after Current in the human's cycle (the first one when Current isn't in it). */
    UFUNCTION(BlueprintPure, Category = "AI|Situation")
    EPSTempo GetNextHumanTempo(EPSTempo Current) const;

    /** The offense's clock play for the snap: Kneel in victory formation; Spike in the
     *  two-minute drill when the clock runs under ClockUrgencySeconds, no timeout is left, the
     *  down is MaxSpikeDown or earlier and there is time for another play; else None. */
    UFUNCTION(BlueprintPure, Category = "AI|Situation")
    EPSClockPlay DecideClockPlay(const FPSSituationContext& Situation) const;

    /** Whether the side calls a timeout now. The offense does in the two-minute drill with
     *  the clock running under ClockUrgencySeconds; the defense does against a four-minute
     *  offense it can still catch, with the clock running under DefenseTimeoutWindowSeconds.
     *  Never with none left or the clock stopped. */
    UFUNCTION(BlueprintPure, Category = "AI|Situation")
    bool ShouldCallTimeout(const FPSSituationContext& Situation, bool bForOffense) const;

    /** Get out of bounds in the two-minute drill, stay in bounds when protecting a lead. */
    UFUNCTION(BlueprintPure, Category = "AI|Situation")
    EPSBoundaryIntent GetBoundaryIntent(const FPSSituationContext& Situation) const;

    /**
     * The situation's change to a play's weight for the side calling it, each rule that moved
     * it adding a reason to OutReasons. A spike or kneel play gets ClockPlayWeight when the
     * clock calls for it; bOutExcluded is set when it doesn't (it must not be called at all).
     */
    float GetPlayAdjustment(const FPSPlayDefinition& Play, const FPSSituationContext& Situation, bool bOffense, TArray<FString>* OutReasons, bool& bOutExcluded) const;

    /** Whether the play's routes take a catch toward the sideline (any SidelineRouteIds route). */
    bool IsSidelinePlay(const FPSPlayDefinition& Play) const;

    /** Whether the play throws only inside the numbers (MiddleRouteIds routes and no sideline one). */
    bool IsMiddlePlay(const FPSPlayDefinition& Play) const;

    /** "Two-minute drill", "Four-minute offense", ... for the call screen. */
    static FString DescribeSituation(EPSGameSituation Situation);

private:
    UPROPERTY(Transient)
    FPSSituationalTuning Tuning;
};
