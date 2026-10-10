#include "PSAIFieldSnapshot.h"
#include "PSPerfBudget.h"
#include "Engine/World.h"
#include "EngineUtils.h"

DECLARE_CYCLE_STAT(TEXT("Field scan"), STAT_PSAIFieldScan, STATGROUP_PSAI);

void UPSAIFieldSnapshot::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (UWorld* World = GetWorld())
    {
        SpawnHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UPSAIFieldSnapshot::HandleActorSpawned));
    }
}

void UPSAIFieldSnapshot::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->RemoveOnActorSpawnedHandler(SpawnHandle);
    }
    SpawnHandle.Reset();
    Pawns.Reset();
    Roles.Reset();
    Super::Deinitialize();
}

void UPSAIFieldSnapshot::HandleActorSpawned(AActor* Actor)
{
    if (Actor && Actor->IsA<APSPlayerPawn>())
    {
        bDirty = true;
    }
}

void UPSAIFieldSnapshot::EnsureScanned()
{
    // A pawn on the list destroyed since the scan also starts a new one.
    bool bStale = bDirty || ScannedFrame != GFrameCounter;
    for (int32 Index = 0; !bStale && Index < Pawns.Num(); ++Index)
    {
        bStale = !IsValid(Pawns[Index]);
    }
    if (!bStale)
    {
        return;
    }

    SCOPE_CYCLE_COUNTER(STAT_PSAIFieldScan);
    PS_PERF_SCOPE_NESTED(AI);
    PSPerf::AddCount(EPSPerfCounter::FieldScans);
    Pawns.Reset();
    Roles.Reset();
    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        Pawns.Add(*It);
        Roles.Add(It->GetAttributes().Role);
    }
    ScannedFrame = GFrameCounter;
    bDirty = false;
    ++ScanCount;
}

const TArray<APSPlayerPawn*>& UPSAIFieldSnapshot::GetPawns()
{
    EnsureScanned();
    return Pawns;
}

const TArray<EPlayerRole>& UPSAIFieldSnapshot::GetRoles()
{
    EnsureScanned();
    return Roles;
}

APSPlayerPawn* UPSAIFieldSnapshot::FindPawn(EPSTeamSide Side, EPlayerRole Role, const APSPlayerPawn* Except)
{
    EnsureScanned();
    for (int32 Index = 0; Index < Pawns.Num(); ++Index)
    {
        APSPlayerPawn* Pawn = Pawns[Index];
        if (Pawn != Except && Roles[Index] == Role && Pawn->TeamSide == Side)
        {
            return Pawn;
        }
    }
    return nullptr;
}

APSPlayerPawn* UPSAIFieldSnapshot::FindBallCarrier()
{
    EnsureScanned();
    for (APSPlayerPawn* Pawn : Pawns)
    {
        if (Pawn->HasPossession())
        {
            return Pawn;
        }
    }
    return nullptr;
}

const TArray<APSPlayerPawn*>& UPSAIFieldSnapshot::GetFieldPawns(const UWorld* World)
{
    UPSAIFieldSnapshot* Snapshot = World ? World->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    if (Snapshot)
    {
        return Snapshot->GetPawns();
    }
    static const TArray<APSPlayerPawn*> NoPawns;
    return NoPawns;
}
