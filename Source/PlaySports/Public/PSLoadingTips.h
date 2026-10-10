// PSLoadingTips.h - Epic 101: the tips shown while a level loads
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Math/RandomStream.h"
#include "PSLoadingTips.generated.h"

/** One tip and the modes it suits ("Any", "PlayNow", "Franchise", "Practice"). */
USTRUCT(BlueprintType)
struct FPSLoadingTipDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loading")
    FName TipId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loading")
    FString Text;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loading")
    TArray<FName> Contexts;
};

/** Top-level shape of Data/loading_tips.json, loaded by UPSDataIngestion::LoadLoadingTipsFromJson. */
USTRUCT(BlueprintType)
struct FPSLoadingTipCatalog
{
    GENERATED_BODY()

    /** The loading screen stays up at least this long, so a tip can be read. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loading")
    float MinimumDisplaySeconds = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loading")
    TArray<FPSLoadingTipDef> Tips;
};

/**
 * UPSLoadingTips picks the next tip for a mode. Tips for that mode (plus "Any" tips) come out
 * of a shuffled bag, so none repeats until every eligible tip has been shown. Seed it for a
 * repeatable order (tests); otherwise it seeds from the clock.
 */
UCLASS(BlueprintType)
class PLAYSPORTS_API UPSLoadingTips : public UObject
{
    GENERATED_BODY()

public:
    UPSLoadingTips();

    static FString GetDefaultTipsPath();

    /** Loads the tips through UPSDataIngestion. */
    bool LoadFromJson(const FString& JsonFilePath);

    /** Loads from the default path once; later calls do nothing. */
    void EnsureLoaded();

    void SetSeed(int32 Seed) { Random.Initialize(Seed); }

    /** The next tip text for Context; empty when no tip fits. */
    FString NextTip(FName Context);

    /** Empty or duplicate IDs, empty text, a tip with no context, an unknown context. */
    TArray<FString> Validate() const;

    const FPSLoadingTipCatalog& GetCatalog() const { return Catalog; }

    static const FName AnyContext;

private:
    UPROPERTY(Transient)
    FPSLoadingTipCatalog Catalog;

    bool bLoaded = false;
    FRandomStream Random;
    TMap<FName, TArray<int32>> Bags;
};
