#include "PSMenuGameMode.h"
#include "PSMenuComponent.h"
#include "PSPlayerController.h"

APSMenuGameMode::APSMenuGameMode()
{
    PlayerControllerClass = APSPlayerController::StaticClass();
    DefaultPawnClass = nullptr;
}

void APSMenuGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    if (APSPlayerController* Player = Cast<APSPlayerController>(NewPlayer))
    {
        // Nothing to take control of in the front end.
        Player->bTakeDefaultControlOnBeginPlay = false;
        if (UPSMenuComponent* Menu = Player->GetMenuComponent())
        {
            Menu->RequestRootScreenOnBeginPlay();
        }
    }
}
