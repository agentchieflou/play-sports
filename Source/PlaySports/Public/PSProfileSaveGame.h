// PSProfileSaveGame.h - Epic 102/103: the player's profile save (favourite plays, settings)
#pragma once

#include "CoreMinimal.h"
#include "PSSaveGame.h"
#include "PSInputConfigTypes.h"
#include "PSProfileSaveGame.generated.h"

/**
 * UPSProfileSaveGame is the player's own data across games, saved through UPSSaveSubsystem
 * in the Profile category: favourite plays (Epic 102.3) and settings (Epic 103). Load the
 * existing object, change your field and save it back, so one feature never clobbers another's.
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

    /** The player's settings by ID (UPSSettingsSubsystem, Epic 103); a setting missing here is
     *  at its default. */
    UPROPERTY(BlueprintReadWrite, Category = "Profile")
    TMap<FName, float> Settings;

    /** The player's own keys over the input catalog's (Epic 103.4). */
    UPROPERTY(BlueprintReadWrite, Category = "Profile")
    TArray<FPSInputRemap> InputRemaps;
};
