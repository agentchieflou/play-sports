#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PSScheduleEngine.generated.h"

USTRUCT(BlueprintType)
struct FSeasonWeek
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 WeekNumber;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FDateTime StartDate;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bByeWeek;
};

UCLASS(Blueprintable)
class PLAYSPORTS_API UPScheduleEngine : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Schedule")
    TArray<FSeasonWeek> GenerateSeasonSchedule(const FDateTime& SeasonStart, int32 NumWeeks, const TArray<int32>& ByeWeekNumbers);
};
