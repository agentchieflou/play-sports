#include "PSAutoPauseSubsystem.h"
#include "PSMenuComponent.h"
#include "PSPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void UPSAutoPauseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>();
    if (Bus)
    {
        Bus->OnLifecycleMC.AddUObject(this, &UPSAutoPauseSubsystem::OnLifecycle);
        BoundBus = Bus;
    }
}

void UPSAutoPauseSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnLifecycleMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

void UPSAutoPauseSubsystem::OnLifecycle(const FPSTelemetryLifecycleEvent& Event)
{
    HandleLifecycle(Event);
}

int32 UPSAutoPauseSubsystem::HandleLifecycle(const FPSTelemetryLifecycleEvent& Event)
{
    // A resume or an unconstrain leaves the pause up: the player resumes when ready.
    if (Event.Lifecycle != EPSPlatformLifecycle::Suspend && Event.Lifecycle != EPSPlatformLifecycle::Constrained)
    {
        return 0;
    }
    return PauseLiveGame(UEnum::GetValueAsString(Event.Lifecycle));
}

int32 UPSAutoPauseSubsystem::PauseLiveGame(const FString& Reason)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return 0;
    }
    int32 Paused = 0;
    for (TActorIterator<APSPlayerController> It(World); It; ++It)
    {
        UPSMenuComponent* Menu = It->GetMenuComponent();
        if (Menu && Menu->PauseForInterruption())
        {
            ++Paused;
        }
    }
    if (Paused > 0)
    {
        UE_LOG(LogTemp, Display, TEXT("UPSAutoPauseSubsystem: Paused the game (%s)."), *Reason);
    }
    return Paused;
}
