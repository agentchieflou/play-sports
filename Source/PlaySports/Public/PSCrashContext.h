// PSCrashContext.h - Epic 117: what a crash report says about the game
#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "PSTelemetryBus.h"

/**
 * The game's half of a crash report. The engine's crash handler writes the crash context's game
 * data into the report (the GameData section of CrashContext.runtime-xml); this class keeps the
 * PS.* keys there current: the session's mode, plays and play time, and the telemetry bus's most
 * recent events as breadcrumbs. tools/crash_report.py copies them into the GitHub issue.
 *
 * Nothing here sends anything. Reports stay on the machine unless a person routes them
 * (Specs/Privacy_Telemetry.md). UPSSessionTelemetrySubsystem is the only writer.
 */
class PLAYSPORTS_API FPSCrashContext
{
public:
    static constexpr const TCHAR* ModeKey = TEXT("PS.Mode");
    static constexpr const TCHAR* PlaysKey = TEXT("PS.Plays");
    static constexpr const TCHAR* SessionSecondsKey = TEXT("PS.SessionSeconds");
    static constexpr const TCHAR* SessionIdKey = TEXT("PS.SessionId");
    static constexpr const TCHAR* PlatformTierKey = TEXT("PS.PlatformTier");
    static constexpr const TCHAR* RecentEventsKey = TEXT("PS.RecentEvents");

    /** Sets the session keys. */
    static void SetSession(const FGuid& SessionId, const FString& Mode, const FString& PlatformTier, int32 PlayCount, float SessionSeconds);

    /** Sets the breadcrumbs to the last Count events of History (oldest first). */
    static void SetBreadcrumbs(const TArray<FPSTelemetryEvent>& History, int32 Count);

    /** The last Count events of History, oldest first, one per line: "[12.5s] Snap: ...".
     *  Empty for no events or Count <= 0. */
    static FString FormatBreadcrumbs(const TArray<FPSTelemetryEvent>& History, int32 Count);

    /** Removes every PS.* key, so a crash after the session says nothing stale about it. */
    static void Clear();

    /** The value a crash report would carry for Key now; empty when unset. */
    static FString GetValue(const TCHAR* Key);
};
