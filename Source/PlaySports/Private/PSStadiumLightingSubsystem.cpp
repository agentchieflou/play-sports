// PSStadiumLightingSubsystem.cpp - lane V2 (Epic 46): day and night light and the broadcast look, from data
#include "PSStadiumLightingSubsystem.h"
#include "PSDataIngestion.h"
#include "PSPlatformTiers.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/EngineTypes.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/SpotLight.h"
#include "Engine/Texture.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"

const FName UPSStadiumLightingSubsystem::FloodlightTag(TEXT("PSFloodlight"));
const FName UPSStadiumLightingSubsystem::BroadcastLookTag(TEXT("PSBroadcastLook"));

namespace PSStadiumLightingPrivate
{
    /** The level's first actor of a class: a light the level already has is the one to set. */
    template <typename ActorType>
    ActorType* FindFirst(UWorld& World)
    {
        for (TActorIterator<ActorType> It(&World); It; ++It)
        {
            if (IsValid(*It))
            {
                return *It;
            }
        }
        return nullptr;
    }

    void MakeMovable(AActor* Actor)
    {
        USceneComponent* Root = Actor ? Actor->GetRootComponent() : nullptr;
        if (Root && Root->Mobility != EComponentMobility::Movable)
        {
            Root->SetMobility(EComponentMobility::Movable);
        }
    }

    /** Spawns a lighting actor made movable before it registers: a movable light needs no
     *  lighting build. */
    template <typename ActorType>
    ActorType* SpawnMovable(UWorld& World, const FTransform& Transform)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Params.bDeferConstruction = true;
        ActorType* Actor = World.SpawnActor<ActorType>(ActorType::StaticClass(), Transform, Params);
        if (Actor)
        {
            MakeMovable(Actor);
            Actor->FinishSpawning(Transform);
        }
        return Actor;
    }

    template <typename AssetType>
    AssetType* LoadOptional(const FString& Path)
    {
        return Path.IsEmpty() ? nullptr : Cast<AssetType>(FSoftObjectPath(Path).TryLoad());
    }

    bool IsExtendedLuminanceRange()
    {
        IConsoleVariable* Setting = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));
        return Setting && Setting->GetInt() != 0;
    }

    void AddRangeProblem(TArray<FString>& Problems, const FString& Where, const TCHAR* Field, float Value, float Min, float Max)
    {
        if (!(Value >= Min && Value <= Max))
        {
            Problems.Add(FString::Printf(TEXT("%s: %s %g must be from %g to %g"), *Where, Field, Value, Min, Max));
        }
    }
}

bool UPSStadiumLightingSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    if (!Super::ShouldCreateSubsystem(Outer))
    {
        return false;
    }
    // Only where the game runs: never in an editor world the content pipeline saves.
    const UWorld* World = Cast<UWorld>(Outer);
    return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UPSStadiumLightingSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    if (!bDefaultCatalogLoaded)
    {
        bDefaultCatalogLoaded = LoadCatalog(GetDefaultCatalogPath(), DefaultCatalog);
    }
    if (bDefaultCatalogLoaded)
    {
        ApplyPreset(ResolvePresetId(DefaultCatalog, FCommandLine::Get()));
    }
}

FString UPSStadiumLightingSubsystem::GetDefaultCatalogPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/stadium_lighting.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSStadiumLightingSubsystem::LoadCatalog(const FString& Path, FPSStadiumLightingCatalog& OutCatalog)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSStadiumLightingCatalog Loaded;
    if (!Ingestion->LoadStadiumLightingFromJson(Path, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSStadiumLightingSubsystem: Could not load %s; the level's lights stay as they are."), *Path);
        return false;
    }
    const TArray<FString> Problems = ValidateCatalog(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSStadiumLightingSubsystem: %s: %s"), *Path, *Problem);
    }
    if (Problems.Num() > 0)
    {
        return false;
    }
    OutCatalog = Loaded;
    return true;
}

