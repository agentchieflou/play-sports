// PSPlayerController.h - Epic 126/127/128/102: the project player controller; owns all human input
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerPawn.h"
#include "PSPlayerController.generated.h"

class AAIController;
class UEnhancedInputComponent;
class UPSInputConfig;
class UPSInputDeviceComponent;
class UPSForceFeedbackComponent;
class UPSMenuComponent;
class UPSPlayCallComponent;
struct FInputActionValue;
struct FInputActionInstance;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSInputCatalogActionSignature, FName, ActionId);

/**
 * APSPlayerController is where human input enters the game. It binds the UPSInputConfig
 * catalog onto its Enhanced Input component and applies the gameplay mapping context when
 * it possesses an APSPlayerPawn, so the pawn itself stays input-free (Architecture rule 1).
 *
 * The controller keeps its own context stack (ActiveInputContexts) and mirrors it into the
 * local player's UEnhancedInputLocalPlayerSubsystem when one exists; headless worlds have no
 * local player, so the stack is what tests observe.
 *
 * Human possession (Epic 127): at BeginPlay the controller takes the designated pawn
 * (DefaultControlRole on HumanSide, the QB by default) from its AI controller, which
 * resumes the pawn on ReleaseControl. SwitchPlayer moves control to the ball carrier when
 * the human's side has the ball (the possession component is the authority, rule 6), and
 * otherwise to the eligible teammate nearest the ball. Every handoff is published on
 * UPSTelemetryBus; HUD and camera read it there or from APSPlayerPawn::IsUserControlled().
 *
 * Rumble (Epic 128) is UPSForceFeedbackComponent's: it hears gameplay on the bus and plays
 * the authored pattern on this controller's gamepad. Play calling (Epic 102) is
 * UPSPlayCallComponent's: it opens the play-call screens and hikes on Confirm.
 *
 * Move, Sprint, SwitchPlayer and Pause drive the game here (Pause opens UPSMenuComponent's
 * pause screen, Epic 101). Every other Boolean catalog action is broadcast on
 * OnCatalogActionStarted by ID for its consumer to subscribe to -- no consumer casts to this
 * controller to read input.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    APSPlayerController();

    /** The action catalog and tuning, loaded from Data/ on first use when no InputConfig
     *  is assigned. */
    UFUNCTION(BlueprintPure, Category = "Input")
    UPSInputConfig* GetInputConfig();

    UFUNCTION(BlueprintPure, Category = "Input")
    UPSInputDeviceComponent* GetInputDeviceComponent() const { return InputDeviceComponent; }

    UFUNCTION(BlueprintPure, Category = "Input")
    UPSForceFeedbackComponent* GetForceFeedbackComponent() const { return ForceFeedbackComponent; }

    /** The front-end shell and pause menu (Epic 101). */
    UFUNCTION(BlueprintPure, Category = "Menu")
    UPSMenuComponent* GetMenuComponent() const { return MenuComponent; }

    /** The human side of play calling (Epic 102). */
    UFUNCTION(BlueprintPure, Category = "PlayCall")
    UPSPlayCallComponent* GetPlayCallComponent() const { return PlayCallComponent; }

    /** True while ContextId is on this controller's context stack. */
    UFUNCTION(BlueprintPure, Category = "Input")
    bool IsInputContextActive(FName ContextId) const;

    /** Binds every catalog action this controller handles onto InInputComponent. Called
     *  from SetupInputComponent; public so a test can bind onto a component it owns. */
    void BindCatalogActions(UEnhancedInputComponent& InInputComponent);

    /** Move handler: X is right, Y is forward, relative to the control rotation's yaw.
     *  This is the bound Move delegate's target; public so a value can be injected
     *  headlessly without a local player. */
    void HandleMove(const FInputActionValue& Value);

    /** Takes control of Target from whatever controls it. Its AI controller is remembered
     *  and resumes the pawn when control is released or moves elsewhere. */
    UFUNCTION(BlueprintCallable, Category = "Possession")
    bool TakeControlOf(APSPlayerPawn* Target);

    /** Hands the controlled APSPlayerPawn back to the AI controller it was taken from (or a
     *  fresh one), and returns to the pawn held before taking control, if any. */
    UFUNCTION(BlueprintCallable, Category = "Possession")
    void ReleaseControl();

    /** Takes the first DefaultControlRole pawn on HumanSide. False when none exists. */
    UFUNCTION(BlueprintCallable, Category = "Possession")
    bool TakeDefaultControl();

    /** The player-switch rule. If a pawn on the controlled side holds the ball, control goes
     *  to it; otherwise to the non-downed teammate nearest BallLocation. False when there is
     *  no better pawn than the current one. */
    UFUNCTION(BlueprintCallable, Category = "Possession")
    bool SwitchToBestPawn(const FVector& BallLocation);

    /** Fires once per press for every Boolean catalog action without a dedicated handler. */
    UPROPERTY(BlueprintAssignable, Category = "Input")
    FPSInputCatalogActionSignature OnCatalogActionStarted;

    /** Optional override for the catalog; when null one is created from the default paths. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UPSInputConfig* InputConfig;

    /** Catalog context applied while possessing an APSPlayerPawn. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    FName GameplayContextId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    FName MoveActionId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    FName SprintActionId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    FName SwitchPlayerActionId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    FName PauseActionId;

    /** The side the human plays when not yet controlling a pawn. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Possession")
    EPSTeamSide HumanSide;

    /** The role taken by default on HumanSide. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Possession")
    EPlayerRole DefaultControlRole;

    /** Take the default pawn on the tick after BeginPlay (once the game mode has spawned the
     *  roster). Off leaves every pawn to the AI, e.g. for watching a CPU-vs-CPU game. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Possession")
    bool bTakeDefaultControlOnBeginPlay;

protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;

private:
    void HandleSprintStarted(const FInputActionValue& Value);
    void HandleSprintCompleted(const FInputActionValue& Value);
    void HandleSwitchPlayer(const FInputActionValue& Value);
    void HandlePause(const FInputActionValue& Value);
    void HandleCatalogActionStarted(const FInputActionInstance& Instance);
    void HandleDeferredDefaultControl();

    /** Gives the current APSPlayerPawn back to its AI without touching ParkedPawn. */
    void ReturnControlledPawnToAI();
    void PublishControlChange(const APSPlayerPawn* PlayerPawn, bool bHumanControlled);
    void ViewThroughBroadcastCamera();

    void PushInputContext(FName ContextId);
    void PopInputContext(FName ContextId);

    UPROPERTY(VisibleAnywhere, Category = "Input")
    UPSInputDeviceComponent* InputDeviceComponent;

    UPROPERTY(VisibleAnywhere, Category = "Input")
    UPSForceFeedbackComponent* ForceFeedbackComponent;

    UPROPERTY(VisibleAnywhere, Category = "Menu")
    UPSMenuComponent* MenuComponent;

    UPROPERTY(VisibleAnywhere, Category = "PlayCall")
    UPSPlayCallComponent* PlayCallComponent;

    UPROPERTY(Transient)
    TArray<FName> ActiveInputContexts;

    /** The AI controller displaced by TakeControlOf; it resumes the pawn on release. */
    UPROPERTY(Transient)
    AAIController* DisplacedAIController;

    /** The non-football pawn (the game mode's spectator) held before taking control. */
    UPROPERTY(Transient)
    APawn* ParkedPawn;
};
