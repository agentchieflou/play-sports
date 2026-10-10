#include "PSCrashContext.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"

namespace PSCrashContextPrivate
{
    /** The engine keeps its game-data lookup protected; a derived type may call it. Used only to
     *  read back what a crash report would carry (tests, diagnostics). */
    struct FGameDataReader : public FGenericCrashContext
    {
        static const FString* Find(const FString& Key)
        {
            return FGenericCrashContext::GetGameData(Key);
        }
    };
}

void FPSCrashContext::SetSession(const FGuid& SessionId, const FString& Mode, const FString& PlatformTier, int32 PlayCount, float SessionSeconds)
{
    FGenericCrashContext::SetGameData(FString(SessionIdKey), SessionId.ToString(EGuidFormats::DigitsWithHyphens));
    FGenericCrashContext::SetGameData(FString(ModeKey), Mode);
    FGenericCrashContext::SetGameData(FString(PlatformTierKey), PlatformTier);
    FGenericCrashContext::SetGameData(FString(PlaysKey), FString::FromInt(PlayCount));
    FGenericCrashContext::SetGameData(FString(SessionSecondsKey), FString::Printf(TEXT("%.1f"), SessionSeconds));
}

void FPSCrashContext::SetBreadcrumbs(const TArray<FPSTelemetryEvent>& History, int32 Count)
{
    // An empty value removes the key, so no events means no breadcrumbs section entry.
    FGenericCrashContext::SetGameData(FString(RecentEventsKey), FormatBreadcrumbs(History, Count));
}

FString FPSCrashContext::FormatBreadcrumbs(const TArray<FPSTelemetryEvent>& History, int32 Count)
{
    if (Count <= 0 || History.Num() == 0)
    {
        return FString();
    }

    TArray<FString> Lines;
    for (int32 Index = FMath::Max(0, History.Num() - Count); Index < History.Num(); ++Index)
    {
        const FPSTelemetryEvent& Event = History[Index];
        Lines.Add(FString::Printf(TEXT("[%.1fs] %s"), Event.Timestamp, *Event.Description));
    }
    return FString::Join(Lines, TEXT("\n"));
}

void FPSCrashContext::Clear()
{
    for (const TCHAR* Key : { ModeKey, PlaysKey, SessionSecondsKey, SessionIdKey, PlatformTierKey, RecentEventsKey })
    {
        FGenericCrashContext::SetGameData(FString(Key), FString());
    }
}

FString FPSCrashContext::GetValue(const TCHAR* Key)
{
    const FString* Value = PSCrashContextPrivate::FGameDataReader::Find(FString(Key));
    return Value ? *Value : FString();
}
