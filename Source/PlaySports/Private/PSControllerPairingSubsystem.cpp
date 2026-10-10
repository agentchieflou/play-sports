#include "PSControllerPairingSubsystem.h"
#include "PSAutoPauseSubsystem.h"
#include "PSInputConfig.h"
#include "PSInputDeviceComponent.h"
#include "PSInputGlyphs.h"
#include "PSLocalization.h"
#include "PSPlatformServices.h"
#include "PSPlayerController.h"
#include "PSVersusSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UPSControllerPairingSubsystem::UPSControllerPairingSubsystem()
{
    ContinueActionId = TEXT("Confirm");
    ContinueContextId = TEXT("Menu");
}

void UPSControllerPairingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>();
    if (Bus)
    {
        Bus->OnInputDeviceChangeMC.AddUObject(this, &UPSControllerPairingSubsystem::HandleInputDevice);
        Bus->OnLifecycleMC.AddUObject(this, &UPSControllerPairingSubsystem::HandleLifecycle);
        BoundBus = Bus;
    }
}

void UPSControllerPairingSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnInputDeviceChangeMC.RemoveAll(this);
        Bus->OnLifecycleMC.RemoveAll(this);
    }
    BoundBus.Reset();
    SetPlatformServices(nullptr);
    Super::Deinitialize();
}

bool UPSControllerPairingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSControllerPairingSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    if (!Services.IsValid())
    {
        SetPlatformServices(UPSPlatformServices::Get(&InWorld));
    }
    RememberUsers();
}

void UPSControllerPairingSubsystem::SetPlatformServices(UPSPlatformServices* InServices)
{
    if (UPSPlatformServices* Previous = Services.Get())
    {
        Previous->OnUsersChangedMC.Remove(UsersChangedHandle);
    }
    UsersChangedHandle.Reset();
    Services = InServices;
    if (InServices)
    {
        UsersChangedHandle = InServices->OnUsersChangedMC.AddUObject(this, &UPSControllerPairingSubsystem::HandleUsersChanged);
    }
}

UPSPlatformServices* UPSControllerPairingSubsystem::GetServices() const
{
    return Services.Get();
}

APSPlayerController* UPSControllerPairingSubsystem::FindHuman(int32 HumanIndex) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }
    for (TActorIterator<APSPlayerController> It(World); It; ++It)
    {
        if (It->HumanIndex == HumanIndex)
        {
            return *It;
        }
    }
    return nullptr;
}

UPSVersusSubsystem* UPSControllerPairingSubsystem::GetActiveVersus() const
{
    UWorld* World = GetWorld();
    UPSVersusSubsystem* Versus = World ? World->GetSubsystem<UPSVersusSubsystem>() : nullptr;
    return Versus && Versus->IsSessionActive() ? Versus : nullptr;
}

FPSControllerPairing UPSControllerPairingSubsystem::GetPairing(int32 HumanIndex) const
{
    FPSControllerPairing Pairing;
    Pairing.HumanIndex = HumanIndex;
    const APSPlayerController* Human = FindHuman(HumanIndex);
    if (const UPSInputDeviceComponent* Devices = Human ? Human->GetInputDeviceComponent() : nullptr)
    {
        Pairing.InputUserIndex = Devices->OwnerUserIndex;
    }
    const int32 UserSlot = Pairing.InputUserIndex != INDEX_NONE ? Pairing.InputUserIndex : HumanIndex;
    if (const UPSPlatformServices* Platform = GetServices())
    {
        Pairing.User = Platform->GetSignedInUser(UserSlot);
    }
    Pairing.bControllerLost = IsControllerLost(HumanIndex);
    Pairing.bUserSignedOut = SignedOutHumans.Contains(HumanIndex);
    return Pairing;
}

TArray<FPSControllerPairing> UPSControllerPairingSubsystem::GetPairings() const
{
    TArray<int32> Humans;
    if (UWorld* World = GetWorld())
    {
        for (TActorIterator<APSPlayerController> It(World); It; ++It)
        {
            Humans.AddUnique(It->HumanIndex);
        }
    }
    Humans.Sort();
    TArray<FPSControllerPairing> Pairings;
    for (const int32 HumanIndex : Humans)
    {
        Pairings.Add(GetPairing(HumanIndex));
    }
    return Pairings;
}

