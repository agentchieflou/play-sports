// PSControllerPairingSubsystem.h - Epic 150: each local player's user and controller, and getting a lost controller back
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "InputCoreTypes.h"
#include "PSPlatformTypes.h"
#include "PSTelemetryBus.h"
#include "PSControllerPairingSubsystem.generated.h"

class APSPlayerController;
class UPSPlatformServices;
class UPSVersusSubsystem;

/** One local human's user and controller (UPSControllerPairingSubsystem::GetPairing). */
USTRUCT(BlueprintType)
struct FPSControllerPairing
{
    GENERATED_BODY()

    /** Which human (APSPlayerController::HumanIndex). */
    UPROPERTY(BlueprintReadOnly, Category = "Platform")
    int32 HumanIndex = INDEX_NONE;

    /** The platform user whose controllers drive the human: the seat's user in a head-to-head
     *  game (UPSVersusSubsystem), INDEX_NONE (any controller) for one player. */
    UPROPERTY(BlueprintReadOnly, Category = "Platform")
    int32 InputUserIndex = INDEX_NONE;

    /** Who the human is to the platform (UPSPlatformServices): the user of InputUserIndex's slot,
     *  or of the human's own slot when any controller drives them. */
    UPROPERTY(BlueprintReadOnly, Category = "Platform")
    FPSPlatformUser User;

    /** The controller driving the human disconnected, and none has taken over yet. */
    UPROPERTY(BlueprintReadOnly, Category = "Platform")
    bool bControllerLost = false;

    /** The human's user signed out, or another took their place, while the game was away. */
    UPROPERTY(BlueprintReadOnly, Category = "Platform")
    bool bUserSignedOut = false;
};

/**
 * UPSControllerPairingSubsystem keeps each local human paired with a platform user and a
 * controller (Epic 150), the way the Xbox Requirements ask (XR-112, XR-115), on every platform:
 *
 *  - Pairing: a human is the platform user of their seat (UPSPlatformServices::GetSignedInUser),
 *    driven by that user's controllers in a head-to-head game (Epic 107's seats) and by any
 *    controller alone.
 *  - Controller lost: when the controller driving a lone player's game disconnects (the bus's
 *    InputDeviceChange), the game pauses (UPSAutoPauseSubsystem) and the pause screen says what to
 *    do (DescribeStatus). In a head-to-head game the session pauses by its own etiquette.
 *  - Reassignment: while a controller is lost, the continue button (the Menu context's Confirm
 *    action, A on an Xbox controller) on any other controller makes that controller the human's
 *    (HandleButtonPress; UPSVersusSubsystem::ReassignSeat in a session). The same controller coming
 *    back restores the pairing too.
 *  - Users: on a resume or an unconstrain, and whenever the platform reports a sign-in change, it
 *    checks every human's user is still the one who was playing; when not, the game pauses and
 *    says so until that user is back.
 *
 * Every change is published on the bus (ControllerPairing). It reads the platform only through
 * UPSPlatformServices; headless tests hand it one (SetPlatformServices).
 */
UCLASS()
class PLAYSPORTS_API UPSControllerPairingSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    UPSControllerPairingSubsystem();

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

    /** Takes users from InServices (the game instance's at begin play; tests hand their own). */
    void SetPlatformServices(UPSPlatformServices* InServices);

    /** HumanIndex's user and controller. */
    UFUNCTION(BlueprintCallable, Category = "Platform")
    FPSControllerPairing GetPairing(int32 HumanIndex) const;

    /** Every human in the world, in HumanIndex order. */
    UFUNCTION(BlueprintCallable, Category = "Platform")
    TArray<FPSControllerPairing> GetPairings() const;

    /** True while HumanIndex's controller is lost: the session seat's state in a head-to-head game. */
    UFUNCTION(BlueprintPure, Category = "Platform")
    bool IsControllerLost(int32 HumanIndex) const;

    /** A controller button from platform user InputUserIndex. Every press reaches here through the
     *  humans' UPSInputDeviceComponent. When a human's controller is lost and Key is the continue
     *  button, the pressing controller becomes theirs. True when it did. */
    bool HandleButtonPress(int32 InputUserIndex, const FKey& Key);

    /** Checks every human's user is still signed in and the one who was playing (XR-112): a human
     *  whose user is not pauses the game and is marked signed out until the user is back. Returns
     *  how many humans have no user. */
    int32 ValidateUsers();

    /** What HumanIndex must do to play on, for the pause screen ("Player 1: reconnect your
     *  controller, or press A on another one to continue"); empty when nothing. */
    FText DescribeStatus(int32 HumanIndex) const;

    /** The input action and context whose gamepad keys are the continue button. */
    UPROPERTY(EditDefaultsOnly, Category = "Platform")
    FName ContinueActionId;

    UPROPERTY(EditDefaultsOnly, Category = "Platform")
    FName ContinueContextId;

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandleInputDevice(const FPSTelemetryInputDeviceEvent& Event);
    void HandleLifecycle(const FPSTelemetryLifecycleEvent& Event);
    void HandleUsersChanged();

    APSPlayerController* FindHuman(int32 HumanIndex) const;
    UPSVersusSubsystem* GetActiveVersus() const;
    UPSPlatformServices* GetServices() const;
    bool IsContinueKey(APSPlayerController* Human, const FKey& Key) const;
    /** Remembers each human's user, so a resume can tell whether it is still the same one. */
    void RememberUsers();
    void PauseLiveGame(const FString& Reason) const;
    void Publish(EPSControllerPairingKind Kind, int32 HumanIndex, int32 InputUserIndex) const;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    TWeakObjectPtr<UPSPlatformServices> Services;
    FDelegateHandle UsersChangedHandle;

    /** Lone players whose controller is lost (a head-to-head session keeps its seats' own). */
    TSet<int32> LostControllers;

    /** Humans whose user signed out or changed. */
    TSet<int32> SignedOutHumans;

    /** Each human's user the last time it was known good. */
    TMap<int32, FString> KnownUserIds;
};
