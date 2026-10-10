#include "PSInputBufferComponent.h"
#include "PSDataIngestion.h"
#include "PSInputConfig.h"
#include "PSPlayerController.h"
#include "Misc/Paths.h"

UPSInputBufferComponent::UPSInputBufferComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

FString UPSInputBufferComponent::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/input_buffer.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FInputBufferTuningRow& UPSInputBufferComponent::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSInputBufferComponent::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FInputBufferTuningRow Loaded;
    if (!Ingestion->LoadInputBufferTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSInputBufferComponent: Could not load buffer tuning from %s; nothing is buffered."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSInputBufferComponent: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

TArray<FString> UPSInputBufferComponent::ValidateTuning(const FInputBufferTuningRow& InTuning, const FPSInputCatalog* Catalog)
{
    TArray<FString> Problems;
    if (InTuning.MaxQueued < 1)
    {
        Problems.Add(FString::Printf(TEXT("MaxQueued (%d) must be 1 or more"), InTuning.MaxQueued));
    }
    TSet<FName> Seen;
    for (int32 Index = 0; Index < InTuning.Actions.Num(); ++Index)
    {
        const FPSInputBufferDef& Def = InTuning.Actions[Index];
        const FString Where = FString::Printf(TEXT("Actions[%d]"), Index);
        if (Def.ActionId.IsNone() || Seen.Contains(Def.ActionId))
        {
            Problems.Add(Where + TEXT(": ActionId is empty or used twice"));
        }
        Seen.Add(Def.ActionId);
        if (Def.BufferSeconds < 0.f)
        {
            Problems.Add(Where + TEXT(": BufferSeconds must be 0 or more"));
        }
        if (Catalog && !Def.ActionId.IsNone())
        {
            const FPSInputActionDef* Action = Catalog->Actions.FindByPredicate(
                [&Def](const FPSInputActionDef& Candidate) { return Candidate.ActionId == Def.ActionId; });
            if (!Action || Action->ValueType != EInputActionValueType::Boolean)
            {
                Problems.Add(Where + FString::Printf(TEXT(": '%s' is not a Boolean action in the input catalog"), *Def.ActionId.ToString()));
            }
        }
    }
    return Problems;
}

void UPSInputBufferComponent::BeginPlay()
{
    Super::BeginPlay();
    GetTuning();
    BindToController();
}

void UPSInputBufferComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (APSPlayerController* Controller = GetPlayerController())
    {
        Controller->OnCatalogActionStarted.RemoveDynamic(this, &UPSInputBufferComponent::PressAction);
        Controller->OnCatalogActionCompleted.RemoveDynamic(this, &UPSInputBufferComponent::ReleaseAction);
    }
    BusyChecks.Reset();
    Flush();
    Super::EndPlay(EndPlayReason);
}

void UPSInputBufferComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    AdvanceTime(DeltaTime);
}

void UPSInputBufferComponent::BindToController()
{
    if (APSPlayerController* Controller = GetPlayerController())
    {
        Controller->OnCatalogActionStarted.AddUniqueDynamic(this, &UPSInputBufferComponent::PressAction);
        Controller->OnCatalogActionCompleted.AddUniqueDynamic(this, &UPSInputBufferComponent::ReleaseAction);
    }
}

void UPSInputBufferComponent::AddBusyCheck(FPSInputBusyCheck Check)
{
    if (Check.IsBound())
    {
        BusyChecks.Add(MoveTemp(Check));
    }
}

void UPSInputBufferComponent::RemoveBusyChecks(const UObject* CheckOwner)
{
    BusyChecks.RemoveAll([CheckOwner](const FPSInputBusyCheck& Check) { return !Check.IsBound() || Check.IsBoundToObject(CheckOwner); });
}

const FPSInputBufferDef* UPSInputBufferComponent::FindDef(FName ActionId)
{
    return GetTuning().Actions.FindByPredicate([ActionId](const FPSInputBufferDef& Def) { return Def.ActionId == ActionId; });
}

bool UPSInputBufferComponent::IsBuffered(FName ActionId)
{
    return FindDef(ActionId) != nullptr;
}

bool UPSInputBufferComponent::IsBusy(FName ActionId)
{
    for (const FPSInputBusyCheck& Check : BusyChecks)
    {
        if (Check.IsBound() && Check.Execute(ActionId))
        {
            return true;
        }
    }
    return false;
}

