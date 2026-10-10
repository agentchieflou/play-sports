// PSPassingComponent.h - Epic 104: the human passer -- receiver slots, touch and bullet, placement, pump fake
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "PSPassingComponent.generated.h"

class APSPlayerController;
class APSPlayerPawn;

/** Human passing tuning (Data/passing_input.json; Architecture rule 4). Distances in cm. */
USTRUCT(BlueprintType)
struct FPassingInputTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    /** The catalog actions that throw to receiver slots 1..N, the receivers ordered left to
     *  right across the field as the passer faces upfield. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passing")
    TArray<FName> SlotActions;

    /** The catalog action that pump-fakes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passing")
    FName PumpFakeAction;

    /** A slot button held at least this long throws a bullet; a quicker tap throws touch. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passing")
    float BulletHoldSeconds = 0.25f;

    /** A touch pass leaves at this fraction of the passer's full arm (a softer, higher ball). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passing")
    float TouchSpeedScale = 0.75f;

    /** The Move stick held forward or back at release moves the throw this far deeper or
     *  shorter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passing")
    float PlacementDepth = 300.f;

    /** The Move stick held left or right at release moves the throw this far that way. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passing")
    float PlacementWidth = 250.f;

    /** Ball speed used to lead a moving receiver: lead time = distance / this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passing")
    float LeadSpeed = 2000.f;
};

/**
 * UPSPassingComponent is the human QB's arm (Epic 104.3). Each slot button throws to one
 * eligible receiver -- the slots ordered left to right across the field, so the button layout
 * matches the formation -- and how the button is pressed shapes the throw:
 *
 *   - a tap throws a touch pass (TouchSpeedScale of the passer's arm); holding past
 *     BulletHoldSeconds throws a bullet;
 *   - the Move stick at release places the ball deeper, shorter, or to either side of the
 *     receiver's lead point;
 *   - the pump-fake button sells a throw without making it: coverage that bites freezes for a
 *     moment (UPSDefenderAIComponent), and the ball stays in the passer's hands.
 *
 * It throws through APSPlayerPawn::ThrowPass like the AI passer, so accuracy, arm strength,
 * the throw event and its landing point all work the same for a human. Input arrives through
 * the owning APSPlayerController's UPSInputBufferComponent (only while the Passing context is
 * on): a pass button pressed before the passer holds the ball waits for it, and the hold that
 * picks touch or bullet is timed from the physical press (Epic 104.4). ThrowToSlot and PumpFake
 * are public so headless tests can drive them directly.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSPassingComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSPassingComponent();

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FPassingInputTuningRow& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Listens to the owning controller's input buffer (binding the buffer to the controller
     *  too) and gives it this component's busy check. Idempotent. */
    void BindToController();

    /** True while ActionId is a pass button or the pump fake and the controlled player can't
     *  pass yet, so the press waits in the input buffer. */
    bool IsPassActionBusy(FName ActionId);

    /** True when the controlled player is a QB holding the ball. */
    UFUNCTION(BlueprintPure, Category = "Passing")
    bool CanPass() const;

    /** The receivers the slot buttons throw to: the controlled player's eligible teammates
     *  (receivers, tight ends, backs), left to right across the field. */
    UFUNCTION(BlueprintPure, Category = "Passing")
    TArray<APSPlayerPawn*> GetReceiverSlots() const;

    /** Throws to the receiver in Slot (0-based). HeldSeconds picks touch or bullet; Placement
     *  is the Move stick (-1..1 each axis, X right, Y forward). False when the controlled
     *  player can't pass or the slot is empty. */
    UFUNCTION(BlueprintCallable, Category = "Passing")
    bool ThrowToSlot(int32 Slot, float HeldSeconds, FVector2D Placement);

    /** Sells a throw without making it. False when the controlled player can't pass. */
    UFUNCTION(BlueprintCallable, Category = "Passing")
    bool PumpFake();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleActionPressed(FName ActionId, float HeldSeconds);

    UFUNCTION()
    void HandleActionReleased(FName ActionId, float HeldSeconds);

    APSPlayerController* GetPlayerController() const;
    APSPlayerPawn* GetPasser() const;

    UPROPERTY(Transient)
    FPassingInputTuningRow Tuning;

    bool bTuningLoaded = false;
};
