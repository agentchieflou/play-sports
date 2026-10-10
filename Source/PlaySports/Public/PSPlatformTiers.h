// PSPlatformTiers.h - Epic 129: platform tiers, so every system knows its budget on a phone
#pragma once

#include "CoreMinimal.h"
#include "PSPerfTypes.h"
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

    /** How often a replay re-poses the players and the ball, per second (UPSReplaySubsystem,
     *  Epic 41); 0 re-poses every frame. Each pose moves every pawn and the ball. A scrub or a
     *  frame step always shows at once. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    float ReplayPoseRateHz = 0.f;

    /** How much broadcast overlay this tier draws (Track A). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    EPSOverlayDetail OverlayDetail = EPSOverlayDetail::Full;

    /** The title-safe area (Epic 150): the share of the screen's width and height, centred, that
     *  HUD and menu text and controls stay inside, so a TV's edge never hides them. 1 is the whole
     *  screen (a monitor; a phone keeps to its own safe insets); 0.9 is the inner 90% Microsoft
     *  asks of games on a TV. PSTitleSafeArea applies it to every HUD and menu widget. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    float TitleSafeArea = 1.f;

    /** The frame rate this tier is budgeted for (Epic 114): a frame is 1000 / this ms. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    float TargetFrameRate = 60.f;

    /** Game-thread ms per frame each of the game's systems may use (Epic 114): one per
     *  EPSPerfSystem, together within the frame. The profiling harness and CI hold the
     *  measured times to them (Specs/Platform_Audit.md section 7). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    TArray<FPSSystemBudget> SystemBudgets;

    /** How often a second the pre-snap play art (Epic 27) resolves the call again to follow the
     *  players as they shift and go in motion; 0 redraws it only when something is announced
     *  (a call, a hot route, a new spot). Each refresh resolves every player's route. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    float PlayArtRefreshHz = 30.f;

    /** How often a second the audio (UPSAudioSubsystem, Epic 23) releases finished voices and
     *  follows the volume settings, and the commentary booth (Epic 96) moves its lines along;
     *  0 is every frame. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    float AudioUpdateHz = 0.f;

    /** One-shot sounds that may play at once; past it a cue takes a lower-priority one's voice
     *  or gives way. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    int32 AudioMaxVoices = 32;

    /** How often a second the crowd's excitement (UPSCrowdExcitementSubsystem, Epic 23.2) settles
     *  toward its resting level and its level is re-rated; 0 is every frame. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    float CrowdUpdateHz = 30.f;
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

    /** Problems with one tier's system budgets (Epic 114); Index names it in the messages. */
    PLAYSPORTS_API TArray<FString> ValidateSystemBudgets(const FPSPlatformTier& Tier, int32 Index);

    PLAYSPORTS_API const FPSPlatformTier* FindTier(const FPSPlatformTierCatalog& Catalog, FName TierId);

    /** System's frame-time budget on Tier, ms; negative when the tier has none for it. */
    PLAYSPORTS_API float FindSystemBudget(const FPSPlatformTier& Tier, EPSPerfSystem System);

    /** 1000 / the tier's target frame rate: the whole frame, ms. */
    PLAYSPORTS_API float GetFrameBudgetMs(const FPSPlatformTier& Tier);

    /** The tier for PlatformName, unless Override names a tier. */
    PLAYSPORTS_API FName ResolveTierId(const FPSPlatformTierCatalog& Catalog, const FString& PlatformName, const FString& Override);

    /** The tier this run uses, loaded from the default catalog and resolved on first use. */
    PLAYSPORTS_API const FPSPlatformTier& GetActiveTier();
}
