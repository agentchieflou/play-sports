#include "PSInputDeviceComponent.h"
#include "PSInputConfigTypes.h"
#include "PSPlayerController.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "GenericPlatform/ICursor.h"
#include "HAL/PlatformMisc.h"
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
        Forward(static_cast<int32>(InKeyEvent.GetUserIndex()), InKeyEvent.GetKey(), 1.f);
        return false;
    }

    virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
    {
        Forward(static_cast<int32>(InAnalogInputEvent.GetUserIndex()), InAnalogInputEvent.GetKey(), FMath::Abs(InAnalogInputEvent.GetAnalogValue()));
        return false;
    }

    virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
    {
        // A finger arrives as a pointer event flagged as touch (Epic 130).
        if (MouseEvent.IsTouchEvent())
        {
            if (UPSInputDeviceComponent* Component = Owner.Get())
            {
                Component->NotifyUserTouch(static_cast<int32>(MouseEvent.GetUserIndex()));
            }
            return false;
        }
        Forward(static_cast<int32>(MouseEvent.GetUserIndex()), MouseEvent.GetEffectingButton(), 1.f);
        return false;
    }

private:
    void Forward(int32 UserIndex, const FKey& Key, float AnalogValue)
    {
        if (UPSInputDeviceComponent* Component = Owner.Get())
        {
            Component->NotifyUserInput(UserIndex, Key, AnalogValue);
        }
    }

    TWeakObjectPtr<UPSInputDeviceComponent> Owner;
};

UPSInputDeviceComponent::UPSInputDeviceComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    AnalogThreshold = FInputTuningRow().DeviceSwitchAnalogThreshold;
    OwnerUserIndex = INDEX_NONE;
    bHasTouchScreen = FPlatformMisc::SupportsTouchInput();
    ActiveDevice = GetFallbackDevice();
}

EPSInputDevice UPSInputDeviceComponent::GetFallbackDevice() const
{
    return bHasTouchScreen ? EPSInputDevice::Touch : EPSInputDevice::KeyboardMouse;
}

void UPSInputDeviceComponent::NotifyTouch()
{
    SetActiveDevice(EPSInputDevice::Touch, false, true);
}

void UPSInputDeviceComponent::NotifyUserInput(int32 UserIndex, const FKey& Key, float AnalogValue)
{
    if (IsOwnUser(UserIndex))
    {
        NotifyInput(Key, AnalogValue);
    }
}

void UPSInputDeviceComponent::NotifyUserTouch(int32 UserIndex)
{
    if (IsOwnUser(UserIndex))
    {
        NotifyTouch();
    }
}

void UPSInputDeviceComponent::NotifyUserConnectionChange(int32 UserIndex, bool bConnected, bool bIsGamepad)
{
    if (IsOwnUser(UserIndex))
    {
        NotifyConnectionChange(bConnected, bIsGamepad);
    }
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
            NotifyUserConnectionChange(UserId.GetInternalId(), NewState == EInputDeviceConnectionState::Connected, bIsGamepad);
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

    EPSInputDevice Device = EPSInputDevice::KeyboardMouse;
    if (Key.IsGamepadKey())
    {
        Device = EPSInputDevice::Gamepad;
    }
    else if (Key.IsTouch())
    {
        Device = EPSInputDevice::Touch;
    }
    SetActiveDevice(Device, false, true);
}

void UPSInputDeviceComponent::NotifyConnectionChange(bool bConnected, bool bIsGamepad)
{
    if (!bIsGamepad)
    {
        return;
    }

    if (!bConnected && ActiveDevice == EPSInputDevice::Gamepad)
    {
        SetActiveDevice(GetFallbackDevice(), true, false);
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
    const APSPlayerController* OwningController = Cast<APSPlayerController>(GetOwner());
    Event.HumanIndex = OwningController ? OwningController->HumanIndex : 0;
    Bus->PublishInputDeviceChange(Event);
}
