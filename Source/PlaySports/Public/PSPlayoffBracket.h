#pragma once

#include "CoreMinimal.h"
#include "PSLeagueData.h"
#include "PSPlayoffBracket.generated.h"

/** Generates a single-elimination bracket from final regular-season standings
 *  (Epic 20). Seeds NumPlayoffTeams by win percentage; round 1 pairs
 *  1-vs-N, 2-vs-(N-1), etc. (standard seeded bracket, no play-in games). */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSPlayoffBracket : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Franchise|Playoffs")
    TArray<FPSPlayoffMatchup> GenerateBracket(const TArray<FPSTeamStanding>& FinalStandings, int32 NumPlayoffTeams);
};
