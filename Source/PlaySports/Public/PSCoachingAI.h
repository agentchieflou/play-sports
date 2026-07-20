#pragma once

#include "CoreMinimal.h"
#include "PSCoachingData.h"
#include "PSCoachingSuggestionProvider.h"
#include "PSPlaybookData.h"
#include "PSCoachingAI.generated.h"

/**
 * CPU play-selection AI (Epic 18): weights play categories from the down/distance/
 * clock/score situation and a per-opponent tendency profile, then picks among the
 * matching plays in the active playbook. Also owns 4th-down, 2-point, and clock
 * management decisions. If an external suggestion provider is registered (the
 * Epic 25 AgenticLink bridge, once it exists), its suggestion is used instead.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSCoachingAI : public UObject
{
    GENERATED_BODY()

public:
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

    UFUNCTION(BlueprintPure, Category = "AI|Coaching")
    bool ShouldGoForItOnFourthDown(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency) const;

    /** ScoreDifferentialAfterTD is the possessing team's lead/deficit assuming the
     *  extra point is missed (i.e. before the conversion attempt is resolved). */
    UFUNCTION(BlueprintPure, Category = "AI|Coaching")
    bool ShouldAttemptTwoPointConversion(int32 ScoreDifferentialAfterTD, int32 Quarter, const FPSTendencyProfile& Tendency) const;

    UFUNCTION(BlueprintPure, Category = "AI|Coaching")
    bool ShouldCallTimeoutForClockManagement(const FPSSituationContext& Situation, bool bIsTrailing) const;

private:
    float GetSituationalCategoryWeight(const FString& Category, const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, bool bOffense) const;

    FName SelectWeightedPlay(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, const TArray<FPSPlayDefinition>& Candidates, bool bOffense);

    UPROPERTY(Transient)
    FRandomStream DeterminismStream;

    UPROPERTY(Transient)
    TScriptInterface<IPSCoachingSuggestionProvider> SuggestionProvider;
};
