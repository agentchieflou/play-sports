#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.h"
#include "PSTelemetryBus.h"
#include "PSArchetypeTuning.h"
#include "PSLevelingTuning.h"
#include "PSGameMode.generated.h"

class UPSPlaySimulation;
class APSBroadcastCamera;
class APSPlayerPawn;
class UPSRoster;
class UPSPlayerLeveling;

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

    /** Hitpoint/damage tuning per character archetype (Epic 139). Defaults are used
     *  when no DataTable/JSON override is configured. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat")
    FPSArchetypeTuning ArchetypeTuningSettings;

    /** XP curve/award tuning (Epic 141). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat")
    FPSLevelingTuning LevelingTuningSettings;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Gameplay|Combat")
    UPSPlayerLeveling* PlayerLeveling;

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

    /** Authoritative combat/leveling live-state for the active roster (Epic 139/141):
     *  who's downed, sitting out, or leveled up. Initialized from the same roster
     *  split used to build OffenseRoster/DefenseRoster. Full personnel substitution
     *  (benching an unavailable player's pawn) is Epic 19's still-open "personnel
     *  packages tied into formations" work -- this tracks the authoritative state
     *  that substitution will consume once it exists, it does not yet bench pawns. */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Gameplay|Combat")
    UPSRoster* ActiveRoster;

    /** Increments once per play in ResetPawnPositions; the play index a downed ball
     *  carrier's sit-out is measured against (Epic 139). */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Gameplay|Combat")
    int32 CurrentPlayIndex;

    /** The extra defender spawned for a 4th-down defensive overload (Epic 140), or
     *  null when not on 4th down. Despawned once the down changes. */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Gameplay|Combat")
    APSPlayerPawn* ExtraDefenderPawn;

    /** Backing storage for ExtraDefenderPawn's attributes -- must outlive the pawn
     *  (APSPlayerPawn::InitializePlayerPointer stores a raw pointer into this, not a
     *  copy), so it cannot be a stack local in ResetPawnPositions. */
    UPROPERTY(Transient)
    FPlayerAttributes ExtraDefenderAttributes;

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

    UFUNCTION()
    void OnBusDeathEvent(const FPSTelemetryDeathEvent& Event);
};

