#include "PSPlatformServices.h"
#include "PSPlatformBackend.h"
#include "PSPlatformBackendLocal.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace PSPlatformServicesPrivate
{
    /** PS.Platform.Lifecycle <Suspend|Resume|Constrained|Unconstrained>: plays a platform
     *  lifecycle event on a PC, where nothing else does. */
    void RunLifecycleCommand(const TArray<FString>& Args, UWorld* World)
    {
        UPSPlatformServices* Services = UPSPlatformServices::Get(World);
        const UEnum* LifecycleEnum = StaticEnum<EPSPlatformLifecycle>();
        const int64 Value = Args.Num() > 0 ? LifecycleEnum->GetValueByNameString(Args[0]) : INDEX_NONE;
        if (!Services || Value == INDEX_NONE)
        {
            UE_LOG(LogTemp, Warning, TEXT("PS.Platform.Lifecycle: give Suspend, Resume, Constrained or Unconstrained, in a running game."));
            return;
        }
        Services->HandleLifecycle(static_cast<EPSPlatformLifecycle>(Value));
    }

    FAutoConsoleCommandWithWorldAndArgs LifecycleCommand(
        TEXT("PS.Platform.Lifecycle"),
        TEXT("Plays a platform lifecycle event: Suspend, Resume, Constrained or Unconstrained (Epic 152)."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunLifecycleCommand));
}

void UPSPlatformServices::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UseBackend(NewObject<UPSPlatformBackend>(this, ResolveBackendClass()));
    UE_LOG(LogTemp, Display, TEXT("UPSPlatformServices: Serving through the %s platform implementation."), *GetBackendName().ToString());
}

void UPSPlatformServices::Deinitialize()
{
    UseBackend(nullptr);
    OnLifecycleMC.Clear();
    Super::Deinitialize();
}

UPSPlatformServices* UPSPlatformServices::Get(const UObject* WorldContext)
{
    const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    return GameInstance ? GameInstance->GetSubsystem<UPSPlatformServices>() : nullptr;
}

TSubclassOf<UPSPlatformBackend> UPSPlatformServices::ResolveBackendClass()
{
    const FSoftClassPath& Configured = GetDefault<UPSPlatformServices>()->BackendClass;
    if (Configured.IsValid())
    {
        UClass* Loaded = Configured.TryLoadClass<UPSPlatformBackend>();
        if (Loaded && Loaded->IsChildOf(UPSPlatformBackend::StaticClass()) && !Loaded->HasAnyClassFlags(CLASS_Abstract))
        {
            return Loaded;
        }
        UE_LOG(LogTemp, Warning, TEXT("UPSPlatformServices: BackendClass %s is not a platform implementation; using the local one."), *Configured.ToString());
    }
    return UPSPlatformBackendLocal::StaticClass();
}

void UPSPlatformServices::UseBackend(UPSPlatformBackend* InBackend)
{
    if (Backend == InBackend)
    {
        return;
    }
    if (Backend)
    {
        Backend->Shutdown();
    }
    Backend = InBackend;
    if (Backend)
    {
        Backend->Startup(this);
    }
}

FName UPSPlatformServices::GetBackendName() const
{
    return Backend ? Backend->GetBackendName() : NAME_None;
}

FPSPlatformUser UPSPlatformServices::GetSignedInUser(int32 LocalUserIndex) const
{
    if (!Backend)
    {
        FPSPlatformUser NoUser;
        NoUser.LocalUserIndex = LocalUserIndex;
        return NoUser;
    }
    return Backend->GetUser(LocalUserIndex);
}

FString UPSPlatformServices::GetSaveStorageRoot() const
{
    return Backend ? Backend->GetSaveStorageRoot() : GetDefaultSaveStorageRoot();
}

FString UPSPlatformServices::GetDefaultSaveStorageRoot()
{
    const TSubclassOf<UPSPlatformBackend> BackendType = ResolveBackendClass();
    return GetDefault<UPSPlatformBackend>(BackendType)->GetSaveStorageRoot();
}

bool UPSPlatformServices::UnlockAchievement(FName AchievementId)
{
    return Backend && !AchievementId.IsNone() && Backend->UnlockAchievement(AchievementId);
}

bool UPSPlatformServices::IsAchievementUnlocked(FName AchievementId) const
{
    return Backend && Backend->IsAchievementUnlocked(AchievementId);
}

void UPSPlatformServices::SetPresence(FName PresenceId)
{
    if (Backend)
    {
        Backend->SetPresence(PresenceId);
    }
}

FName UPSPlatformServices::GetPresence() const
{
    return Backend ? Backend->GetPresence() : NAME_None;
}

UWorld* UPSPlatformServices::GetEventWorld() const
{
    if (UWorld* Override = EventWorld.Get())
    {
        return Override;
    }
    const UGameInstance* GameInstance = GetGameInstance();
    return GameInstance ? GameInstance->GetWorld() : nullptr;
}

bool UPSPlatformServices::HandleLifecycle(EPSPlatformLifecycle Lifecycle)
{
    bool bChanged = false;
    switch (Lifecycle)
    {
    case EPSPlatformLifecycle::Suspend:
        bChanged = !bSuspended;
        bSuspended = true;
        break;
    case EPSPlatformLifecycle::Resume:
        bChanged = bSuspended;
        bSuspended = false;
        break;
    case EPSPlatformLifecycle::Constrained:
        bChanged = !bConstrained;
        bConstrained = true;
        break;
    case EPSPlatformLifecycle::Unconstrained:
        bChanged = bConstrained;
        bConstrained = false;
        break;
    }
    if (!bChanged)
    {
        return false;
    }

    UE_LOG(LogTemp, Display, TEXT("UPSPlatformServices: %s."), *UEnum::GetValueAsString(Lifecycle));

    // The bus first, so a live game is paused before anything is written.
    UWorld* World = GetEventWorld();
    if (UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr)
    {
        FPSTelemetryLifecycleEvent Event;
        Event.Lifecycle = Lifecycle;
        Event.bSuspended = bSuspended;
        Event.bConstrained = bConstrained;
        Event.Backend = GetBackendName();
        Bus->PublishLifecycle(Event);
    }
    OnLifecycleMC.Broadcast(Lifecycle);
    return true;
}
