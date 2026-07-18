#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.h"
#include "PSDataIngestion.generated.h"

UCLASS(Blueprintable)
class PLAYSPORTS_API UPSDataIngestion : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadPlayerAttributesFromJson(const FString& JsonFilePath, UDataTable* TargetDataTable);
};
