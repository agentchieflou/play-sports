#include "PSLoadingTips.h"
#include "PSDataIngestion.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"

const FName UPSLoadingTips::AnyContext(TEXT("Any"));

namespace PSLoadingTips
{
    // The contexts a tip may name: "Any" plus the front end's modes (EPSMenuCommand travel).
    static const TArray<FName>& KnownContexts()
    {
        static const TArray<FName> Contexts = { TEXT("Any"), TEXT("PlayNow"), TEXT("Franchise"), TEXT("Practice") };
        return Contexts;
    }
}

UPSLoadingTips::UPSLoadingTips()
{
    Random.Initialize(static_cast<int32>(FDateTime::Now().GetTicks() & 0x7fffffff));
}

FString UPSLoadingTips::GetDefaultTipsPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/loading_tips.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSLoadingTips::LoadFromJson(const FString& JsonFilePath)
{
    bLoaded = true;
    Bags.Reset();
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    if (!Ingestion->LoadLoadingTipsFromJson(JsonFilePath, Catalog))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSLoadingTips: Could not load tips from %s."), *JsonFilePath);
        return false;
    }
    return true;
}

void UPSLoadingTips::EnsureLoaded()
{
    if (!bLoaded)
    {
        LoadFromJson(GetDefaultTipsPath());
    }
}

FString UPSLoadingTips::NextTip(FName Context)
{
    TArray<int32>& Bag = Bags.FindOrAdd(Context);
    if (Bag.Num() == 0)
    {
        for (int32 Index = 0; Index < Catalog.Tips.Num(); ++Index)
        {
            const TArray<FName>& TipContexts = Catalog.Tips[Index].Contexts;
            if (TipContexts.Contains(AnyContext) || TipContexts.Contains(Context))
            {
                Bag.Add(Index);
            }
        }
        // Fisher-Yates, drawn from the end below.
        for (int32 Index = Bag.Num() - 1; Index > 0; --Index)
        {
            Bag.Swap(Index, Random.RandRange(0, Index));
        }
    }

    if (Bag.Num() == 0)
    {
        return FString();
    }
    return Catalog.Tips[Bag.Pop()].Text;
}

TArray<FString> UPSLoadingTips::Validate() const
{
    TArray<FString> Errors;
    TSet<FName> Ids;
    for (int32 Index = 0; Index < Catalog.Tips.Num(); ++Index)
    {
        const FPSLoadingTipDef& Tip = Catalog.Tips[Index];
        const FString Label = Tip.TipId.IsNone() ? FString::Printf(TEXT("Tips[%d]"), Index) : FString::Printf(TEXT("Tip '%s'"), *Tip.TipId.ToString());
        if (Tip.TipId.IsNone() || Ids.Contains(Tip.TipId))
        {
            Errors.Add(FString::Printf(TEXT("%s: empty or duplicate TipId"), *Label));
        }
        Ids.Add(Tip.TipId);
        if (Tip.Text.TrimStartAndEnd().IsEmpty())
        {
            Errors.Add(FString::Printf(TEXT("%s: empty Text"), *Label));
        }
        if (Tip.Contexts.Num() == 0)
        {
            Errors.Add(FString::Printf(TEXT("%s: names no context"), *Label));
        }
        for (const FName& Context : Tip.Contexts)
        {
            if (!PSLoadingTips::KnownContexts().Contains(Context))
            {
                Errors.Add(FString::Printf(TEXT("%s: unknown context '%s'"), *Label, *Context.ToString()));
            }
        }
    }
    if (Catalog.MinimumDisplaySeconds < 0.f)
    {
        Errors.Add(TEXT("MinimumDisplaySeconds must not be negative"));
    }
    return Errors;
}
