#include "PSFieldGrid.h"
#include "PSPlayerPawn.h"
#include "Engine/World.h"

APSFieldGrid::APSFieldGrid()
{
    PrimaryActorTick.bCanEverTick = false;
}

FVector APSFieldGrid::GetWorldPositionFromFieldCoordinate(float YardLine, float LateralYard) const
{
    // Local X is relative to the 50-yard line (which is local X = 0)
    float LocalX = (YardLine - 50.0f) * 91.44f;

    // Local Y is relative to the center of the field width (which is local Y = 0)
    float LocalY = (LateralYard - 26.6667f) * 91.44f;

    // Local Z is at the actor's level
    FVector LocalPos(LocalX, LocalY, 0.0f);

    // Transform local position to world space
    return GetActorTransform().TransformPosition(LocalPos);
}

void APSFieldGrid::GetFieldCoordinateFromWorldPosition(const FVector& WorldPosition, float& OutYardLine, float& OutLateralYard) const
{
    // Transform world position to local space
    FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldPosition);

    // Convert local X and Y back to yards
    OutYardLine = (LocalPos.X / 91.44f) + 50.0f;
    OutLateralYard = (LocalPos.Y / 91.44f) + 26.6667f;
}

bool APSFieldGrid::IsLocationOutOfBounds(const FVector& WorldPosition) const
{
    FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldPosition);

    // Lengthwise bounds: endlines are at +/- 60 yards (+/- 5,486.4 cm)
    // Widthwise bounds: sidelines are at +/- 26.6667 yards (+/- 2,438.4 cm)
    bool bLengthwiseOut = (LocalPos.X < -5486.4f || LocalPos.X > 5486.4f);
    bool bWidthwiseOut = (LocalPos.Y < -2438.4f || LocalPos.Y > 2438.4f);

    return (bLengthwiseOut || bWidthwiseOut);
}

bool APSFieldGrid::IsLocationInEndZone(const FVector& WorldPosition, bool& bOutIsEndZoneA) const
{
    bOutIsEndZoneA = false;

    if (IsLocationOutOfBounds(WorldPosition))
    {
        return false;
    }

    FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldPosition);

    // End Zone A (Home): X = -60 to -50 yards (-5,486.4 cm to -4,572.0 cm)
    if (LocalPos.X >= -5486.4f && LocalPos.X < -4572.0f)
    {
        bOutIsEndZoneA = true;
        return true;
    }

    // End Zone B (Away): X = 50 to 60 yards (4,572.0 cm to 5,486.4 cm)
    if (LocalPos.X > 4572.0f && LocalPos.X <= 5486.4f)
    {
        bOutIsEndZoneA = false;
        return true;
    }

    return false;
}

float APSFieldGrid::GetDistanceToGoalLine(const FVector& WorldPosition, bool bTargetGoalLineB) const
{
    float YardLine = 0.0f;
    float LateralYard = 0.0f;
    GetFieldCoordinateFromWorldPosition(WorldPosition, YardLine, LateralYard);

    if (bTargetGoalLineB)
    {
        // Goal Line B is at YardLine = 100
        return 100.0f - YardLine;
    }
    else
    {
        // Goal Line A is at YardLine = 0
        return YardLine;
    }
}

FVector APSFieldGrid::GetFormationSpawnLocation(
    const FPSFormationSpawnPoint& SpawnPoint,
    float LineOfScrimmageYard,
    bool bIsOffense,
    bool bPlayTowardsGoalLineB) const
{
    float TargetYardLine = LineOfScrimmageYard;
    float LateralYard = 26.6667f + SpawnPoint.LateralYardOffset;

    // If play direction is towards B (standard positive X direction):
    // Offense is lined up behind (less than) the line of scrimmage.
    // Defense is lined up in front of (greater than) the line of scrimmage.
    if (bPlayTowardsGoalLineB)
    {
        if (bIsOffense)
        {
            TargetYardLine += SpawnPoint.ScrimmageYardOffset; // ScrimmageYardOffset is negative for offense behind line
        }
        else
        {
            TargetYardLine += SpawnPoint.ScrimmageYardOffset; // ScrimmageYardOffset is positive for defense in front of line
        }
    }
    else
    {
        // Playing towards Goal Line A (reverse direction)
        if (bIsOffense)
        {
            TargetYardLine -= SpawnPoint.ScrimmageYardOffset;
        }
        else
        {
            TargetYardLine -= SpawnPoint.ScrimmageYardOffset;
        }
    }

    return GetWorldPositionFromFieldCoordinate(TargetYardLine, LateralYard);
}

TArray<APSPlayerPawn*> APSFieldGrid::SpawnPlayersFromRoster(
    const TArray<FPlayerAttributes*>& Roster,
    float ScrimmageX,
    UWorld* World)
{
    TArray<APSPlayerPawn*> SpawnedPawns;
    if (!World || Roster.Num() == 0)
    {
        return SpawnedPawns;
    }

    float XOffset = 0.f;
    for (FPlayerAttributes* Player : Roster)
    {
        if (!Player)
        {
            continue;
        }

        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        // Position logic mirrors original GameMode spawn loop
        float SpawnX = (Player->Role == EPlayerRole::Quarterback) ? ScrimmageX - 300.f : ScrimmageX;
        FVector SpawnLocation(SpawnX, XOffset, 100.f);
        XOffset += 150.f;

        APSPlayerPawn* NewPawn = World->SpawnActor<APSPlayerPawn>(
            APSPlayerPawn::StaticClass(), SpawnLocation, FRotator::ZeroRotator, SpawnParams);
        if (NewPawn)
        {
            NewPawn->InitializePlayer(*Player);
            SpawnedPawns.Add(NewPawn);
        }
    }

    UE_LOG(LogTemp, Display, TEXT("APSFieldGrid::SpawnPlayersFromRoster: Spawned %d pawns."), SpawnedPawns.Num());
    return SpawnedPawns;
}
