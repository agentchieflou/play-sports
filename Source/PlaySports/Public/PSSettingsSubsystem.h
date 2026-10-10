// PSSettingsSubsystem.h - Epic 103: the player's settings, persisted in the profile save
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PSSettingsTypes.h"
#include "PSInputConfigTypes.h"
#include "PSSettingsSubsystem.generated.h"

class UPSSaveSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPSSettingChangedSignature, FName, SettingId, float, Value);
DECLARE_MULTICAST_DELEGATE_TwoParams(FPSSettingChangedMC, FName, float);
DECLARE_MULTICAST_DELEGATE(FPSInputRemapsChangedMC);

/**
 * UPSSettingsSubsystem holds the player's settings (Epic 103) for the whole session, across
 * level travel. Which settings exist, their categories, ranges and defaults are data
 * (Data/ui_settings.json); the player's values live in the profile save
 * (UPSProfileSaveGame::Settings) and are saved as they change.
 *
 * It applies what belongs to the engine: video (window mode, resolution scale, quality, VSync,
 * frame cap) through UGameUserSettings, outside the editor only, and the master volume. Every
 * other setting is read by the system it concerns, which hears changes on OnSettingChanged:
 * the player's own controls through UPSSettingsComponent on each player controller.
 *
 * A setting's value is a number: 0 or 1 for a toggle, a choice's index, a slider's value.
 * GetNumber gives what it stands for (a choice's entry in Values).
 */
UCLASS(BlueprintType)
class PLAYSPORTS_API UPSSettingsSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    UPSSettingsSubsystem();

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** The game instance's settings for WorldContext, or null (a world without one). */
    static UPSSettingsSubsystem* Get(const UObject* WorldContext);

    static FString GetDefaultCatalogPath();

    /** The settings catalog, loaded from the default path on first use. */
    const FPSSettingsCatalog& GetCatalog();

    bool LoadCatalogFromJson(const FString& JsonFilePath);

    /** Problems with InCatalog (empty when sound): empty or repeated IDs, a setting in an
     *  unknown category, a choice without choices or with Values of the wrong length, a slider
     *  with a bad range or step, a default out of range. */
    static TArray<FString> ValidateCatalog(const FPSSettingsCatalog& InCatalog);

    /** The setting's value: the player's, or its default. 0 for an unknown setting. */
    UFUNCTION(BlueprintPure, Category = "Settings")
    float GetValue(FName SettingId);

    /** What the value stands for: a choice's entry in Values, otherwise the value. */
    UFUNCTION(BlueprintPure, Category = "Settings")
    float GetNumber(FName SettingId);

    /** A toggle's state (a value of 0.5 or more). */
    UFUNCTION(BlueprintPure, Category = "Settings")
    bool GetBool(FName SettingId);

    /** Sets the value, snapped into the setting's range (and to a slider's step). Saves the
     *  profile, applies engine settings and tells OnSettingChanged when it changed. False for an
     *  unknown setting. */
    UFUNCTION(BlueprintCallable, Category = "Settings")
    bool SetValue(FName SettingId, float Value);

    /** The settings menu's press: a toggle flips, a choice moves to the next (wrapping), a slider
     *  goes up a step (wrapping to Min past Max). */
    UFUNCTION(BlueprintCallable, Category = "Settings")
    bool StepSetting(FName SettingId);

    /** Puts every setting in Category (all of them for NAME_None) back to its default. */
    UFUNCTION(BlueprintCallable, Category = "Settings")
    void ResetToDefaults(FName Category);

    /** The value as the menu shows it, in the player's language (Epic 106): On/Off, the
     *  choice's name, or the number and its unit (a percentage as the culture writes one). */
    UFUNCTION(BlueprintCallable, Category = "Settings")
    FText FormatValue(FName SettingId);

    /** The player's own keys over the input catalog's (Epic 103.4), as saved. Each player
     *  controller's UPSSettingsComponent applies them to its input config. */
    const TArray<FPSInputRemap>& GetInputRemaps() const { return InputRemaps; }

    /** Replaces the player's remaps, saves the profile and tells OnInputRemapsChangedMC. The
     *  caller checks them first (UPSSettingsComponent::RequestRemap). */
    void SetInputRemaps(const TArray<FPSInputRemap>& InRemaps);

    /** The remaps changed. */
    FPSInputRemapsChangedMC OnInputRemapsChangedMC;

    /** Reads the player's values from the profile in Slot (keeping defaults for the rest). */
    bool LoadFromProfile(UPSSaveSubsystem* InSaves, const FString& InSlot);

    /** Writes the player's values into the profile in Slot, keeping the rest of the profile. */
    bool SaveToProfile(UPSSaveSubsystem* InSaves, const FString& InSlot);

    /** Applies the video settings (outside the editor) and the master volume to the engine.
     *  Does nothing unless this is a running game's subsystem. */
    void ApplyEngineSettings();

    /** Every setting change, by ID with its new value. */
    UPROPERTY(BlueprintAssignable, Category = "Settings")
    FPSSettingChangedSignature OnSettingChanged;

    FPSSettingChangedMC OnSettingChangedMC;

    /** Settings applied to the engine (Data/ui_settings.json IDs). */
    UPROPERTY(EditDefaultsOnly, Category = "Settings")
    FName WindowModeSettingId;

    UPROPERTY(EditDefaultsOnly, Category = "Settings")
    FName ResolutionScaleSettingId;

    UPROPERTY(EditDefaultsOnly, Category = "Settings")
    FName QualitySettingId;

    UPROPERTY(EditDefaultsOnly, Category = "Settings")
    FName VSyncSettingId;

    UPROPERTY(EditDefaultsOnly, Category = "Settings")
    FName FrameRateLimitSettingId;

    UPROPERTY(EditDefaultsOnly, Category = "Settings")
    FName MasterVolumeSettingId;

private:
    float Snap(const FPSSettingDef& Def, float Value) const;

    UPROPERTY(Transient)
    FPSSettingsCatalog Catalog;

    /** The player's values; settings missing here are at their default. */
    UPROPERTY(Transient)
    TMap<FName, float> Values;

    UPROPERTY(Transient)
    TArray<FPSInputRemap> InputRemaps;

    /** Where SetValue saves: the game's save subsystem and profile slot. */
    UPROPERTY(Transient)
    UPSSaveSubsystem* Saves = nullptr;

    FString Slot;
    bool bCatalogLoaded = false;
    bool bApplyToEngine = false;
};
