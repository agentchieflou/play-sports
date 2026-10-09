// PSMenuGameMode.h - Epic 101: the front end's game mode (main menu, mode select)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PSMenuGameMode.generated.h"

/**
 * APSMenuGameMode runs the front end on any map: no roster, no pawn, and every player
 * controller opens the menu catalog's root screen. It is selected with the "Menu" game mode
 * alias (Config/DefaultEngine.ini): packaged builds boot into it through LocalMapOptions,
 * and "Quit to Main Menu" travels back with ?game=Menu. Choosing a mode travels to the
 * default map without a game option, so the map's own game mode runs the match.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSMenuGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    APSMenuGameMode();

    virtual void PostLogin(APlayerController* NewPlayer) override;
};
