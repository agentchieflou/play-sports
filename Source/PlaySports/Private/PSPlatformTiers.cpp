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
