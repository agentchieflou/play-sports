// PSStadiumLightingSubsystem.h - lane V2 (Epic 46): day and night light and the broadcast look, from data
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSFieldDimensions.h"
#include "PSStadiumLightingTypes.h"
#include "PSStadiumLightingSubsystem.generated.h"

class ADirectionalLight;
class AExponentialHeightFog;
class APostProcessVolume;
class ASkyAtmosphere;
class ASkyLight;
class ASpotLight;
class AVolumetricCloud;

/**
 * The stadium's light, from Data/stadium_lighting.json (Architecture rule 4): a preset (Day, Night)
 * sets the sun or moon, the sky light, the sky atmosphere, the volumetric clouds, the height fog,
 * the floodlight banks, the field's dew and an unbound post-process volume with the broadcast look.
 * Every light is movable, so nothing needs a lighting build.
 *
 * It uses the level's own lights where it has them (GameMap's sun, sky light, atmosphere and fog,
 * from the content pipeline's game_map step) and spawns what's missing, so the level stays the one
 * place the lights live and the data the one place their settings do. The floodlights and the
 * post-process volume are its own, tagged PSFloodlight and PSBroadcastLook.
 *
 * The run's platform tier (PSPlatformTiers) decides what's affordable: Lumen, a real-time sky
 * capture, volumetric clouds and fog, how many floodlights cast shadows, lens effects and bloom.
 *
 * Applied when the world begins play, with DefaultPreset or -PSLighting=<PresetId>; ApplyPreset
 * switches later. Headless tests call ApplyPresetForTier directly (a test world never begins play).
 */
UCLASS()
class PLAYSPORTS_API UPSStadiumLightingSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

    static FString GetDefaultCatalogPath();

    /** Data/stadium_lighting.json through UPSDataIngestion; false when it can't be read. */
    static bool LoadCatalog(const FString& Path, FPSStadiumLightingCatalog& OutCatalog);

    /** Problems with Catalog, one line each (empty when sound); mirrors tools/validate_data.py. */
    static TArray<FString> ValidateCatalog(const FPSStadiumLightingCatalog& Catalog);

    static const FPSLightingPreset* FindPreset(const FPSStadiumLightingCatalog& Catalog, FName PresetId);

    /** TierId's switches, or everything on when the catalog doesn't list it. */
    static FPSLightingTierSettings ResolveTierSettings(const FPSStadiumLightingCatalog& Catalog, FName TierId);

    /** The preset a run starts with: -PSLighting=<PresetId> in CommandLine when the catalog has it,
     *  else DefaultPreset. */
    static FName ResolvePresetId(const FPSStadiumLightingCatalog& Catalog, const TCHAR* CommandLine);

    /** The sun's rotation: pitched down by its elevation, yawed to the way its light travels. */
    static FRotator ComputeSunRotation(const FPSLightingPreset& Preset);

    /** Each bank's light, in Banks order: at its place above the field, aimed at its aim point. */
    static TArray<FTransform> ComputeFloodlightTransforms(const FPSStadiumLightingCatalog& Catalog, const FPSFieldDimensions& Dimensions);

    /** An exposure in EV100 as the post-process volume's brightness setting: EV100 itself when the
     *  project extends the default luminance range, else the luminance it stands for. */
    static float ExposureToBrightnessSetting(float EV100, bool bExtendedLuminanceRange);

    /** Applies PresetId from the default catalog (loaded once) for the run's tier. False when the
     *  catalog or the preset isn't there. */
    bool ApplyPreset(FName PresetId);

    /** Applies PresetId from Catalog with TierId's switches: the testable core of ApplyPreset. */
    bool ApplyPresetForTier(const FPSStadiumLightingCatalog& Catalog, FName PresetId, FName TierId);

    FName GetActivePresetId() const { return ActivePresetId; }
    ADirectionalLight* GetSun() const { return Sun; }
    ASkyLight* GetSkyLight() const { return SkyLight; }
    ASkyAtmosphere* GetSkyAtmosphere() const { return SkyAtmosphere; }
    AVolumetricCloud* GetClouds() const { return Clouds; }
    AExponentialHeightFog* GetFog() const { return Fog; }
    APostProcessVolume* GetBroadcastLook() const { return BroadcastLook; }
    const TArray<TObjectPtr<ASpotLight>>& GetFloodlights() const { return Floodlights; }

    static const FName FloodlightTag;
    static const FName BroadcastLookTag;

private:
    void ApplySun(const FPSLightingPreset& Preset);
    void ApplySky(const FPSLightingPreset& Preset, const FPSLightingTierSettings& Tier);
    void ApplyFog(const FPSLightingPreset& Preset, const FPSLightingTierSettings& Tier);
    void ApplyFloodlights(const FPSStadiumLightingCatalog& Catalog, const FPSLightingPreset& Preset, const FPSLightingTierSettings& Tier);
    void ApplyLook(const FPSLightingPreset& Preset, const FPSLightingTierSettings& Tier);
    void ApplyDew(const FPSStadiumLightingCatalog& Catalog, const FPSLightingPreset& Preset);

    UPROPERTY(Transient)
    FPSStadiumLightingCatalog DefaultCatalog;

    bool bDefaultCatalogLoaded = false;

    FName ActivePresetId;

    UPROPERTY(Transient)
    TObjectPtr<ADirectionalLight> Sun;

    UPROPERTY(Transient)
    TObjectPtr<ASkyLight> SkyLight;

    UPROPERTY(Transient)
    TObjectPtr<ASkyAtmosphere> SkyAtmosphere;

    UPROPERTY(Transient)
    TObjectPtr<AVolumetricCloud> Clouds;

    UPROPERTY(Transient)
    TObjectPtr<AExponentialHeightFog> Fog;

    UPROPERTY(Transient)
    TObjectPtr<APostProcessVolume> BroadcastLook;

    UPROPERTY(Transient)
    TArray<TObjectPtr<ASpotLight>> Floodlights;
};
