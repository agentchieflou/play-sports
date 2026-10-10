#include "PSRushMoveComponent.h"
#include "PSDataIngestion.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"

namespace PSRushMovePrivate
{
    bool IsKnownAttribute(FName Attribute)
    {
        return Attribute == TEXT("Strength") || Attribute == TEXT("Agility") || Attribute == TEXT("Speed") || Attribute == TEXT("Awareness");
    }

    FString MoveName(EPSRushMove Move)
    {
        return StaticEnum<EPSRushMove>()->GetNameStringByValue(static_cast<int64>(Move));
    }

    FString ResponseName(EPSBlockResponse Response)
    {
        return StaticEnum<EPSBlockResponse>()->GetNameStringByValue(static_cast<int64>(Response));
    }
}

float PSRushMoves::GetRating(const FPlayerAttributes& Attributes, FName Attribute)
{
    if (Attribute == TEXT("Strength"))
    {
        return Attributes.Strength;
    }
    if (Attribute == TEXT("Agility"))
    {
        return Attributes.Agility;
    }
    if (Attribute == TEXT("Speed"))
    {
        return Attributes.Speed;
    }
    if (Attribute == TEXT("Awareness"))
    {
        return Attributes.Awareness;
    }
    return 0.f;
}

float PSRushMoves::ComputeWinChance(const FPSRushMoveDef& Def, const FPlayerAttributes& Rusher, const FPlayerAttributes& Blocker,
    EPSBlockResponse LastResponse, bool bDoubleTeamed, const FPSRushMoveCatalog& Catalog)
{
    if (Def.bDoubleTeamOnly && !bDoubleTeamed)
    {
        return 0.f;
    }
    const float Rating = FMath::Clamp(GetRating(Rusher, Def.Attribute), 0.f, 100.f);
    if (Rating < Def.MinAttribute)
    {
        return 0.f;
    }
    const float Resistance = FMath::Clamp(GetRating(Blocker, Def.BlockerAttribute), 0.f, 100.f);
    float Chance = Def.BaseWinChance + (Rating - Resistance) * Def.RatingScalar;
    if (Def.Counters != EPSBlockResponse::None && Def.Counters == LastResponse)
    {
        Chance += Catalog.CounterBonus;
    }
    Chance = FMath::Clamp(Chance, Catalog.WinChanceMin, Catalog.WinChanceMax);
    if (bDoubleTeamed)
    {
        Chance *= Def.DoubleTeamWinScale;
    }
    return Chance;
}

float PSRushMoves::ScoreMove(float WinChance, const FPSRushMoveRecord& Record, float PriorWeight)
{
    const float Denominator = Record.Attempts + PriorWeight;
    if (Denominator <= 0.f)
    {
        return WinChance;
    }
    return (Record.Wins + PriorWeight * WinChance) / Denominator;
}

TArray<FString> PSRushMoves::ValidateCatalog(const FPSRushMoveCatalog& Catalog)
{
    TArray<FString> Problems;
    TSet<EPSRushMove> Seen;
    for (int32 Index = 0; Index < Catalog.RushMoves.Num(); ++Index)
    {
        const FPSRushMoveDef& Def = Catalog.RushMoves[Index];
        const FString Where = FString::Printf(TEXT("RushMoves[%d]"), Index);
        if (Def.Move == EPSRushMove::None)
        {
            Problems.Add(Where + TEXT(": Move is None"));
        }
        else if (Seen.Contains(Def.Move))
        {
            Problems.Add(Where + TEXT(": the move is defined twice"));
        }
        Seen.Add(Def.Move);
        if (!PSRushMovePrivate::IsKnownAttribute(Def.Attribute) || !PSRushMovePrivate::IsKnownAttribute(Def.BlockerAttribute))
        {
            Problems.Add(Where + TEXT(": Attribute and BlockerAttribute must be Strength, Agility, Speed or Awareness"));
        }
        if (Def.Response == EPSBlockResponse::None)
        {
            Problems.Add(Where + TEXT(": Response must name the blocker response that stops the move"));
        }
        else if (Def.Counters == Def.Response)
        {
            Problems.Add(Where + TEXT(": a move can't counter the response that stops it"));
        }
        if (Def.MinAttribute < 0.f || Def.MinAttribute > 100.f || Def.BaseWinChance < 0.f || Def.BaseWinChance > 1.f || Def.RatingScalar < 0.f
            || Def.MoveSeconds <= 0.f || Def.StaminaCost < 0.f || Def.WinBurstSpeed < 0.f || Def.DoubleTeamWinScale < 0.f || Def.DoubleTeamWinScale > 1.f)
        {
            Problems.Add(Where + TEXT(": a number is out of range"));
        }
    }
    if (Catalog.FirstMoveSeconds < 0.f || Catalog.RecoverySeconds < 0.f || Catalog.CounterBonus < 0.f || Catalog.CounterBonus > 1.f
        || Catalog.WinChanceMin < 0.f || Catalog.WinChanceMin > Catalog.WinChanceMax || Catalog.WinChanceMax > 1.f
        || Catalog.HistoryPriorWeight <= 0.f || Catalog.DoubleTeamRadius < 0.f)
    {
        Problems.Add(TEXT("a catalog setting is out of range"));
    }
    return Problems;
}