TArray<FString> UPSStadiumLightingSubsystem::ValidateCatalog(const FPSStadiumLightingCatalog& Catalog)
{
    using namespace PSStadiumLightingPrivate;

    TArray<FString> Problems;
    if (Catalog.Presets.Num() == 0)
    {
        Problems.Add(TEXT("Presets: there must be at least one"));
    }
    if (!FindPreset(Catalog, Catalog.DefaultPreset))
    {
        Problems.Add(FString::Printf(TEXT("DefaultPreset '%s' is not a preset"), *Catalog.DefaultPreset.ToString()));
    }

    TSet<FName> BankIds;
    for (int32 Index = 0; Index < Catalog.Banks.Num(); ++Index)
    {
        const FPSFloodlightBank& Bank = Catalog.Banks[Index];
        const FString Where = FString::Printf(TEXT("Banks[%d]"), Index);
        if (Bank.BankId.IsNone() || BankIds.Contains(Bank.BankId))
        {
            Problems.Add(FString::Printf(TEXT("%s: BankId must be set and unique"), *Where));
        }
        BankIds.Add(Bank.BankId);
        if (!(Bank.HeightYards > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("%s: HeightYards must be above 0"), *Where));
        }
    }

    TSet<FName> PresetIds;
    bool bAnyFloodlights = false;
    for (int32 Index = 0; Index < Catalog.Presets.Num(); ++Index)
    {
        const FPSLightingPreset& Preset = Catalog.Presets[Index];
        const FString Where = FString::Printf(TEXT("Presets[%d] (%s)"), Index, *Preset.PresetId.ToString());
        if (Preset.PresetId.IsNone() || PresetIds.Contains(Preset.PresetId))
        {
            Problems.Add(FString::Printf(TEXT("%s: PresetId must be set and unique"), *Where));
        }
        PresetIds.Add(Preset.PresetId);
        AddRangeProblem(Problems, Where, TEXT("SunElevationDeg"), Preset.SunElevationDeg, -90.f, 90.f);
        AddRangeProblem(Problems, Where, TEXT("SunAzimuthDeg"), Preset.SunAzimuthDeg, -360.f, 360.f);
        AddRangeProblem(Problems, Where, TEXT("SunIntensityLux"), Preset.SunIntensityLux, 0.f, 200000.f);
        AddRangeProblem(Problems, Where, TEXT("SunTemperatureK"), Preset.SunTemperatureK, 1700.f, 12000.f);
        AddRangeProblem(Problems, Where, TEXT("SunSourceAngleDeg"), Preset.SunSourceAngleDeg, 0.f, 10.f);
        AddRangeProblem(Problems, Where, TEXT("SkyLightIntensity"), Preset.SkyLightIntensity, 0.f, 10.f);
        AddRangeProblem(Problems, Where, TEXT("CloudBottomAltitudeKm"), Preset.CloudBottomAltitudeKm, 0.f, 20.f);
        AddRangeProblem(Problems, Where, TEXT("CloudLayerHeightKm"), Preset.CloudLayerHeightKm, 0.1f, 20.f);
        AddRangeProblem(Problems, Where, TEXT("FogDensity"), Preset.FogDensity, 0.f, 1.f);
        AddRangeProblem(Problems, Where, TEXT("FogHeightFalloff"), Preset.FogHeightFalloff, 0.001f, 2.f);
        AddRangeProblem(Problems, Where, TEXT("VolumetricFogExtinctionScale"), Preset.VolumetricFogExtinctionScale, 0.f, 10.f);
        AddRangeProblem(Problems, Where, TEXT("Dew"), Preset.Dew, 0.f, 1.f);

        const FPSFloodlightSettings& Flood = Preset.Floodlights;
        AddRangeProblem(Problems, Where, TEXT("Floodlights.IntensityCandela"), Flood.IntensityCandela, 0.f, 100000000.f);
        AddRangeProblem(Problems, Where, TEXT("Floodlights.TemperatureK"), Flood.TemperatureK, 1700.f, 12000.f);
        AddRangeProblem(Problems, Where, TEXT("Floodlights.OuterConeDeg"), Flood.OuterConeDeg, 1.f, 80.f);
        AddRangeProblem(Problems, Where, TEXT("Floodlights.InnerConeDeg"), Flood.InnerConeDeg, 0.f, Flood.OuterConeDeg);
        AddRangeProblem(Problems, Where, TEXT("Floodlights.SourceRadiusCm"), Flood.SourceRadiusCm, 0.f, 1000.f);
        AddRangeProblem(Problems, Where, TEXT("Floodlights.AttenuationRadiusYards"), Flood.AttenuationRadiusYards, 1.f, 1000.f);
        AddRangeProblem(Problems, Where, TEXT("Floodlights.VolumetricScattering"), Flood.VolumetricScattering, 0.f, 10.f);
        bAnyFloodlights |= Flood.IntensityCandela > 0.f;

        const FPSBroadcastLook& Look = Preset.Look;
        AddRangeProblem(Problems, Where, TEXT("Look.ExposureMinEV100"), Look.ExposureMinEV100, -10.f, 20.f);
        AddRangeProblem(Problems, Where, TEXT("Look.ExposureMaxEV100"), Look.ExposureMaxEV100, Look.ExposureMinEV100, 20.f);
        AddRangeProblem(Problems, Where, TEXT("Look.ExposureCompensation"), Look.ExposureCompensation, -15.f, 15.f);
        AddRangeProblem(Problems, Where, TEXT("Look.BloomIntensity"), Look.BloomIntensity, 0.f, 8.f);
        AddRangeProblem(Problems, Where, TEXT("Look.VignetteIntensity"), Look.VignetteIntensity, 0.f, 1.f);
        AddRangeProblem(Problems, Where, TEXT("Look.ChromaticAberration"), Look.ChromaticAberration, 0.f, 5.f);
        AddRangeProblem(Problems, Where, TEXT("Look.LensFlareIntensity"), Look.LensFlareIntensity, 0.f, 16.f);
        AddRangeProblem(Problems, Where, TEXT("Look.FilmGrainIntensity"), Look.FilmGrainIntensity, 0.f, 1.f);
        AddRangeProblem(Problems, Where, TEXT("Look.MotionBlurAmount"), Look.MotionBlurAmount, 0.f, 1.f);
        AddRangeProblem(Problems, Where, TEXT("Look.WhiteTemp"), Look.WhiteTemp, 1500.f, 15000.f);
        AddRangeProblem(Problems, Where, TEXT("Look.Saturation"), Look.Saturation, 0.f, 2.f);
        AddRangeProblem(Problems, Where, TEXT("Look.Contrast"), Look.Contrast, 0.f, 2.f);
        AddRangeProblem(Problems, Where, TEXT("Look.ColorGradingLutIntensity"), Look.ColorGradingLutIntensity, 0.f, 1.f);
    }
    if (bAnyFloodlights && Catalog.Banks.Num() == 0)
    {
        Problems.Add(TEXT("Banks: a preset turns the floodlights on, so there must be at least one bank"));
    }

    TSet<FName> TierIds;
    for (int32 Index = 0; Index < Catalog.TierSettings.Num(); ++Index)
    {
        const FPSLightingTierSettings& Tier = Catalog.TierSettings[Index];
        if (Tier.TierId.IsNone() || TierIds.Contains(Tier.TierId))
        {
            Problems.Add(FString::Printf(TEXT("TierSettings[%d]: TierId must be set and unique"), Index));
        }
        TierIds.Add(Tier.TierId);
        if (Tier.MaxShadowedFloodlights < -1)
        {
            Problems.Add(FString::Printf(TEXT("TierSettings[%d]: MaxShadowedFloodlights must be -1 (all) or more"), Index));
        }
    }
    return Problems;
}

