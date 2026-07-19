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

    UPROPERTY(BlueprintReadOnly, Category = "Score")
    int32 HomeScore;

    UPROPERTY(BlueprintReadOnly, Category = "Score")
    int32 AwayScore;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Movement")
    UDataTable* MovementTuningTable;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Movement")
    FString MovementTuningJsonPath;

    UPROPERTY(BlueprintReadOnly, Category = "Movement")
    FMovementTuningRow MovementTuningSettings;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Gameplay")
    class APSBall* ActiveBall;

    UFUNCTION(BlueprintCallable, Category = "Gameplay")
    void ExecuteSnap();

    UFUNCTION(BlueprintCallable, Category = "Gameplay")
    void PairLinemen();

    UFUNCTION(BlueprintCallable, Category = "Gameplay|Blocking")
    FVector GetLargestRunLaneGap() const;

    UFUNCTION(BlueprintCallable, Category = "Gameplay")
    void ResetPawnPositions();

    UFUNCTION(BlueprintCallable, Category = "Gameplay")
    class APSPlayerPawn* FindPlayerPawnByRole(EPlayerRole PlayerRole) const;
};
