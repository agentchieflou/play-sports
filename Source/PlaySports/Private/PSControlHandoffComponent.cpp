#include "PSControlHandoffComponent.h"
#include "PSBall.h"
#include "PSDataIngestion.h"
#include "PSHealthComponent.h"
#include "PSInputBufferComponent.h"
#include "PSPlayContextComponent.h"
#include "PSPlayerController.h"
#include "PSPossessionComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"

namespace PSControlHandoffPrivate
{
    bool IsDowned(const APSPlayerPawn* Pawn)
    {
        const UPSHealthComponent* Health = Pawn ? Pawn->GetHealthComponent() : nullptr;
        return Health && Health->IsDowned();
    }

    bool HasBall(const APSPlayerPawn* Pawn)
    {
        // The possession component is the authority on who has the ball (rule 6).
        const UPSPossessionComponent* Possession = Pawn ? Pawn->GetPossessionComponent() : nullptr;
        return Possession && Possession->HasPossession();
    }
}

UPSControlHandoffComponent::UPSControlHandoffComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

FString UPSControlHandoffComponent::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/control_handoff.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FControlHandoffTuningRow& UPSControlHandoffComponent::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSControlHandoffComponent::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FControlHandoffTuningRow Loaded;
    if (!Ingestion->LoadControlHandoffTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSControlHandoffComponent: Could not load switch tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSControlHandoffComponent: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

TArray<FString> UPSControlHandoffComponent::ValidateTuning(const FControlHandoffTuningRow& InTuning, const FPSInputCatalog* Catalog)
{
    TArray<FString> Problems;
    if (InTuning.CycleWindowSeconds < 0.f)
    {
        Problems.Add(FString::Printf(TEXT("CycleWindowSeconds (%g) must be 0 or more"), InTuning.CycleWindowSeconds));
    }
    if (InTuning.PickLeftAction.IsNone() || InTuning.PickRightAction.IsNone() || InTuning.PickLeftAction == InTuning.PickRightAction)
    {
        Problems.Add(TEXT("PickLeftAction and PickRightAction must be two different actions"));
    }
    if (Catalog)
    {
        for (const FName ActionId : { InTuning.PickLeftAction, InTuning.PickRightAction })
        {
            const FPSInputActionDef* Action = Catalog->Actions.FindByPredicate(
                [ActionId](const FPSInputActionDef& Candidate) { return Candidate.ActionId == ActionId; });
            if (!Action || Action->ValueType != EInputActionValueType::Boolean || !Action->Contexts.Contains(FName(TEXT("PreSnap"))))
            {
                Problems.Add(FString::Printf(TEXT("'%s' is not a Boolean action in the input catalog's PreSnap context"), *ActionId.ToString()));
            }
        }
    }
    return Problems;
}

void UPSControlHandoffComponent::BeginPlay()
{
    Super::BeginPlay();
    GetTuning();
    BindToController();
}

void UPSControlHandoffComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    APSPlayerController* Controller = GetPlayerController();
    if (UPSInputBufferComponent* Buffer = Controller ? Controller->GetInputBufferComponent() : nullptr)
    {
        Buffer->OnActionPressed.RemoveDynamic(this, &UPSControlHandoffComponent::HandleActionPressed);
    }
    Super::EndPlay(EndPlayReason);
}

void UPSControlHandoffComponent::BindToController()
{
    APSPlayerController* Controller = GetPlayerController();
    UPSInputBufferComponent* Buffer = Controller ? Controller->GetInputBufferComponent() : nullptr;
    if (!Buffer)
    {
        return;
    }
    Buffer->BindToController();
    Buffer->OnActionPressed.AddUniqueDynamic(this, &UPSControlHandoffComponent::HandleActionPressed);
}

void UPSControlHandoffComponent::HandleActionPressed(FName ActionId, float HeldSeconds)
{
    const FControlHandoffTuningRow& Settings = GetTuning();
    if (ActionId == Settings.PickLeftAction)
    {
        PickAcross(-1);
    }
    else if (ActionId == Settings.PickRightAction)
    {
        PickAcross(1);
    }
}

APSPlayerController* UPSControlHandoffComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}

bool UPSControlHandoffComponent::IsPlayLive() const
{
    const APSPlayerController* Controller = GetPlayerController();
    const UPSPlayContextComponent* PlayContext = Controller ? Controller->GetPlayContextComponent() : nullptr;
    return PlayContext && PlayContext->IsPlayLive();
}

TArray<APSPlayerPawn*> UPSControlHandoffComponent::GetPickableTeammates() const
{
    TArray<APSPlayerPawn*> Teammates;
    const APSPlayerController* Controller = GetPlayerController();
    UWorld* World = GetWorld();
    if (!Controller || !World)
    {
        return Teammates;
    }
    const APSPlayerPawn* Current = Cast<APSPlayerPawn>(Controller->GetPawn());
    const EPSTeamSide Side = Current ? Current->TeamSide : Controller->HumanSide;
    for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
    {
        APSPlayerPawn* Candidate = *It;
        // Another human's player is never offered (Epic 107).
        const APlayerController* OtherHuman = Cast<APlayerController>(Candidate->GetController());
        if (IsValid(Candidate) && Candidate->TeamSide == Side && !PSControlHandoffPrivate::IsDowned(Candidate) && (!OtherHuman || OtherHuman == Controller))
        {
            Teammates.Add(Candidate);
        }
    }
    return Teammates;
}