UPSRushMoveComponent::UPSRushMoveComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

FString UPSRushMoveComponent::GetDefaultCatalogPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/pass_rush_moves.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSRushMoveCatalog& UPSRushMoveComponent::GetCatalog()
{
    if (!bCatalogLoaded)
    {
        LoadCatalogFromJson(GetDefaultCatalogPath());
    }
    return Catalog;
}

bool UPSRushMoveComponent::LoadCatalogFromJson(const FString& JsonFilePath)
{
    bCatalogLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSRushMoveCatalog Loaded;
    if (!Ingestion->LoadRushMovesFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSRushMoveComponent: Could not load the move library from %s; no moves."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : PSRushMoves::ValidateCatalog(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSRushMoveComponent: %s"), *Problem);
    }
    Catalog = Loaded;
    return true;
}

void UPSRushMoveComponent::SetCatalog(const FPSRushMoveCatalog& InCatalog)
{
    Catalog = InCatalog;
    bCatalogLoaded = true;
}

void UPSRushMoveComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    TickRush(DeltaTime);
}

void UPSRushMoveComponent::TickRush(float DeltaSeconds)
{
    Clock += DeltaSeconds;
    APSPlayerPawn* Blocker = GetBlocker();
    if (!Blocker || !IsRushing())
    {
        EndEngagementState();
        return;
    }
    if (CurrentBlocker.Get() != Blocker)
    {
        // A new man on him: the rush starts over against this blocker.
        EndEngagementState();
        CurrentBlocker = Blocker;
        NextMoveAt = Clock + GetCatalog().FirstMoveSeconds;
    }

    if (ActiveMove != EPSRushMove::None)
    {
        if (Clock >= ResolveAt)
        {
            ResolveActiveMove(RollStream.FRand());
        }
        return;
    }
    if (Clock >= NextMoveAt)
    {
        const EPSRushMove Move = ChooseMove();
        if (Move != EPSRushMove::None)
        {
            StartMove(Move);
        }
        else
        {
            // Nothing he can do right now (no move rated, or no stamina): look again shortly.
            NextMoveAt = Clock + GetCatalog().RecoverySeconds;
        }
    }
}

EPSRushMove UPSRushMoveComponent::ChooseMove()
{
    const APSPlayerPawn* Self = GetSelf();
    const APSPlayerPawn* Blocker = GetBlocker();
    if (!Self || !Blocker)
    {
        return EPSRushMove::None;
    }
    const FPSRushMoveCatalog& Library = GetCatalog();
    bDoubleTeamed = ReadDoubleTeam(Self);
    const FPlayerAttributes RusherAttributes = Self->GetAttributes();
    const FPlayerAttributes BlockerAttributes = Blocker->GetAttributes();
    const FMatchup* Matchup = Matchups.Find(BlockerAttributes.PlayerId);

    // The best score wins; on a tie, the move listed first.
    EPSRushMove Best = EPSRushMove::None;
    float BestScore = 0.f;
    for (const FPSRushMoveDef& Def : Library.RushMoves)
    {
        if (Self->CurrentStamina < Def.StaminaCost)
        {
            continue;
        }
        const float Chance = PSRushMoves::ComputeWinChance(Def, RusherAttributes, BlockerAttributes, LastResponse, bDoubleTeamed, Library);
        if (Chance <= 0.f)
        {
            continue;
        }
        const FPSRushMoveRecord* Record = Matchup ? Matchup->Records.Find(Def.Move) : nullptr;
        const float Score = PSRushMoves::ScoreMove(Chance, Record ? *Record : FPSRushMoveRecord(), Library.HistoryPriorWeight);
        if (Score > BestScore)
        {
            BestScore = Score;
            Best = Def.Move;
        }
    }
    return Best;
}

void UPSRushMoveComponent::StartMove(EPSRushMove Move)
{
    const FPSRushMoveDef* Def = FindDef(Move);
    APSPlayerPawn* Self = GetSelf();
    const APSPlayerPawn* Blocker = GetBlocker();
    if (!Def || !Self || !Blocker)
    {
        return;
    }
    ActiveWinChance = PSRushMoves::ComputeWinChance(*Def, Self->GetAttributes(), Blocker->GetAttributes(), LastResponse, bDoubleTeamed, GetCatalog());
    ActiveMove = Move;
    ResolveAt = Clock + Def->MoveSeconds;
    Self->ApplyFatigue(Def->StaminaCost);
}

