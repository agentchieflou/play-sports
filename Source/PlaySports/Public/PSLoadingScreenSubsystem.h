// PSLoadingScreenSubsystem.h - Epic 101: the loading screen shown while a level loads
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PSLoadingScreenSubsystem.generated.h"

class UPSLoadingTips;

/**
 * UPSLoadingScreenSubsystem owns the tips pipeline for the whole game session. The front end
 * asks it for the next tip before travelling (and shows that tip on its Loading screen); when
 * the engine starts loading the next map, the subsystem puts the same tip on the engine's
 * loading screen (MoviePlayer) for at least MinimumDisplaySeconds. In the editor MoviePlayer
 * is off, so PIE shows only the front end's Loading screen.
 */
UCLASS()
class PLAYSPORTS_API UPSLoadingScreenSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** Picks the tip for the next level load, for the mode being travelled to. */
    FString PrepareTip(FName Context);

    UPSLoadingTips* GetTips();

private:
    void HandlePreLoadMap(const FString& MapName);

    UPROPERTY(Transient)
    UPSLoadingTips* Tips;

    FString PreparedTip;
    FDelegateHandle PreLoadMapHandle;
};
