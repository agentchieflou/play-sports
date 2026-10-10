// PSInputConfig.h - Epic 126: code-defined input action catalog (Enhanced Input)
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PSInputConfigTypes.h"
#include "PSInputGlyphs.h"
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
 * code refers to actions and contexts by catalog ID only. The button glyph table (Epic 128)
 * loads alongside, so the action's glyph always follows its current binding.
 */
UCLASS(BlueprintType)
class PLAYSPORTS_API UPSInputConfig : public UDataAsset
{
    GENERATED_BODY()

public:
    /** Absolute path of the authored catalog: <ProjectDir>/Data/input_actions.json. */
    static FString GetDefaultCatalogPath();

    /** Absolute path of the gamepad tuning: <ProjectDir>/Data/input_tuning.json. */
    static FString GetDefaultTuningPath();

    /** Loads the tuning, then the catalog, then the glyph table, from their default paths. A
     *  missing tuning or glyph file keeps defaults (or no glyphs); false only when the
     *  catalog fails to load. */
    UFUNCTION(BlueprintCallable, Category = "Input")
    bool LoadDefaults();

    /** Loads Tuning through UPSDataIngestion. Takes effect on the next BuildRuntimeObjects
     *  (LoadFromJson rebuilds), so load tuning before the catalog. */
    UFUNCTION(BlueprintCallable, Category = "Input")
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Loads Catalog from JsonFilePath through UPSDataIngestion, then rebuilds the
     *  runtime actions and contexts. False when the file is missing or malformed. */
    UFUNCTION(BlueprintCallable, Category = "Input")
    bool LoadFromJson(const FString& JsonFilePath);

    /** (Re)creates the UInputAction / UInputMappingContext objects from Catalog. Every
     *  gamepad stick binding gets a radial dead zone and a response curve from Tuning. Keys
     *  that are not valid engine keys are skipped here and reported by Validate(). */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void BuildRuntimeObjects();

    /** Problems that make the catalog unusable, one actionable line each: empty or
     *  duplicate IDs, unknown context references, invalid keys, a key bound twice in one
     *  context, or an action missing a keyboard/mouse or a gamepad binding in a context it
     *  is declared for; also out-of-range Tuning values. Empty when everything is valid. */
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

    /** Every key bound to ActionId in ContextId (empty when the action does not live
     *  there). Slate-driven screens such as menus read their keys from here (Epic 101). */
    TArray<FKey> GetKeysFor(FName ActionId, FName ContextId) const;

    /** The action Key is bound to in ContextId, or NAME_None when it means nothing there. The
     *  input buffer uses it to tell a press that did something from one that did nothing. */
    FName FindActionForKey(const FKey& Key, FName ContextId) const;

    /** The button glyph for ActionId in ContextId on Device (UPSInputGlyphs over this
     *  catalog's bindings). False when there is none, e.g. the action isn't bound there. */
    UFUNCTION(BlueprintCallable, Category = "Input")
    bool GetGlyphForAction(FName ActionId, FName ContextId, EPSInputDevice Device, FPSInputGlyph& OutGlyph) const;

    /** The glyph table; empty until LoadDefaults (or its own LoadFromJson) runs. */
    UFUNCTION(BlueprintPure, Category = "Input")
    UPSInputGlyphs* GetGlyphs() const { return Glyphs; }

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    FPSInputCatalog Catalog;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    FInputTuningRow Tuning;

private:
    UPROPERTY(Transient)
    UPSInputGlyphs* Glyphs;

    UPROPERTY(Transient)
    TMap<FName, UInputAction*> RuntimeActions;

    UPROPERTY(Transient)
    TMap<FName, UInputMappingContext*> RuntimeContexts;
};
