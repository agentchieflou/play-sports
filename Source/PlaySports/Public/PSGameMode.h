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
class UPSPersonnelManager;
class UPSFieldSides;
class UPSPlayerLeveling;
class UPSMatchSetup;
class UPSStaffManager;
class UPSStatsEngine;

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

    /** The match's players: both teams', each from its own team's roster at its staff's scheme
     *  fit (MatchSetup), or RosterJsonPath's players when the match has no teams; with its depth
     *  chart (FieldSides arranges it for the team with the ball) and the combat/leveling
     *  live-state (Epic 139/141): who's downed, sitting out, or leveled up. The on-field pawns
     *  point at its rows. */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Gameplay|Combat")
    UPSRoster* ActiveRoster;

    /** Puts the team with the ball on offense: follows the simulation's possession and arranges
     *  ActiveRoster's depth chart, so the personnel manager fields each side from its team. */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Gameplay")
    UPSFieldSides* FieldSides;

    /** Picks who is on the field from ActiveRoster (Epic 19.5): the default personnel
     *  packages at kickoff, each play call's package, and the next man up for a sit-out
     *  or a tired player. */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Gameplay")
    UPSPersonnelManager* PersonnelManager;

    /** Who plays this game: the home and away teams and the player's team, read in StartPlay
     *  from the travel options (team select's pick, or the franchise schedule's matchup). */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Match")
    UPSMatchSetup* MatchSetup;

    /** The league's coaching staffs (Epic 89, Data/coaching_staffs.json). Both teams' staffs
     *  take over at kickoff through MatchSetup. */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Match")
    UPSStaffManager* StaffManager;

    /** This game's statistics (Epic 92): every play the simulation resolves, from the bus, into
     *  the match's box score (MatchStats->GetCurrentGame()). */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Match")
    UPSStatsEngine* MatchStats;

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
    /** Opens UPSPlayCallSubsystem's call window for the current down (Epic 102). */
    void OpenPlayCallWindow();

    UFUNCTION()
    void OnBusScoreEvent(const FPSTelemetryScoreEvent& Event);

    UFUNCTION()
    void OnBusCatchEvent(const FPSTelemetryCatchEvent& Event);

    UFUNCTION()
    void OnBusDeathEvent(const FPSTelemetryDeathEvent& Event);
};