const FPSLightingPreset* UPSStadiumLightingSubsystem::FindPreset(const FPSStadiumLightingCatalog& Catalog, FName PresetId)
{
    return Catalog.Presets.FindByPredicate([PresetId](const FPSLightingPreset& Preset) { return Preset.PresetId == PresetId; });
}

FPSLightingTierSettings UPSStadiumLightingSubsystem::ResolveTierSettings(const FPSStadiumLightingCatalog& Catalog, FName TierId)
{
    if (const FPSLightingTierSettings* Found = Catalog.TierSettings.FindByPredicate(
        [TierId](const FPSLightingTierSettings& Tier) { return Tier.TierId == TierId; }))
    {
        return *Found;
    }
    FPSLightingTierSettings Everything;
    Everything.TierId = TierId;
    return Everything;
}

FName UPSStadiumLightingSubsystem::ResolvePresetId(const FPSStadiumLightingCatalog& Catalog, const TCHAR* CommandLine)
{
    FString Requested;
    if (CommandLine && FParse::Value(CommandLine, TEXT("PSLighting="), Requested) && FindPreset(Catalog, FName(*Requested)))
    {
        return FName(*Requested);
    }
    return Catalog.DefaultPreset;
}

FRotator UPSStadiumLightingSubsystem::ComputeSunRotation(const FPSLightingPreset& Preset)
{
    return FRotator(-Preset.SunElevationDeg, Preset.SunAzimuthDeg, 0.f);
}

