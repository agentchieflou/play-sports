#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.h"
#include "PSRosterData.generated.h"

/** Ordered depth chart for one position: index 0 is the starter. */
USTRUCT(BlueprintType)
struct FPSDepthChartEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPlayerRole Role = EPlayerRole::Quarterback;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FName> PlayerIdsByPriority;
};

/** Personnel package: how many of each role take the field for a formation grouping,
 *  e.g. "11 Personnel" (1 RB, 1 TE, 3 WR), "Nickel" (5 DB). Tuning lives in data,
 *  not code (Architecture rule 4). */
USTRUCT(BlueprintType)
struct FPSPersonnelPackage : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName PackageId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TMap<FString, int32> RoleCounts;
};
