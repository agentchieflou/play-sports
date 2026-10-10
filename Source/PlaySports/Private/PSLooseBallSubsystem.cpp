#include "PSLooseBallSubsystem.h"
#include "PSAIFieldSnapshot.h"
#include "PSBall.h"
#include "PSBallActionComponent.h"
#include "PSBallResolutionHelpers.h"
#include "PSDataIngestion.h"
#include "PSFieldDimensions.h"
#include "PSHealthComponent.h"
#include "PSPlatformTiers.h"
#include "PSPlayerPawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"

DECLARE_CYCLE_STAT(TEXT("Loose ball"), STAT_PSAILooseBall, STATGROUP_PSAI);

namespace PSLooseBallPrivate
{
    bool IsStanding(const APSPlayerPawn* Pawn)
    {
        const UPSHealthComponent* Health = Pawn ? Pawn->GetHealthComponent() : nullptr;
        return Pawn && !(Health && Health->IsDowned());
    }
}

void UPSLooseBallSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>();
    if (Bus)
    {
        Bus->OnSnapMC.AddUObject(this, &UPSLooseBallSubsystem::HandleSnap);
        Bus->OnLooseBallMC.AddUObject(this, &UPSLooseBallSubsystem::HandleLooseBall);
        Bus->OnTackleMC.AddUObject(this, &UPSLooseBallSubsystem::HandleTackle);
        Bus->OnPhaseChangeMC.AddUObject(this, &UPSLooseBallSubsystem::HandlePhaseChange);
        BoundBus = Bus;
    }
}

void UPSLooseBallSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnLooseBallMC.RemoveAll(this);
        Bus->OnTackleMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

bool UPSLooseBallSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPSLooseBallSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSLooseBallSubsystem, STATGROUP_Tickables);
}

void UPSLooseBallSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (Stage != EPSLooseBallStage::Loose && Stage != EPSLooseBallStage::Returning)
    {
        return;
    }
    // At the AI's decision rate (the platform tier's, Epic 129), like the players chasing it.
    SinceUpdate += DeltaTime;
    if (SinceUpdate >= PSPlatformTiers::GetActiveTier().AIDecisionInterval)
    {
        UpdateLooseBall(SinceUpdate);
        SinceUpdate = 0.f;
    }
}

FString UPSLooseBallSubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/loose_ball.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSLooseBallTuning& UPSLooseBallSubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSLooseBallSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSLooseBallTuning Loaded;
    if (!Ingestion->LoadLooseBallTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSLooseBallSubsystem: Could not load loose-ball tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    const TArray<FString> Problems = ValidateTuning(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSLooseBallSubsystem: %s"), *Problem);
    }
    Tuning = Loaded;
    return Problems.Num() == 0;
}

void UPSLooseBallSubsystem::SetTuning(const FPSLooseBallTuning& InTuning)
{
    Tuning = InTuning;
    bTuningLoaded = true;
}

TArray<FString> UPSLooseBallSubsystem::ValidateTuning(const FPSLooseBallTuning& InTuning)
{
    TArray<FString> Problems;
    if (InTuning.BlockedFieldGoalYards < 0)
    {
        Problems.Add(TEXT("BlockedFieldGoalYards must be 0 or more"));
    }
    if (InTuning.ChaseRadius < 0.f || InTuning.RecoverRadius < 0.f || InTuning.ScoopClearRadius < 0.f || InTuning.SquirtDistance < 0.f || InTuning.RetrySeconds < 0.f)
    {
        Problems.Add(TEXT("ChaseRadius, RecoverRadius, ScoopClearRadius, SquirtDistance and RetrySeconds must be 0 or more"));
    }
    if (InTuning.RecoverRadius > InTuning.ChaseRadius)
    {
        Problems.Add(TEXT("RecoverRadius must not exceed ChaseRadius: a player close enough to take the ball must be chasing it"));
    }
    if (InTuning.MaxLooseSeconds <= 0.f || InTuning.MaxReturnSeconds <= 0.f)
    {
        Problems.Add(TEXT("MaxLooseSeconds and MaxReturnSeconds must be above 0"));
    }
    return Problems;
}

void UPSLooseBallSubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    LineOfScrimmage = Event.LineOfScrimmage;
    SnapYardLine = Event.YardLine;
    Stage = EPSLooseBallStage::None;
    Ball.Reset();
    Returner.Reset();
    RetryAt.Reset();
    LooseSeconds = 0.f;
    ReturnSeconds = 0.f;
    SinceUpdate = 0.f;
    // Seeded from the snap's situation, so a replayed snap plays the ball the same way.
    Rolls.Initialize(static_cast<int32>(HashCombine(HashCombine(GetTypeHash(Event.YardLine), GetTypeHash(Event.Down)), GetTypeHash(Event.GameClockSeconds))));
}

void UPSLooseBallSubsystem::HandleLooseBall(const FPSTelemetryLooseBallEvent& Event)
{
    if (Event.Kind == EPSLooseBallEventKind::Blocked && Stage == EPSLooseBallStage::None)
    {
        MakeLoose(Event.KickType, Event.YardsBehindLine);
    }
}

void UPSLooseBallSubsystem::HandleTackle(const FPSTelemetryTackleEvent& Event)
{
    // The returner brought down: dead where he went down.
    APSPlayerPawn* Carrier = Returner.Get();
    if (Stage == EPSLooseBallStage::Returning && Carrier && Event.BallCarrierName == Carrier->GetAttributes().DisplayName)
    {
        BlowDead(Carrier->GetActorLocation(), false, false, Carrier);
    }
}

void UPSLooseBallSubsystem::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("Scoring") || Event.NewPhase == TEXT("PreSnap"))
    {
        Stage = EPSLooseBallStage::None;
        Ball.Reset();
        Returner.Reset();
        RetryAt.Reset();
    }
}

bool UPSLooseBallSubsystem::MakeLoose(const FString& InKickType, int32 YardsBehindLine)
{
    UWorld* World = GetWorld();
    UPSAIFieldSnapshot* Field = World ? World->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    if (!Field)
    {
        return false;
    }

    // The ball: in the holder's hands, or (snapped, not yet held) wherever it is. One look at the
    // moment of the block, not a scan per frame.
    APSPlayerPawn* Holder = Field->FindBallCarrier();
    const UPSBallActionComponent* HolderActions = Holder ? Holder->GetBallActionComponent() : nullptr;
    APSBall* Football = HolderActions ? HolderActions->GetCarriedBall() : nullptr;
    if (!Football)
    {
        for (TActorIterator<APSBall> It(World); It; ++It)
        {
            Football = *It;
            break;
        }
    }
    if (!Football)
    {
        return false;
    }

    // It comes loose behind the line: a punt's recoil, a field goal's hold spot.
    const FPSLooseBallTuning& Settings = GetTuning();
    const int32 Yards = InKickType == TEXT("FieldGoal") ? FMath::Max(YardsBehindLine, Settings.BlockedFieldGoalYards) : YardsBehindLine;
    const FVector Spot(LineOfScrimmage.X - PSField::YardsToCentimetres(Yards), Holder ? Holder->GetActorLocation().Y : LineOfScrimmage.Y, Football->GetActorLocation().Z);

    // Out of the holder's hands (possession is the pawn's possession component's) and lying on
    // the ground: not a fumble in flight, so the ball's own catch and recovery rolls stay out of
    // it and the players' tries here decide it.
    if (Holder)
    {
        Holder->LosePossession();
    }
    Football->DetachFromCarrier();
    Football->bIsFumbled = false;
    Football->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);

    Ball = Football;
    KickType = InKickType;
    Stage = EPSLooseBallStage::Loose;
    LooseSeconds = 0.f;
    ReturnSeconds = 0.f;
    RetryAt.Reset();

    FPSTelemetryLooseBallEvent Event;
    Event.Kind = EPSLooseBallEventKind::Loose;
    Event.YardsBehindLine = Yards;
    Event.Location = Spot;
    Publish(Event);
    return true;
}