TArray<FTransform> UPSStadiumLightingSubsystem::ComputeFloodlightTransforms(const FPSStadiumLightingCatalog& Catalog, const FPSFieldDimensions& Dimensions)
{
    const float Cm = Dimensions.CentimetresPerYard;
    TArray<FTransform> Transforms;
    for (const FPSFloodlightBank& Bank : Catalog.Banks)
    {
        const FVector Position(Bank.XYards * Cm, Bank.YYards * Cm, Bank.HeightYards * Cm);
        const FVector Aim(Bank.AimXYards * Cm, Bank.AimYYards * Cm, 0.0);
        Transforms.Add(FTransform((Aim - Position).Rotation(), Position));
    }
    return Transforms;
}

float UPSStadiumLightingSubsystem::ExposureToBrightnessSetting(float EV100, bool bExtendedLuminanceRange)
{
    // Without the extended range the volume's brightness is a luminance: the engine's
    // EV100ToLuminance, 1.2 * 2^EV100 (its lens calibration).
    return bExtendedLuminanceRange ? EV100 : 1.2f * FMath::Pow(2.f, EV100);
}

bool UPSStadiumLightingSubsystem::ApplyPreset(FName PresetId)
{
    if (!bDefaultCatalogLoaded)
    {
        bDefaultCatalogLoaded = LoadCatalog(GetDefaultCatalogPath(), DefaultCatalog);
        if (!bDefaultCatalogLoaded)
        {
            return false;
        }
    }
    if (!FindPreset(DefaultCatalog, PresetId))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSStadiumLightingSubsystem: No lighting preset '%s' in %s."), *PresetId.ToString(), *GetDefaultCatalogPath());
        return false;
    }
    return ApplyPresetForTier(DefaultCatalog, PresetId, PSPlatformTiers::GetActiveTier().TierId);
}

bool UPSStadiumLightingSubsystem::ApplyPresetForTier(const FPSStadiumLightingCatalog& Catalog, FName PresetId, FName TierId)
{
    const FPSLightingPreset* Preset = FindPreset(Catalog, PresetId);
    if (!Preset || !GetWorld())
    {
        return false;
    }
    const FPSLightingTierSettings Tier = ResolveTierSettings(Catalog, TierId);
    ApplySun(*Preset);
    ApplySky(*Preset, Tier);
    ApplyFog(*Preset, Tier);
    ApplyFloodlights(Catalog, *Preset, Tier);
    ApplyLook(*Preset, Tier);
    ApplyDew(Catalog, *Preset);
    ActivePresetId = Preset->PresetId;
    UE_LOG(LogTemp, Display, TEXT("UPSStadiumLightingSubsystem: '%s' lighting for tier %s (%d floodlights)."),
        *ActivePresetId.ToString(), *TierId.ToString(), Floodlights.Num());
    return true;
}

