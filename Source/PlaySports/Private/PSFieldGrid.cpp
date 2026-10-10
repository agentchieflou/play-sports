#include "PSFieldGrid.h"
#include "PSPlayerPawn.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "Engine/World.h"
#include "PSEndZoneVolume.h"
#include "PSOutOfBoundsVolume.h"
#include "Components/BoxComponent.h"

APSFieldGrid::APSFieldGrid()
{
    PrimaryActorTick.bCanEverTick = false;
}

void APSFieldGrid::BeginPlay()
{
    Super::BeginPlay();

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // 1. Home End Zone (End Zone A)
    FVector LocEZA = GetActorTransform().TransformPosition(FVector(-5029.2f, 0.0f, 250.0f));
    APSEndZoneVolume* EZA = World->SpawnActor<APSEndZoneVolume>(APSEndZoneVolume::StaticClass(), LocEZA, GetActorRotation(), SpawnParams);
    if (EZA && EZA->CollisionBox)
    {
        EZA->bIsEndZoneA = true;
        EZA->CollisionBox->SetBoxExtent(FVector(457.2f, 2438.4f, 250.0f));
        EZA->Tags.Add(TEXT("EndZoneA"));
    }

    // 2. Away End Zone (End Zone B)
    FVector LocEZB = GetActorTransform().TransformPosition(FVector(5029.2f, 0.0f, 250.0f));
    APSEndZoneVolume* EZB = World->SpawnActor<APSEndZoneVolume>(APSEndZoneVolume::StaticClass(), LocEZB, GetActorRotation(), SpawnParams);
    if (EZB && EZB->CollisionBox)
    {
        EZB->bIsEndZoneA = false;
        EZB->CollisionBox->SetBoxExtent(FVector(457.2f, 2438.4f, 250.0f));
        EZB->Tags.Add(TEXT("EndZoneB"));
    }

    // 3. Left Sideline Boundary
    FVector LocOOB_L = GetActorTransform().TransformPosition(FVector(0.0f, -3719.2f, 250.0f));
    APSOutOfBoundsVolume* OOB_L = World->SpawnActor<APSOutOfBoundsVolume>(APSOutOfBoundsVolume::StaticClass(), LocOOB_L, GetActorRotation(), SpawnParams);
    if (OOB_L && OOB_L->CollisionBox)
    {
        OOB_L->CollisionBox->SetBoxExtent(FVector(6000.0f, 1280.8f, 250.0f));
        OOB_L->Tags.Add(TEXT("OutOfBounds"));
    }

    // 4. Right Sideline Boundary
    FVector LocOOB_R = GetActorTransform().TransformPosition(FVector(0.0f, 3719.2f, 250.0f));
    APSOutOfBoundsVolume* OOB_R = World->SpawnActor<APSOutOfBoundsVolume>(APSOutOfBoundsVolume::StaticClass(), LocOOB_R, GetActorRotation(), SpawnParams);
    if (OOB_R && OOB_R->CollisionBox)
    {
        OOB_R->CollisionBox->SetBoxExtent(FVector(6000.0f, 1280.8f, 250.0f));
        OOB_R->Tags.Add(TEXT("OutOfBounds"));
    }

    // 5. Back Endline A Boundary (Home Side)
    FVector LocOOB_BA = GetActorTransform().TransformPosition(FVector(-7986.4f, 0.0f, 250.0f));
    APSOutOfBoundsVolume* OOB_BA = World->SpawnActor<APSOutOfBoundsVolume>(APSOutOfBoundsVolume::StaticClass(), LocOOB_BA, GetActorRotation(), SpawnParams);
    if (OOB_BA && OOB_BA->CollisionBox)
    {
        OOB_BA->CollisionBox->SetBoxExtent(FVector(2500.0f, 5000.0f, 250.0f));
        OOB_BA->Tags.Add(TEXT("OutOfBounds"));
    }

    // 6. Back Endline B Boundary (Away Side)
    FVector LocOOB_BB = GetActorTransform().TransformPosition(FVector(7986.4f, 0.0f, 250.0f));
    APSOutOfBoundsVolume* OOB_BB = World->SpawnActor<APSOutOfBoundsVolume>(APSOutOfBoundsVolume::StaticClass(), LocOOB_BB, GetActorRotation(), SpawnParams);
    if (OOB_BB && OOB_BB->CollisionBox)
    {
        OOB_BB->CollisionBox->SetBoxExtent(FVector(2500.0f, 5000.0f, 250.0f));
        OOB_BB->Tags.Add(TEXT("OutOfBounds"));
    }
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

EPSTeamSide APSFieldGrid::GetSideForRole(EPlayerRole Role)
{
    return (Role == EPlayerRole::DefensiveLineman || Role == EPlayerRole::Linebacker || Role == EPlayerRole::DefensiveBack)
        ? EPSTeamSide::Defense
        : EPSTeamSide::Offense;
}

TArray<FVector> APSFieldGrid::ComputeLineup(const TArray<EPlayerRole>& Roles, float ScrimmageX)
{
    TMap<EPlayerRole, int32> Counts;
    for (const EPlayerRole Role : Roles)
    {
        Counts.FindOrAdd(Role)++;
    }

    // Centred across the field: Index 0..Count-1 spread Spacing apart around Y = 0.
    auto Centred = [](int32 Index, int32 Count, float Spacing)
    {
        return (Index - (Count - 1) * 0.5f) * Spacing;
    };
    // Split wide, alternating right and left, each pair Stagger further out.
    auto Split = [](int32 Index, float Width, float Stagger)
    {
        const float Side = (Index % 2 == 0) ? 1.f : -1.f;
        return Side * (Width + Stagger * (Index / 2));
    };

    TMap<EPlayerRole, int32> Seen;
    TArray<FVector> Lineup;
    Lineup.Reserve(Roles.Num());
    for (const EPlayerRole Role : Roles)
    {
        const int32 Index = Seen.FindOrAdd(Role)++;
        const int32 Count = Counts[Role];
        float X = ScrimmageX;
        float Y = 0.f;
        switch (Role)
        {
        case EPlayerRole::OffensiveLineman:
            X -= LineSetback;
            Y = Centred(Index, Count, LinemanSpacing);
            break;
        case EPlayerRole::Quarterback:
            X -= QBDepth;
            Y = Centred(Index, Count, LinemanSpacing);
            break;
        case EPlayerRole::RunningBack:
            X -= RunningBackDepth;
            Y = Centred(Index, Count, LinemanSpacing);
            break;
        case EPlayerRole::WideReceiver:
            X -= LineSetback;
            Y = Split(Index, ReceiverSplit, ReceiverStagger);
            break;
        case EPlayerRole::TightEnd:
            X -= LineSetback;
            Y = Split(Index, TightEndSplit, LinemanSpacing);
            break;
        case EPlayerRole::DefensiveLineman:
            X += DefensiveLineDepth;
            Y = Centred(Index, Count, DefensiveLineSpacing);
            break;
        case EPlayerRole::Linebacker:
            X += LinebackerDepth;
            Y = Centred(Index, Count, LinebackerSpacing);
            break;
        case EPlayerRole::DefensiveBack:
            X += SecondaryDepth;
            Y = Split(Index, ReceiverSplit, ReceiverStagger);
            break;
        default:
            break;
        }
        Lineup.Add(FVector(X, Y, PawnHeight));
    }
    return Lineup;
}

TArray<APSPlayerPawn*> APSFieldGrid::SpawnPlayersFromRoster(
    const TArray<const FPlayerAttributes*>& Roster,
    float ScrimmageX,
    UWorld* World)
{
    TArray<APSPlayerPawn*> SpawnedPawns;
    if (!World || Roster.Num() == 0)
    {
        return SpawnedPawns;
    }

    TArray<const FPlayerAttributes*> Players;
    TArray<EPlayerRole> Roles;
    for (const FPlayerAttributes* Player : Roster)
    {
        if (Player)
        {
            Players.Add(Player);
            Roles.Add(Player->Role);
        }
    }
    const TArray<FVector> Lineup = ComputeLineup(Roles, ScrimmageX);

    for (int32 Index = 0; Index < Players.Num(); ++Index)
    {
        // Deferred so each pawn gets its side's AI before it is auto-possessed: the
        // defense's assignments only reach an APSDefenseController (Epic 14).
        const FTransform SpawnTransform(Lineup[Index]);
        APSPlayerPawn* NewPawn = World->SpawnActorDeferred<APSPlayerPawn>(
            APSPlayerPawn::StaticClass(), SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!NewPawn)
        {
            continue;
        }
        NewPawn->AIControllerClass = GetSideForRole(Players[Index]->Role) == EPSTeamSide::Defense
            ? APSDefenseController::StaticClass()
            : APSOffenseController::StaticClass();
        NewPawn->FinishSpawning(SpawnTransform);
        NewPawn->InitializePlayerPointer(Players[Index]);
        SpawnedPawns.Add(NewPawn);
    }

    UE_LOG(LogTemp, Display, TEXT("APSFieldGrid::SpawnPlayersFromRoster: Spawned %d pawns."), SpawnedPawns.Num());
    return SpawnedPawns;
}
