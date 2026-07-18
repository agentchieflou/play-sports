#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSFieldGrid.generated.h"

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
};
