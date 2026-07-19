#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "PSSaveGame.generated.h"

UENUM(BlueprintType)
enum class EPSSaveCategory : uint8
{
    Profile,
    Franchise,
    Replay
};

UCLASS(Blueprintable)
class PLAYSPORTS_API UPSSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    static const int32 CurrentSaveVersion = 1;

    UPROPERTY(BlueprintReadOnly, Category = "Save")
    int32 SaveVersion = CurrentSaveVersion;

    UPROPERTY(BlueprintReadWrite, Category = "Save")
    EPSSaveCategory Category = EPSSaveCategory::Profile;

    UPROPERTY(BlueprintReadOnly, Category = "Save")
    FDateTime SavedAtUtc;

    // Migrate this object one step forward from FromVersion. Called repeatedly by the
    // subsystem until SaveVersion reaches CurrentSaveVersion. Subclasses override per
    // persisted-schema change; return false if the step cannot be migrated.
    virtual bool MigrateFrom(int32 FromVersion)
    {
        return true;
    }
};
