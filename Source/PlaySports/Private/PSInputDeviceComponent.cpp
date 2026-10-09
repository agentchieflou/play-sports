#include "PSInputDeviceComponent.h"
#include "PSInputConfigTypes.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "GenericPlatform/ICursor.h"
#include "Input/Events.h"

/** Observe-only Slate pre-processor feeding the last-input heuristic. Returns false from
 *  every handler so input still reaches the game unchanged. */
class FPSInputDevicePreProcessor : public IInputProcessor
{
public:
    explicit FPSInputDevicePreProcessor(UPSInputDeviceComponent* InOwner)
        : Owner(InOwner)
    {
    }

    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
    {
    }

    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
    {
        Forward(InKeyEvent.GetKey(), 1.f);
        return false;
    }

    virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
    {
        Forward(InAnalogInputEvent.GetKey(), FMath::Abs(InAnalogInputEvent.GetAnalogValue()));
        return false;
    }

    virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
    {
        Forward(MouseEvent.GetEffectingButton(), 1.f);
        return false;
    }

private:
    void Forward(const FKey& Key, float AnalogValue)
    {
        if (UPSInputDeviceComponent* Component = Owner.Get())
        {
            Component->NotifyInput(Key, AnalogValue);
        }
    }

    TWeakObjectPtr<UPSInputDeviceComponent> Owner;
};

UPSInputDeviceComponent::UPSInputDeviceComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    AnalogThreshold = FInputTuningRow().DeviceSwitchAnalogThreshold;
    ActiveDevice = EPSInputDevice::KeyboardMouse;
}

void UPSInputDeviceComponent::BeginPlay()
{
    Super::BeginPlay();

    if (FSlateApplication::IsInitialized())
    {
        PreProcessor = MakeShared<FPSInputDevicePreProcessor>(this);
        FSlateApplication::Get().RegisterInputPreProcessor(PreProcessor);
    }

    // Anything other than the platform's default device (keyboard/mouse) is a gamepad.
    ConnectionChangeHandle = IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddWeakLambda(this,
        [this](EInputDeviceConnectionState NewState, FPlatformUserId UserId, FInputDeviceId DeviceId)
        {
            const bool bIsGamepad = DeviceId != IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
            NotifyConnectionChange(NewState == EInputDeviceConnectionState::Connected, bIsGamepad);
        });
}

void UPSInputDeviceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (PreProcessor.IsValid() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().UnregisterInputPreProcessor(PreProcessor);
    }
    PreProcessor.Reset();

    IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().Remove(ConnectionChangeHandle);
    ConnectionChangeHandle.Reset();

    Super::EndPlay(EndPlayReason);
}

void UPSInputDeviceComponent::NotifyInput(const FKey& Key, float AnalogValue)
{
    if (!Key.IsValid())
    {
        return;
    }
    if (Key.IsAnalog() && AnalogValue < AnalogThreshold)
    {
        return;
    }

    SetActiveDevice(Key.IsGamepadKey() ? EPSInputDevice::Gamepad : EPSInputDevice::KeyboardMouse, false, true);
}

void UPSInputDeviceComponent::NotifyConnectionChange(bool bConnected, bool bIsGamepad)
{
    if (!bIsGamepad)
    {
        return;
    }

    if (!bConnected && ActiveDevice == EPSInputDevice::Gamepad)
    {
        SetActiveDevice(EPSInputDevice::KeyboardMouse, true, false);
        return;
    }

    // A connect does not switch the active device -- the next input does -- but HUD and
    // menus still hear about it.
    PublishDeviceEvent(ActiveDevice, true, bConnected);
}

void UPSInputDeviceComponent::SetActiveDevice(EPSInputDevice NewDevice, bool bFromConnectionChange, bool bConnected)
{
    if (NewDevice == ActiveDevice)
    {
        return;
    }

    const EPSInputDevice PreviousDevice = ActiveDevice;
    ActiveDevice = NewDevice;
    UE_LOG(LogTemp, Display, TEXT("UPSInputDeviceComponent: Active input device is now %s."), *UEnum::GetValueAsString(ActiveDevice));
    PublishDeviceEvent(PreviousDevice, bFromConnectionChange, bConnected);
}

void UPSInputDeviceComponent::PublishDeviceEvent(EPSInputDevice PreviousDevice, bool bFromConnectionChange, bool bConnected)
{
    UWorld* World = GetWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus)
    {
        return;
    }

    FPSTelemetryInputDeviceEvent Event;
    Event.ActiveDevice = ActiveDevice;
    Event.PreviousDevice = PreviousDevice;
    Event.bFromConnectionChange = bFromConnectionChange;
    Event.bConnected = bConnected;
    Bus->PublishInputDeviceChange(Event);
}
