#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSPlayerAttributes.h"
#include "PSFieldGrid.generated.h"

class APSPlayerPawn;

USTRUCT(BlueprintType)
struct FPSFormationSpawnPoint
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    EPlayerRole Role = EPlayerRole::Quarterback;

    // Offset in yards relative to the line of scrimmage (positive values mean in the play direction, negative behind)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float ScrimmageYardOffset = 0.0f;

    // Offset in yards laterally relative to the center of the field width (26.6667 yards)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float LateralYardOffset = 0.0f;
};

/**
 * Field coordinate helper that handles coordinate transformations between yard-line dimensions and world-space positions.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSFieldGrid : public AActor
{
    GENERATED_BODY()

public:
    APSFieldGrid();

    // Converts a field coordinate (YardLine, LateralYard) to a world space position (FVector)
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
     * @param ScrimmageX   World-space X position of the line of scrimmage (yards * 100).
     * @param World        UWorld to spawn into.
     * @return             Array of spawned APSPlayerPawn pointers.
     */
    static TArray<APSPlayerPawn*> SpawnPlayersFromRoster(
        const TArray<FPlayerAttributes*>& Roster,
        float ScrimmageX,
        UWorld* World);

    /** How far behind the scrimmage line a QB lines up (world units). Shared by
     *  SpawnPlayersFromRoster and GameMode::ResetPawnPositions so the two formation
     *  call sites don't each hardcode their own copy (Epic C3: "field constants
     *  live in one place"). */
    static constexpr float QBDropbackDistance = 300.f;

    /** Lateral spacing between formation-mates who aren't on the scrimmage line. */
    static constexpr float FormationLateralSpacing = 150.f;
};

