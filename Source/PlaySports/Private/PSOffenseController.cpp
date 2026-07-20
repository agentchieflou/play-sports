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
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "EngineUtils.h"
#include "Engine/World.h"

APSOffenseController::APSOffenseController()
{
    BehaviorTreeAsset = nullptr;

    // Create the Blackboard component here (constructor-time default subobject) rather
    // than lazily via UseBlackboard() in OnPossess: components created via NewObject at
    // runtime are never registered in headless/automation-test worlds, which silently
    // fails UseBlackboard() and leaves every key at KeyID 65535 (invalid).
    BlackboardComp = CreateDefaultSubobject<UBlackboardComponent>(TEXT("BlackboardComponent"));
    Blackboard = BlackboardComp;
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
    UE_LOG(LogTemp, Warning, TEXT("InitializeBlackboardState: BB=%p, HasAsset=%d"), BB, BB ? (BB->GetBlackboardAsset() != nullptr) : 0);
    // Programmatically create BlackboardData fallback if Blackboard or its asset is not initialized
    if (!BB || !BB->GetBlackboardAsset())
    {
        UBlackboardData* TemporaryBBData = NewObject<UBlackboardData>();
        UE_LOG(LogTemp, Warning, TEXT("InitializeBlackboardState: Created TemporaryBBData=%p"), TemporaryBBData);
        if (TemporaryBBData)
        {
            TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Int>(TEXT("PlayPhase"));
            
            UBlackboardKeyType_Object* BallKeyType = TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Object>(TEXT("Ball"));
            if (BallKeyType)
            {
                BallKeyType->BaseClass = UObject::StaticClass();
            }

            UBlackboardKeyType_Object* BallCarrierKeyType = TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Object>(TEXT("BallCarrier"));
            if (BallCarrierKeyType)
            {
                BallCarrierKeyType->BaseClass = UObject::StaticClass();
            }

            TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Bool>(TEXT("bHasPossession"));
            TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Vector>(TEXT("TargetLocation"));
            TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Int>(TEXT("RouteWaypointIndex"));

            for (int32 i = 0; i < TemporaryBBData->Keys.Num(); ++i)
            {
                UBlackboardKeyType_Object* ObjKeyType = Cast<UBlackboardKeyType_Object>(TemporaryBBData->Keys[i].KeyType);
                if (ObjKeyType && !ObjKeyType->BaseClass)
                {
                    if (TemporaryBBData->Keys[i].EntryName == TEXT("SelfActor"))
                    {
                        ObjKeyType->BaseClass = AActor::StaticClass();
                    }
                    else
                    {
                        ObjKeyType->BaseClass = UObject::StaticClass();
                    }
                }
            }

            UE_LOG(LogTemp, Warning, TEXT("InitializeBlackboardState: TemporaryBBData key count=%d"), TemporaryBBData->Keys.Num());
            for (int32 i = 0; i < TemporaryBBData->Keys.Num(); ++i)
            {
                UBlackboardKeyType_Object* ObjKeyType = Cast<UBlackboardKeyType_Object>(TemporaryBBData->Keys[i].KeyType);
                UE_LOG(LogTemp, Warning, TEXT("  Key %d: Name=%s, Type=%s, BaseClass=%s"), 
                    i, 
                    *TemporaryBBData->Keys[i].EntryName.ToString(), 
                    TemporaryBBData->Keys[i].KeyType ? *TemporaryBBData->Keys[i].KeyType->GetName() : TEXT("null"),
                    ObjKeyType && ObjKeyType->BaseClass ? *ObjKeyType->BaseClass->GetName() : TEXT("none"));
            }

            if (BB)
            {
                bool bInitResult = BB->InitializeBlackboard(*TemporaryBBData);
                UE_LOG(LogTemp, Warning, TEXT("InitializeBlackboardState: BB->InitializeBlackboard result=%d, BBAsset=%p"), bInitResult, BB->GetBlackboardAsset());
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

void APSOffenseController::SetAssignedRoute(const TArray<FVector>& WorldSpaceWaypoints)
{
    RouteWaypoints = WorldSpaceWaypoints;
    CurrentWaypointIndex = 0;

    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!BB)
    {
        return;
    }

    BB->SetValueAsInt(TEXT("RouteWaypointIndex"), 0);
    if (RouteWaypoints.Num() > 0)
    {
        BB->SetValueAsVector(TEXT("TargetLocation"), RouteWaypoints[0]);
    }
}

void APSOffenseController::AdvanceToNextWaypoint()
{
    if (CurrentWaypointIndex + 1 >= RouteWaypoints.Num())
    {
        return;
    }

    ++CurrentWaypointIndex;

    UBlackboardComponent* BB = GetBlackboardComponent();
    if (BB)
    {
        BB->SetValueAsInt(TEXT("RouteWaypointIndex"), CurrentWaypointIndex);
        BB->SetValueAsVector(TEXT("TargetLocation"), RouteWaypoints[CurrentWaypointIndex]);
    }
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