void UPSStadiumLightingSubsystem::ApplySun(const FPSLightingPreset& Preset)
{
    using namespace PSStadiumLightingPrivate;

    UWorld& World = *GetWorld();
    const FRotator Rotation = ComputeSunRotation(Preset);
    if (!IsValid(Sun))
    {
        Sun = FindFirst<ADirectionalLight>(World);
    }
    if (!Sun)
    {
        Sun = SpawnMovable<ADirectionalLight>(World, FTransform(Rotation, FVector(0.0, 0.0, 2000.0)));
    }
    if (!Sun)
    {
        return;
    }
    MakeMovable(Sun);
    Sun->SetActorRotation(Rotation);
    if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
    {
        Light->SetAtmosphereSunLight(true);
        Light->SetIntensity(Preset.SunIntensityLux);
        Light->SetLightColor(FLinearColor::White);
        Light->bUseTemperature = true;
        Light->SetTemperature(Preset.SunTemperatureK);
        Light->LightSourceAngle = Preset.SunSourceAngleDeg;
        Light->SetCastShadows(Preset.bSunCastsShadows);
        Light->MarkRenderStateDirty();
    }
}

void UPSStadiumLightingSubsystem::ApplySky(const FPSLightingPreset& Preset, const FPSLightingTierSettings& Tier)
{
    using namespace PSStadiumLightingPrivate;

    UWorld& World = *GetWorld();
    if (!IsValid(SkyAtmosphere))
    {
        SkyAtmosphere = FindFirst<ASkyAtmosphere>(World);
    }
    if (!SkyAtmosphere)
    {
        SkyAtmosphere = SpawnMovable<ASkyAtmosphere>(World, FTransform::Identity);
    }

    if (!IsValid(SkyLight))
    {
        SkyLight = FindFirst<ASkyLight>(World);
    }
    if (!SkyLight)
    {
        SkyLight = SpawnMovable<ASkyLight>(World, FTransform(FVector(0.0, 0.0, 1000.0)));
    }
    if (SkyLight)
    {
        MakeMovable(SkyLight);
        if (USkyLightComponent* Sky = SkyLight->GetLightComponent())
        {
            // A real-time capture follows the sky; a tier without one lights from the preset's
            // cubemap (and falls back to a capture when there is none).
            UTextureCube* Cubemap = Tier.bRealTimeSkyCapture ? nullptr : LoadOptional<UTextureCube>(Preset.SkyCubemapPath);
            Sky->bRealTimeCapture = Cubemap == nullptr;
            Sky->SourceType = Cubemap ? ESkyLightSourceType::SLS_SpecifiedCubemap : ESkyLightSourceType::SLS_CapturedScene;
            if (Cubemap)
            {
                Sky->SetCubemap(Cubemap);
            }
            Sky->SetIntensity(Preset.SkyLightIntensity);
            Sky->RecaptureSky();
        }
    }

    const bool bClouds = Preset.bVolumetricClouds && Tier.bVolumetricClouds;
    if (!IsValid(Clouds))
    {
        Clouds = FindFirst<AVolumetricCloud>(World);
    }
    if (!Clouds && bClouds)
    {
        Clouds = SpawnMovable<AVolumetricCloud>(World, FTransform::Identity);
    }
    if (Clouds)
    {
        Clouds->SetActorHiddenInGame(!bClouds);
        if (UVolumetricCloudComponent* Layer = Clouds->FindComponentByClass<UVolumetricCloudComponent>())
        {
            Layer->SetLayerBottomAltitude(Preset.CloudBottomAltitudeKm);
            Layer->SetLayerHeight(Preset.CloudLayerHeightKm);
            Layer->SetVisibility(bClouds);
        }
    }
}

