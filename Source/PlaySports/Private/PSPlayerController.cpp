#include "PSPlayerController.h"
#include "PSInputConfig.h"
#include "PSInputDeviceComponent.h"
#include "PSForceFeedbackComponent.h"
#include "PSMenuComponent.h"
#include "PSPlayCallComponent.h"
#include "PSPlayContextComponent.h"
#include "PSPassingComponent.h"
#include "PSCarrierInputComponent.h"
#include "PSPreSnapInputComponent.h"
#include "PSInputBufferComponent.h"
#include "PSTouchInputComponent.h"
#include "PSPlayerPawn.h"
#include "PSBall.h"
#include "PSBroadcastCamera.h"
#include "PSHealthComponent.h"
#include "PSPossessionComponent.h"
#include "PSTelemetryBus.h"
#include "AIController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

APSPlayerController::APSPlayerController()
{
    InputConfig = nullptr;
    GameplayContextId = TEXT("OnField");
    MoveActionId = TEXT("Move");
    SprintActionId = TEXT("Sprint");
    SwitchPlayerActionId = TEXT("SwitchPlayer");
    PauseActionId = TEXT("Pause");
    HumanSide = EPSTeamSide::Offense;
    DefaultControlRole = EPlayerRole::Quarterback;
    bTakeDefaultControlOnBeginPlay = true;
    DisplacedAIController = nullptr;
    ParkedPawn = nullptr;

    InputDeviceComponent = CreateDefaultSubobject<UPSInputDeviceComponent>(TEXT("InputDeviceComp"));
    ForceFeedbackComponent = CreateDefaultSubobject<UPSForceFeedbackComponent>(TEXT("ForceFeedbackComp"));
    MenuComponent = CreateDefaultSubobject<UPSMenuComponent>(TEXT("MenuComp"));
    PlayCallComponent = CreateDefaultSubobject<UPSPlayCallComponent>(TEXT("PlayCallComp"));
    PlayContextComponent = CreateDefaultSubobject<UPSPlayContextComponent>(TEXT("PlayContextComp"));
    PassingComponent = CreateDefaultSubobject<UPSPassingComponent>(TEXT("PassingComp"));
    CarrierInputComponent = CreateDefaultSubobject<UPSCarrierInputComponent>(TEXT("CarrierInputComp"));
    PreSnapInputComponent = CreateDefaultSubobject<UPSPreSnapInputComponent>(TEXT("PreSnapInputComp"));
    InputBufferComponent = CreateDefaultSubobject<UPSInputBufferComponent>(TEXT("InputBufferComp"));
    TouchInputComponent = CreateDefaultSubobject<UPSTouchInputComponent>(TEXT("TouchInputComp"));
}

UPSInputConfig* APSPlayerController::GetInputConfig()
{
    if (!InputConfig)
    {
        InputConfig = NewObject<UPSInputConfig>(this, TEXT("RuntimeInputConfig"));
        InputConfig->LoadDefaults();
    }
    if (InputDeviceComponent)
    {
        InputDeviceComponent->AnalogThreshold = InputConfig->Tuning.DeviceSwitchAnalogThreshold;
    }
    return InputConfig;
}

bool APSPlayerController::IsInputContextActive(FName ContextId) const
{
    return ActiveInputContexts.Contains(ContextId);
}

bool APSPlayerController::InjectCatalogInput(FName ActionId, const FInputActionValue& RawValue, const TArray<UInputModifier*>& Modifiers, const TArray<UInputTrigger*>& Triggers)
{
    UPSInputConfig* Config = GetInputConfig();
    const UInputAction* Action = Config ? Config->FindAction(ActionId) : nullptr;
    UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
    if (!Action || !Subsystem)
    {
        return false;
    }

    Subsystem->InjectInputForAction(Action, RawValue, Modifiers, Triggers);
    return true;
}

