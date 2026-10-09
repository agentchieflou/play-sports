#include "PSPlayerController.h"
#include "PSInputConfig.h"
#include "PSPlayerPawn.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"

APSPlayerController::APSPlayerController()
{
    InputConfig = nullptr;
    GameplayContextId = TEXT("OnField");
    MoveActionId = TEXT("Move");
    SprintActionId = TEXT("Sprint");
}

UPSInputConfig* APSPlayerController::GetInputConfig()
{
    if (!InputConfig)
    {
        InputConfig = NewObject<UPSInputConfig>(this, TEXT("RuntimeInputConfig"));
        InputConfig->LoadFromJson(UPSInputConfig::GetDefaultCatalogPath());
    }
    return InputConfig;
}

bool APSPlayerController::IsInputContextActive(FName ContextId) const
{
    return ActiveInputContexts.Contains(ContextId);
}

void APSPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
    {
        BindCatalogActions(*EnhancedInput);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerController: InputComponent is not a UEnhancedInputComponent; check DefaultInputComponentClass in Config/DefaultInput.ini."));
    }
}

void APSPlayerController::BindCatalogActions(UEnhancedInputComponent& InInputComponent)
{
    UPSInputConfig* Config = GetInputConfig();
    if (!Config)
    {
        return;
    }

    for (const FPSInputActionDef& ActionDef : Config->Catalog.Actions)
    {
        const UInputAction* Action = Config->FindAction(ActionDef.ActionId);
        if (!Action)
        {
            continue;
        }

        if (ActionDef.ActionId == MoveActionId)
        {
            InInputComponent.BindAction(Action, ETriggerEvent::Triggered, this, &APSPlayerController::HandleMove);
        }
        else if (ActionDef.ActionId == SprintActionId)
        {
            InInputComponent.BindAction(Action, ETriggerEvent::Started, this, &APSPlayerController::HandleSprintStarted);
            InInputComponent.BindAction(Action, ETriggerEvent::Completed, this, &APSPlayerController::HandleSprintCompleted);
        }
        else if (ActionDef.ValueType == EInputActionValueType::Boolean)
        {
            InInputComponent.BindAction(Action, ETriggerEvent::Started, this, &APSPlayerController::HandleCatalogActionStarted);
        }
    }
}

void APSPlayerController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (Cast<APSPlayerPawn>(InPawn))
    {
        PushInputContext(GameplayContextId);
    }
}

void APSPlayerController::OnUnPossess()
{
    PopInputContext(GameplayContextId);

    Super::OnUnPossess();
}

void APSPlayerController::HandleMove(const FInputActionValue& Value)
{
    APawn* ControlledPawn = GetPawn();
    const FVector2D Axis = Value.Get<FVector2D>();
    if (!ControlledPawn || Axis.IsNearlyZero())
    {
        return;
    }

    const FRotationMatrix YawMatrix(FRotator(0.f, GetControlRotation().Yaw, 0.f));
    ControlledPawn->AddMovementInput(YawMatrix.GetUnitAxis(EAxis::X), Axis.Y);
    ControlledPawn->AddMovementInput(YawMatrix.GetUnitAxis(EAxis::Y), Axis.X);
}

void APSPlayerController::HandleSprintStarted(const FInputActionValue& Value)
{
    if (APSPlayerPawn* PlayerPawn = Cast<APSPlayerPawn>(GetPawn()))
    {
        PlayerPawn->UseBurst(true);
    }
}

void APSPlayerController::HandleSprintCompleted(const FInputActionValue& Value)
{
    if (APSPlayerPawn* PlayerPawn = Cast<APSPlayerPawn>(GetPawn()))
    {
        PlayerPawn->UseBurst(false);
    }
}

void APSPlayerController::HandleCatalogActionStarted(const FInputActionInstance& Instance)
{
    const FName ActionId = InputConfig ? InputConfig->FindActionId(Instance.GetSourceAction()) : NAME_None;
    if (!ActionId.IsNone())
    {
        OnCatalogActionStarted.Broadcast(ActionId);
    }
}

void APSPlayerController::PushInputContext(FName ContextId)
{
    if (ActiveInputContexts.Contains(ContextId))
    {
        return;
    }

    UPSInputConfig* Config = GetInputConfig();
    UInputMappingContext* Context = Config ? Config->FindContext(ContextId) : nullptr;
    if (!Context)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerController: Input context '%s' is not in the input catalog."), *ContextId.ToString());
        return;
    }

    ActiveInputContexts.Add(ContextId);
    if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
    {
        Subsystem->AddMappingContext(Context, Config->GetContextPriority(ContextId));
    }
}

void APSPlayerController::PopInputContext(FName ContextId)
{
    if (ActiveInputContexts.Remove(ContextId) == 0)
    {
        return;
    }

    UInputMappingContext* Context = InputConfig ? InputConfig->FindContext(ContextId) : nullptr;
    UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
    if (Context && Subsystem)
    {
        Subsystem->RemoveMappingContext(Context);
    }
}
