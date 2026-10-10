// PSPlatformServices.h - Epic 152: one interface to the platform for the whole game
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPath.h"
#include "PSPlatformTypes.h"
#include "PSTelemetryBus.h"
#include "PSPlatformServices.generated.h"

class UPSPlatformBackend;
class UWorld;

/** A lifecycle event, for listeners that live as long as the game instance (UPSSaveSubsystem
 *  flushes its writes on a suspend). Gameplay listens on the bus instead. */
DECLARE_MULTICAST_DELEGATE_OneParam(FPSPlatformLifecycleMC, EPSPlatformLifecycle);

/** Someone signed in or out on the platform (UPSControllerPairingSubsystem checks its pairings). */
DECLARE_MULTICAST_DELEGATE(FPSPlatformUsersChangedMC);

/**
 * UPSPlatformServices is the game's one door to the platform (Epic 152): who is signed in, where
 * saves go, achievements and presence, and what the platform does to the running game (suspend,
 * resume, constrain). Gameplay code asks this subsystem and never branches on the platform: each
 * platform is a UPSPlatformBackend behind it, chosen by config, not by #if.
 *
 *  - The implementation is BackendClass, from the [/Script/PlaySports.PSPlatformServices]
 *    section of Config/DefaultGame.ini; a platform's own Game.ini (Config/IOS/IOSGame.ini, the
 *    Xbox platform's later) names its own. Win64 and every test use UPSPlatformBackendLocal, the
 *    null/local implementation.
 *  - Saves: UPSSaveSubsystem keeps its slots under GetSaveStorageRoot(). The format is the same
 *    everywhere, so a save written on one platform loads on another.
 *  - Lifecycle: a backend calls HandleLifecycle. The event is published on the current world's
 *    bus (Lifecycle), where UPSAutoPauseSubsystem pauses a live game, and on OnLifecycleMC,
 *    where UPSSaveSubsystem finishes every write in flight on a suspend or a constrain.
 *    Repeats (a second suspend while suspended) are dropped. On a PC nothing suspends; the
 *    console command PS.Platform.Lifecycle <Suspend|Resume|Constrained|Unconstrained> plays one.
 *  - Achievements and presence are stubs in the null implementation: it remembers them and logs.
 *
 * Headless tests have no game instance to initialize it: they create one with NewObject, hand
 * it a backend (UseBackend) and the world whose bus gets its events (SetEventWorld).
 */
UCLASS(Config = Game)
class PLAYSPORTS_API UPSPlatformServices : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** The running game's platform services, or null (headless tests, or no game instance). */
    static UPSPlatformServices* Get(const UObject* WorldContext);

    /** The implementation BackendClass names, or the null/local one when it names nothing
     *  loadable. */
    static TSubclassOf<UPSPlatformBackend> ResolveBackendClass();

    /** Serves through InBackend from now on, shutting the previous one down. Initialize calls it
     *  with a new ResolveBackendClass(); tests call it with their own. */
    void UseBackend(UPSPlatformBackend* InBackend);

    UPSPlatformBackend* GetBackend() const { return Backend; }

    /** The implementation's name ("Local"); None without one. */
    UFUNCTION(BlueprintPure, Category = "Platform")
    FName GetBackendName() const;

    // --- Users ----------------------------------------------------------------------------

    /** The user in local player slot LocalUserIndex (0 for the first); not signed in when the
     *  slot has none. */
    UFUNCTION(BlueprintCallable, Category = "Platform")
    FPSPlatformUser GetSignedInUser(int32 LocalUserIndex = 0) const;

    /** A backend reports that a user signed in or out: broadcasts OnUsersChangedMC. */
    void HandleUsersChanged();

    /** Sign-in changes, as the backend reports them (Epic 150: XR-112 and XR-115 on Xbox). */
    FPSPlatformUsersChangedMC OnUsersChangedMC;

    // --- Storage --------------------------------------------------------------------------

    /** The folder save slots are kept in, as the implementation says. */
    UFUNCTION(BlueprintPure, Category = "Platform")
    FString GetSaveStorageRoot() const;

    /** The same for a game without platform services (headless tests): the configured
     *  implementation's default answer. */
    static FString GetDefaultSaveStorageRoot();

    // --- Achievements and presence ----------------------------------------------------------

    /** Unlocks AchievementId; false for None, when already unlocked, or when the platform
     *  refuses it. */
    UFUNCTION(BlueprintCallable, Category = "Platform")
    bool UnlockAchievement(FName AchievementId);

    UFUNCTION(BlueprintPure, Category = "Platform")
    bool IsAchievementUnlocked(FName AchievementId) const;

    /** Tells the platform what the player is doing ("InMenus", "PlayingGame"). */
    UFUNCTION(BlueprintCallable, Category = "Platform")
    void SetPresence(FName PresenceId);

    UFUNCTION(BlueprintPure, Category = "Platform")
    FName GetPresence() const;

    // --- Lifecycle --------------------------------------------------------------------------

    /** The platform suspended, resumed or constrained the game: updates IsSuspended and
     *  IsConstrained, publishes on the bus, then broadcasts OnLifecycleMC. False (and nothing
     *  published) when it changes nothing. */
    bool HandleLifecycle(EPSPlatformLifecycle Lifecycle);

    UFUNCTION(BlueprintPure, Category = "Platform")
    bool IsSuspended() const { return bSuspended; }

    UFUNCTION(BlueprintPure, Category = "Platform")
    bool IsConstrained() const { return bConstrained; }

    /** Publishes lifecycle events on InWorld's bus instead of the game instance's world
     *  (headless tests, which have no game instance world). */
    void SetEventWorld(UWorld* InWorld) { EventWorld = InWorld; }

    /** Lifecycle events, after the bus has had them. */
    FPSPlatformLifecycleMC OnLifecycleMC;

    /** The implementation to use (a UPSPlatformBackend subclass), from config. */
    UPROPERTY(Config)
    FSoftClassPath BackendClass;

private:
    UWorld* GetEventWorld() const;

    UPROPERTY(Transient)
    UPSPlatformBackend* Backend = nullptr;

    TWeakObjectPtr<UWorld> EventWorld;
    bool bSuspended = false;
    bool bConstrained = false;
};
