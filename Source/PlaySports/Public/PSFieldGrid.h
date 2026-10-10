#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerPawn.h"
#include "PSFormations.h"
#include "PSFieldGrid.generated.h"

class APSFieldSurface;

/**
 * The field in the level: its end zones and boundary volumes, and yard-line coordinates. Every
 * position is in the field's one frame (PSField, Data/field_dimensions.json) -- yard line N at
 * X = N * CentimetresPerYard, the offense's own goal line at X = 0, Y = 0 the middle of the field
 * -- which is where the game mode lines up and snaps, so a volume's goal line is the game's.
 * The frame is the world's, wherever this actor stands; the game mode spawns one when the
 * level has none.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSFieldGrid : public AActor
{
    GENERATED_BODY()

public:
    APSFieldGrid();

    /** Spawns the end zones (goal line to end line, sideline to sideline) and the
     *  out-of-bounds volumes past the sidelines and end lines, on the field's frame. BeginPlay
     *  calls it; headless tests call it. Once: later calls return the same volumes. */
    const TArray<AActor*>& SpawnBoundaryVolumes();

    /** The volumes SpawnBoundaryVolumes made. */
    const TArray<AActor*>& GetBoundaryVolumes() const { return BoundaryVolumes; }

    /** The field you see (APSFieldSurface, Epic 146.3): the level's own if it has one, else one
     *  spawned now. Either way it is built from data. BeginPlay calls it; headless tests call it. */
    APSFieldSurface* SpawnFieldSurface();

    // Converts a field coordinate (YardLine, LateralYard from the left sideline) to a world space position (FVector)
    UFUNCTION(BlueprintCallable, Category = "Field")
    FVector GetWorldPositionFromFieldCoordinate(float YardLine, float LateralYard) const;

    // Converts a world space position (FVector) to field coordinates (YardLine, LateralYard)
    UFUNCTION(BlueprintCallable, Category = "Field")
    void GetFieldCoordinateFromWorldPosition(const FVector& WorldPosition, float& OutYardLine, float& OutLateralYard) const;

    // Checks if a world position is out of bounds
    UFUNCTION(BlueprintCallable, Category = "Field")
    bool IsLocationOutOfBounds(const FVector& WorldPosition) const;

    // Checks if a world position is in either of the end zones
    UFUNCTION(BlueprintCallable, Category = "Field")
    bool IsLocationInEndZone(const FVector& WorldPosition, bool& bOutIsEndZoneA) const;

    // Gets the distance to the goal line in yards
    UFUNCTION(BlueprintCallable, Category = "Field")
    float GetDistanceToGoalLine(const FVector& WorldPosition, bool bTargetGoalLineB) const;

    // Calculates the world space location for a player in a formation lineup
    UFUNCTION(BlueprintCallable, Category = "Field|Formation")
    FVector GetFormationSpawnLocation(
        const FPSFormationSpawnPoint& SpawnPoint,
        float LineOfScrimmageYard,
        bool bIsOffense,
        bool bPlayTowardsGoalLineB = true) const;

    /**
     * Spawn pawns from a roster array, positioning them relative to the scrimmage line.
     * Used by GameMode to replace its inline spawn loop (Epic C3).
     * Returns the list of spawned pawns so the caller can cache them.
     *
     * @param Roster       Player attribute rows to spawn.
     * @param ScrimmageX   World-space X position of the line of scrimmage (PSField::YardLineToWorld).
     * @param World        UWorld to spawn into.
     * @return             Array of spawned APSPlayerPawn pointers.
     */
    static TArray<APSPlayerPawn*> SpawnPlayersFromRoster(
        const TArray<const FPlayerAttributes*>& Roster,
        float ScrimmageX,
        UWorld* World);

protected:
    virtual void BeginPlay() override;

    UPROPERTY(Transient)
    TArray<AActor*> BoundaryVolumes;

public:
    /** The side a role plays on: linemen, linebackers and backs of the defense defend. */
    static EPSTeamSide GetSideForRole(EPlayerRole Role);

    /**
     * The pre-snap lineup for players of these roles at ScrimmageX (world X of the line; +X
     * is upfield), one location per role in order (Epic 14). The offense sets up behind the
     * ball -- line centred on it, QB under centre, back behind him, receivers split wide
     * and alternating sides -- and the defense across from it: line, then linebackers, then
     * backs over the receivers. Shared by SpawnPlayersFromRoster and the game mode's reset
     * between plays, so every down starts from the same picture (Epic C3: "field constants
     * live in one place").
     */
    static TArray<FVector> ComputeLineup(const TArray<EPlayerRole>& Roles, float ScrimmageX);

    /**
     * The lineup for a call: the offense in Call's formation, the defense in its front and shell
     * against Offense (or, without it, against the offensive players of Roles lined up here),
     * from Data/formations.json (PSFormations::LineUp, the one authority on alignment). A side
     * whose formation, front or shell the data doesn't know, or with an empty one, lines up by
     * role as above. The ball is at ScrimmageX in the middle of the field.
     */
    static TArray<FVector> ComputeLineup(const TArray<EPlayerRole>& Roles, float ScrimmageX, const FPSLineupCall& Call, const TArray<FPSAlignedPlayer>* Offense = nullptr);

    /** The role lineup's geometry (cm): where a side stands before its call, and the fallback
     *  for a formation, front or shell Data/formations.json doesn't know. */
    static constexpr float LineSetback = 50.f;
    static constexpr float QBDepth = 100.f;
    static constexpr float RunningBackDepth = 500.f;
    static constexpr float LinemanSpacing = 150.f;
    static constexpr float ReceiverSplit = 900.f;
    static constexpr float ReceiverStagger = 300.f;
    static constexpr float TightEndSplit = 450.f;
    static constexpr float DefensiveLineDepth = 100.f;
    static constexpr float DefensiveLineSpacing = 200.f;
    static constexpr float LinebackerDepth = 450.f;
    static constexpr float LinebackerSpacing = 400.f;
    static constexpr float SecondaryDepth = 900.f;
    static constexpr float PawnHeight = 100.f;
};