bool UPSInputBufferComponent::IsActionLive(FName ActionId) const
{
    APSPlayerController* Controller = GetPlayerController();
    const UPSInputConfig* Config = Controller ? Controller->GetInputConfig() : nullptr;
    if (!Config)
    {
        return true;
    }
    const FPSInputActionDef* Action = Config->Catalog.Actions.FindByPredicate(
        [ActionId](const FPSInputActionDef& Candidate) { return Candidate.ActionId == ActionId; });
    if (!Action)
    {
        return true;
    }
    for (const FName& ContextId : Action->Contexts)
    {
        if (Controller->IsInputContextActive(ContextId))
        {
            return true;
        }
    }
    return false;
}

float UPSInputBufferComponent::GetKeySecondsDown(const FKey& Key) const
{
    if (KeyStateQuery.IsBound())
    {
        return KeyStateQuery.Execute(Key);
    }
    const APlayerController* Controller = Cast<APlayerController>(GetOwner());
    return Controller && Controller->IsInputKeyDown(Key) ? Controller->GetInputKeyTimeDown(Key) : -1.f;
}

void UPSInputBufferComponent::PressAction(FName ActionId)
{
    PressActionAt(ActionId, Clock);
}

void UPSInputBufferComponent::PressActionAt(FName ActionId, float PressedAt)
{
    if (ActionId.IsNone())
    {
        return;
    }

    // A new press replaces an earlier one of the same action that is still waiting.
    Queue.Remove(ActionId);
    FPSBufferedPress& Press = Held.Add(ActionId);
    Press.PressedAt = PressedAt;

    if (!FindDef(ActionId) || !IsBusy(ActionId))
    {
        Deliver(ActionId);
        return;
    }

    Queue.Add(ActionId);
    const int32 MaxQueued = FMath::Max(1, GetTuning().MaxQueued);
    while (Queue.Num() > MaxQueued)
    {
        // The oldest press makes way; its release, when it comes, is swallowed.
        const FName Dropped = Queue[0];
        Queue.RemoveAt(0);
        Forget(Dropped);
    }
}

void UPSInputBufferComponent::ReleaseAction(FName ActionId)
{
    FPSBufferedPress* Press = Held.Find(ActionId);
    if (!Press)
    {
        // Its press was dropped, or went down before the buffer was listening.
        return;
    }
    if (!Press->bDelivered)
    {
        // Still waiting: the release follows the press when it is passed on.
        Press->ReleasedAt = Clock;
        return;
    }
    const float HeldSeconds = Clock - Press->PressedAt;
    Forget(ActionId);
    OnActionReleased.Broadcast(ActionId, HeldSeconds);
}

void UPSInputBufferComponent::Deliver(FName ActionId)
{
    FPSBufferedPress* Press = Held.Find(ActionId);
    if (!Press)
    {
        return;
    }
    Press->bDelivered = true;
    const float PressedAt = Press->PressedAt;
    const float ReleasedAt = Press->ReleasedAt;

    OnActionPressed.Broadcast(ActionId, Clock - PressedAt);

    // A press released while it waited is released straight after, held as long as it was.
    const FPSBufferedPress* Still = Held.Find(ActionId);
    if (ReleasedAt >= 0.f && Still && Still->PressedAt == PressedAt)
    {
        Forget(ActionId);
        OnActionReleased.Broadcast(ActionId, ReleasedAt - PressedAt);
    }
}

void UPSInputBufferComponent::DeliverReady()
{
    // Delivering a press can change what is busy (a move starts) or the queue itself (a
    // consumer presses again), so walk a copy and re-check each entry.
    const TArray<FName> Waiting = Queue;
    for (const FName& ActionId : Waiting)
    {
        if (!Queue.Contains(ActionId))
        {
            continue;
        }
        const FPSBufferedPress* Press = Held.Find(ActionId);
        const FPSInputBufferDef* Def = FindDef(ActionId);
        if (!Press || !Def || Clock - Press->PressedAt > Def->BufferSeconds || !IsActionLive(ActionId))
        {
            Queue.Remove(ActionId);
            Forget(ActionId);
            continue;
        }
        if (IsBusy(ActionId))
        {
            continue;
        }
        Queue.Remove(ActionId);
        Deliver(ActionId);
    }
}

