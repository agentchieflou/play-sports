#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.h"
#include "PSTelemetryBus.h"
#include "PSGameMode.generated.h"

class UPSPlaySimulation;
class APSBroadcastCamera;
class APSPlayerPawn;

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

    /** Cached BroadcastCamera -- found via GetActorOfClass in StartPlay; SetTargetActor
     *  called from OnBusCatchEvent to fix the orphan (Epic C3). */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Gameplay")
    APSBroadcastCamera* BroadcastCamera;

    /** Populated once in StartPlay() so hot paths (PairLinemen, FindPlayerPawnByRole,
     *  GetLargestRunLaneGap, ResetPawnPositions) don't each re-run GetAllActorsOfClass
     *  (Epic C3). Pawns are only ever spawned once at StartPlay, so this stays valid
     *  for the lifetime of the play. */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Gameplay")
    TArray<APSPlayerPawn*> CachedPawns;

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

private:
    UFUNCTION()
    void OnBusScoreEvent(const FPSTelemetryScoreEvent& Event);

    UFUNCTION()
    void OnBusCatchEvent(const FPSTelemetryCatchEvent& Event);
};

