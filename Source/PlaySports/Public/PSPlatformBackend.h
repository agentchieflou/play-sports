// PSPlatformBackend.h - Epic 152: what one platform implementation of the platform services does
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSPlatformTypes.h"
#include "PSPlatformBackend.generated.h"

class UPSPlatformServices;

/**
 * UPSPlatformBackend is one platform's implementation behind UPSPlatformServices (Epic 152): the
 * null/local one (UPSPlatformBackendLocal, Win64 and every test) today, the iOS (Epic 149) and
 * Xbox (Epic 151) ones later. UPSPlatformServices creates the one its config names
 * (BackendClass) and is the only code that talks to it: gameplay never references a backend,
 * which tools/lint_conventions.py enforces. A platform difference lives here or in data, never
 * as an #if PLATFORM_* in gameplay code.
 *
 * A backend reports what the platform does to the game (suspend, resume, constrain) by calling
 * UPSPlatformServices::HandleLifecycle on the services it was started with.
 */
UCLASS(Abstract)
class PLAYSPORTS_API UPSPlatformBackend : public UObject
{
    GENERATED_BODY()

public:
    /** Starts serving InServices: hooks the platform's own callbacks (sign-in, lifecycle). */
    virtual void Startup(UPSPlatformServices* InServices);

    /** Stops serving: unhooks what Startup hooked. */
    virtual void Shutdown();

    /** A short name for logs and bus events ("Local", "IOS", "GDK"). */
    virtual FName GetBackendName() const PURE_VIRTUAL(UPSPlatformBackend::GetBackendName, return NAME_None;);

    /** The user playing in local player slot LocalUserIndex (0 for the first); not signed in
     *  when the slot has no user. */
    virtual FPSPlatformUser GetUser(int32 LocalUserIndex) const PURE_VIRTUAL(UPSPlatformBackend::GetUser, return FPSPlatformUser(););

    /** The folder save slots are kept in. The save format is the same on every platform; only
     *  where the files go differs. Must be safe to call on the class default object, which
     *  UPSPlatformServices::GetDefaultSaveStorageRoot does. */
    virtual FString GetSaveStorageRoot() const PURE_VIRTUAL(UPSPlatformBackend::GetSaveStorageRoot, return FString(););

    /** Unlocks AchievementId for the primary user; false when it was already unlocked or the
     *  platform refused it. */
    virtual bool UnlockAchievement(FName AchievementId) PURE_VIRTUAL(UPSPlatformBackend::UnlockAchievement, return false;);

    virtual bool IsAchievementUnlocked(FName AchievementId) const PURE_VIRTUAL(UPSPlatformBackend::IsAchievementUnlocked, return false;);

    /** Tells the platform what the player is doing (rich presence: "InMenus", "PlayingGame"). */
    virtual void SetPresence(FName PresenceId) PURE_VIRTUAL(UPSPlatformBackend::SetPresence, );

    virtual FName GetPresence() const PURE_VIRTUAL(UPSPlatformBackend::GetPresence, return NAME_None;);

protected:
    /** The services this backend serves, between Startup and Shutdown. */
    UPSPlatformServices* GetServices() const { return Services.Get(); }

private:
    TWeakObjectPtr<UPSPlatformServices> Services;
};
