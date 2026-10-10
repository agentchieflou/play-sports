// PSPlayCallComponent.h - Epic 102: the human side of play calling, on the player controller
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSPlayCallComponent.generated.h"

class APSPlayerController;
class UPSPlayCallSubsystem;

/**
 * UPSPlayCallComponent connects a human player to UPSPlayCallSubsystem: when the player's
 * side waits for a call it opens the play-call screens (Data/ui_menus.json's PlayCallScreen,
 * run by UPSMenuComponent), and on the field the Confirm action hikes the ball once the
 * player's offense has called -- or reopens the screens if no call is in yet.
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

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void HandleHumanCallNeeded(bool bOffense);
    APSPlayerController* GetOwningController() const;
    UPSPlayCallSubsystem* GetPlayCall() const;

    TWeakObjectPtr<UPSPlayCallSubsystem> BoundPlayCall;
    FDelegateHandle CallNeededHandle;
};