void UPSInputBufferComponent::HandleContextEntered(FName ContextId, const TArray<FName>& ContextsBefore)
{
    if (ContextId.IsNone())
    {
        return;
    }
    EnteredContexts.RemoveAll([ContextId](const FPSEnteredInputContext& Existing) { return Existing.ContextId == ContextId; });
    FPSEnteredInputContext& Entered = EnteredContexts.AddDefaulted_GetRef();
    Entered.ContextId = ContextId;
    Entered.ContextsBefore = ContextsBefore;
    Entered.EnteredAt = Clock;
    CarryEarlyPresses();
}

void UPSInputBufferComponent::CarryEarlyPresses()
{
    APSPlayerController* Controller = GetPlayerController();
    const UPSInputConfig* Config = Controller ? Controller->GetInputConfig() : nullptr;
    if (!Config)
    {
        EnteredContexts.Reset();
        return;
    }

    float LongestWindow = 0.f;
    for (const FPSInputBufferDef& Def : GetTuning().Actions)
    {
        LongestWindow = FMath::Max(LongestWindow, Def.BufferSeconds);
    }
    EnteredContexts.RemoveAll([this, Controller, LongestWindow](const FPSEnteredInputContext& Existing)
    {
        return Clock - Existing.EnteredAt > LongestWindow || !Controller->IsInputContextActive(Existing.ContextId);
    });

    // Enhanced Input ignores a key held when its mapping arrives, so a press that landed just
    // before its context came on would be lost. Look for one for a while after the context
    // comes on (input and this check need not run in the same frame); a press Enhanced Input
    // did deliver is already held here and is left alone. Presses go out after the scan, since
    // passing one on runs consumer code.
    struct FPSCarry
    {
        FName ActionId;
        FKey Key;
        float SecondsDown = 0.f;
    };
    TArray<FPSCarry> Carries;
    for (FPSEnteredInputContext& Entered : EnteredContexts)
    {
        for (const FPSInputBufferDef& Def : GetTuning().Actions)
        {
            if (Entered.Settled.Contains(Def.ActionId))
            {
                continue;
            }
            if (Held.Contains(Def.ActionId))
            {
                Entered.Settled.Add(Def.ActionId);
                continue;
            }
            for (const FKey& Key : Config->GetKeysFor(Def.ActionId, Entered.ContextId))
            {
                const float SecondsDown = GetKeySecondsDown(Key);
                if (SecondsDown < 0.f || SecondsDown > Def.BufferSeconds)
                {
                    continue;
                }
                // A key that meant something where it went down did that; never replay it here.
                bool bUsedBefore = false;
                for (const FName& Before : Entered.ContextsBefore)
                {
                    if (!Config->FindActionForKey(Key, Before).IsNone())
                    {
                        bUsedBefore = true;
                        break;
                    }
                }
                if (bUsedBefore)
                {
                    continue;
                }
                Entered.Settled.Add(Def.ActionId);
                Carries.Add(FPSCarry{ Def.ActionId, Key, SecondsDown });
                break;
            }
        }
    }

    for (const FPSCarry& Carry : Carries)
    {
        if (!Held.Contains(Carry.ActionId))
        {
            CarriedKeys.Add(Carry.ActionId, Carry.Key);
            PressActionAt(Carry.ActionId, Clock - Carry.SecondsDown);
        }
    }
}

void UPSInputBufferComponent::Flush()
{
    Queue.Reset();
    Held.Reset();
    CarriedKeys.Reset();
    EnteredContexts.Reset();
}

void UPSInputBufferComponent::AdvanceTime(float DeltaSeconds)
{
    Clock += DeltaSeconds;

    // A carried press ends when its key comes up.
    TArray<FName> KeysUp;
    for (const TPair<FName, FKey>& Carried : CarriedKeys)
    {
        if (GetKeySecondsDown(Carried.Value) < 0.f)
        {
            KeysUp.Add(Carried.Key);
        }
    }
    for (const FName& ActionId : KeysUp)
    {
        CarriedKeys.Remove(ActionId);
        ReleaseAction(ActionId);
    }

    if (EnteredContexts.Num() > 0)
    {
        CarryEarlyPresses();
    }
    DeliverReady();
}

void UPSInputBufferComponent::Forget(FName ActionId)
{
    Held.Remove(ActionId);
    CarriedKeys.Remove(ActionId);
}

APSPlayerController* UPSInputBufferComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}
