#include "PSLoadingScreenSubsystem.h"
#include "PSLoadingTips.h"
#include "PSLocalization.h"
#include "MoviePlayer.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

void UPSLoadingScreenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UPSLoadingScreenSubsystem::HandlePreLoadMap);
}

void UPSLoadingScreenSubsystem::Deinitialize()
{
    FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
    Super::Deinitialize();
}

UPSLoadingTips* UPSLoadingScreenSubsystem::GetTips()
{
    if (!Tips)
    {
        Tips = NewObject<UPSLoadingTips>(this);
        Tips->EnsureLoaded();
    }
    return Tips;
}

FString UPSLoadingScreenSubsystem::PrepareTip(FName Context)
{
    PreparedTip = GetTips()->NextTip(Context);
    return PreparedTip;
}

void UPSLoadingScreenSubsystem::HandlePreLoadMap(const FString& MapName)
{
    // MoviePlayer only runs in standalone and packaged games.
    if (GIsEditor || IsRunningDedicatedServer() || !IsMoviePlayerEnabled() || !GetMoviePlayer())
    {
        return;
    }

    const FString Tip = PreparedTip.IsEmpty() ? GetTips()->NextTip(UPSLoadingTips::AnyContext) : PreparedTip;
    PreparedTip.Reset();

    FLoadingScreenAttributes Attributes;
    Attributes.bAutoCompleteWhenLoadingCompletes = true;
    Attributes.MinimumLoadingScreenDisplayTime = GetTips()->GetCatalog().MinimumDisplaySeconds;
    Attributes.WidgetLoadingScreen = SNew(SBorder)
        .BorderBackgroundColor(FLinearColor::Black)
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(UPSLocalization::FromLocalized(Tip))
        ];
    GetMoviePlayer()->SetupLoadingScreen(Attributes);
}
