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
class UPSPlayContextComponent;
class UPSPassingComponent;
class UPSCarrierInputComponent;
class UPSPreSnapInputComponent;
class UPSInputBufferComponent;
class UPSDefenseInputComponent;
class UPSKickMeterComponent;
class UPSSettingsComponent;
class UPSControlHandoffComponent;
class UPSOverlayReticleComponent;
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
 * UPSPlayCallComponent's: it opens the play-call screens and hikes on Confirm. Input depth
 * (Epic 104) is two components': UPSPlayContextComponent keeps the gameplay-depth context
 * (PreSnap, Passing, BallCarrier, Defense) matching the moment of the play,
 * UPSPassingComponent throws to receiver slots when the controlled QB passes, and
 * UPSCarrierInputComponent turns the move buttons into the carrier's moves. Both hear their
 * buttons through UPSInputBufferComponent, which holds a press while its target is busy and
 * carries a press into a depth context that came on just after it (Epic 104.4). On defense,
 * UPSDefenseInputComponent times the jump at the snap and the strip; on a kick,
 * UPSKickMeterComponent is the kicker's meter (Epic 104.5).
 *
 * Control switching (Epic 30) is UPSControlHandoffComponent's: it picks who the switch and
 * pre-snap pick buttons go to, and this controller moves control there. A handoff in either
 * direction keeps the pawn's velocity, so neither the human nor the resuming AI starts from a
 * standstill. UPSOverlayReticleComponent draws the selected-player reticle under the
 * controlled pawn.
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

    /** Keeps the gameplay-depth context matching the moment of the play (Epic 104). */
    UFUNCTION(BlueprintPure, Category = "Input")
    UPSPlayContextComponent* GetPlayContextComponent() const { return PlayContextComponent; }

    /** The human passer: receiver slots, touch and bullet, placement, pump fake (Epic 104). */
    UFUNCTION(BlueprintPure, Category = "Input")
    UPSPassingComponent* GetPassingComponent() const { return PassingComponent; }

    /** The human ball carrier's move buttons (Epic 104.2). */
    UFUNCTION(BlueprintPure, Category = "Input")
    UPSCarrierInputComponent* GetCarrierInputComponent() const { return CarrierInputComponent; }

    /** The human offense's pre-snap buttons: audible, hot route, motion, protection (Epic 66). */
    UFUNCTION(BlueprintPure, Category = "Input")
    UPSPreSnapInputComponent* GetPreSnapInputComponent() const { return PreSnapInputComponent; }

    /** Buffers catalog presses whose target is busy (Epic 104.4). */
    UFUNCTION(BlueprintPure, Category = "Input")
    UPSInputBufferComponent* GetInputBufferComponent() const { return InputBufferComponent; }

    /** The human defender's jump at the snap and strip button (Epic 104.5). */
    UFUNCTION(BlueprintPure, Category = "Input")
    UPSDefenseInputComponent* GetDefenseInputComponent() const { return DefenseInputComponent; }

    /** The human kicker's meter (Epic 104.5). */
    UFUNCTION(BlueprintPure, Category = "Input")
    UPSKickMeterComponent* GetKickMeterComponent() const { return KickMeterComponent; }

    /** Applies the player's settings to this controller's input and rumble (Epic 103). */
    UFUNCTION(BlueprintPure, Category = "Settings")
    UPSSettingsComponent* GetSettingsComponent() const { return SettingsComponent; }

    /** Re-applies the context stack to the local player's Enhanced Input subsystem after the
     *  input config rebuilt its mapping contexts (a settings change or a remap). */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void RefreshInputMappings();

    /** Who the switch and pick buttons give control to (Epic 30). */
    UFUNCTION(BlueprintPure, Category = "Possession")
    UPSControlHandoffComponent* GetControlHandoffComponent() const { return ControlHandoffComponent; }

    /** The selected-player reticle (Epic 30). */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    UPSOverlayReticleComponent* GetOverlayReticleComponent() const { return OverlayReticleComponent; }

    /** The Move stick's value right now (X right, Y forward); zero once released. */
    UFUNCTION(BlueprintPure, Category = "Input")
    FVector2D GetMoveInput() const { return MoveInput; }

    /** Puts ContextId on the stack as the one gameplay-depth context (Epic 104), above the
     *  gameplay context, replacing the previous one; NAME_None clears it. The input buffer
     *  hears of the new context so a press made just before it counts there. */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void SetDepthContext(FName ContextId);

    /** The gameplay-depth context on the stack, or NAME_None. */
    UFUNCTION(BlueprintPure, Category = "Input")
    FName GetDepthContext() const { return DepthContextId; }

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

    /** Fires once per release of the same actions, for consumers that time a hold. */
    UPROPERTY(BlueprintAssignable, Category = "Input")
    FPSInputCatalogActionSignature OnCatalogActionCompleted;

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

    /** With the Reduced motion setting on (Epic 103.5), a blended change of view is a cut:
     *  the blend time goes through UPSUIAccessibilitySubsystem::GetTransitionSeconds. */
    virtual void SetViewTarget(AActor* NewViewTarget, FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams()) override;

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
    void HandleMoveCompleted(const FInputActionValue& Value);
    void HandleCatalogActionStarted(const FInputActionInstance& Instance);
    void HandleCatalogActionCompleted(const FInputActionInstance& Instance);
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

    UPROPERTY(VisibleAnywhere, Category = "Input")
    UPSPlayContextComponent* PlayContextComponent;

    UPROPERTY(VisibleAnywhere, Category = "Input")
    UPSPassingComponent* PassingComponent;

    UPROPERTY(VisibleAnywhere, Category = "Input")
    UPSCarrierInputComponent* CarrierInputComponent;

    UPROPERTY(VisibleAnywhere, Category = "Input")
    UPSPreSnapInputComponent* PreSnapInputComponent;

    UPROPERTY(VisibleAnywhere, Category = "Input")
    UPSInputBufferComponent* InputBufferComponent;

    UPROPERTY(VisibleAnywhere, Category = "Input")
    UPSDefenseInputComponent* DefenseInputComponent;

    UPROPERTY(VisibleAnywhere, Category = "Input")
    UPSKickMeterComponent* KickMeterComponent;

    UPROPERTY(VisibleAnywhere, Category = "Settings")
    UPSSettingsComponent* SettingsComponent;

    UPROPERTY(VisibleAnywhere, Category = "Possession")
    UPSControlHandoffComponent* ControlHandoffComponent;

    UPROPERTY(VisibleAnywhere, Category = "Overlay")
    UPSOverlayReticleComponent* OverlayReticleComponent;

    UPROPERTY(Transient)
    TArray<FName> ActiveInputContexts;

    /** The gameplay-depth context SetDepthContext put on the stack. */
    FName DepthContextId;

    FVector2D MoveInput = FVector2D::ZeroVector;

    /** The AI controller displaced by TakeControlOf; it resumes the pawn on release. */
    UPROPERTY(Transient)
    AAIController* DisplacedAIController;

    /** The non-football pawn (the game mode's spectator) held before taking control. */
    UPROPERTY(Transient)
    APawn* ParkedPawn;
};
