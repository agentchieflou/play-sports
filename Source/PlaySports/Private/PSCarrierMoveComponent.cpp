#include "PSCarrierMoveComponent.h"
#include "PSDataIngestion.h"
#include "PSPlayerPawn.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Misc/Paths.h"

UPSCarrierMoveComponent::UPSCarrierMoveComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

FString UPSCarrierMoveComponent::GetDefaultCatalogPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/carrier_moves.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSCarrierMoveCatalog& UPSCarrierMoveComponent::GetCatalog()
{
    if (!bCatalogLoaded)
    {
        LoadCatalogFromJson(GetDefaultCatalogPath());
    }
    return Catalog;
}

bool UPSCarrierMoveComponent::LoadCatalogFromJson(const FString& JsonFilePath)
{
    bCatalogLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSCarrierMoveCatalog Loaded;
    if (!Ingestion->LoadCarrierMovesFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCarrierMoveComponent: Could not load the move set from %s; no moves."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateCatalog(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCarrierMoveComponent: %s"), *Problem);
    }
    Catalog = Loaded;
    return true;
}

TArray<FString> UPSCarrierMoveComponent::ValidateCatalog(const FPSCarrierMoveCatalog& InCatalog)
{
    TArray<FString> Problems;
    TSet<EPSCarrierMove> Moves;
    TSet<FName> Actions;
    for (int32 Index = 0; Index < InCatalog.Moves.Num(); ++Index)
    {
        const FPSCarrierMoveDef& Def = InCatalog.Moves[Index];
        const FString Where = FString::Printf(TEXT("Moves[%d]"), Index);
        if (Def.Move == EPSCarrierMove::None)
        {
            Problems.Add(Where + TEXT(": Move is None"));
        }
        else if (Moves.Contains(Def.Move))
        {
            Problems.Add(Where + TEXT(": the move is defined twice"));
        }
        Moves.Add(Def.Move);
        if (Def.ActionId.IsNone() || Actions.Contains(Def.ActionId))
        {
            Problems.Add(Where + TEXT(": ActionId is empty or used twice"));
        }
        Actions.Add(Def.ActionId);
        if (Def.Attribute != TEXT("Agility") && Def.Attribute != TEXT("Strength") && Def.Attribute != TEXT("Speed"))
        {
            Problems.Add(Where + TEXT(": Attribute must be Agility, Strength or Speed"));
        }
        if (Def.MinAttribute < 0.f || Def.MinAttribute > 100.f || Def.WindowSeconds < 0.f || Def.CooldownSeconds < 0.f || Def.StaminaCost < 0.f
            || Def.TackleChanceScale < 0.f || Def.SpeedRetained < 0.f || Def.SpeedRetained > 1.f || Def.LateralSpeed < 0.f || Def.ForwardSpeed < 0.f)
        {
            Problems.Add(Where + TEXT(": a number is out of range"));
        }
    }
    return Problems;
}

EPSCarrierMove UPSCarrierMoveComponent::FindMoveForAction(FName ActionId)
{
    for (const FPSCarrierMoveDef& Def : GetCatalog().Moves)
    {
        if (Def.ActionId == ActionId)
        {
            return Def.Move;
        }
    }
    return EPSCarrierMove::None;
}

const FPSCarrierMoveDef* UPSCarrierMoveComponent::FindDef(EPSCarrierMove Move)
{
    return GetCatalog().Moves.FindByPredicate([Move](const FPSCarrierMoveDef& Def) { return Def.Move == Move; });
}

float UPSCarrierMoveComponent::GetRating(const APSPlayerPawn* Carrier, FName Attribute)
{
    const FPlayerAttributes& Attributes = Carrier->GetAttributes();
    if (Attribute == TEXT("Strength"))
    {
        return Attributes.Strength;
    }
    if (Attribute == TEXT("Speed"))
    {
        return Attributes.Speed;
    }
    return Attributes.Agility;
}

bool UPSCarrierMoveComponent::TryMove(EPSCarrierMove Move, FVector2D Stick)
{
    APSPlayerPawn* Carrier = GetCarrier();
    const FPSCarrierMoveDef* Def = FindDef(Move);
    if (!Carrier || !Def || !Carrier->HasPossession() || bGaveUp)
    {
        return false;
    }
    const float Rating = FMath::Clamp(GetRating(Carrier, Def->Attribute), 0.f, 100.f);
    const float* Ready = ReadyAt.Find(Move);
    if (Rating < Def->MinAttribute || (Ready && Clock < *Ready) || Carrier->CurrentStamina < Def->StaminaCost)
    {
        return false;
    }

    Carrier->ApplyFatigue(Def->StaminaCost);
    ActiveDef = *Def;
    ActiveMove = Move;
    ActiveRating = Rating;
    ActiveUntil = Clock + Def->WindowSeconds;
    ReadyAt.Add(Move, Clock + Def->CooldownSeconds);
    bGaveUp = Def->bGivesUp;

    // The move changes the carrier's momentum now; the movement rules carry it on.
    if (UFloatingPawnMovement* Movement = Carrier->GetFloatingMovementComponent())
    {
        FVector Forward = Carrier->GetActorForwardVector();
        Forward.Z = 0.f;
        Forward = Forward.GetSafeNormal();
        const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
        const float Side = Stick.X < 0.f ? -1.f : 1.f;
        Movement->Velocity = Movement->Velocity * Def->SpeedRetained + Forward * Def->ForwardSpeed + Right * (Side * Def->LateralSpeed);
        if (Def->bGivesUp)
        {
            Movement->StopActiveMovement();
        }
    }
    return true;
}

bool UPSCarrierMoveComponent::IsMoveActive() const
{
    return ActiveMove != EPSCarrierMove::None && Clock < ActiveUntil;
}

float UPSCarrierMoveComponent::GetTackleChanceMultiplier() const
{
    if (!IsMoveActive())
    {
        return 1.f;
    }
    return FMath::Lerp(1.f, ActiveDef.TackleChanceScale, ActiveRating / 100.f);
}

void UPSCarrierMoveComponent::ResetMoves()
{
    ActiveMove = EPSCarrierMove::None;
    ActiveUntil = -1.f;
    ReadyAt.Reset();
    bGaveUp = false;
}

void UPSCarrierMoveComponent::AdvanceTime(float DeltaSeconds)
{
    Clock += DeltaSeconds;
    // A move only means something with the ball: losing it ends the move and the slide.
    const APSPlayerPawn* Carrier = GetCarrier();
    if ((ActiveMove != EPSCarrierMove::None || bGaveUp) && Carrier && !Carrier->HasPossession())
    {
        ResetMoves();
    }
}

void UPSCarrierMoveComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    AdvanceTime(DeltaTime);
}

APSPlayerPawn* UPSCarrierMoveComponent::GetCarrier() const
{
    return Cast<APSPlayerPawn>(GetOwner());
}