void UPSLooseBallSubsystem::UpdateLooseBall(float DeltaSeconds)
{
    SCOPE_CYCLE_COUNTER(STAT_PSAILooseBall);
    const FPSLooseBallTuning& Settings = GetTuning();

    if (Stage == EPSLooseBallStage::Returning)
    {
        ReturnSeconds += DeltaSeconds;
        APSPlayerPawn* Carrier = Returner.Get();
        if (!Carrier || !PSLooseBallPrivate::IsStanding(Carrier) || !Carrier->HasPossession())
        {
            // Down, or stripped of it: dead where he is.
            const APSBall* Football = Ball.Get();
            BlowDead(Carrier ? Carrier->GetActorLocation() : (Football ? Football->GetActorLocation() : LineOfScrimmage), false, false, Carrier);
        }
        else if (Carrier->GetActorLocation().X <= GetGoalLineX())
        {
            BlowDead(Carrier->GetActorLocation(), false, true, Carrier);
        }
        else if (ReturnSeconds >= Settings.MaxReturnSeconds)
        {
            BlowDead(Carrier->GetActorLocation(), false, false, Carrier);
        }
        return;
    }
    if (Stage != EPSLooseBallStage::Loose)
    {
        return;
    }

    LooseSeconds += DeltaSeconds;
    const APSBall* Football = Ball.Get();
    if (!Football)
    {
        Stage = EPSLooseBallStage::None;
        return;
    }

    // The nearest player on the ball who may try for it.
    const FVector At = Football->GetActorLocation();
    APSPlayerPawn* Nearest = nullptr;
    float NearestDistance = Settings.RecoverRadius;
    for (APSPlayerPawn* Player : UPSAIFieldSnapshot::GetFieldPawns(GetWorld()))
    {
        if (!PSLooseBallPrivate::IsStanding(Player))
        {
            continue;
        }
        const float* TryAgainAt = RetryAt.Find(FObjectKey(Player));
        if (TryAgainAt && LooseSeconds < *TryAgainAt)
        {
            continue;
        }
        const float Distance = FVector::Dist2D(Player->GetActorLocation(), At);
        if (Distance <= NearestDistance)
        {
            NearestDistance = Distance;
            Nearest = Player;
        }
    }
    if (Nearest)
    {
        TryRecover(Nearest);
        return;
    }
    if (LooseSeconds >= Settings.MaxLooseSeconds)
    {
        // Nobody has it: dead where it lies, the defense's ball.
        BlowDead(At, false, false, nullptr);
    }
}

void UPSLooseBallSubsystem::TryRecover(APSPlayerPawn* Player)
{
    const APSBall* Football = Ball.Get();
    const float Chance = Football ? PSBallResolutionHelpers::ComputeFumbleRecoveryChance(Player->GetAttributes(), Football->CatchTuningSettings) : 0.f;
    if (Rolls.FRand() < Chance)
    {
        Recover(Player);
    }
    else
    {
        Squirt(Player);
    }
}

void UPSLooseBallSubsystem::Recover(APSPlayerPawn* Player)
{
    APSBall* Football = Ball.Get();
    if (!Football)
    {
        return;
    }
    Football->AttachToCarrier(Player, TEXT("HandSocket"));
    Player->GainPossession();

    // A defender in the clear scoops it up and goes; anyone else falls on it.
    const bool bKickingTeam = Player->TeamSide == EPSTeamSide::Offense;
    bool bClear = !bKickingTeam;
    for (const APSPlayerPawn* Other : UPSAIFieldSnapshot::GetFieldPawns(GetWorld()))
    {
        if (bClear && Other && Other->TeamSide != Player->TeamSide && PSLooseBallPrivate::IsStanding(Other)
            && FVector::Dist2D(Other->GetActorLocation(), Player->GetActorLocation()) <= GetTuning().ScoopClearRadius)
        {
            bClear = false;
        }
    }

    FPSTelemetryLooseBallEvent Event;
    Event.Kind = EPSLooseBallEventKind::Recovered;
    Event.PlayerName = Player->GetAttributes().DisplayName;
    Event.bKickingTeam = bKickingTeam;
    Event.bScooped = bClear;
    Event.Location = Player->GetActorLocation();
    Publish(Event);

    if (bClear)
    {
        Returner = Player;
        Stage = EPSLooseBallStage::Returning;
        ReturnSeconds = 0.f;
    }
    else
    {
        BlowDead(Player->GetActorLocation(), bKickingTeam, false, Player);
    }
}

