#include "PSBlownCoverageSubsystem.h"
#include "PSDataIngestion.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSHealthComponent.h"
#include "PSPlayerPawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"

namespace PSBlownCoveragePrivate
{
    bool IsStanding(const APSPlayerPawn* Pawn)
    {
        const UPSHealthComponent* Health = Pawn ? Pawn->GetHealthComponent() : nullptr;
        return Pawn && !(Health && Health->IsDowned());
    }

    bool IsReceiver(const APSPlayerPawn* Pawn)
    {
        if (!Pawn || Pawn->TeamSide != EPSTeamSide::Offense)
        {
            return false;
        }
        const EPlayerRole Role = Pawn->GetAttributes().Role;
        return Role == EPlayerRole::WideReceiver || Role == EPlayerRole::TightEnd || Role == EPlayerRole::RunningBack;
    }

    const UPSDefenderAIComponent* DefenderAIOf(const APSPlayerPawn* Pawn)
    {
        const APSDefenseController* Controller = Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr;
        return Controller ? Controller->GetDefenderAI() : nullptr;
    }
}

void UPSBlownCoverageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>();
    if (Bus)
    {
        Bus->OnSnapMC.AddUObject(this, &UPSBlownCoverageSubsystem::HandleSnap);
        Bus->OnThrowMC.AddUObject(this, &UPSBlownCoverageSubsystem::HandleThrow);
        Bus->OnPhaseChangeMC.AddUObject(this, &UPSBlownCoverageSubsystem::HandlePhaseChange);
        BoundBus = Bus;
    }
}

void UPSBlownCoverageSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnThrowMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

bool UPSBlownCoverageSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPSBlownCoverageSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSBlownCoverageSubsystem, STATGROUP_Tickables);
}

void UPSBlownCoverageSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!bWatching)
    {
        return;
    }
    SinceCheck += DeltaTime;
    if (SinceCheck >= GetTuning().CheckIntervalSeconds)
    {
        SinceCheck = 0.f;
        CheckCoverage();
    }
}

FString UPSBlownCoverageSubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/blown_coverage.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FBlownCoverageTuningRow& UPSBlownCoverageSubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSBlownCoverageSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FBlownCoverageTuningRow Loaded;
    if (!Ingestion->LoadBlownCoverageTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSBlownCoverageSubsystem: Could not load blown-coverage tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    Tuning = Loaded;
    return true;
}

void UPSBlownCoverageSubsystem::SetTuning(const FBlownCoverageTuningRow& InTuning)
{
    Tuning = InTuning;
    bTuningLoaded = true;
}

void UPSBlownCoverageSubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    LineOfScrimmage = Event.LineOfScrimmage;
    Spotted.Reset();
    Sent.Reset();
    SinceCheck = 0.f;
    bWatching = true;
}

void UPSBlownCoverageSubsystem::HandleThrow(const FPSTelemetryThrowEvent& Event)
{
    // The ball is in the air: the defense plays it, not the coverage.
    bWatching = false;
}

void UPSBlownCoverageSubsystem::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("Scoring") || Event.NewPhase == TEXT("PreSnap"))
    {
        bWatching = false;
    }
}

int32 UPSBlownCoverageSubsystem::CheckCoverage()
{
    if (!bWatching)
    {
        return 0;
    }
    const TArray<APSPlayerPawn*> Pawns = GetFieldPawns();

    // Only while the quarterback holds the ball behind the line: once a back or receiver has
    // it, or the quarterback has run past the line, the play is a chase.
    const APSPlayerPawn* Carrier = nullptr;
    for (const APSPlayerPawn* Pawn : Pawns)
    {
        if (Pawn && Pawn->HasPossession())
        {
            Carrier = Pawn;
            break;
        }
    }
    if (!Carrier || Carrier->TeamSide != EPSTeamSide::Offense || Carrier->GetAttributes().Role != EPlayerRole::Quarterback
        || Carrier->GetActorLocation().X > LineOfScrimmage.X)
    {
        return 0;
    }

    const FBlownCoverageTuningRow& Settings = GetTuning();
    int32 Count = 0;
    for (const APSPlayerPawn* Receiver : Pawns)
    {
        if (!PSBlownCoveragePrivate::IsReceiver(Receiver) || !PSBlownCoveragePrivate::IsStanding(Receiver) || Spotted.Contains(FObjectKey(Receiver))
            || Receiver->GetActorLocation().X - LineOfScrimmage.X < Settings.MinDepthPastLine)
        {
            continue;
        }
        float Separation = TNumericLimits<float>::Max();
        for (const APSPlayerPawn* Defender : Pawns)
        {
            if (Defender && Defender->TeamSide == EPSTeamSide::Defense && PSBlownCoveragePrivate::IsStanding(Defender))
            {
                Separation = FMath::Min(Separation, FVector::Dist2D(Defender->GetActorLocation(), Receiver->GetActorLocation()));
            }
        }
        if (Separation < Settings.UncoveredSeparation)
        {
            continue;
        }

        // Running free: the nearest zone defender who can help rotates to him.
        Spotted.Add(FObjectKey(Receiver));
        const APSPlayerPawn* Helper = FindHelper(Receiver, Pawns);
        if (Helper)
        {
            Sent.Add(FObjectKey(Helper));
        }
        ++Count;

        if (UPSTelemetryBus* Bus = BoundBus.Get())
        {
            FPSTelemetryBlownCoverageEvent Event;
            Event.ReceiverName = Receiver->GetAttributes().DisplayName;
            Event.HelperName = Helper ? Helper->GetAttributes().DisplayName : FString();
            Event.Separation = Separation == TNumericLimits<float>::Max() ? 0.f : Separation;
            Event.Location = Receiver->GetActorLocation();
            Bus->PublishBlownCoverage(Event);
        }
    }
    return Count;
}

APSPlayerPawn* UPSBlownCoverageSubsystem::FindHelper(const APSPlayerPawn* Receiver, const TArray<APSPlayerPawn*>& Pawns) const
{
    APSPlayerPawn* Best = nullptr;
    float BestDistance = Tuning.HelpRadius;
    for (APSPlayerPawn* Defender : Pawns)
    {
        if (!Defender || Defender->TeamSide != EPSTeamSide::Defense || !PSBlownCoveragePrivate::IsStanding(Defender) || Sent.Contains(FObjectKey(Defender)))
        {
            continue;
        }
        const UPSDefenderAIComponent* AI = PSBlownCoveragePrivate::DefenderAIOf(Defender);
        if (!AI || AI->IsFrozen())
        {
            continue;
        }
        // A zone player can leave his spot; a man defender only when he has nobody to cover.
        const EPSDefenderAction Action = AI->GetAction();
        const bool bFree = Action == EPSDefenderAction::Zone || (Action == EPSDefenderAction::Cover && !AI->GetCoveredReceiver());
        const float Distance = FVector::Dist2D(Defender->GetActorLocation(), Receiver->GetActorLocation());
        if (bFree && Distance <= BestDistance)
        {
            BestDistance = Distance;
            Best = Defender;
        }
    }
    return Best;
}

TArray<APSPlayerPawn*> UPSBlownCoverageSubsystem::GetFieldPawns() const
{
    TArray<APSPlayerPawn*> Pawns;
    if (UWorld* World = GetWorld())
    {
        for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
        {
            Pawns.Add(*It);
        }
    }
    return Pawns;
}
