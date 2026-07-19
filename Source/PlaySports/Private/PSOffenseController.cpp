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
        if (Bus)
        {
            Bus->OnPhaseChange.AddDynamic(this, &APSOffenseController::OnPhaseChanged);
            Bus->OnSnap.AddDynamic(this, &APSOffenseController::OnSnapEvent);
            Bus->OnThrow.AddDynamic(this, &APSOffenseController::OnThrowEvent);
            Bus->OnCatch.AddDynamic(this, &APSOffenseController::OnCatchEvent);
            Bus->OnFumble.AddDynamic(this, &APSOffenseController::OnFumbleEvent);
            Bus->OnTackle.AddDynamic(this, &APSOffenseController::OnTackleEvent);
        }
    }
}

void APSOffenseController::OnUnPossess()
{
    // Unsubscribe from telemetry events
    if (GetWorld())
    {
        UPSTelemetryBus* Bus = GetWorld()->GetSubsystem<UPSTelemetryBus>();
        if (Bus)
        {
            Bus->OnPhaseChange.RemoveAll(this);
            Bus->OnSnap.RemoveAll(this);
            Bus->OnThrow.RemoveAll(this);
            Bus->OnCatch.RemoveAll(this);
            Bus->OnFumble.RemoveAll(this);
            Bus->OnTackle.RemoveAll(this);
        }
    }

    Super::OnUnPossess();
}

void APSOffenseController::InitializeBlackboardState()
{
    // Programmatically create BlackboardData fallback if Blackboard is not initialized
    if (!GetBlackboardComponent())
    {
        UBlackboardData* TemporaryBBData = NewObject<UBlackboardData>(this);
        if (TemporaryBBData)
        {
            auto AddKey = [](UBlackboardData* Data, FName Name, UClass* TypeClass)
            {
                FBlackboardEntry Entry;
                Entry.EntryName = Name;
                Entry.KeyType = NewObject<UBlackboardKeyType>(Data, TypeClass);
                Data->Keys.Add(Entry);
            };

            AddKey(TemporaryBBData, TEXT("PlayPhase"), UBlackboardKeyType_Int::StaticClass());
            AddKey(TemporaryBBData, TEXT("Ball"), UBlackboardKeyType_Object::StaticClass());
            AddKey(TemporaryBBData, TEXT("BallCarrier"), UBlackboardKeyType_Object::StaticClass());
            AddKey(TemporaryBBData, TEXT("bHasPossession"), UBlackboardKeyType_Bool::StaticClass());

            UBlackboardComponent* RawBB = nullptr;
            if (UseBlackboard(TemporaryBBData, RawBB))
            {
                Blackboard = RawBB;
            }
        }
    }

    UBlackboardComponent* BB = GetBlackboardComponent();
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
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!BB)
    {
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
}

void APSOffenseController::OnSnapEvent(const FPSTelemetrySnapEvent& Event)
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
