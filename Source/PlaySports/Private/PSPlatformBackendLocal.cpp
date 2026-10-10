#include "PSPlatformBackendLocal.h"
#include "PSLocalization.h"
#include "Misc/Paths.h"

FName UPSPlatformBackendLocal::GetBackendName() const
{
    return TEXT("Local");
}

FPSPlatformUser UPSPlatformBackendLocal::GetUser(int32 LocalUserIndex) const
{
    FPSPlatformUser User;
    User.LocalUserIndex = LocalUserIndex;
    if (LocalUserIndex < 0 || LocalUserIndex >= MaxLocalUsers)
    {
        return User;
    }
    User.UserId = FString::Printf(TEXT("Local%d"), LocalUserIndex);
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Number"), LocalUserIndex + 1);
    User.DisplayName = UPSLocalization::Format(TEXT("Platform.LocalUser"), Arguments);
    User.bSignedIn = true;
    return User;
}

FString UPSPlatformBackendLocal::GetSaveStorageRoot() const
{
    return StorageRoot.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("SaveGames") : StorageRoot;
}

bool UPSPlatformBackendLocal::UnlockAchievement(FName AchievementId)
{
    if (AchievementId.IsNone() || UnlockedAchievements.Contains(AchievementId))
    {
        return false;
    }
    UnlockedAchievements.Add(AchievementId);
    UE_LOG(LogTemp, Display, TEXT("UPSPlatformBackendLocal: Achievement %s unlocked (kept on this machine only)."), *AchievementId.ToString());
    return true;
}

bool UPSPlatformBackendLocal::IsAchievementUnlocked(FName AchievementId) const
{
    return UnlockedAchievements.Contains(AchievementId);
}

void UPSPlatformBackendLocal::SetPresence(FName PresenceId)
{
    if (Presence != PresenceId)
    {
        Presence = PresenceId;
        UE_LOG(LogTemp, Display, TEXT("UPSPlatformBackendLocal: Presence is now %s."), *PresenceId.ToString());
    }
}

FName UPSPlatformBackendLocal::GetPresence() const
{
    return Presence;
}