bool UPSRushMoveComponent::ResolveActiveMove(float Roll)
{
    const EPSRushMove Move = ActiveMove;
    ActiveMove = EPSRushMove::None;
    const FPSRushMoveDef* Def = FindDef(Move);
    APSPlayerPawn* Self = GetSelf();
    APSPlayerPawn* Blocker = GetBlocker();
    if (Move == EPSRushMove::None || !Def || !Self || !Blocker)
    {
        return false;
    }

    const bool bWon = Roll < ActiveWinChance;
    FPSRushMoveRecord& Record = Matchups.FindOrAdd(Blocker->GetAttributes().PlayerId).Records.FindOrAdd(Move);
    ++Record.Attempts;
    if (bWon)
    {
        ++Record.Wins;
    }

    FPSTelemetryPassRushEvent Event;
    Event.RusherName = Self->GetAttributes().DisplayName;
    Event.BlockerName = Blocker->GetAttributes().DisplayName;
    Event.Move = PSRushMovePrivate::MoveName(Move);
    Event.WinChance = ActiveWinChance;
    Event.bWon = bWon;
    Event.bDoubleTeamed = bDoubleTeamed;

    if (bWon)
    {
        // Free of the block, and past him toward the passer.
        Self->bIsEngaged = false;
        Self->EngagedOpponent = nullptr;
        if (Blocker->EngagedOpponent == Self)
        {
            Blocker->bIsEngaged = false;
            Blocker->EngagedOpponent = nullptr;
        }
        FVector Toward(-1.f, 0.f, 0.f);
        for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
        {
            if (It->TeamSide == EPSTeamSide::Offense && It->GetAttributes().Role == EPlayerRole::Quarterback)
            {
                Toward = It->GetActorLocation() - Self->GetActorLocation();
                Toward.Z = 0.f;
                Toward = Toward.GetSafeNormal();
                break;
            }
        }
        if (UFloatingPawnMovement* Movement = Self->GetFloatingMovementComponent())
        {
            Movement->Velocity += Toward * Def->WinBurstSpeed;
        }
        EndEngagementState();
    }
    else
    {
        LastResponse = Def->Response;
        Event.Response = PSRushMovePrivate::ResponseName(Def->Response);
        NextMoveAt = Clock + GetCatalog().RecoverySeconds;
    }

    if (UPSTelemetryBus* Bus = GetWorld() ? GetWorld()->GetSubsystem<UPSTelemetryBus>() : nullptr)
    {
        Bus->PublishPassRushMove(Event);
    }
    return bWon;
}

FPSRushMoveRecord UPSRushMoveComponent::GetMatchupRecord(FName BlockerId, EPSRushMove Move) const
{
    const FMatchup* Matchup = Matchups.Find(BlockerId);
    const FPSRushMoveRecord* Record = Matchup ? Matchup->Records.Find(Move) : nullptr;
    return Record ? *Record : FPSRushMoveRecord();
}

void UPSRushMoveComponent::ResetMatchupHistory()
{
    Matchups.Reset();
}

void UPSRushMoveComponent::EndEngagementState()
{
    ActiveMove = EPSRushMove::None;
    LastResponse = EPSBlockResponse::None;
    CurrentBlocker.Reset();
    bDoubleTeamed = false;
    ResolveAt = -1.f;
    NextMoveAt = -1.f;
}

bool UPSRushMoveComponent::ReadDoubleTeam(const APSPlayerPawn* Self) const
{
    // His own blocker, any lineman engaged on him, and any free lineman at his side.
    const float Radius = Catalog.DoubleTeamRadius;
    int32 Blockers = 0;
    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        const APSPlayerPawn* Pawn = *It;
        if (Pawn == Self || Pawn->TeamSide != EPSTeamSide::Offense || Pawn->GetAttributes().Role != EPlayerRole::OffensiveLineman)
        {
            continue;
        }
        const bool bOnHim = Pawn == Self->EngagedOpponent
            || (Pawn->bIsEngaged && Pawn->EngagedOpponent == Self)
            || (!Pawn->bIsEngaged && FVector::Dist2D(Pawn->GetActorLocation(), Self->GetActorLocation()) <= Radius);
        if (bOnHim)
        {
            ++Blockers;
        }
    }
    return Blockers >= 2;
}

const FPSRushMoveDef* UPSRushMoveComponent::FindDef(EPSRushMove Move)
{
    return GetCatalog().RushMoves.FindByPredicate([Move](const FPSRushMoveDef& Def) { return Def.Move == Move; });
}

APSDefenseController* UPSRushMoveComponent::GetDefenseController() const
{
    return Cast<APSDefenseController>(GetOwner());
}

APSPlayerPawn* UPSRushMoveComponent::GetSelf() const
{
    const APSDefenseController* Controller = GetDefenseController();
    return Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
}

bool UPSRushMoveComponent::IsRushing() const
{
    const APSDefenseController* Controller = GetDefenseController();
    const UPSDefenderAIComponent* DefenderAI = Controller ? Controller->GetDefenderAI() : nullptr;
    const EPSDefenderAction Action = DefenderAI ? DefenderAI->GetAction() : EPSDefenderAction::Idle;
    return Action == EPSDefenderAction::Rush || Action == EPSDefenderAction::Contain;
}

APSPlayerPawn* UPSRushMoveComponent::GetBlocker() const
{
    const APSPlayerPawn* Self = GetSelf();
    APSPlayerPawn* Blocker = Self && Self->bIsEngaged ? Self->EngagedOpponent : nullptr;
    return Blocker && Blocker->TeamSide == EPSTeamSide::Offense ? Blocker : nullptr;
}
