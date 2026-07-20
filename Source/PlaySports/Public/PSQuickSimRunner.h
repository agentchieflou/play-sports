#pragma once

#include "CoreMinimal.h"
#include "PSPlaySimulation.h"
#include "PSQuickSimRunner.generated.h"

USTRUCT(BlueprintType)
struct FPSQuickSimResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int32 HomeScore = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 AwayScore = 0;
};

/** Resolves a non-played schedule game headlessly by driving UPSPlaySimulation's
 *  existing quick-sim statistical rolls to completion, with no rendering (Epic 20).
 *  Reuses UPSPlaySimulation as the single authority on play outcomes rather than
 *  duplicating scoring logic. */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSQuickSimRunner : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Franchise|QuickSim")
    FPSQuickSimResult SimulateGame(const TArray<FPlayerAttributes>& HomeRoster, const TArray<FPlayerAttributes>& AwayRoster);

    /** Safety cap on simulation ticks (AdvancePlay calls, not football plays -- each
     *  play takes several ticks to progress through phases) so a stuck game clock
     *  can't hang the season loop. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Franchise|QuickSim")
    int32 MaxPlaysPerGame = 2000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Franchise|QuickSim")
    float SecondsPerPlayAdvance = 6.f;
};