void UPSStadiumLightingSubsystem::ApplyFog(const FPSLightingPreset& Preset, const FPSLightingTierSettings& Tier)
{
    using namespace PSStadiumLightingPrivate;

    UWorld& World = *GetWorld();
    if (!IsValid(Fog))
    {
        Fog = FindFirst<AExponentialHeightFog>(World);
    }
    if (!Fog)
    {
        Fog = SpawnMovable<AExponentialHeightFog>(World, FTransform(FVector(0.0, 0.0, -100.0)));
    }
    UExponentialHeightFogComponent* Haze = Fog ? Fog->GetComponent() : nullptr;
    if (Haze)
    {
        Haze->SetFogDensity(Preset.FogDensity);
        Haze->SetFogHeightFalloff(Preset.FogHeightFalloff);
        Haze->SetVolumetricFog(Preset.bVolumetricFog && Tier.bVolumetricFog);
        Haze->SetVolumetricFogExtinctionScale(Preset.VolumetricFogExtinctionScale);
    }
}

void UPSStadiumLightingSubsystem::ApplyFloodlights(const FPSStadiumLightingCatalog& Catalog, const FPSLightingPreset& Preset, const FPSLightingTierSettings& Tier)
{
    using namespace PSStadiumLightingPrivate;

    // A preset owns the floodlights outright: the previous preset's go, this one's are spawned.
    for (ASpotLight* Previous : Floodlights)
    {
        if (IsValid(Previous))
        {
            Previous->Destroy();
        }
    }
    Floodlights.Reset();
    const FPSFloodlightSettings& Settings = Preset.Floodlights;
    if (Settings.IntensityCandela <= 0.f)
    {
        return;
    }

    UWorld& World = *GetWorld();
    const FPSFieldDimensions& Dimensions = PSField::GetDimensions();
    const TArray<FTransform> Transforms = ComputeFloodlightTransforms(Catalog, Dimensions);
    for (int32 Index = 0; Index < Transforms.Num(); ++Index)
    {
        ASpotLight* Bank = SpawnMovable<ASpotLight>(World, Transforms[Index]);
        if (!Bank)
        {
            continue;
        }
        Bank->Tags.Add(FloodlightTag);
        if (USpotLightComponent* Spot = Cast<USpotLightComponent>(Bank->GetLightComponent()))
        {
            Spot->SetIntensityUnits(ELightUnits::Candelas);
            Spot->SetIntensity(Settings.IntensityCandela);
            Spot->SetLightColor(FLinearColor::White);
            Spot->bUseTemperature = true;
            Spot->SetTemperature(Settings.TemperatureK);
            Spot->SetInnerConeAngle(Settings.InnerConeDeg);
            Spot->SetOuterConeAngle(Settings.OuterConeDeg);
            Spot->SetSourceRadius(Settings.SourceRadiusCm);
            Spot->SetAttenuationRadius(Settings.AttenuationRadiusYards * Dimensions.CentimetresPerYard);
            Spot->SetVolumetricScatteringIntensity(Settings.VolumetricScattering);
            const bool bShadowed = Tier.MaxShadowedFloodlights < 0 || Index < Tier.MaxShadowedFloodlights;
            Spot->SetCastShadows(Settings.bCastShadows && bShadowed);
        }
        Floodlights.Add(Bank);
    }
}

