#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.h"
#include "PSGameMode.generated.h"

class UPSPlaySimulation;

/**
 * GameMode subclass for PlaySports which orchestrates play simulation and roster loading.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    APSGameMode();

    virtual void StartPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Roster")
    UDataTable* PlayerRosterTable;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Roster")
    FString RosterJsonPath;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Simulation")
    UPSPlaySimulation* PlaySimulation;
};
