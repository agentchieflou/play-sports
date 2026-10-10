// PSInputBufferComponent.h - Epic 104.4: presses wait out their target's commitment window
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "InputCoreTypes.h"
#include "PSInputConfigTypes.h"
#include "PSInputBufferComponent.generated.h"

class APSPlayerController;

/** How long one catalog action's press stays good (Data/input_buffer.json). */
USTRUCT(BlueprintType)
struct FPSInputBufferDef
{
    GENERATED_BODY()

    /** The catalog action that is buffered. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FName ActionId;

    /** A press waits this long for its target to take it, then is dropped. A press that did
     *  nothing because the action's context was off counts in that context if the context
     *  comes on within this long. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    float BufferSeconds = 0.2f;
};

/** Input buffering tuning (Data/input_buffer.json; Architecture rule 4). */
USTRUCT(BlueprintType)
struct FInputBufferTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    /** The most presses waiting at once; a newer press pushes out the oldest. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    int32 MaxQueued = 1;

    /** The buffered actions. Every other action passes straight through. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    TArray<FPSInputBufferDef> Actions;
};

/** True while the target of the named action can't take a press yet: committed to a move,
 *  cooling down, not holding the ball yet. A check answers false for actions it doesn't own. */
DECLARE_DELEGATE_RetVal_OneParam(bool, FPSInputBusyCheck, FName);

/** Seconds the key has been held, or a negative number while it is up. */
DECLARE_DELEGATE_RetVal_OneParam(float, FPSInputKeyStateQuery, const FKey&);

/** A press or release passed on by the buffer. HeldSeconds is the time since the physical
 *  press: on a press, how long it waited; on a release, how long the button was held. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPSBufferedActionSignature, FName, ActionId, float, HeldSeconds);

/** One press between its button going down and its release reaching the consumers. */
struct FPSBufferedPress
{
    /** Buffer clock time of the physical press. */
    float PressedAt = 0.f;

    /** Buffer clock time of the release while the press was still waiting; negative while held. */
    float ReleasedAt = -1.f;

    /** True once the press has been passed on. */
    bool bDelivered = false;
};

/** A depth context that came on recently, and what was on the stack before it. */
struct FPSEnteredInputContext
{
    FName ContextId;
    TArray<FName> ContextsBefore;

    /** Buffer clock time it came on. */
    float EnteredAt = 0.f;

    /** Actions already pressed since it came on, carried or delivered by Enhanced Input; each
     *  is carried at most once. */
    TSet<FName> Settled;
};

/**
 * UPSInputBufferComponent sits between the controller's catalog actions and the components
 * that act on them (Epic 104.4). A press whose target is busy waits, for its action's
 * BufferSeconds, instead of being lost:
 *
 *   - a carrier move pressed during another move's commitment window, or near the end of its
 *     own cooldown, fires as soon as the carrier is free (UPSCarrierMoveComponent::IsMoveBusy);
 *   - a pass button pressed before the passer holds the ball throws once he does;
 *   - a button pressed just before its context comes on (a pass slot in the frame before
 *     Passing replaces PreSnap) counts in that context, provided the key meant nothing in the
 *     contexts that were on when it went down. A hike press is never replayed as a throw.
 *
 * Busy means what each consumer's busy check says (AddBusyCheck); the commitment windows are
 * data (Data/carrier_moves.json's CommitSeconds until Track D's animations own them) and so
 * are the buffer windows (Data/input_buffer.json). A waiting press is dropped when it outlives
 * its window, when none of its action's contexts is on any more, or when control moves to
 * another player. MaxQueued presses wait at most; the newest wins.
 *
 * Consumers subscribe to OnActionPressed and OnActionReleased instead of the controller's
 * catalog events. Every Boolean catalog action passes through, buffered or not, and a release
 * always follows its own press, so a hold is timed from the physical press even when the
 * press waited. The buffer reads keys only through the catalog and the controller's player
 * input, so it is device-agnostic; KeyStateQuery lets tests and input that isn't an engine key
 * (Epic 130's touch layer, if it injects actions) supply the key state.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSInputBufferComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSInputBufferComponent();

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FInputBufferTuningRow& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Problems with InTuning (empty when sound): MaxQueued below 1, an empty or repeated
     *  action, a negative window; with Catalog, an action that isn't a Boolean catalog action. */
    static TArray<FString> ValidateTuning(const FInputBufferTuningRow& InTuning, const FPSInputCatalog* Catalog = nullptr);

