#include "PSPlatformTiers.h"
#include "PSDataIngestion.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

FString PSPlatformTiers::GetDefaultCatalogPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/platform_tiers.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

TArray<FString> PSPlatformTiers::ValidateCatalog(const FPSPlatformTierCatalog& Catalog)
{
    TArray<FString> Problems;
    TSet<FName> Seen;
    for (int32 Index = 0; Index < Catalog.Tiers.Num(); ++Index)
    {
        const FPSPlatformTier& Tier = Catalog.Tiers[Index];
        if (Tier.TierId.IsNone() || Seen.Contains(Tier.TierId))
        {
            Problems.Add(FString::Printf(TEXT("Tiers[%d]: TierId is empty or used twice"), Index));
        }
        Seen.Add(Tier.TierId);
        if (Tier.DeviceProfile.IsNone())
        {
            Problems.Add(FString::Printf(TEXT("Tiers[%d]: DeviceProfile is empty"), Index));
        }
        if (Tier.AIDecisionInterval < 0.f)
        {
            Problems.Add(FString::Printf(TEXT("Tiers[%d]: AIDecisionInterval must be 0 or more"), Index));
        }
        if (!(Tier.TelemetrySampleRateHz > 0.f) || !(Tier.TelemetrySampleBudgetMs > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("Tiers[%d]: TelemetrySampleRateHz and TelemetrySampleBudgetMs must be above 0"), Index));
        }
        if (Tier.ReplayPoseRateHz < 0.f)
        {
            Problems.Add(FString::Printf(TEXT("Tiers[%d]: ReplayPoseRateHz must be 0 or more"), Index));
        }
        Problems.Append(ValidateSystemBudgets(Tier, Index));
        if (Tier.PlayArtRefreshHz < 0.f)
        {
            Problems.Add(FString::Printf(TEXT("Tiers[%d]: PlayArtRefreshHz must be 0 or more"), Index));
        }
        if (Tier.AudioUpdateHz < 0.f || Tier.CrowdUpdateHz < 0.f)
        {
            Problems.Add(FString::Printf(TEXT("Tiers[%d]: AudioUpdateHz and CrowdUpdateHz must be 0 or more"), Index));
        }
        if (Tier.AudioMaxVoices < 1)
        {
            Problems.Add(FString::Printf(TEXT("Tiers[%d]: AudioMaxVoices must be 1 or more"), Index));
        }
    }
    if (!FindTier(Catalog, Catalog.DefaultTier))
    {
        Problems.Add(FString::Printf(TEXT("DefaultTier '%s' is not a tier"), *Catalog.DefaultTier.ToString()));
    }
    for (const FPSPlatformTierMapping& Mapping : Catalog.Platforms)
    {
        if (!FindTier(Catalog, Mapping.Tier))
        {
            Problems.Add(FString::Printf(TEXT("Platform '%s' maps to unknown tier '%s'"), *Mapping.Platform, *Mapping.Tier.ToString()));
        }
    }
    return Problems;
}

