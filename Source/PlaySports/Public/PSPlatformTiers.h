// PSPlatformTiers.h - Epic 129: platform tiers, so every system knows its budget on a phone
#pragma once

#include "CoreMinimal.h"
#include "PSPlatformTiers.generated.h"

/** How much broadcast overlay a tier draws (Specs/Platform_Audit.md section 4). */
UENUM(BlueprintType)
enum class EPSOverlayDetail : uint8
{
    /** Everything, animated. */
    Full,
    /** Everything, without animated transitions or pulses. */
    Simplified,
    /** The score bug and the indicators play needs (the control reticle), static. */
    Minimal
};

/**
 * One performance tier (Data/platform_tiers.json; Architecture rule 4). A tier pairs the
 * code-side budgets below with a device profile in Config/DefaultDeviceProfiles.ini, which
 * carries the rendering half (scalability groups, frame cap). See Specs/Platform_Audit.md.
 */
USTRUCT(BlueprintType)
struct FPSPlatformTier
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    FName TierId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    FString Description;

    /** The device profile whose rendering settings go with this tier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    FName DeviceProfile;

    /** Seconds between decisions of each AI player (0 = every frame). Twenty-two AI players
     *  each read the whole field per decision, so this is the main CPU knob on a phone. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    float AIDecisionInterval = 0.f;

    /** Telemetry snapshots per second (UPSTelemetrySamplingSubsystem, Epic 26): every pawn's
     *  position and motion, for overlays, trails and replay. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    float TelemetrySampleRateHz = 30.f;

    /** What one telemetry snapshot may cost, in ms; over it the sampler halves its rate. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    float TelemetrySampleBudgetMs = 0.25f;

    /** How much broadcast overlay this tier draws (Track A). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    EPSOverlayDetail OverlayDetail = EPSOverlayDetail::Full;
};

/** Which tier a platform runs by default (platform names as UGameplayStatics::GetPlatformName
 *  reports them: Windows, Mac, IOS, Android). */
USTRUCT(BlueprintType)
struct FPSPlatformTierMapping
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    FString Platform;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    FName Tier;
};

USTRUCT(BlueprintType)
struct FPSPlatformTierCatalog
{
    GENERATED_BODY()

    /** The tier for a platform with no mapping. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    FName DefaultTier;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    TArray<FPSPlatformTierMapping> Platforms;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    TArray<FPSPlatformTier> Tiers;
};

/**
 * The run's platform tier (Epic 129). Resolved once: a -PSTier=<TierId> command-line
 * override, else the platform's mapping, else DefaultTier. Systems with a per-tier cost read
 * their budget from GetActiveTier() rather than hardcoding one -- today the AI players'
 * decision rate (UPSSkillPlayerAIComponent, UPSDefenderAIComponent).
 */
namespace PSPlatformTiers
{
    PLAYSPORTS_API FString GetDefaultCatalogPath();

    /** Problems with Catalog (empty when sound). */
    PLAYSPORTS_API TArray<FString> ValidateCatalog(const FPSPlatformTierCatalog& Catalog);

    PLAYSPORTS_API const FPSPlatformTier* FindTier(const FPSPlatformTierCatalog& Catalog, FName TierId);

    /** The tier for PlatformName, unless Override names a tier. */
    PLAYSPORTS_API FName ResolveTierId(const FPSPlatformTierCatalog& Catalog, const FString& PlatformName, const FString& Override);

    /** The tier this run uses, loaded from the default catalog and resolved on first use. */
    PLAYSPORTS_API const FPSPlatformTier& GetActiveTier();
}