void UPSStadiumLightingSubsystem::ApplyLook(const FPSLightingPreset& Preset, const FPSLightingTierSettings& Tier)
{
    using namespace PSStadiumLightingPrivate;

    UWorld& World = *GetWorld();
    if (!IsValid(BroadcastLook))
    {
        // Only this subsystem's own volume: a volume someone placed by hand keeps its settings.
        for (TActorIterator<APostProcessVolume> It(&World); It; ++It)
        {
            if (IsValid(*It) && It->Tags.Contains(BroadcastLookTag))
            {
                BroadcastLook = *It;
                break;
            }
        }
    }
    if (!BroadcastLook)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        BroadcastLook = World.SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, Params);
        if (!BroadcastLook)
        {
            return;
        }
        BroadcastLook->Tags.Add(BroadcastLookTag);
    }
    BroadcastLook->bUnbound = true;
    BroadcastLook->BlendWeight = 1.f;

    const FPSBroadcastLook& Look = Preset.Look;
    const bool bExtended = IsExtendedLuminanceRange();
    const float Lens = Tier.bLensEffects ? 1.f : 0.f;
    FPostProcessSettings& Settings = BroadcastLook->Settings;

    Settings.bOverride_AutoExposureMethod = true;
    Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
    Settings.bOverride_AutoExposureMinBrightness = true;
    Settings.AutoExposureMinBrightness = ExposureToBrightnessSetting(Look.ExposureMinEV100, bExtended);
    Settings.bOverride_AutoExposureMaxBrightness = true;
    Settings.AutoExposureMaxBrightness = ExposureToBrightnessSetting(Look.ExposureMaxEV100, bExtended);
    Settings.bOverride_AutoExposureBias = true;
    Settings.AutoExposureBias = Look.ExposureCompensation;

    Settings.bOverride_BloomIntensity = true;
    Settings.BloomIntensity = Tier.bBloom ? Look.BloomIntensity : 0.f;
    Settings.bOverride_VignetteIntensity = true;
    Settings.VignetteIntensity = Look.VignetteIntensity * Lens;
    Settings.bOverride_SceneFringeIntensity = true;
    Settings.SceneFringeIntensity = Look.ChromaticAberration * Lens;
    Settings.bOverride_LensFlareIntensity = true;
    Settings.LensFlareIntensity = Look.LensFlareIntensity * Lens;
    Settings.bOverride_FilmGrainIntensity = true;
    Settings.FilmGrainIntensity = Look.FilmGrainIntensity * Lens;
    Settings.bOverride_MotionBlurAmount = true;
    Settings.MotionBlurAmount = Look.MotionBlurAmount;

    Settings.bOverride_WhiteTemp = true;
    Settings.WhiteTemp = Look.WhiteTemp;
    Settings.bOverride_ColorSaturation = true;
    Settings.ColorSaturation = FVector4(Look.Saturation, Look.Saturation, Look.Saturation, 1.0);
    Settings.bOverride_ColorContrast = true;
    Settings.ColorContrast = FVector4(Look.Contrast, Look.Contrast, Look.Contrast, 1.0);
    UTexture* Lut = LoadOptional<UTexture>(Look.ColorGradingLutPath);
    Settings.bOverride_ColorGradingLUT = Lut != nullptr;
    Settings.ColorGradingLUT = Lut;
    Settings.bOverride_ColorGradingIntensity = Lut != nullptr;
    Settings.ColorGradingIntensity = Look.ColorGradingLutIntensity;

    // Lumen where the tier affords it; the mobile renderer ignores both.
    Settings.bOverride_DynamicGlobalIlluminationMethod = true;
    Settings.DynamicGlobalIlluminationMethod = Tier.bLumen ? EDynamicGlobalIlluminationMethod::Lumen : EDynamicGlobalIlluminationMethod::None;
    Settings.bOverride_ReflectionMethod = true;
    Settings.ReflectionMethod = Tier.bLumen ? EReflectionMethod::Lumen : EReflectionMethod::ScreenSpace;
}

void UPSStadiumLightingSubsystem::ApplyDew(const FPSStadiumLightingCatalog& Catalog, const FPSLightingPreset& Preset)
{
    using namespace PSStadiumLightingPrivate;

    // The field's materials read it (MPC_Field, from the content pipeline); without them there is
    // nothing to set.
    if (UMaterialParameterCollection* Collection = LoadOptional<UMaterialParameterCollection>(Catalog.DewCollectionPath))
    {
        UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), Collection, Catalog.DewParameter, Preset.Dew);
    }
}
