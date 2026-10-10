#include "PSFieldGrid.h"
#include "PSFieldDimensions.h"
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
    SpawnBoundaryVolumes();
}

const TArray<AActor*>& APSFieldGrid::SpawnBoundaryVolumes()
{
    UWorld* World = GetWorld();
    if (!World || BoundaryVolumes.Num() > 0)
    {
        return BoundaryVolumes;
    }

    // Laid out on the field's one frame (PSField): yard line N at X = N yards, the offense's goal
    // line at X = 0, Y = 0 the middle of the field -- where the game mode lines up and snaps.
    const FPSFieldDimensions& Field = PSField::GetDimensions();
    const float HalfHeight = Field.BoundaryHeightCm * 0.5f;
    const float SidelineY = PSField::SidelineY();
    const float EndZoneHalfDepth = PSField::YardsToCentimetres(Field.EndZoneDepthYards) * 0.5f;
    const float OutHalfDepth = PSField::YardsToCentimetres(Field.OutOfBoundsDepthYards) * 0.5f;
    const float HalfSpan = (PSField::EndLineX(true) - PSField::EndLineX(false)) * 0.5f;

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto SpawnVolume = [this, World, &SpawnParams](UClass* VolumeClass, const FVector& Center, const FVector& Extent, FName Tag) -> AActor*
    {
        AActor* Volume = World->SpawnActor<AActor>(VolumeClass, Center, FRotator::ZeroRotator, SpawnParams);
        UBoxComponent* Box = Volume ? Volume->FindComponentByClass<UBoxComponent>() : nullptr;
        if (!Box)
        {
            return nullptr;
        }
        Box->SetBoxExtent(Extent);
        Volume->Tags.Add(Tag);
        BoundaryVolumes.Add(Volume);
        return Volume;
    };

    // The end zones: from each goal line to its end line, sideline to sideline.
    const FVector EndZoneExtent(EndZoneHalfDepth, SidelineY, HalfHeight);
    if (APSEndZoneVolume* NearEndZone = Cast<APSEndZoneVolume>(SpawnVolume(APSEndZoneVolume::StaticClass(),
        FVector(PSField::GoalLineX(false) - EndZoneHalfDepth, 0.f, HalfHeight), EndZoneExtent, TEXT("EndZoneA"))))
    {
        NearEndZone->bIsEndZoneA = true;
    }
    if (APSEndZoneVolume* FarEndZone = Cast<APSEndZoneVolume>(SpawnVolume(APSEndZoneVolume::StaticClass(),
        FVector(PSField::GoalLineX(true) + EndZoneHalfDepth, 0.f, HalfHeight), EndZoneExtent, TEXT("EndZoneB"))))
    {
        FarEndZone->bIsEndZoneA = false;
    }

    // Out of bounds: past each sideline, and past each end line, overlapping at the corners.
    const FVector SidelineExtent(HalfSpan + 2.f * OutHalfDepth, OutHalfDepth, HalfHeight);
    for (const float Side : { -1.f, 1.f })
    {
        SpawnVolume(APSOutOfBoundsVolume::StaticClass(), FVector(PSField::MidfieldX(), Side * (SidelineY + OutHalfDepth), HalfHeight), SidelineExtent, TEXT("OutOfBounds"));
    }
    const FVector EndLineExtent(OutHalfDepth, SidelineY + 2.f * OutHalfDepth, HalfHeight);
    SpawnVolume(APSOutOfBoundsVolume::StaticClass(), FVector(PSField::EndLineX(false) - OutHalfDepth, 0.f, HalfHeight), EndLineExtent, TEXT("OutOfBounds"));
    SpawnVolume(APSOutOfBoundsVolume::StaticClass(), FVector(PSField::EndLineX(true) + OutHalfDepth, 0.f, HalfHeight), EndLineExtent, TEXT("OutOfBounds"));
    return BoundaryVolumes;
}

FVector APSFieldGrid::GetWorldPositionFromFieldCoordinate(float YardLine, float LateralYard) const
{
    // LateralYard runs from the left sideline (0) across; the field's frame is centred on Y = 0.
    return PSField::YardLineToWorld(YardLine, LateralYard - PSField::GetDimensions().FieldWidthYards * 0.5f);
}

void APSFieldGrid::GetFieldCoordinateFromWorldPosition(const FVector& WorldPosition, float& OutYardLine, float& OutLateralYard) const
{
    OutYardLine = PSField::WorldToYardLine(WorldPosition);
    OutLateralYard = PSField::CentimetresToYards(WorldPosition.Y) + PSField::GetDimensions().FieldWidthYards * 0.5f;
}

bool APSFieldGrid::IsLocationOutOfBounds(const FVector& WorldPosition) const
{
    // Past an end line (behind an end zone) or a sideline.
    const bool bLengthwiseOut = WorldPosition.X < PSField::EndLineX(false) || WorldPosition.X > PSField::EndLineX(true);
    const bool bWidthwiseOut = FMath::Abs(WorldPosition.Y) > PSField::SidelineY();
    return bLengthwiseOut || bWidthwiseOut;
}

bool APSFieldGrid::IsLocationInEndZone(const FVector& WorldPosition, bool& bOutIsEndZoneA) const
{
    bOutIsEndZoneA = false;
    if (IsLocationOutOfBounds(WorldPosition))
    {
        return false;
    }

    // End Zone A is behind the offense's own goal line (X = 0), End Zone B past the one it attacks.
    if (WorldPosition.X < PSField::GoalLineX(false))
    {
        bOutIsEndZoneA = true;
        return true;
    }
    return WorldPosition.X > PSField::GoalLineX(true);
}

float APSFieldGrid::GetDistanceToGoalLine(const FVector& WorldPosition, bool bTargetGoalLineB) const
{
    const float YardLine = PSField::WorldToYardLine(WorldPosition);
    return bTargetGoalLineB ? PSField::GetDimensions().FieldLengthYards - YardLine : YardLine;
}

FVector APSFieldGrid::GetFormationSpawnLocation(
    const FPSFormationSpawnPoint& SpawnPoint,
    float LineOfScrimmageYard,
    bool bIsOffense,
    bool bPlayTowardsGoalLineB) const
{
    // The spawn point's offset is in the play's direction: toward Goal Line B it adds, toward A
    // it subtracts (negative for the offense behind the line, positive for the defense).
    const float TargetYardLine = bPlayTowardsGoalLineB
        ? LineOfScrimmageYard + SpawnPoint.ScrimmageYardOffset
        : LineOfScrimmageYard - SpawnPoint.ScrimmageYardOffset;
    const float LateralYard = PSField::GetDimensions().FieldWidthYards * 0.5f + SpawnPoint.LateralYardOffset;
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