TArray<FString> PSPlatformTiers::ValidateSystemBudgets(const FPSPlatformTier& Tier, int32 Index)
{
    // Epic 114: a budget for every system, none twice, all within the tier's frame.
    TArray<FString> Problems;
    if (!(Tier.TargetFrameRate > 0.f))
    {
        Problems.Add(FString::Printf(TEXT("Tiers[%d]: TargetFrameRate must be above 0"), Index));
        return Problems;
    }
    const UEnum* Systems = StaticEnum<EPSPerfSystem>();
    float Total = 0.f;
    for (int32 SystemIndex = 0; Systems && SystemIndex < Systems->NumEnums() - 1; ++SystemIndex)
    {
        const EPSPerfSystem System = static_cast<EPSPerfSystem>(Systems->GetValueByIndex(SystemIndex));
        int32 Count = 0;
        for (const FPSSystemBudget& Budget : Tier.SystemBudgets)
        {
            Count += Budget.System == System ? 1 : 0;
        }
        if (Count != 1)
        {
            Problems.Add(FString::Printf(TEXT("Tiers[%d]: SystemBudgets needs exactly one budget for %s (has %d)"), Index, *Systems->GetNameStringByIndex(SystemIndex), Count));
        }
    }
    for (const FPSSystemBudget& Budget : Tier.SystemBudgets)
    {
        if (Budget.BudgetMs < 0.f)
        {
            Problems.Add(FString::Printf(TEXT("Tiers[%d]: the %s budget must be 0 or more"), Index, *UEnum::GetValueAsString(Budget.System)));
        }
        Total += FMath::Max(Budget.BudgetMs, 0.f);
    }
    if (Total > GetFrameBudgetMs(Tier) + KINDA_SMALL_NUMBER)
    {
        Problems.Add(FString::Printf(TEXT("Tiers[%d]: the system budgets add up to %.2f ms, more than a %.0f fps frame (%.2f ms)"), Index, Total, Tier.TargetFrameRate, GetFrameBudgetMs(Tier)));
    }
    const float TelemetryBudget = FindSystemBudget(Tier, EPSPerfSystem::Telemetry);
    if (TelemetryBudget >= 0.f && TelemetryBudget + KINDA_SMALL_NUMBER < Tier.TelemetrySampleBudgetMs)
    {
        Problems.Add(FString::Printf(TEXT("Tiers[%d]: the Telemetry budget is below TelemetrySampleBudgetMs, which it includes"), Index));
    }
    return Problems;
}

float PSPlatformTiers::FindSystemBudget(const FPSPlatformTier& Tier, EPSPerfSystem System)
{
    const FPSSystemBudget* Budget = Tier.SystemBudgets.FindByPredicate([System](const FPSSystemBudget& Candidate) { return Candidate.System == System; });
    return Budget ? Budget->BudgetMs : -1.f;
}

float PSPlatformTiers::GetFrameBudgetMs(const FPSPlatformTier& Tier)
{
    return Tier.TargetFrameRate > 0.f ? 1000.f / Tier.TargetFrameRate : 0.f;
}

const FPSPlatformTier* PSPlatformTiers::FindTier(const FPSPlatformTierCatalog& Catalog, FName TierId)
{
    return Catalog.Tiers.FindByPredicate([TierId](const FPSPlatformTier& Tier) { return Tier.TierId == TierId; });
}

FName PSPlatformTiers::ResolveTierId(const FPSPlatformTierCatalog& Catalog, const FString& PlatformName, const FString& Override)
{
    if (!Override.IsEmpty() && FindTier(Catalog, FName(*Override)))
    {
        return FName(*Override);
    }
    for (const FPSPlatformTierMapping& Mapping : Catalog.Platforms)
    {
        if (Mapping.Platform.Equals(PlatformName, ESearchCase::IgnoreCase) && FindTier(Catalog, Mapping.Tier))
        {
            return Mapping.Tier;
        }
    }
    return Catalog.DefaultTier;
}

const FPSPlatformTier& PSPlatformTiers::GetActiveTier()
{
    // Resolved once per run; a missing or broken catalog leaves the defaults (every frame).
    static FPSPlatformTier Active;
    static bool bResolved = false;
    if (!bResolved)
    {
        bResolved = true;
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        FPSPlatformTierCatalog Catalog;
        if (Ingestion->LoadPlatformTiersFromJson(GetDefaultCatalogPath(), Catalog))
        {
            FString Override;
            FParse::Value(FCommandLine::Get(), TEXT("PSTier="), Override);
            const FName TierId = ResolveTierId(Catalog, UGameplayStatics::GetPlatformName(), Override);
            if (const FPSPlatformTier* Tier = FindTier(Catalog, TierId))
            {
                Active = *Tier;
            }
            UE_LOG(LogTemp, Display, TEXT("PSPlatformTiers: running tier '%s' on %s."), *Active.TierId.ToString(), *UGameplayStatics::GetPlatformName());
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("PSPlatformTiers: Could not load %s; using desktop defaults."), *GetDefaultCatalogPath());
        }
    }
    return Active;
}