TArray<APSPlayerPawn*> UPSControlHandoffComponent::RankSwitchCandidates(const FVector& BallLocation) const
{
    const APSPlayerController* Controller = GetPlayerController();
    const APSPlayerPawn* Current = Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;

    TArray<APSPlayerPawn*> Ranked = GetPickableTeammates();
    APSPlayerPawn* const* Carrier = Ranked.FindByPredicate([](const APSPlayerPawn* Candidate) { return PSControlHandoffPrivate::HasBall(Candidate); });
    APSPlayerPawn* BallCarrier = Carrier ? *Carrier : nullptr;

    Ranked.RemoveAll([Current, BallCarrier](const APSPlayerPawn* Candidate) { return Candidate == Current || Candidate == BallCarrier; });
    Ranked.Sort([&BallLocation](const APSPlayerPawn& A, const APSPlayerPawn& B)
    {
        return FVector::DistSquared(A.GetActorLocation(), BallLocation) < FVector::DistSquared(B.GetActorLocation(), BallLocation);
    });
    if (BallCarrier)
    {
        Ranked.Insert(BallCarrier, 0);
    }
    return Ranked;
}

bool UPSControlHandoffComponent::CycleSwitch(const FVector& BallLocation, double Now)
{
    APSPlayerController* Controller = GetPlayerController();
    if (!Controller)
    {
        return false;
    }
    APSPlayerPawn* Current = Cast<APSPlayerPawn>(Controller->GetPawn());

    const bool bContinuing = Now - LastSwitchAt <= GetTuning().CycleWindowSeconds && CycleOrder.Num() > 0;
    LastSwitchAt = Now;

    if (!bContinuing)
    {
        CycleOrder.Reset();
        for (APSPlayerPawn* Candidate : RankSwitchCandidates(BallLocation))
        {
            CycleOrder.Add(Candidate);
        }
        // The player the run started from comes last, so pressing on comes back round to him.
        if (Current && !CycleOrder.Contains(Current))
        {
            CycleOrder.Add(Current);
        }
        CycleIndex = INDEX_NONE;
    }

    // The side's ball carrier always gets control, and nobody switches away from him.
    APSPlayerPawn* First = CycleOrder.Num() > 0 ? CycleOrder[0].Get() : nullptr;
    if (First && PSControlHandoffPrivate::HasBall(First) && First->TeamSide == (Current ? Current->TeamSide : Controller->HumanSide))
    {
        CycleIndex = 0;
        return First != Current && Controller->TakeControlOf(First);
    }

    // Held to his player until the whistle (Epic 107's versus rule): only the carrier above.
    if (!bSwitchDuringPlay && IsPlayLive())
    {
        return false;
    }

    // Otherwise on round the order, skipping whoever is now controlled or downed.
    for (int32 Step = 1; Step <= CycleOrder.Num(); ++Step)
    {
        const int32 Index = (CycleIndex + Step + CycleOrder.Num()) % CycleOrder.Num();
        APSPlayerPawn* Candidate = CycleOrder[Index].Get();
        if (IsValid(Candidate) && Candidate != Current && !PSControlHandoffPrivate::IsDowned(Candidate))
        {
            CycleIndex = Index;
            return Controller->TakeControlOf(Candidate);
        }
    }
    return false;
}

bool UPSControlHandoffComponent::SwitchPlayer()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }
    TActorIterator<APSBall> BallIt(World);
    if (!BallIt)
    {
        return false;
    }
    return CycleSwitch(BallIt->GetActorLocation(), World->GetTimeSeconds());
}

bool UPSControlHandoffComponent::PickAcross(int32 Direction)
{
    APSPlayerController* Controller = GetPlayerController();
    APSPlayerPawn* Current = Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
    if (!Current || Direction == 0 || !bPreSnapPicks || IsPlayLive())
    {
        return false;
    }

    // Facing upfield (+X), left is -Y: the nearest teammate on that side across the field,
    // the nearer one upfield-and-back breaking a tie.
    const FVector From = Current->GetActorLocation();
    const float Sign = Direction < 0 ? -1.f : 1.f;
    APSPlayerPawn* Best = nullptr;
    float BestAcross = TNumericLimits<float>::Max();
    float BestDepth = TNumericLimits<float>::Max();
    for (APSPlayerPawn* Candidate : GetPickableTeammates())
    {
        if (Candidate == Current)
        {
            continue;
        }
        const FVector Offset = Candidate->GetActorLocation() - From;
        const float Across = static_cast<float>(Offset.Y) * Sign;
        const float Depth = FMath::Abs(static_cast<float>(Offset.X));
        if (Across <= KINDA_SMALL_NUMBER)
        {
            continue;
        }
        if (Across < BestAcross - KINDA_SMALL_NUMBER || (FMath::IsNearlyEqual(Across, BestAcross) && Depth < BestDepth))
        {
            Best = Candidate;
            BestAcross = Across;
            BestDepth = Depth;
        }
    }
    return Best && Controller->TakeControlOf(Best);
}

bool UPSControlHandoffComponent::PickPlayer(FName PlayerId)
{
    APSPlayerController* Controller = GetPlayerController();
    if (!Controller || PlayerId.IsNone() || !bPreSnapPicks || IsPlayLive())
    {
        return false;
    }
    for (APSPlayerPawn* Candidate : GetPickableTeammates())
    {
        if (Candidate->GetAttributes().PlayerId == PlayerId)
        {
            return Controller->TakeControlOf(Candidate);
        }
    }
    return false;
}