    /** Listens to the owning controller's catalog actions. Idempotent. */
    void BindToController();

    /** Adds a busy check; presses of buffered actions wait while any check says busy. */
    void AddBusyCheck(FPSInputBusyCheck Check);

    /** Removes every busy check bound to CheckOwner. */
    void RemoveBusyChecks(const UObject* CheckOwner);

    /** A press of ActionId: passed on now, or held while its target is busy. Bound to the
     *  controller's OnCatalogActionStarted; public so tests can press. */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void PressAction(FName ActionId);

    /** The release of ActionId: passed on after its press, or swallowed when that press was
     *  dropped. Bound to the controller's OnCatalogActionCompleted. */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void ReleaseAction(FName ActionId);

    /** Called by the controller once ContextId is on its stack. For the longest buffer window
     *  afterwards, a key bound to a buffered action in ContextId that is down, went down within
     *  that action's window, and is bound to nothing in ContextsBefore is pressed here. */
    void HandleContextEntered(FName ContextId, const TArray<FName>& ContextsBefore);

    /** Drops every waiting press and forgets every held button (control moved to another
     *  player, so the presses were for someone else). */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void Flush();

    /** Moves the buffer's clock on, passes on presses whose target is free, drops stale ones.
     *  Ticking does this; headless tests call it. */
    void AdvanceTime(float DeltaSeconds);

    /** True while ActionId has a buffered entry in its tuning. */
    UFUNCTION(BlueprintPure, Category = "Input")
    bool IsBuffered(FName ActionId);

    /** True while a press of ActionId waits for its target. */
    UFUNCTION(BlueprintPure, Category = "Input")
    bool IsQueued(FName ActionId) const { return Queue.Contains(ActionId); }

    /** How many presses wait. */
    UFUNCTION(BlueprintPure, Category = "Input")
    int32 GetQueuedCount() const { return Queue.Num(); }

    /** True between a press of ActionId and its release, whether or not it was passed on. */
    UFUNCTION(BlueprintPure, Category = "Input")
    bool IsActionDown(FName ActionId) const { return Held.Contains(ActionId); }

    /** Every Boolean catalog action's press, once its target can take it. */
    UPROPERTY(BlueprintAssignable, Category = "Input")
    FPSBufferedActionSignature OnActionPressed;

    /** The release that goes with each passed-on press. */
    UPROPERTY(BlueprintAssignable, Category = "Input")
    FPSBufferedActionSignature OnActionReleased;

    /** Where key state comes from. Unbound, the owning controller's player input. */
    FPSInputKeyStateQuery KeyStateQuery;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    const FPSInputBufferDef* FindDef(FName ActionId);
    bool IsBusy(FName ActionId);
    bool IsActionLive(FName ActionId) const;
    float GetKeySecondsDown(const FKey& Key) const;
    void PressActionAt(FName ActionId, float PressedAt);
    void Deliver(FName ActionId);
    void DeliverReady();
    void CarryEarlyPresses();
    void Forget(FName ActionId);
    APSPlayerController* GetPlayerController() const;

    UPROPERTY(Transient)
    FInputBufferTuningRow Tuning;

    TArray<FPSInputBusyCheck> BusyChecks;

    /** Every press between its button going down and its release being passed on. */
    TMap<FName, FPSBufferedPress> Held;

    /** Presses waiting for their target, oldest first. */
    TArray<FName> Queue;

    /** Presses carried into a context that came on after the key went down. Enhanced Input
     *  ignores a key held when its mapping arrives, so their release is read from the key. */
    TMap<FName, FKey> CarriedKeys;

    TArray<FPSEnteredInputContext> EnteredContexts;

    float Clock = 0.f;
    bool bTuningLoaded = false;
};
