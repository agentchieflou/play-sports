// PSStadiumLightingTypes.h - lane V2 (Epic 46): the stadium's light and broadcast look, as data
#pragma once

#include "CoreMinimal.h"
#include "PSStadiumLightingTypes.generated.h"

/**
 * One floodlight bank on the stadium's rim or a tower (Data/stadium_lighting.json's Banks). A bank
 * is one spot light standing for its fixtures. Positions are yards of the field's frame (PSField:
 * X along the field from the near goal line, Y across it from the middle), so the banks keep their
 * places at any scale. This is where the stadium's lights are: the stadium's structure (towers,
 * rim fixtures) reads it rather than keeping its own copy.
 */
USTRUCT(BlueprintType)
struct FPSFloodlightBank
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    FName BankId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float XYards = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float YYards = 0.f;

    /** Height above the field. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float HeightYards = 45.f;

    /** The point on the field (Z = 0) the bank aims at. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float AimXYards = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float AimYYards = 0.f;
};

/** How the floodlight banks shine in a preset (all banks alike). IntensityCandela 0 turns them off. */
USTRUCT(BlueprintType)
struct FPSFloodlightSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float IntensityCandela = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float TemperatureK = 5700.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float InnerConeDeg = 15.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float OuterConeDeg = 35.f;

    /** The light's size: bigger is softer shadows (a bank is many fixtures). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float SourceRadiusCm = 150.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float AttenuationRadiusYards = 160.f;

    /** How much each bank lights the fog (beams in the haze). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float VolumetricScattering = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    bool bCastShadows = true;
};

/**
 * The broadcast look: the unbound post-process volume's settings for a preset. Exposure is in EV100
 * (converted for the project's luminance-range setting); colour grading is the engine's white
 * balance, saturation and contrast, plus an optional LUT texture.
 */
USTRUCT(BlueprintType)
struct FPSBroadcastLook
{
    GENERATED_BODY()

    /** The auto exposure's range, EV100: a narrow range holds the picture steady as a broadcast
     *  camera's would. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float ExposureMinEV100 = 14.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float ExposureMaxEV100 = 15.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float ExposureCompensation = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float BloomIntensity = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float VignetteIntensity = 0.2f;

    /** Chromatic aberration (the engine's scene fringe). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float ChromaticAberration = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float LensFlareIntensity = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float FilmGrainIntensity = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float MotionBlurAmount = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float WhiteTemp = 6500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float Saturation = 1.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float Contrast = 1.05f;

    /** A colour-grading LUT texture; empty for none. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    FString ColorGradingLutPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float ColorGradingLutIntensity = 1.f;
};

/** A lighting preset: day or night (Data/stadium_lighting.json's Presets). */
USTRUCT(BlueprintType)
struct FPSLightingPreset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    FName PresetId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    FString Description;

    /** The sun (or the moon): its height above the horizon and the way its light travels (yaw:
     *  0 along +X, upfield; 90 along +Y, from the near sideline to the far one). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float SunElevationDeg = 48.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float SunAzimuthDeg = 65.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float SunIntensityLux = 90000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float SunTemperatureK = 5800.f;

    /** The disc's angular size; bigger is softer shadows. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float SunSourceAngleDeg = 0.5357f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    bool bSunCastsShadows = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float SkyLightIntensity = 1.f;

    /** The sky light's cubemap on tiers without a real-time sky capture (a TextureCube). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    FString SkyCubemapPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    bool bVolumetricClouds = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float CloudBottomAltitudeKm = 5.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float CloudLayerHeightKm = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float FogDensity = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float FogHeightFalloff = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    bool bVolumetricFog = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float VolumetricFogExtinctionScale = 1.f;

    /** How dewy the grass is, 0-1: the field materials' MPC_Field Dew (glossier grass). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float Dew = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    FPSFloodlightSettings Floodlights;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    FPSBroadcastLook Look;
};

/**
 * What a platform tier can afford (Data/stadium_lighting.json's TierSettings, keyed by the tiers of
 * Data/platform_tiers.json). A tier not listed gets everything (these defaults).
 */
USTRUCT(BlueprintType)
struct FPSLightingTierSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    FName TierId;

    /** Lumen global illumination and reflections, through the broadcast look's volume; off: no
     *  dynamic GI and screen-space reflections. The mobile renderer has no Lumen either way. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    bool bLumen = true;

    /** The sky light re-captures the sky as it changes; off: the preset's SkyCubemapPath. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    bool bRealTimeSkyCapture = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    bool bVolumetricClouds = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    bool bVolumetricFog = true;

    /** How many floodlight banks cast shadows, the first in Banks order; -1 for all. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    int32 MaxShadowedFloodlights = -1;

    /** Vignette, chromatic aberration, lens flares and film grain. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    bool bLensEffects = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    bool bBloom = true;
};

/** Data/stadium_lighting.json: every preset, the floodlight banks and each tier's switches. */
USTRUCT(BlueprintType)
struct FPSStadiumLightingCatalog
{
    GENERATED_BODY()

    /** The preset a run starts with unless -PSLighting=<PresetId> names another. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    FName DefaultPreset = TEXT("Day");

    /** The material parameter collection whose Dew scalar the presets set. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    FString DewCollectionPath = TEXT("/Game/Field/Materials/MPC_Field.MPC_Field");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    FName DewParameter = TEXT("Dew");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    TArray<FPSFloodlightBank> Banks;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    TArray<FPSLightingPreset> Presets;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    TArray<FPSLightingTierSettings> TierSettings;
};
