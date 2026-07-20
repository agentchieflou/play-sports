#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlaybookData.h"
#include "PSPlaybookIngestion.generated.h"

/** Loads playbook content (plays + route library) from JSON, mirroring
 *  UPSDataIngestion::LoadPlayerAttributesFromJson for the Players table. */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSPlaybookIngestion : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Data|Playbook")
    bool LoadPlaysFromJson(const FString& JsonFilePath, UDataTable* TargetDataTable);

    UFUNCTION(BlueprintCallable, Category = "Data|Playbook")
    bool LoadRoutesFromJson(const FString& JsonFilePath, UDataTable* TargetDataTable);
};