void APSPlayerController::BeginPlay()
{
    Super::BeginPlay();

    // The game mode spawns the roster after BeginPlay, in the same frame; take control on
    // the next tick once the pawns exist.
    if (bTakeDefaultControlOnBeginPlay && IsLocalController())
    {
        GetWorldTimerManager().SetTimerForNextTick(this, &APSPlayerController::HandleDeferredDefaultControl);
    }
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
            InInputComponent.BindAction(Action, ETriggerEvent::Completed, this, &APSPlayerController::HandleMoveCompleted);
        }
        else if (ActionDef.ActionId == SprintActionId)
        {
            InInputComponent.BindAction(Action, ETriggerEvent::Started, this, &APSPlayerController::HandleSprintStarted);
            InInputComponent.BindAction(Action, ETriggerEvent::Completed, this, &APSPlayerController::HandleSprintCompleted);
        }
        else if (ActionDef.ActionId == SwitchPlayerActionId)
        {
            InInputComponent.BindAction(Action, ETriggerEvent::Started, this, &APSPlayerController::HandleSwitchPlayer);
        }
        else if (ActionDef.ActionId == PauseActionId)
        {
            InInputComponent.BindAction(Action, ETriggerEvent::Started, this, &APSPlayerController::HandlePause);
        }
        else if (ActionDef.ValueType == EInputActionValueType::Boolean)
        {
            InInputComponent.BindAction(Action, ETriggerEvent::Started, this, &APSPlayerController::HandleCatalogActionStarted);
            InInputComponent.BindAction(Action, ETriggerEvent::Completed, this, &APSPlayerController::HandleCatalogActionCompleted);
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
    SetDepthContext(NAME_None);
    PopInputContext(GameplayContextId);
    // Presses still waiting were for the player being let go.
    if (InputBufferComponent)
    {
        InputBufferComponent->Flush();
    }

    Super::OnUnPossess();
}

bool APSPlayerController::TakeControlOf(APSPlayerPawn* Target)
{
    if (!Target)
    {
        return false;
    }
    if (GetPawn() == Target)
    {
        return true;
    }

    // Moving between football pawns hands the old one back to its AI first; a non-football
    // pawn (the spectator) is parked to return to on ReleaseControl.
    if (Cast<APSPlayerPawn>(GetPawn()))
    {
        ReturnControlledPawnToAI();
    }
    else if (GetPawn())
    {
        // Parked out of sight and out of the way so it can't block play.
        ParkedPawn = GetPawn();
        ParkedPawn->SetActorHiddenInGame(true);
        ParkedPawn->SetActorEnableCollision(false);
    }

    DisplacedAIController = Cast<AAIController>(Target->GetController());
    Possess(Target);
    if (GetPawn() != Target)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerController: Could not take control of %s."), *Target->GetAttributes().DisplayName);
        DisplacedAIController = nullptr;
        return false;
    }

    ViewThroughBroadcastCamera();
    PublishControlChange(Target, true);
    UE_LOG(LogTemp, Display, TEXT("APSPlayerController: Human now controls %s."), *Target->GetAttributes().DisplayName);
    return true;
}

void APSPlayerController::ReleaseControl()
{
    if (!Cast<APSPlayerPawn>(GetPawn()))
    {
        return;
    }

    ReturnControlledPawnToAI();

    if (IsValid(ParkedPawn) && !ParkedPawn->GetController())
    {
        ParkedPawn->SetActorHiddenInGame(false);
        ParkedPawn->SetActorEnableCollision(true);
        Possess(ParkedPawn);
    }
    ParkedPawn = nullptr;
}

void APSPlayerController::ReturnControlledPawnToAI()
{
    APSPlayerPawn* Released = Cast<APSPlayerPawn>(GetPawn());
    if (!Released)
    {
        return;
    }

    AAIController* ResumingAI = DisplacedAIController;
    DisplacedAIController = nullptr;

    UnPossess();
    if (IsValid(ResumingAI) && !ResumingAI->GetPawn())
    {
        ResumingAI->Possess(Released);
    }
    else
    {
        Released->SpawnDefaultController();
    }

    PublishControlChange(Released, false);
}

bool APSPlayerController::TakeDefaultControl()
{
    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        APSPlayerPawn* Candidate = *It;
        if (Candidate->TeamSide == HumanSide && Candidate->GetAttributes().Role == DefaultControlRole)
        {
            return TakeControlOf(Candidate);
        }
    }
    return false;
}

void APSPlayerController::HandleDeferredDefaultControl()
{
    if (!Cast<APSPlayerPawn>(GetPawn()) && !TakeDefaultControl())
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerController: No %s on the human's side to take control of; the AI keeps every pawn."),
            *UEnum::GetValueAsString(DefaultControlRole));
    }
}

