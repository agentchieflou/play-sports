#pragma once

#include "CoreMinimal.h"
#include "PSCoachingData.h"
#include "PSCoachingSuggestionProvider.h"
#include "PSPlaybookData.h"
#include "PSSituationData.h"
#include "PSCoachingAI.generated.h"

class UPSSituationAI;
class UPSSpecialTeamsAI;

/**
 * CPU play-selection AI (Epic 18): weights play categories from the down/distance/
 * clock/score situation and a per-opponent tendency profile, then picks among the
 * matching plays in the active playbook. Also owns 4th-down, 2-point, and clock
 * management decisions. If an external suggestion provider is registered (Epic 82's
 * UPSGameIntelligenceSubsystem, an outside model over the Epic 25 bridge) and suggests one of
 * the candidates, its suggestion is used instead.
 *
 * The end-of-half read (Epic 76) comes from its UPSSituationAI: the situation's play
 * weights (two-minute sideline throws, four-minute runs), and the clock's own calls -- a
 * kneel in victory formation or a spike with no timeout left is chosen outright, and a
 * spike or kneel play is never chosen when the clock doesn't call for it.
 *
 * Special teams (Epic 75) come from its UPSSpecialTeamsAI: on 4th down the punt, the field
 * goal or a fake (the 4th-down read above decides going for it), the kickoff or onside kick, and
 * the receiving team's return, block, hands team or laterals. A due special-teams call is chosen
 * outright; a special-teams play is never chosen otherwise, and a kickoff calls nothing else.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSCoachingAI : public UObject
{
    GENERATED_BODY()

public:
    UPSCoachingAI();

    UFUNCTION(BlueprintCallable, Category = "AI|Coaching")
    void SeedDeterminism(int32 Seed);

    UFUNCTION(BlueprintCallable, Category = "AI|Coaching")
    void SetSuggestionProvider(TScriptInterface<IPSCoachingSuggestionProvider> InProvider);

    /** Picks an offensive play from Candidates (all should have bIsOffensivePlay == true). */
    UFUNCTION(BlueprintCallable, Category = "AI|Coaching")
    FName SelectOffensivePlay(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, const TArray<FPSPlayDefinition>& Candidates);

    /** Picks a defensive call from Candidates (all should have bIsOffensivePlay == false). */
    UFUNCTION(BlueprintCallable, Category = "AI|Coaching")
    FName SelectDefensivePlay(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, const TArray<FPSPlayDefinition>& Candidates);

    /** Every candidate with its situational weight and the reasons behind it, best first
     *  (no roll, so a person sees the same ranking the CPU's weighting would favour). Feeds
     *  the play-call screen's suggestions (Epic 102). */
    UFUNCTION(BlueprintCallable, Category = "AI|Coaching")
    TArray<FPSPlaySuggestion> RankPlays(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, const TArray<FPSPlayDefinition>& Candidates, bool bOffense) const;

    UFUNCTION(BlueprintPure, Category = "AI|Coaching")
    bool ShouldGoForItOnFourthDown(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency) const;

    /** ScoreDifferentialAfterTD is the possessing team's lead/deficit assuming the
     *  extra point is missed (i.e. before the conversion attempt is resolved). */
    UFUNCTION(BlueprintPure, Category = "AI|Coaching")
    bool ShouldAttemptTwoPointConversion(int32 ScoreDifferentialAfterTD, int32 Quarter, const FPSTendencyProfile& Tendency) const;

    /** Whether the side calls a timeout now (UPSSituationAI::ShouldCallTimeout). Situation is
     *  the possessing team's; bForOffense picks the side deciding. */
    UFUNCTION(BlueprintPure, Category = "AI|Coaching")
    bool ShouldCallTimeout(const FPSSituationContext& Situation, bool bForOffense) const;

    /** The CPU offense's tempo for the situation, before a spike or kneel takes its own. */
    UFUNCTION(BlueprintPure, Category = "AI|Coaching")
    EPSTempo ChooseTempo(const FPSSituationContext& Situation) const;

    /** The situational read; Data/situational_tuning.json is loaded on first use. */
    UFUNCTION(BlueprintPure, Category = "AI|Coaching")
    UPSSituationAI* GetSituationAI() const;

    /** The special-teams calls; Data/special_teams.json is loaded on first use. */
    UFUNCTION(BlueprintPure, Category = "AI|Coaching")
    UPSSpecialTeamsAI* GetSpecialTeamsAI() const;

private:
    /** The play's weight for the situation: 1, moved by the down/distance rules, the
     *  end-of-half rules (UPSSituationAI) and the team's tendency. OutReasons, when given,
     *  collects one line per rule that moved it. 0 for a spike or kneel the clock doesn't
     *  call for, and for a special-teams play that isn't due (SpecialTeamsRoll, 0-1, drives the
     *  special-teams gambles; 1 takes none). */
    float GetPlayWeight(const FPSPlayDefinition& Play, const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, bool bOffense, TArray<FString>* OutReasons = nullptr, float SpecialTeamsRoll = 1.f) const;

    FName SelectWeightedPlay(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, const TArray<FPSPlayDefinition>& Candidates, bool bOffense);

    UPROPERTY(Transient)
    FRandomStream DeterminismStream;

    UPROPERTY(Transient)
    TScriptInterface<IPSCoachingSuggestionProvider> SuggestionProvider;

    UPROPERTY(Transient)
    UPSSituationAI* SituationAI = nullptr;

    /** The situational tuning file is read on first use, not at construction. */
    mutable bool bSituationTuningLoaded = false;

    UPROPERTY(Transient)
    UPSSpecialTeamsAI* SpecialTeamsAI = nullptr;

    mutable bool bSpecialTeamsTuningLoaded = false;
};
