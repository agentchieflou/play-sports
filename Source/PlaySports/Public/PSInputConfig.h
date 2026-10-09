// PSInputConfig.h - Epic 126: code-defined input action catalog (Enhanced Input)
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PSInputConfigTypes.h"
#include "PSInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * UPSInputConfig is the one place input actions and mapping contexts are declared.
 * The catalog is authored in Data/input_actions.json and read through UPSDataIngestion
 * (Architecture rule 4: no magic bindings in gameplay code, no ad-hoc parser). From it
 * the config builds one transient UInputAction per action and one UInputMappingContext
 * per context, so nothing in Content/ is needed for input to work.
 *
 * APSPlayerController owns an instance and applies its contexts on possession; gameplay
 * code refers to actions and contexts by catalog ID only.
 */
UCLASS(BlueprintType)
class PLAYSPORTS_API UPSInputConfig : public UDataAsset
{
    GENERATED_BODY()

public:
    /** Absolute path of the authored catalog: <ProjectDir>/Data/input_actions.json. */
    static FString GetDefaultCatalogPath();

    /** Loads Catalog from JsonFilePath through UPSDataIngestion, then rebuilds the
     *  runtime actions and contexts. False when the file is missing or malformed. */
    UFUNCTION(BlueprintCallable, Category = "Input")
    bool LoadFromJson(const FString& JsonFilePath);

    /** (Re)creates the UInputAction / UInputMappingContext objects from Catalog. Keys
     *  that are not valid engine keys are skipped here and reported by Validate(). */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void BuildRuntimeObjects();

    /** Problems that make the catalog unusable, one actionable line each: empty or
     *  duplicate IDs, unknown context references, invalid keys, a key bound twice in one
     *  context, or an action missing a keyboard/mouse or a gamepad binding in a context it
     *  is declared for. Empty when the catalog is valid. */
    UFUNCTION(BlueprintCallable, Category = "Input")
    TArray<FString> Validate() const;

    UFUNCTION(BlueprintPure, Category = "Input")
    UInputAction* FindAction(FName ActionId) const;

    UFUNCTION(BlueprintPure, Category = "Input")
    UInputMappingContext* FindContext(FName ContextId) const;

    /** The catalog priority of ContextId, or INDEX_NONE when it is not in the catalog. */
    UFUNCTION(BlueprintPure, Category = "Input")
    int32 GetContextPriority(FName ContextId) const;

    /** Reverse lookup for catalog-wide handlers; NAME_None when Action is not ours. */
    FName FindActionId(const UInputAction* Action) const;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    FPSInputCatalog Catalog;

private:
    UPROPERTY(Transient)
    TMap<FName, UInputAction*> RuntimeActions;

    UPROPERTY(Transient)
    TMap<FName, UInputMappingContext*> RuntimeContexts;
};
