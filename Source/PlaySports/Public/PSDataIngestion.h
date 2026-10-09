#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.h"
#include "PSLeagueData.h"
#include "PSArchetypeTuning.h"
#include "PSInputConfigTypes.h"
#include "PSDataIngestion.generated.h"

/** JSON-to-engine-data ingestion (Epic 21: generalized beyond just players to
 *  teams and league config, all through this one validated path -- no ad-hoc
 *  parsers elsewhere per Architecture rule 4). */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSDataIngestion : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadPlayerAttributesFromJson(const FString& JsonFilePath, UDataTable* TargetDataTable);

    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadTeamsFromJson(const FString& JsonFilePath, UDataTable* TargetDataTable);

    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadLeagueConfigFromJson(const FString& JsonFilePath, FPSLeagueConfig& OutConfig);

    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadArchetypeTuningFromJson(const FString& JsonFilePath, FPSArchetypeTuning& OutTuning);

    /** Loads the input action catalog (Data/input_actions.json) consumed by
     *  UPSInputConfig (Epic 142/126). False on a missing file, malformed JSON, or an
     *  unrecognized ValueType string. */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadInputCatalogFromJson(const FString& JsonFilePath, FPSInputCatalog& OutCatalog);

    /** Validates a Players JSON file's schema without loading it into a DataTable:
     *  missing PlayerId, unrecognized Role string, or out-of-range (negative)
     *  numeric attributes. OutErrors entries are "Row N: <field> <problem>" so
     *  content authors get an actionable pointer back to the bad row. */
    UFUNCTION(BlueprintCallable, Category = "Data|Validation")
    bool ValidatePlayersJson(const FString& JsonFilePath, TArray<FString>& OutErrors);

    UFUNCTION(BlueprintCallable, Category = "Data|Validation")
    bool ValidateTeamsJson(const FString& JsonFilePath, TArray<FString>& OutErrors);

private:
    static bool IsValidPlayerRoleString(const FString& RoleString);
};
