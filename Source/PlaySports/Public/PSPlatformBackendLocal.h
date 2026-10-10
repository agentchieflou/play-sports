// PSPlatformBackendLocal.h - Epic 152: the null/local platform implementation (Win64, tests)
#pragma once

#include "CoreMinimal.h"
#include "PSPlatformBackend.h"
#include "PSPlatformBackendLocal.generated.h"

/**
 * UPSPlatformBackendLocal is the null/local implementation of the platform services (Epic 152):
 * what Win64 and every headless test run on.
 *
 *  - Users: no sign-in on a PC, so every local player slot has a user: "Local<index>", shown as
 *    "Player <index + 1>" (Data/ui_text.csv Platform.LocalUser).
 *  - Storage: Saved/SaveGames under the project, or StorageRoot when set (a test standing in for
 *    a second platform).
 *  - Achievements and presence: remembered in memory and logged; nothing leaves the machine.
 *  - Lifecycle: a PC does not suspend its games, so nothing calls HandleLifecycle but the
 *    PS.Platform.Lifecycle console command and tests.
 */
UCLASS()
class PLAYSPORTS_API UPSPlatformBackendLocal : public UPSPlatformBackend
{
    GENERATED_BODY()

public:
    virtual FName GetBackendName() const override;
    virtual FPSPlatformUser GetUser(int32 LocalUserIndex) const override;
    virtual FString GetSaveStorageRoot() const override;
    virtual bool UnlockAchievement(FName AchievementId) override;
    virtual bool IsAchievementUnlocked(FName AchievementId) const override;
    virtual void SetPresence(FName PresenceId) override;
    virtual FName GetPresence() const override;

    /** Where saves go instead of Saved/SaveGames, when not empty. */
    UPROPERTY(EditAnywhere, Category = "Platform")
    FString StorageRoot;

    /** How many local player slots have a user (two for local head-to-head, Epic 107). */
    UPROPERTY(EditAnywhere, Category = "Platform")
    int32 MaxLocalUsers = 4;

private:
    TSet<FName> UnlockedAchievements;
    FName Presence;
};
