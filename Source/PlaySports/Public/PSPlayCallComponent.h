// PSPlayCallComponent.h - Epic 102: the human side of play calling, on the player controller
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSTelemetryBus.h"
#include "PSPlayCallComponent.generated.h"

class APSPlayerController;
class UPSPlayCallSubsystem;

/**
 * UPSPlayCallComponent connects a human player to UPSPlayCallSubsystem: when the player's
 * side waits for a call it opens the play-call screens (Data/ui_menus.json's PlayCallScreen,
 * run by UPSMenuComponent), and on the field the Confirm action hikes the ball once the
 * player's offense has called -- or reopens the screens if no call is in yet. When the
 * play clock forces a quick-call for the player's side (PlayCall on the bus), the open
 * screens close. Before the snap the Tempo action cycles the player's offensive tempo and the
 * Timeout action asks for a timeout (Epic 76).
 *
 * The player's side is the side of the pawn the controller possesses, else the controller's
 * HumanSide. APSPlayerController owns one; it binds at BeginPlay, headless tests call
 * BindToPlayCall.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSPlayCallComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSPlayCallComponent();

    /** Listens to the play-call authority and the controller's catalog actions. Idempotent. */
    void BindToPlayCall();

    void UnbindFromPlayCall();

    /** True when this player calls for the offense. */
    UFUNCTION(BlueprintPure, Category = "PlayCall")
    bool IsCallingForOffense() const;

    /** Opens the play-call screens unless another menu is up. */
    UFUNCTION(BlueprintCallable, Category = "PlayCall")
    bool OpenCallScreen();

    /** Catalog action handler (APSPlayerController::OnCatalogActionStarted). */
    UFUNCTION()
    void HandleCatalogAction(FName ActionId);

    /** The catalog action that hikes, or opens the screens when no call is in yet. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PlayCall")
    FName ConfirmActionId;

    /** The catalog action that cycles the offense's tempo (huddle, no-huddle, hurry-up). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PlayCall")
    FName TempoActionId;

    /** The catalog action that calls a timeout for the player's side. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PlayCall")
    FName TimeoutActionId;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void HandleHumanCallNeeded(bool bOffense);
    void HandlePlayCall(const FPSTelemetryPlayCallEvent& Event);
    APSPlayerController* GetOwningController() const;
    UPSPlayCallSubsystem* GetPlayCall() const;

    TWeakObjectPtr<UPSPlayCallSubsystem> BoundPlayCall;
    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    FDelegateHandle CallNeededHandle;
};
