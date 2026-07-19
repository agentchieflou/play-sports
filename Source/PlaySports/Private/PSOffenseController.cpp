#include "PSOffenseController.h"
#include "PSPlayerPawn.h"
#include "PSGameMode.h"
#include "PSBall.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "EngineUtils.h"
#include "Engine/World.h"

APSOffenseController::APSOffenseController()
{
    BehaviorTreeAsset = nullptr;
}

void APSOffenseController::OnPossess(APawn* InPawn)
{
    UE_LOG(LogTemp, Log, TEXT("APSOffenseController::OnPossess: Controller=%p Possessing Pawn=%p"), this, InPawn);
    Super::OnPossess(InPawn);

    if (BehaviorTreeAsset)
    {
        RunBehaviorTree(BehaviorTreeAsset);
    }

    InitializeBlackboardState();

    // Subscribe to telemetry events
    if (GetWorld())
    {
        UPSTelemetryBus* Bus = GetWorld()->GetSubsystem<UPSTelemetryBus>();
        UE_LOG(LogTemp, Log, TEXT("APSOffenseController::OnPossess: World=%p, Bus=%p"), GetWorld(), Bus);
        if (Bus)
        {
            Bus->OnPhaseChangeMC.AddUObject(this, &APSOffenseController::OnPhaseChanged);
            Bus->OnSnapMC.AddUObject(this, &APSOffenseController::OnSnapEvent);
            Bus->OnThrowMC.AddUObject(this, &APSOffenseController::OnThrowEvent);
            Bus->OnCatchMC.AddUObject(this, &APSOffenseController::OnCatchEvent);
            Bus->OnFumbleMC.AddUObject(this, &APSOffenseController::OnFumbleEvent);
            Bus->OnTackleMC.AddUObject(this, &APSOffenseController::OnTackleEvent);
            UE_LOG(LogTemp, Log, TEXT("APSOffenseController::OnPossess: Subscribed to telemetry bus MC delegates successfully."));
        }
    }
}

void APSOffenseController::OnUnPossess()
{
    UE_LOG(LogTemp, Log, TEXT("APSOffenseController::OnUnPossess: Controller=%p"), this);
    // Unsubscribe from telemetry events
    if (GetWorld())
    {
        UPSTelemetryBus* Bus = GetWorld()->GetSubsystem<UPSTelemetryBus>();
        if (Bus)
        {
            Bus->OnPhaseChangeMC.RemoveAll(this);
            Bus->OnSnapMC.RemoveAll(this);
            Bus->OnThrowMC.RemoveAll(this);
            Bus->OnCatchMC.RemoveAll(this);
            Bus->OnFumbleMC.RemoveAll(this);
            Bus->OnTackleMC.RemoveAll(this);
        }
    }

    Super::OnUnPossess();
}

void APSOffenseController::InitializeBlackboardState()
{
    UBlackboardComponent* BB = GetBlackboardComponent();
    // Programmatically create BlackboardData fallback if Blackboard or its asset is not initialized
    if (!BB || !BB->GetBlackboardAsset())
    {
        UBlackboardData* TemporaryBBData = NewObject<UBlackboardData>(this);
        if (TemporaryBBData)
        {
            TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Int>(TEXT("PlayPhase"));
            TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Object>(TEXT("Ball"));
            TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Object>(TEXT("BallCarrier"));
            TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Bool>(TEXT("bHasPossession"));

            if (BB)
            {
                BB->InitializeBlackboard(*TemporaryBBData);
            }
            else
            {
                UBlackboardComponent* RawBB = nullptr;
                if (UseBlackboard(TemporaryBBData, RawBB))
                {
                    Blackboard = RawBB;
                }
            }
        }
    }

    BB = GetBlackboardComponent();
    if (!BB)
    {
        return;
    }

    BB->SetValueAsInt(TEXT("PlayPhase"), 0); // PreSnap
    BB->SetValueAsObject(TEXT("Ball"), nullptr);
    BB->SetValueAsObject(TEXT("BallCarrier"), nullptr);
    BB->SetValueAsBool(TEXT("bHasPossession"), false);
}

