// PSAutoPauseSubsystem.h - Epic 152: a live game pauses when the platform interrupts it
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSTelemetryBus.h"
#include "PSAutoPauseSubsystem.generated.h"

/**
 * UPSAutoPauseSubsystem pauses a live game when something outside it interrupts the player:
 * the platform suspends the game or constrains it (the bus's Lifecycle event, from
 * UPSPlatformServices, Epic 152). Every human's menu gets the interruption
 * (UPSMenuComponent::PauseForInterruption): the pause screen opens over whatever is up, the front
 * end stays as it is, and a local head-to-head game pauses by its own etiquette
 * (UPSVersusSubsystem). A resume leaves the game paused until the player resumes it, so a
 * console's Quick Resume needs no gameplay code of its own.
 *
 * It listens only to the bus (Architecture rule 5); headless tests publish there, or call
 * HandleLifecycle.
 */
UCLASS()
class PLAYSPORTS_API UPSAutoPauseSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** Pauses every human's live game on a suspend or a constrain; nothing on the others.
     *  Returns how many menus paused. */
    int32 HandleLifecycle(const FPSTelemetryLifecycleEvent& Event);

    /** Pauses every human's live game, for Reason (logged). Returns how many menus paused. */
    int32 PauseLiveGame(const FString& Reason);

private:
    void OnLifecycle(const FPSTelemetryLifecycleEvent& Event);

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
};
