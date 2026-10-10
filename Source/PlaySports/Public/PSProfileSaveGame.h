// PSProfileSaveGame.h - Epic 102/103: the player's profile save (favourite plays, settings)
#pragma once

#include "CoreMinimal.h"
#include "PSSaveGame.h"
#include "PSInputConfigTypes.h"
#include "PSOpponentModelTypes.h"
#include "PSProfileSaveGame.generated.h"

/**
 * UPSProfileSaveGame is the player's own data across games, saved through UPSSaveSubsystem
 * in the Profile category: favourite plays (Epic 102.3), settings (Epic 103) and the play-calling
 * the CPU has seen (Epic 78). Load the existing object, change your field and save it back, so one
 * feature never clobbers another's.
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

    /** The first-time hints the player has been shown (UPSUIHintSubsystem, Epic 105.4). */
    UPROPERTY(BlueprintReadWrite, Category = "Profile")
    TArray<FName> SeenHints;

    /** How the player calls plays, every game so far (UPSOpponentModel, Epic 78): what the
     *  CPU has seen and adapts to across games. */
    UPROPERTY(BlueprintReadWrite, Category = "Profile")
    TArray<FPSTendencyCell> OpponentTendencies;
};