bool UPSControllerPairingSubsystem::IsControllerLost(int32 HumanIndex) const
{
    if (const UPSVersusSubsystem* Versus = GetActiveVersus())
    {
        const int32 Seat = Versus->FindSeat(FindHuman(HumanIndex));
        return Seat != INDEX_NONE && Versus->GetSeat(Seat).bDisconnected;
    }
    return LostControllers.Contains(HumanIndex);
}

bool UPSControllerPairingSubsystem::IsContinueKey(APSPlayerController* Human, const FKey& Key) const
{
    UPSInputConfig* Config = Human ? Human->GetInputConfig() : nullptr;
    return Key.IsGamepadKey() && Config && Config->GetKeysFor(ContinueActionId, ContinueContextId).Contains(Key);
}

void UPSControllerPairingSubsystem::HandleInputDevice(const FPSTelemetryInputDeviceEvent& Event)
{
    // A head-to-head session handles its seats' controllers by its etiquette (Epic 107).
    if (!Event.bFromConnectionChange || GetActiveVersus() || !FindHuman(Event.HumanIndex))
    {
        return;
    }
    const int32 HumanIndex = Event.HumanIndex;
    if (!Event.bConnected)
    {
        // Only the controller the player was playing with: a spare pad going away is no matter.
        if (Event.PreviousDevice != EPSInputDevice::Gamepad || LostControllers.Contains(HumanIndex))
        {
            return;
        }
        LostControllers.Add(HumanIndex);
        UE_LOG(LogTemp, Display, TEXT("UPSControllerPairingSubsystem: Human %d's controller disconnected."), HumanIndex);
        Publish(EPSControllerPairingKind::ControllerLost, HumanIndex, INDEX_NONE);
        PauseLiveGame(TEXT("ControllerLost"));
    }
    else if (LostControllers.Remove(HumanIndex) > 0)
    {
        UE_LOG(LogTemp, Display, TEXT("UPSControllerPairingSubsystem: Human %d's controller is back."), HumanIndex);
        Publish(EPSControllerPairingKind::ControllerRestored, HumanIndex, INDEX_NONE);
    }
}

bool UPSControllerPairingSubsystem::HandleButtonPress(int32 InputUserIndex, const FKey& Key)
{
    if (UPSVersusSubsystem* Versus = GetActiveVersus())
    {
        for (int32 Seat = 0; Seat < UPSVersusSubsystem::SeatCount; ++Seat)
        {
            APSPlayerController* Human = Versus->GetSeatController(Seat);
            if (Human && Versus->GetSeat(Seat).bDisconnected && IsContinueKey(Human, Key) && Versus->ReassignSeat(Seat, InputUserIndex))
            {
                KnownUserIds.Remove(Human->HumanIndex);
                RememberUsers();
                Publish(EPSControllerPairingKind::ControllerReassigned, Human->HumanIndex, InputUserIndex);
                return true;
            }
        }
        return false;
    }

    for (const int32 HumanIndex : LostControllers.Array())
    {
        APSPlayerController* Human = FindHuman(HumanIndex);
        if (!IsContinueKey(Human, Key))
        {
            continue;
        }
        // The controller in the player's hands drives them from now on.
        if (ULocalPlayer* LocalPlayer = Human->GetLocalPlayer())
        {
            LocalPlayer->SetControllerId(InputUserIndex);
        }
        LostControllers.Remove(HumanIndex);
        UE_LOG(LogTemp, Display, TEXT("UPSControllerPairingSubsystem: Human %d plays on with user %d's controller."), HumanIndex, InputUserIndex);
        Publish(EPSControllerPairingKind::ControllerReassigned, HumanIndex, InputUserIndex);
        return true;
    }
    return false;
}

void UPSControllerPairingSubsystem::HandleLifecycle(const FPSTelemetryLifecycleEvent& Event)
{
    switch (Event.Lifecycle)
    {
    case EPSPlatformLifecycle::Suspend:
    case EPSPlatformLifecycle::Constrained:
        RememberUsers();
        break;
    case EPSPlatformLifecycle::Resume:
    case EPSPlatformLifecycle::Unconstrained:
        // XR-112: back from a suspend or a constrain, the pairings are checked again.
        ValidateUsers();
        break;
    }
}

void UPSControllerPairingSubsystem::HandleUsersChanged()
{
    ValidateUsers();
}

void UPSControllerPairingSubsystem::RememberUsers()
{
    for (const FPSControllerPairing& Pairing : GetPairings())
    {
        if (Pairing.User.IsSignedIn() && !SignedOutHumans.Contains(Pairing.HumanIndex))
        {
            KnownUserIds.Add(Pairing.HumanIndex, Pairing.User.UserId);
        }
    }
}