void APSOffenseController::OnPhaseChanged(const FPSTelemetryPhaseChangeEvent& Event)
{
    UE_LOG(LogTemp, Warning, TEXT("APSOffenseController::OnPhaseChanged: Controller=%p, NewPhase=%s"), this, *Event.NewPhase);
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!BB)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSOffenseController::OnPhaseChanged: Blackboard is null!"));
        return;
    }

    int32 PhaseVal = 0;
    if (Event.NewPhase == TEXT("PreSnap")) PhaseVal = 0;
    else if (Event.NewPhase == TEXT("Snap")) PhaseVal = 1;
    else if (Event.NewPhase == TEXT("PassRush")) PhaseVal = 2;
    else if (Event.NewPhase == TEXT("BallCarrierMovement")) PhaseVal = 3;
    else if (Event.NewPhase == TEXT("Scoring")) PhaseVal = 4;
    else if (Event.NewPhase == TEXT("Kickoff")) PhaseVal = 5;
    else if (Event.NewPhase == TEXT("Punt")) PhaseVal = 6;
    else if (Event.NewPhase == TEXT("FieldGoal")) PhaseVal = 7;

    BB->SetValueAsInt(TEXT("PlayPhase"), PhaseVal);
    UE_LOG(LogTemp, Warning, TEXT("APSOffenseController::OnPhaseChanged: Updated PlayPhase key to %d"), PhaseVal);
}

void APSOffenseController::OnSnapEvent(const FPSTelemetrySnapEvent& Event)
{
    UE_LOG(LogTemp, Warning, TEXT("APSOffenseController::OnSnapEvent: Controller=%p"), this);
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!BB)
    {
        return;
    }

    APSPlayerPawn* PossessedPawn = Cast<APSPlayerPawn>(GetPawn());
    if (PossessedPawn)
    {
        BB->SetValueAsBool(TEXT("bHasPossession"), PossessedPawn->HasPossession());
    }
}

void APSOffenseController::OnThrowEvent(const FPSTelemetryThrowEvent& Event)
{
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!BB)
    {
        return;
    }

    BB->SetValueAsObject(TEXT("BallCarrier"), nullptr);
    BB->SetValueAsBool(TEXT("bHasPossession"), false);

    if (GetWorld())
    {
        APSGameMode* GM = Cast<APSGameMode>(GetWorld()->GetAuthGameMode());
        if (GM && GM->ActiveBall)
        {
            BB->SetValueAsObject(TEXT("Ball"), GM->ActiveBall);
        }
    }
}

void APSOffenseController::OnCatchEvent(const FPSTelemetryCatchEvent& Event)
{
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!BB)
    {
        return;
    }

    APSPlayerPawn* PossessedPawn = Cast<APSPlayerPawn>(GetPawn());
    if (PossessedPawn)
    {
        BB->SetValueAsBool(TEXT("bHasPossession"), PossessedPawn->HasPossession());

        if (GetWorld())
        {
            for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
            {
                if (It->HasPossession())
                {
                    BB->SetValueAsObject(TEXT("BallCarrier"), *It);
                    break;
                }
            }
        }
    }
}

void APSOffenseController::OnFumbleEvent(const FPSTelemetryFumbleEvent& Event)
{
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!BB)
    {
        return;
    }

    BB->SetValueAsObject(TEXT("BallCarrier"), nullptr);
    BB->SetValueAsBool(TEXT("bHasPossession"), false);
}

void APSOffenseController::OnTackleEvent(const FPSTelemetryTackleEvent& Event)
{
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!BB)
    {
        return;
    }

    BB->SetValueAsObject(TEXT("BallCarrier"), nullptr);
    BB->SetValueAsBool(TEXT("bHasPossession"), false);
}
