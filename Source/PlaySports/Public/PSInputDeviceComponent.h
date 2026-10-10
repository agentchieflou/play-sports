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