bool APSPlayerController::SwitchToBestPawn(const FVector& BallLocation)
{
    APSPlayerPawn* Current = Cast<APSPlayerPawn>(GetPawn());
    const EPSTeamSide Side = Current ? Current->TeamSide : HumanSide;

    APSPlayerPawn* Best = nullptr;
    float BestDistanceSq = TNumericLimits<float>::Max();
    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        APSPlayerPawn* Candidate = *It;
        if (Candidate->TeamSide != Side)
        {
            continue;
        }

        // The possession component is the authority on who has the ball (rule 6): a
        // carrier on our side always gets control.
        const UPSPossessionComponent* Possession = Candidate->GetPossessionComponent();
        if (Possession && Possession->HasPossession())
        {
            Best = Candidate;
            break;
        }

        const UPSHealthComponent* Health = Candidate->GetHealthComponent();
        if (Candidate == Current || (Health && Health->IsDowned()))
        {
            continue;
        }

        const float DistanceSq = FVector::DistSquared(Candidate->GetActorLocation(), BallLocation);
        if (DistanceSq < BestDistanceSq)
        {
            BestDistanceSq = DistanceSq;
            Best = Candidate;
        }
    }

    if (!Best || Best == Current)
    {
        return false;
    }
    return TakeControlOf(Best);
}

void APSPlayerController::HandleMove(const FInputActionValue& Value)
{
    APawn* ControlledPawn = GetPawn();
    const FVector2D Axis = Value.Get<FVector2D>();
    MoveInput = Axis;
    if (!ControlledPawn || Axis.IsNearlyZero())
    {
        return;
    }

    const FRotationMatrix YawMatrix(FRotator(0.f, GetControlRotation().Yaw, 0.f));
    ControlledPawn->AddMovementInput(YawMatrix.GetUnitAxis(EAxis::X), Axis.Y);
    ControlledPawn->AddMovementInput(YawMatrix.GetUnitAxis(EAxis::Y), Axis.X);
}

void APSPlayerController::HandleMoveCompleted(const FInputActionValue& Value)
{
    MoveInput = FVector2D::ZeroVector;
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

void APSPlayerController::HandleSwitchPlayer(const FInputActionValue& Value)
{
    TActorIterator<APSBall> BallIt(GetWorld());
    if (BallIt)
    {
        SwitchToBestPawn(BallIt->GetActorLocation());
    }
}

void APSPlayerController::HandlePause(const FInputActionValue& Value)
{
    if (MenuComponent)
    {
        MenuComponent->TogglePause();
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

void APSPlayerController::HandleCatalogActionCompleted(const FInputActionInstance& Instance)
{
    const FName ActionId = InputConfig ? InputConfig->FindActionId(Instance.GetSourceAction()) : NAME_None;
    if (!ActionId.IsNone())
    {
        OnCatalogActionCompleted.Broadcast(ActionId);
    }
}

void APSPlayerController::SetDepthContext(FName ContextId)
{
    if (ContextId == DepthContextId)
    {
        return;
    }
    const TArray<FName> ContextsBefore = ActiveInputContexts;
    if (!DepthContextId.IsNone())
    {
        PopInputContext(DepthContextId);
    }
    DepthContextId = NAME_None;
    if (!ContextId.IsNone())
    {
        PushInputContext(ContextId);
        if (IsInputContextActive(ContextId))
        {
            DepthContextId = ContextId;
            if (InputBufferComponent)
            {
                InputBufferComponent->HandleContextEntered(ContextId, ContextsBefore);
            }
        }
    }
}

void APSPlayerController::PublishControlChange(const APSPlayerPawn* PlayerPawn, bool bHumanControlled)
{
    UPSTelemetryBus* Bus = GetWorld() ? GetWorld()->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus || !PlayerPawn)
    {
        return;
    }

    const FPlayerAttributes Attributes = PlayerPawn->GetAttributes();
    FPSTelemetryControlChangeEvent Event;
    Event.PlayerName = Attributes.DisplayName;
    Event.PlayerId = Attributes.PlayerId;
    Event.bHumanControlled = bHumanControlled;
    Bus->PublishControlChange(Event);
}

void APSPlayerController::ViewThroughBroadcastCamera()
{
    // Possession would otherwise put the view inside the pawn's capsule; the broadcast
    // camera (Epic 4) is the game's view whenever one is in the level.
    TActorIterator<APSBroadcastCamera> CameraIt(GetWorld());
    if (CameraIt)
    {
        SetViewTarget(*CameraIt);
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
