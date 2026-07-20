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

/** Optional external play-call advisor, gated behind the Epic 25 AgenticLink bridge
 *  actually existing. UPSCoachingAI falls back to its internal situational weighting
 *  whenever no provider is registered (the bridge plugin is a stub today -- see
 *  Plugins/AgenticLink -- so nothing implements this interface yet). */
class PLAYSPORTS_API IPSCoachingSuggestionProvider
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, Category = "AI|Coaching")
    FName SuggestPlay(const FPSSituationContext& Situation, bool bOffense);
};
