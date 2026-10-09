// PSPlayerController.h - Epic 126: the project player controller; owns all human input
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PSPlayerController.generated.h"

class UEnhancedInputComponent;
class UPSInputConfig;
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
 * Move and Sprint drive the pawn here. Every other Boolean catalog action is broadcast on
 * OnCatalogActionStarted by ID for its consumer (Epic 127's player switch, Epic 101's menus)
 * to subscribe to -- no consumer casts to this controller to read input.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    APSPlayerController();

    /** The action catalog, loaded from Data/input_actions.json on first use when no
     *  InputConfig is assigned. */
    UFUNCTION(BlueprintPure, Category = "Input")
    UPSInputConfig* GetInputConfig();

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

    /** Fires once per press for every Boolean catalog action without a dedicated handler. */
    UPROPERTY(BlueprintAssignable, Category = "Input")
    FPSInputCatalogActionSignature OnCatalogActionStarted;

    /** Optional override for the catalog; when null one is created from the default path. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UPSInputConfig* InputConfig;

    /** Catalog context applied while possessing an APSPlayerPawn. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    FName GameplayContextId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    FName MoveActionId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    FName SprintActionId;

protected:
    virtual void SetupInputComponent() override;
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;

private:
    void HandleSprintStarted(const FInputActionValue& Value);
    void HandleSprintCompleted(const FInputActionValue& Value);
    void HandleCatalogActionStarted(const FInputActionInstance& Instance);

    void PushInputContext(FName ContextId);
    void PopInputContext(FName ContextId);

    UPROPERTY(Transient)
    TArray<FName> ActiveInputContexts;
};
