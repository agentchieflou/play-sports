// PSInputDeviceComponent.h - Epic 127: which device the human player is using right now
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "PSTelemetryBus.h"
#include "PSInputDeviceComponent.generated.h"

class FPSInputDevicePreProcessor;

/**
 * UPSInputDeviceComponent tracks whether the human is on a gamepad, keyboard/mouse or the
 * touch screen and publishes every change on UPSTelemetryBus (Architecture rule 5), so HUD
 * glyphs and rumble subscribe to the bus instead of asking the controller.
 *
 * Two signals feed it:
 *  - the last-input heuristic: every key, button, analog move and finger touch the player
 *    makes, seen through a Slate input pre-processor (observe-only, never consumes input);
 *    analog moves count only past AnalogThreshold so stick drift cannot flip the device;
 *  - IPlatformInputDeviceMapper connect/disconnect: a gamepad disconnect while it is the
 *    active device falls back to the touch screen on a device that has one (a phone), and to
 *    keyboard/mouse otherwise. That is also the device a run starts on.
 *
 * APSPlayerController owns one and sets AnalogThreshold from FInputTuningRow.
 *
 * Two players on one machine (Epic 107): each seat's controller has its own component, and
 * UPSVersusSubsystem gives it the seat's user (OwnerUserIndex). It then counts only that
 * user's input and connection changes, so one player picking up a pad never flips the other's
 * glyphs or rumble; its events carry the controller's HumanIndex.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSInputDeviceComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSInputDeviceComponent();

    UFUNCTION(BlueprintPure, Category = "Input")
    EPSInputDevice GetActiveDevice() const { return ActiveDevice; }

    /** Last-input heuristic entry point. AnalogValue is the absolute deflection for axis
     *  keys; buttons and keys pass 1. */
    void NotifyInput(const FKey& Key, float AnalogValue);

    /** A device connected or disconnected (from IPlatformInputDeviceMapper). */
    void NotifyConnectionChange(bool bConnected, bool bIsGamepad);

    /** A finger touched the screen (Epic 130). */
    void NotifyTouch();

    /** NotifyInput for input from Slate user UserIndex: ignored when it is another seat's. */
    void NotifyUserInput(int32 UserIndex, const FKey& Key, float AnalogValue);

    /** NotifyTouch for a finger of Slate user UserIndex: ignored when it is another seat's. */
    void NotifyUserTouch(int32 UserIndex);

    /** NotifyConnectionChange for a device of platform user UserIndex: ignored when it is
     *  another seat's. */
    void NotifyUserConnectionChange(int32 UserIndex, bool bConnected, bool bIsGamepad);

    /** True when input from UserIndex is this component's to count: always, until a seat gives
     *  it an owner. */
    UFUNCTION(BlueprintPure, Category = "Input")
    bool IsOwnUser(int32 UserIndex) const { return OwnerUserIndex == INDEX_NONE || UserIndex == OwnerUserIndex; }

    /** The user whose devices this component counts (the seat's local player, Epic 107);
     *  INDEX_NONE counts every user's, which is right for one player. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    int32 OwnerUserIndex;

    /** The device used when nothing else is: Touch when bHasTouchScreen, else keyboard/mouse. */
    UFUNCTION(BlueprintPure, Category = "Input")
    EPSInputDevice GetFallbackDevice() const;

    /** Analog deflection needed before an axis counts as the player using that device. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    float AnalogThreshold;

    /** Whether the platform's main input is a touch screen (FPlatformMisc::SupportsTouchInput:
     *  true on a phone, false on a desktop). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    bool bHasTouchScreen;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void SetActiveDevice(EPSInputDevice NewDevice, bool bFromConnectionChange, bool bConnected);
    void PublishDeviceEvent(EPSInputDevice PreviousDevice, bool bFromConnectionChange, bool bConnected);

    EPSInputDevice ActiveDevice;

    TSharedPtr<FPSInputDevicePreProcessor> PreProcessor;
    FDelegateHandle ConnectionChangeHandle;
};
