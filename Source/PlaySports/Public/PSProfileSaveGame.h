// PSProfileSaveGame.h - Epic 102: the player's profile save (favourite plays so far)
#pragma once

#include "CoreMinimal.h"
#include "PSSaveGame.h"
#include "PSProfileSaveGame.generated.h"

/**
 * UPSProfileSaveGame is the player's own data across games, saved through UPSSaveSubsystem
 * in the Profile category. It starts with favourite plays (Epic 102.3); Epic 103's settings
 * are the obvious next tenant. Load the existing object, change your field and save it back,
 * so one feature never clobbers another's.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSProfileSaveGame : public UPSSaveGame
{
    GENERATED_BODY()

public:
    UPSProfileSaveGame()
    {
        Category = EPSSaveCategory::Profile;
    }

    /** The slot every profile lives in (one local profile for now). */
    static FString GetDefaultSlotName();

    /** Plays the player starred on the play-call screens. */
    UPROPERTY(BlueprintReadWrite, Category = "Profile")
    TArray<FName> FavoritePlays;
};