int32 UPSControllerPairingSubsystem::ValidateUsers()
{
    if (!GetServices())
    {
        return 0;
    }
    int32 WithoutUser = 0;
    for (const FPSControllerPairing& Pairing : GetPairings())
    {
        const int32 HumanIndex = Pairing.HumanIndex;
        const FString* Known = KnownUserIds.Find(HumanIndex);
        const bool bSameUser = Pairing.User.IsSignedIn() && (!Known || *Known == Pairing.User.UserId);
        if (!bSameUser)
        {
            ++WithoutUser;
            if (!SignedOutHumans.Contains(HumanIndex))
            {
                SignedOutHumans.Add(HumanIndex);
                UE_LOG(LogTemp, Display, TEXT("UPSControllerPairingSubsystem: Human %d's user is no longer signed in."), HumanIndex);
                Publish(EPSControllerPairingKind::UserSignedOut, HumanIndex, Pairing.InputUserIndex);
            }
            continue;
        }
        KnownUserIds.Add(HumanIndex, Pairing.User.UserId);
        if (SignedOutHumans.Remove(HumanIndex) > 0)
        {
            UE_LOG(LogTemp, Display, TEXT("UPSControllerPairingSubsystem: Human %d's user is back."), HumanIndex);
            Publish(EPSControllerPairingKind::UserRestored, HumanIndex, Pairing.InputUserIndex);
        }
    }
    if (WithoutUser > 0)
    {
        PauseLiveGame(TEXT("UserSignedOut"));
    }
    return WithoutUser;
}

FText UPSControllerPairingSubsystem::DescribeStatus(int32 HumanIndex) const
{
    const FPSControllerPairing Pairing = GetPairing(HumanIndex);
    if (!Pairing.bControllerLost && !Pairing.bUserSignedOut)
    {
        return FText::GetEmpty();
    }

    // The player as the platform names them, or by seat when no user is signed in.
    FText Player = Pairing.User.DisplayName;
    if (!Pairing.User.IsSignedIn() || Player.IsEmpty())
    {
        FFormatNamedArguments Seat;
        Seat.Add(TEXT("Number"), HumanIndex + 1);
        Player = UPSLocalization::Format(TEXT("Platform.LocalUser"), Seat);
    }
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Player"), Player);
    if (Pairing.bUserSignedOut)
    {
        return UPSLocalization::Format(TEXT("Pairing.UserSignedOut"), Arguments);
    }

    // The continue button as the controller shows it (Epic 128's glyph label: "A").
    FText Button = UPSLocalization::Verbatim(ContinueActionId.ToString());
    APSPlayerController* Human = FindHuman(HumanIndex);
    UPSInputConfig* Config = Human ? Human->GetInputConfig() : nullptr;
    FPSInputGlyph Glyph;
    if (Config && Config->GetGlyphForAction(ContinueActionId, ContinueContextId, EPSInputDevice::Gamepad, Glyph))
    {
        Button = UPSLocalization::Verbatim(Glyph.Label);
    }
    Arguments.Add(TEXT("Button"), Button);
    return UPSLocalization::Format(TEXT("Pairing.ControllerLost"), Arguments);
}

void UPSControllerPairingSubsystem::PauseLiveGame(const FString& Reason) const
{
    UWorld* World = GetWorld();
    if (UPSAutoPauseSubsystem* AutoPause = World ? World->GetSubsystem<UPSAutoPauseSubsystem>() : nullptr)
    {
        AutoPause->PauseLiveGame(Reason);
    }
}

void UPSControllerPairingSubsystem::Publish(EPSControllerPairingKind Kind, int32 HumanIndex, int32 InputUserIndex) const
{
    UPSTelemetryBus* Bus = BoundBus.Get();
    if (!Bus)
    {
        return;
    }
    FPSTelemetryControllerPairingEvent Event;
    Event.Kind = Kind;
    Event.HumanIndex = HumanIndex;
    Event.InputUserIndex = InputUserIndex;
    if (const UPSPlatformServices* Platform = GetServices())
    {
        const APSPlayerController* Human = FindHuman(HumanIndex);
        const UPSInputDeviceComponent* Devices = Human ? Human->GetInputDeviceComponent() : nullptr;
        const int32 Owner = Devices ? Devices->OwnerUserIndex : INDEX_NONE;
        Event.UserId = Platform->GetSignedInUser(Owner != INDEX_NONE ? Owner : HumanIndex).UserId;
    }
    Bus->PublishControllerPairing(Event);
}