void UPSLooseBallSubsystem::Squirt(APSPlayerPawn* Player)
{
    APSBall* Football = Ball.Get();
    if (!Football)
    {
        return;
    }
    // He can't hold on: it squirts away from him, and he needs a moment to go after it again.
    RetryAt.Add(FObjectKey(Player), LooseSeconds + GetTuning().RetrySeconds);
    const float Angle = Rolls.FRandRange(0.f, 2.f * PI);
    const FVector Offset(FMath::Cos(Angle) * GetTuning().SquirtDistance, FMath::Sin(Angle) * GetTuning().SquirtDistance, 0.f);
    Football->SetActorLocation(Football->GetActorLocation() + Offset, false, nullptr, ETeleportType::TeleportPhysics);

    FPSTelemetryLooseBallEvent Event;
    Event.Kind = EPSLooseBallEventKind::Muffed;
    Event.PlayerName = Player->GetAttributes().DisplayName;
    Event.bKickingTeam = Player->TeamSide == EPSTeamSide::Offense;
    Event.Location = Football->GetActorLocation();
    Publish(Event);
}

void UPSLooseBallSubsystem::BlowDead(const FVector& Spot, bool bKickingTeam, bool bTouchdown, const APSPlayerPawn* Holder)
{
    Stage = EPSLooseBallStage::Dead;
    Returner.Reset();

    FPSTelemetryLooseBallEvent Event;
    Event.Kind = EPSLooseBallEventKind::Dead;
    Event.PlayerName = Holder ? Holder->GetAttributes().DisplayName : FString();
    Event.bKickingTeam = bKickingTeam;
    Event.bTouchdown = bTouchdown;
    Event.YardLine = FMath::Clamp(FMath::RoundToInt(PSField::CentimetresToYards(Spot.X - GetGoalLineX())), 0, FMath::RoundToInt(PSField::GetDimensions().FieldLengthYards));
    Event.Location = Spot;
    Publish(Event);
}

float UPSLooseBallSubsystem::GetGoalLineX() const
{
    return LineOfScrimmage.X - PSField::YardsToCentimetres(SnapYardLine);
}

bool UPSLooseBallSubsystem::GetChaseTarget(const APSPlayerPawn* Player, FVector& OutTarget) const
{
    if (!Player)
    {
        return false;
    }
    if (Stage == EPSLooseBallStage::Loose)
    {
        const APSBall* Football = Ball.Get();
        if (Football && FVector::Dist2D(Player->GetActorLocation(), Football->GetActorLocation()) <= Tuning.ChaseRadius)
        {
            OutTarget = Football->GetActorLocation();
            return true;
        }
        return false;
    }
    if (Stage == EPSLooseBallStage::Returning)
    {
        // The kicking team runs the returner down; his own side's work is done.
        const APSPlayerPawn* Carrier = Returner.Get();
        if (Carrier && Player->TeamSide != Carrier->TeamSide && FVector::Dist2D(Player->GetActorLocation(), Carrier->GetActorLocation()) <= Tuning.ChaseRadius)
        {
            OutTarget = Carrier->GetActorLocation();
            return true;
        }
    }
    return false;
}

void UPSLooseBallSubsystem::Publish(FPSTelemetryLooseBallEvent& Event)
{
    Event.KickType = KickType;
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->PublishLooseBall(Event);
    }
}
