// PSStadiumLightingTests.cpp -- lane V2 (Epic 46): day and night light and the broadcast look, from data
//
// Tests covered:
//   1. Data/stadium_lighting.json loads through UPSDataIngestion and validates: a Day and a Night
//      preset (Day the default), floodlight banks, a switch set for every platform tier; unsound
//      catalogs are caught.
//   2. Placement: the sun pitched down by its elevation; each floodlight at its bank's place above
//      the field, aimed at its aim point; -PSLighting picks a preset the catalog has; exposure in
//      EV100 becomes the volume's brightness setting either way the project's luminance range is;
//      the grade bakes into the engine's LUT layout, the identity grade into the neutral table.
//   3. The tiers: the mobile tiers turn off Lumen, the real-time sky capture, volumetric clouds and
//      fog and lens effects, and cap the shadowed floodlights; a tier not listed gets everything.
//   4. In a world: Day uses the level's own sun (no second one) and adds the sky light, atmosphere,
//      clouds, fog and an unbound broadcast volume with Lumen, and no floodlights. Night spawns a
//      movable floodlight per bank, all shadowed on the PC tier and only the tier's cap on mobile,
//      hides the clouds and turns on volumetric fog. Back to Day, the floodlights go.
//   5. The field_look content (tools/content_pipeline): the turf and paint materials, the dew
//      collection and the skies are real assets with the parameters the code sets.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/SpotLight.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/PackageName.h"
#include "PSDataIngestion.h"
#include "PSFieldDimensions.h"
#include "PSFieldSurfaceTypes.h"
#include "PSStadiumLightingSubsystem.h"
#include "UObject/SoftObjectPath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSStadiumLightingTests
{
    static UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    static void DestroyTestWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    template <typename ActorType>
    static int32 CountActors(UWorld* World)
    {
        int32 Count = 0;
        for (TActorIterator<ActorType> It(World); It; ++It)
        {
            Count += IsValid(*It) ? 1 : 0;
        }
        return Count;
    }

    static int32 CountTagged(UWorld* World, FName Tag)
    {
        int32 Count = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            Count += IsValid(*It) && It->Tags.Contains(Tag) ? 1 : 0;
        }
        return Count;
    }

    static bool LoadDefaultCatalog(FAutomationTestBase& Test, FPSStadiumLightingCatalog& OutCatalog)
    {
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        return Test.TestTrue(TEXT("Data/stadium_lighting.json loads"),
            Ingestion->LoadStadiumLightingFromJson(UPSStadiumLightingSubsystem::GetDefaultCatalogPath(), OutCatalog));
    }

    /** A real asset in this checkout, not a Git LFS pointer file (a few lines of text). */
    static bool IsRealAsset(const FString& ObjectPath)
    {
        FString File;
        const FString Package = FPackageName::ObjectPathToPackageName(ObjectPath);
        return FPackageName::DoesPackageExist(Package, &File) && IFileManager::Get().FileSize(*File) > 1024;
    }

    static bool HasScalar(const UMaterialInterface* Material, FName Name)
    {
        TArray<FMaterialParameterInfo> Infos;
        TArray<FGuid> Ids;
        Material->GetAllScalarParameterInfo(Infos, Ids);
        return Infos.ContainsByPredicate([Name](const FMaterialParameterInfo& Info) { return Info.Name == Name; });
    }

    static bool HasVector(const UMaterialInterface* Material, FName Name)
    {
        TArray<FMaterialParameterInfo> Infos;
        TArray<FGuid> Ids;
        Material->GetAllVectorParameterInfo(Infos, Ids);
        return Infos.ContainsByPredicate([Name](const FMaterialParameterInfo& Info) { return Info.Name == Name; });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The data
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStadiumLightingDataTest,
    "PlaySports.Lighting.CatalogData",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStadiumLightingDataTest::RunTest(const FString& Parameters)
{
    using namespace PSStadiumLightingTests;

    FPSStadiumLightingCatalog Catalog;
    if (!LoadDefaultCatalog(*this, Catalog))
    {
        return false;
    }
    TestEqual(TEXT("...and validates"), UPSStadiumLightingSubsystem::ValidateCatalog(Catalog).Num(), 0);
    TestEqual(TEXT("Day is the default"), Catalog.DefaultPreset, FName(TEXT("Day")));
    const FPSLightingPreset* Day = UPSStadiumLightingSubsystem::FindPreset(Catalog, TEXT("Day"));
    const FPSLightingPreset* Night = UPSStadiumLightingSubsystem::FindPreset(Catalog, TEXT("Night"));
    if (TestNotNull(TEXT("A Day preset"), Day) && TestNotNull(TEXT("A Night preset"), Night))
    {
        TestTrue(TEXT("By day the sun is up and the floodlights off"), Day->SunElevationDeg > 0.f && Day->SunIntensityLux > 10000.f && Day->Floodlights.IntensityCandela == 0.f);
        TestTrue(TEXT("At night the floodlights carry the field"), Night->Floodlights.IntensityCandela > 0.f && Night->SunIntensityLux < 10.f);
        TestTrue(TEXT("...the picture is exposed for them, darker than day"), Night->Look.ExposureMaxEV100 < Day->Look.ExposureMinEV100);
        TestTrue(TEXT("...and the grass is dewy"), Night->Dew > Day->Dew);
    }
    TestTrue(TEXT("Floodlight banks"), Catalog.Banks.Num() >= 4);
    for (const FName TierId : { FName(TEXT("DesktopHigh")), FName(TEXT("MobileBaseline")), FName(TEXT("MobileLow")) })
    {
        TestTrue(*FString::Printf(TEXT("Switches for %s"), *TierId.ToString()),
            Catalog.TierSettings.ContainsByPredicate([TierId](const FPSLightingTierSettings& Tier) { return Tier.TierId == TierId; }));
    }

    FPSStadiumLightingCatalog Broken = Catalog;
    Broken.DefaultPreset = TEXT("Dusk");
    Broken.Presets[0].Look.ExposureMaxEV100 = Broken.Presets[0].Look.ExposureMinEV100 - 1.f;
    Broken.Banks.Add(Broken.Banks.Num() > 0 ? FPSFloodlightBank(Catalog.Banks[0]) : FPSFloodlightBank());
    TestEqual(TEXT("An unsound catalog is caught, one problem each"), UPSStadiumLightingSubsystem::ValidateCatalog(Broken).Num(), 3);

    FPSStadiumLightingCatalog Dark = Catalog;
    Dark.Banks.Reset();
    TestTrue(TEXT("...and floodlights with no banks"), UPSStadiumLightingSubsystem::ValidateCatalog(Dark).Num() > 0);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Placement, preset choice and exposure
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStadiumLightingPlacementTest,
    "PlaySports.Lighting.Placement",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStadiumLightingPlacementTest::RunTest(const FString& Parameters)
{
    using namespace PSStadiumLightingTests;

    FPSStadiumLightingCatalog Catalog;
    if (!LoadDefaultCatalog(*this, Catalog))
    {
        return false;
    }

    FPSLightingPreset Preset;
    Preset.SunElevationDeg = 40.f;
    Preset.SunAzimuthDeg = 90.f;
    const FVector SunDirection = UPSStadiumLightingSubsystem::ComputeSunRotation(Preset).Vector();
    TestTrue(TEXT("The sun shines down"), SunDirection.Z < 0.0);
    TestEqual(TEXT("...at its elevation"), FMath::RadiansToDegrees(FMath::Asin(-SunDirection.Z)), 40.0, 0.01);
    TestTrue(TEXT("...travelling along its azimuth (90: from the near sideline to the far)"), SunDirection.Y > 0.7 && FMath::Abs(SunDirection.X) < 0.01);

    const FPSFieldDimensions& Field = PSField::GetDimensions();
    const TArray<FTransform> Lights = UPSStadiumLightingSubsystem::ComputeFloodlightTransforms(Catalog, Field);
    if (TestEqual(TEXT("A light per bank"), Lights.Num(), Catalog.Banks.Num()))
    {
        const double Cm = Field.CentimetresPerYard;
        for (int32 Index = 0; Index < Lights.Num(); ++Index)
        {
            const FPSFloodlightBank& Bank = Catalog.Banks[Index];
            const FVector Place(Bank.XYards * Cm, Bank.YYards * Cm, Bank.HeightYards * Cm);
            const FVector Aim(Bank.AimXYards * Cm, Bank.AimYYards * Cm, 0.0);
            TestTrue(*FString::Printf(TEXT("%s hangs at its place"), *Bank.BankId.ToString()), Lights[Index].GetLocation().Equals(Place, 0.1));
            TestTrue(TEXT("...aimed at its aim point"), FVector::DotProduct(Lights[Index].GetRotation().GetForwardVector(), (Aim - Place).GetSafeNormal()) > 0.9999);
            TestTrue(TEXT("...outside the field"), FMath::Abs(Place.Y) > PSField::SidelineY() || Place.X < PSField::EndLineX(false) || Place.X > PSField::EndLineX(true));
        }
    }

    TestEqual(TEXT("-PSLighting=Night picks Night"), UPSStadiumLightingSubsystem::ResolvePresetId(Catalog, TEXT("-game -PSLighting=Night -log")), FName(TEXT("Night")));
    TestEqual(TEXT("...a preset the catalog lacks falls back to the default"), UPSStadiumLightingSubsystem::ResolvePresetId(Catalog, TEXT("-PSLighting=Dusk")), Catalog.DefaultPreset);
    TestEqual(TEXT("...as does no switch"), UPSStadiumLightingSubsystem::ResolvePresetId(Catalog, TEXT("-game")), Catalog.DefaultPreset);

    TestEqual(TEXT("With the extended luminance range the setting is EV100"), UPSStadiumLightingSubsystem::ExposureToBrightnessSetting(10.f, true), 10.f);
    TestEqual(TEXT("...without it, the luminance EV100 stands for"), UPSStadiumLightingSubsystem::ExposureToBrightnessSetting(10.f, false), 1.2f * 1024.f, 0.01f);

    // The grade's LUT: the identity grade is the engine's neutral table (red along each slice,
    // green down the rows, blue across the slices), and a gain brightens.
    const int32 Size = UPSStadiumLightingSubsystem::LutSize;
    const TArray<FColor> Neutral = UPSStadiumLightingSubsystem::ComputeGradeLut(FPSLutGrade());
    if (TestEqual(TEXT("The LUT is 256 x 16"), Neutral.Num(), Size * Size * Size))
    {
        const int32 Step = 255 / (Size - 1);
        bool bIdentity = true;
        for (int32 Y = 0; Y < Size; ++Y)
        {
            for (int32 X = 0; X < Size * Size; ++X)
            {
                const FColor& Texel = Neutral[Y * Size * Size + X];
                bIdentity &= Texel.R == (X % Size) * Step && Texel.G == Y * Step && Texel.B == (X / Size) * Step;
            }
        }
        TestTrue(TEXT("...and the identity grade is the neutral table"), bIdentity);
    }
    FPSLutGrade Brighter;
    Brighter.Gain = FLinearColor(1.2f, 1.2f, 1.2f, 1.f);
    const TArray<FColor> Graded = UPSStadiumLightingSubsystem::ComputeGradeLut(Brighter);
    const int32 MidGrey = 7 * Size * Size + 7 * Size + 7;
    TestTrue(TEXT("A gain above 1 brightens the middle grey"), Graded.IsValidIndex(MidGrey) && Neutral.IsValidIndex(MidGrey) && Graded[MidGrey].G > Neutral[MidGrey].G);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The tiers' switches
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStadiumLightingTierTest,
    "PlaySports.Lighting.TierSwitches",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStadiumLightingTierTest::RunTest(const FString& Parameters)
{
    using namespace PSStadiumLightingTests;

    FPSStadiumLightingCatalog Catalog;
    if (!LoadDefaultCatalog(*this, Catalog))
    {
        return false;
    }
    const FPSLightingTierSettings Desktop = UPSStadiumLightingSubsystem::ResolveTierSettings(Catalog, TEXT("DesktopHigh"));
    TestTrue(TEXT("PC: Lumen, a real-time sky, volumetric clouds and fog, lens effects"),
        Desktop.bLumen && Desktop.bRealTimeSkyCapture && Desktop.bVolumetricClouds && Desktop.bVolumetricFog && Desktop.bLensEffects);
    TestEqual(TEXT("...and every floodlight's shadow"), Desktop.MaxShadowedFloodlights, -1);

    for (const FName TierId : { FName(TEXT("MobileBaseline")), FName(TEXT("MobileLow")) })
    {
        const FPSLightingTierSettings Mobile = UPSStadiumLightingSubsystem::ResolveTierSettings(Catalog, TierId);
        TestFalse(*FString::Printf(TEXT("%s: no Lumen"), *TierId.ToString()), Mobile.bLumen);
        TestFalse(TEXT("...a cubemap sky light"), Mobile.bRealTimeSkyCapture);
        TestFalse(TEXT("...no volumetric clouds or fog"), Mobile.bVolumetricClouds || Mobile.bVolumetricFog);
        TestFalse(TEXT("...no lens effects"), Mobile.bLensEffects);
        TestTrue(TEXT("...and a cap on shadowed floodlights"), Mobile.MaxShadowedFloodlights >= 0 && Mobile.MaxShadowedFloodlights < Catalog.Banks.Num());
    }

    const FPSLightingTierSettings Unknown = UPSStadiumLightingSubsystem::ResolveTierSettings(Catalog, TEXT("SomeNewTier"));
    TestTrue(TEXT("A tier not listed gets everything"), Unknown.bLumen && Unknown.bVolumetricClouds && Unknown.MaxShadowedFloodlights == -1);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The presets in a world
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStadiumLightingWorldTest,
    "PlaySports.Lighting.AppliesInWorld",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStadiumLightingWorldTest::RunTest(const FString& Parameters)
{
    using namespace PSStadiumLightingTests;

    FPSStadiumLightingCatalog Catalog;
    if (!LoadDefaultCatalog(*this, Catalog))
    {
        return false;
    }
    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("World"), World))
    {
        return false;
    }
    UPSStadiumLightingSubsystem* Lighting = World->GetSubsystem<UPSStadiumLightingSubsystem>();
    if (!TestNotNull(TEXT("A game world has the lighting subsystem"), Lighting))
    {
        DestroyTestWorld(World);
        return false;
    }

    // The level's own sun, as GameMap has: the preset sets it rather than adding another.
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADirectionalLight* LevelSun = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform::Identity, SpawnParams);

    // Day on the PC tier.
    TestTrue(TEXT("Day applies"), Lighting->ApplyPresetForTier(Catalog, TEXT("Day"), TEXT("DesktopHigh")));
    TestEqual(TEXT("...as the active preset"), Lighting->GetActivePresetId(), FName(TEXT("Day")));
    TestTrue(TEXT("The level's sun is the sun"), Lighting->GetSun() == LevelSun);
    TestEqual(TEXT("...and the only one"), CountActors<ADirectionalLight>(World), 1);
    const FPSLightingPreset* Day = UPSStadiumLightingSubsystem::FindPreset(Catalog, TEXT("Day"));
    if (LevelSun && Day)
    {
        const ULightComponent* SunLight = LevelSun->GetLightComponent();
        TestTrue(TEXT("...movable"), SunLight && SunLight->Mobility == EComponentMobility::Movable);
        TestEqual(TEXT("...at the preset's intensity"), SunLight ? SunLight->Intensity : -1.f, Day->SunIntensityLux, 1.f);
        TestTrue(TEXT("...pitched to the preset's elevation"), LevelSun->GetActorRotation().Equals(UPSStadiumLightingSubsystem::ComputeSunRotation(*Day), 0.01f));
    }
    TestNotNull(TEXT("A sky light"), Lighting->GetSkyLight());
    TestNotNull(TEXT("A sky atmosphere"), Lighting->GetSkyAtmosphere());
    TestNotNull(TEXT("Height fog"), Lighting->GetFog());
    if (TestNotNull(TEXT("Volumetric clouds by day on PC"), Lighting->GetClouds()))
    {
        TestFalse(TEXT("...shown"), Lighting->GetClouds()->IsHidden());
    }
    TestEqual(TEXT("No floodlights by day"), Lighting->GetFloodlights().Num(), 0);
    APostProcessVolume* Look = Lighting->GetBroadcastLook();
    if (TestNotNull(TEXT("The broadcast look"), Look))
    {
        TestTrue(TEXT("...covers the whole world"), Look->bUnbound != 0);
        TestTrue(TEXT("...tagged as the subsystem's own"), Look->Tags.Contains(UPSStadiumLightingSubsystem::BroadcastLookTag));
        TestTrue(TEXT("...with Lumen on PC"), Look->Settings.bOverride_DynamicGlobalIlluminationMethod != 0
            && Look->Settings.DynamicGlobalIlluminationMethod == EDynamicGlobalIlluminationMethod::Lumen);
        if (Day)
        {
            TestEqual(TEXT("...the preset's bloom"), Look->Settings.BloomIntensity, Day->Look.BloomIntensity);
            TestEqual(TEXT("...and white balance"), Look->Settings.WhiteTemp, Day->Look.WhiteTemp);
            if (Day->Look.ColorGradingLutPath.IsEmpty() && Day->Look.Grade.bEnabled)
            {
                TestTrue(TEXT("...and the grade's LUT"), Look->Settings.bOverride_ColorGradingLUT != 0 && Look->Settings.ColorGradingLUT != nullptr);
            }
        }
    }

    // Night on the PC tier: a movable, shadowed floodlight per bank.
    TestTrue(TEXT("Night applies"), Lighting->ApplyPresetForTier(Catalog, TEXT("Night"), TEXT("DesktopHigh")));
    TestEqual(TEXT("A floodlight per bank"), Lighting->GetFloodlights().Num(), Catalog.Banks.Num());
    TestEqual(TEXT("...each tagged"), CountTagged(World, UPSStadiumLightingSubsystem::FloodlightTag), Catalog.Banks.Num());
    int32 Shadowed = 0;
    for (ASpotLight* Bank : Lighting->GetFloodlights())
    {
        const ULightComponent* Spot = Bank ? Bank->GetLightComponent() : nullptr;
        if (TestNotNull(TEXT("...with its light"), Spot))
        {
            TestTrue(TEXT("...movable"), Spot->Mobility == EComponentMobility::Movable);
            Shadowed += Spot->CastShadows ? 1 : 0;
        }
    }
    TestEqual(TEXT("...every one shadowed on PC"), Shadowed, Catalog.Banks.Num());
    if (Lighting->GetClouds())
    {
        TestTrue(TEXT("No clouds at night"), Lighting->GetClouds()->IsHidden());
    }
    const UExponentialHeightFogComponent* Haze = Lighting->GetFog() ? Lighting->GetFog()->GetComponent() : nullptr;
    TestTrue(TEXT("Haze in the beams on PC"), Haze && Haze->bEnableVolumetricFog);
    TestEqual(TEXT("Still the one sun"), CountActors<ADirectionalLight>(World), 1);
    TestEqual(TEXT("...and the one broadcast look"), CountTagged(World, UPSStadiumLightingSubsystem::BroadcastLookTag), 1);

    // Night on the iPhone tier: the shadow cap, no Lumen, no volumetric fog.
    const FPSLightingTierSettings Mobile = UPSStadiumLightingSubsystem::ResolveTierSettings(Catalog, TEXT("MobileBaseline"));
    TestTrue(TEXT("Night applies on mobile"), Lighting->ApplyPresetForTier(Catalog, TEXT("Night"), TEXT("MobileBaseline")));
    Shadowed = 0;
    for (ASpotLight* Bank : Lighting->GetFloodlights())
    {
        const ULightComponent* Spot = Bank ? Bank->GetLightComponent() : nullptr;
        Shadowed += Spot && Spot->CastShadows ? 1 : 0;
    }
    TestEqual(TEXT("...only the tier's cap of floodlights cast shadows"), Shadowed, FMath::Min(Mobile.MaxShadowedFloodlights, Catalog.Banks.Num()));
    TestEqual(TEXT("...the previous floodlights replaced, not added to"), CountTagged(World, UPSStadiumLightingSubsystem::FloodlightTag), Catalog.Banks.Num());
    if (Look)
    {
        TestTrue(TEXT("...no Lumen"), Look->Settings.DynamicGlobalIlluminationMethod == EDynamicGlobalIlluminationMethod::None);
        TestEqual(TEXT("...no lens effects"), Look->Settings.VignetteIntensity, 0.f);
    }
    TestTrue(TEXT("...no volumetric fog"), Haze && !Haze->bEnableVolumetricFog);

    // Back to day: the floodlights go.
    TestTrue(TEXT("Day applies again"), Lighting->ApplyPresetForTier(Catalog, TEXT("Day"), TEXT("DesktopHigh")));
    TestEqual(TEXT("...and the floodlights are gone"), CountTagged(World, UPSStadiumLightingSubsystem::FloodlightTag), 0);
    TestFalse(TEXT("An unknown preset doesn't apply"), Lighting->ApplyPresetForTier(Catalog, TEXT("Dusk"), TEXT("DesktopHigh")));
    TestEqual(TEXT("...and leaves the active one"), Lighting->GetActivePresetId(), FName(TEXT("Day")));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The field_look content
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFieldLookContentTest,
    "PlaySports.Content.FieldLook",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFieldLookContentTest::RunTest(const FString& Parameters)
{
    using namespace PSStadiumLightingTests;

    // The paths the data names: the field's style and the lighting catalog.
    const FPSFieldMarkingsStyle Style;
    FPSStadiumLightingCatalog Catalog;
    if (!LoadDefaultCatalog(*this, Catalog))
    {
        return false;
    }

    for (const FString& Path : { Style.TurfMaterialPath, Style.PaintMaterialPath, Style.TierLooks.Num() > 0 ? Style.TierLooks[0].TurfMaterialPath : Style.TurfMaterialPath })
    {
        if (!TestTrue(*FString::Printf(TEXT("%s is a real asset (generated by field_look, checked out with LFS)"), *Path), IsRealAsset(Path)))
        {
            continue;
        }
        const UMaterialInterface* Material = Cast<UMaterialInterface>(FSoftObjectPath(Path).TryLoad());
        if (!TestNotNull(TEXT("...and loads as a material"), Material))
        {
            continue;
        }
        TestTrue(TEXT("...with the colour parameter"), HasVector(Material, Style.ColorParameter));
        TestTrue(TEXT("...tiled in world space"), HasScalar(Material, TEXT("TileSizeCm")));
        TestTrue(TEXT("...with mowing stripes"), HasScalar(Material, TEXT("StripeWidthCm")) && HasScalar(Material, TEXT("StripeOriginCm")));
        for (const TPair<FName, float>& Scalar : Style.MaterialScalars)
        {
            // Every scalar the data sets exists in the full turf or the paint (the lite turf drops some).
            if (Path == Style.TurfMaterialPath && Scalar.Key != TEXT("PaintTextureStrength"))
            {
                TestTrue(*FString::Printf(TEXT("...%s"), *Scalar.Key.ToString()), HasScalar(Material, Scalar.Key));
            }
        }
    }
    if (const UMaterialInterface* Paint = Cast<UMaterialInterface>(FSoftObjectPath(Style.PaintMaterialPath).TryLoad()))
    {
        TestTrue(TEXT("The paint is masked: the grass shows through"), Paint->GetBlendMode() == BLEND_Masked);
        TestTrue(TEXT("...with coverage and the arrow's triangle"), HasScalar(Paint, TEXT("Coverage")) && HasScalar(Paint, TEXT("ShapeTriangle")));
    }

    if (TestTrue(TEXT("The dew collection is a real asset"), IsRealAsset(Catalog.DewCollectionPath)))
    {
        const UMaterialParameterCollection* Collection = Cast<UMaterialParameterCollection>(FSoftObjectPath(Catalog.DewCollectionPath).TryLoad());
        TestTrue(TEXT("...with the Dew scalar"), Collection && Collection->GetScalarParameterByName(Catalog.DewParameter) != nullptr);
    }
    for (const FPSLightingPreset& Preset : Catalog.Presets)
    {
        if (!Preset.SkyCubemapPath.IsEmpty() && TestTrue(*FString::Printf(TEXT("%s's sky is a real asset"), *Preset.PresetId.ToString()), IsRealAsset(Preset.SkyCubemapPath)))
        {
            TestNotNull(TEXT("...a cubemap"), Cast<UTextureCube>(FSoftObjectPath(Preset.SkyCubemapPath).TryLoad()));
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
