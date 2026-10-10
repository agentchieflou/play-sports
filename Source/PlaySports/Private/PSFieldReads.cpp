#include "PSFieldReads.h"
#include "PSHealthComponent.h"

FVector PSFieldReads::LargestRunLaneGap(const TArray<APSPlayerPawn*>& Pawns)
{
    TArray<APSPlayerPawn*> OffensiveLinemen;
    for (APSPlayerPawn* Pawn : Pawns)
    {
        if (Pawn && Pawn->GetAttributes().Role == EPlayerRole::OffensiveLineman)
        {
            OffensiveLinemen.Add(Pawn);
        }
    }

    if (OffensiveLinemen.Num() == 0)
    {
        return FVector::ZeroVector;
    }
    if (OffensiveLinemen.Num() == 1)
    {
        return OffensiveLinemen[0]->GetActorLocation();
    }

    OffensiveLinemen.Sort([](const APSPlayerPawn& A, const APSPlayerPawn& B)
    {
        return A.GetActorLocation().Y < B.GetActorLocation().Y;
    });

    float LargestGapSize = 0.f;
    FVector LargestGapCenter = FVector::ZeroVector;
    for (int32 Index = 0; Index < OffensiveLinemen.Num() - 1; ++Index)
    {
        const FVector LocA = OffensiveLinemen[Index]->GetActorLocation();
        const FVector LocB = OffensiveLinemen[Index + 1]->GetActorLocation();
        const float GapSize = FVector::Dist(LocA, LocB);
        if (GapSize > LargestGapSize)
        {
            LargestGapSize = GapSize;
            LargestGapCenter = (LocA + LocB) * 0.5f;
        }
    }
    return LargestGapCenter;
}

APSPlayerPawn* PSFieldReads::NearestOpponent(const TArray<APSPlayerPawn*>& Pawns, EPSTeamSide FromSide, const FVector& From, float* OutDistance)
{
    APSPlayerPawn* Nearest = nullptr;
    float NearestDistance = TNumericLimits<float>::Max();
    for (APSPlayerPawn* Pawn : Pawns)
    {
        if (!Pawn || Pawn->TeamSide == FromSide)
        {
            continue;
        }
        const UPSHealthComponent* Health = Pawn->GetHealthComponent();
        if (Health && Health->IsDowned())
        {
            continue;
        }
        const float Distance = FVector::Dist2D(Pawn->GetActorLocation(), From);
        if (Distance < NearestDistance)
        {
            NearestDistance = Distance;
            Nearest = Pawn;
        }
    }
    if (OutDistance)
    {
        *OutDistance = NearestDistance;
    }
    return Nearest;
}

float PSFieldReads::Separation(const TArray<APSPlayerPawn*>& Pawns, const APSPlayerPawn* Receiver)
{
    if (!Receiver)
    {
        return 0.f;
    }
    float Distance = TNumericLimits<float>::Max();
    NearestOpponent(Pawns, Receiver->TeamSide, Receiver->GetActorLocation(), &Distance);
    return Distance;
}
