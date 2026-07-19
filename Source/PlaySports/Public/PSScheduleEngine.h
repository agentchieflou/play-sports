// PSScheduleEngine.h - renamed UPScheduleEngine -> UPSScheduleEngine (Epic C3 naming cleanup)
// Prefix was missing 'PS' per module conventions (AGENTS.md).
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PSScheduleEngine.generated.h"

USTRUCT(BlueprintType)
struct FSeasonWeek
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 WeekNumber = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FDateTime StartDate;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bByeWeek = false;
};

/** Renamed from UPScheduleEngine -> UPSScheduleEngine (Epic C3: prefix fix). */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSScheduleEngine : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Schedule")
    TArray<FSeasonWeek> GenerateSeasonSchedule(const FDateTime& SeasonStart, int32 NumWeeks, const TArray<int32>& ByeWeekNumbers);
};
