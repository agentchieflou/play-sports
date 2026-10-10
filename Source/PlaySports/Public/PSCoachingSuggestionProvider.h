#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PSCoachingData.h"
#include "PSCoachingSuggestionProvider.generated.h"

UINTERFACE(BlueprintType)
class PLAYSPORTS_API UPSCoachingSuggestionProvider : public UInterface
{
    GENERATED_BODY()
};

/** Optional external play-call advisor, gated behind the Epic 25 AgenticLink bridge.
 *  UPSGameIntelligenceSubsystem implements it (Epic 82): UPSPlayCallSubsystem registers it on
 *  its coaching AI, and it suggests the play an outside model answered through the bridge.
 *  UPSCoachingAI falls back to its internal situational weighting whenever the provider
 *  suggests None or a play that isn't among its candidates. */
class PLAYSPORTS_API IPSCoachingSuggestionProvider
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, Category = "AI|Coaching")
    FName SuggestPlay(const FPSSituationContext& Situation, bool bOffense);
};
