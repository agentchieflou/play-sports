#include "PSDefenseController.h"
#include "PSPlayerPawn.h"
#include "PSGameMode.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "EngineUtils.h"
#include "Engine/World.h"

APSDefenseController::APSDefenseController()
{
    BehaviorTreeAsset = nullptr;
    CurrentAssignment = EPSDefensiveAssignmentType::RunFit;
    AssignmentCoverageTarget = nullptr;
    AssignmentZoneLocation = FVector::ZeroVector;

    // Constructor-time default subobject: components created via NewObject at runtime
    // are never registered in headless/automation-test worlds, which silently fails
    // UseBlackboard() and leaves every key at KeyID 65535 (invalid).
    BlackboardComp = CreateDefaultSubobject<UBlackboardComponent>(TEXT("BlackboardComponent"));
    Blackboard = BlackboardComp;
}

void APSDefenseController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (BehaviorTreeAsset)
    {
        RunBehaviorTree(BehaviorTreeAsset);
    }

    CurrentAssignment = DefaultAssignmentForRole();
    InitializeBlackboardState();

    if (GetWorld())
    {
        UPSTelemetryBus* Bus = GetWorld()->GetSubsystem<UPSTelemetryBus>();
        if (Bus)
        {
            Bus->OnPhaseChangeMC.AddUObject(this, &APSDefenseController::OnPhaseChanged);
            Bus->OnSnapMC.AddUObject(this, &APSDefenseController::OnSnapEvent);
            Bus->OnCatchMC.AddUObject(this, &APSDefenseController::OnCatchEvent);
            Bus->OnTackleMC.AddUObject(this, &APSDefenseController::OnTackleEvent);
            Bus->OnFumbleMC.AddUObject(this, &APSDefenseController::OnFumbleEvent);
        }
    }
}

void APSDefenseController::OnUnPossess()
{
    if (GetWorld())
    {
        UPSTelemetryBus* Bus = GetWorld()->GetSubsystem<UPSTelemetryBus>();
        if (Bus)
        {
            Bus->OnPhaseChangeMC.RemoveAll(this);
            Bus->OnSnapMC.RemoveAll(this);
            Bus->OnCatchMC.RemoveAll(this);
            Bus->OnTackleMC.RemoveAll(this);
            Bus->OnFumbleMC.RemoveAll(this);
        }
    }

    Super::OnUnPossess();
}

EPSDefensiveAssignmentType APSDefenseController::DefaultAssignmentForRole() const
{
    const APSPlayerPawn* PossessedPawn = Cast<APSPlayerPawn>(GetPawn());
    if (!PossessedPawn)
    {
        return EPSDefensiveAssignmentType::RunFit;
    }

    switch (PossessedPawn->GetAttributes().Role)
    {
    case EPlayerRole::DefensiveLineman:
        return EPSDefensiveAssignmentType::PassRush;
    case EPlayerRole::Linebacker:
        return EPSDefensiveAssignmentType::RunFit;
    case EPlayerRole::DefensiveBack:
        return EPSDefensiveAssignmentType::ManCoverage;
    case EPlayerRole::OffensiveLineman:
        return EPSDefensiveAssignmentType::Block;
    default:
        return EPSDefensiveAssignmentType::RunFit;
    }
}

void APSDefenseController::SetAssignment(EPSDefensiveAssignmentType NewAssignment, AActor* CoverageTarget, FVector ZoneLocation)
{
    CurrentAssignment = NewAssignment;
    AssignmentCoverageTarget = CoverageTarget;
    AssignmentZoneLocation = ZoneLocation;

    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!BB)
    {
        return;
    }

    BB->SetValueAsInt(TEXT("AssignmentType"), static_cast<int32>(CurrentAssignment));
    BB->SetValueAsObject(TEXT("CoverageTarget"), CoverageTarget);
    BB->SetValueAsVector(TEXT("ZoneLocation"), ZoneLocation);
}

FVector APSDefenseController::ComputePursuitInterceptPoint(const FVector& CarrierLocation, const FVector& CarrierVelocity, const FVector& SelfLocation) const
{
    const APSPlayerPawn* PossessedPawn = Cast<APSPlayerPawn>(GetPawn());
    const float Awareness = PossessedPawn ? PossessedPawn->GetAttributes().Awareness : 50.f;

    // Awareness (0-100) scales how far ahead of the carrier's current velocity the
    // defender reads/leads the pursuit angle: low awareness chases the current
    // position, high awareness leads the intercept point.
    const float AwarenessLeadSeconds = FMath::GetMappedRangeValueClamped(FVector2D(0.f, 100.f), FVector2D(0.1f, 0.9f), Awareness);

    const float DistanceToCarrier = FVector::Dist(SelfLocation, CarrierLocation);
    const float ClosingTimeEstimate = DistanceToCarrier / FMath::Max(CarrierVelocity.Size(), 1.f);
    const float LeadSeconds = FMath::Min(ClosingTimeEstimate, 2.f) * AwarenessLeadSeconds;

    return CarrierLocation + (CarrierVelocity * LeadSeconds);
}

void APSDefenseController::InitializeBlackboardState()
{
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!BB || !BB->GetBlackboardAsset())
    {
        UBlackboardData* TemporaryBBData = NewObject<UBlackboardData>();
        if (TemporaryBBData)
        {
            TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Int>(TEXT("PlayPhase"));
            TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Int>(TEXT("AssignmentType"));
            TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Vector>(TEXT("ZoneLocation"));
            TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Bool>(TEXT("bIsDefenderNear"));

            UBlackboardKeyType_Object* CoverageKeyType = TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Object>(TEXT("CoverageTarget"));
            if (CoverageKeyType)
            {
                CoverageKeyType->BaseClass = UObject::StaticClass();
            }

            UBlackboardKeyType_Object* BallCarrierKeyType = TemporaryBBData->UpdatePersistentKey<UBlackboardKeyType_Object>(TEXT("BallCarrier"));
            if (BallCarrierKeyType)
            {
                BallCarrierKeyType->BaseClass = UObject::StaticClass();
            }

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

            if (BB)
            {
                BB->InitializeBlackboard(*TemporaryBBData);
            }
        }
    }

    BB = GetBlackboardComponent();
    if (!BB)
    {
        return;
    }

    BB->SetValueAsInt(TEXT("PlayPhase"), 0); // PreSnap
    BB->SetValueAsInt(TEXT("AssignmentType"), static_cast<int32>(CurrentAssignment));
    BB->SetValueAsObject(TEXT("CoverageTarget"), nullptr);
    BB->SetValueAsObject(TEXT("BallCarrier"), nullptr);
    BB->SetValueAsVector(TEXT("ZoneLocation"), FVector::ZeroVector);
    BB->SetValueAsBool(TEXT("bIsDefenderNear"), false);
}

void APSDefenseController::OnPhaseChanged(const FPSTelemetryPhaseChangeEvent& Event)
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

void APSDefenseController::OnSnapEvent(const FPSTelemetrySnapEvent& Event)
{
    // Reset per-play defender-near state at snap; BT services recompute it live.
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (BB)
    {
        BB->SetValueAsBool(TEXT("bIsDefenderNear"), false);
    }
}

void APSDefenseController::OnCatchEvent(const FPSTelemetryCatchEvent& Event)
{
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!BB || !GetWorld())
    {
        return;
    }

    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        if (It->HasPossession())
        {
            BB->SetValueAsObject(TEXT("BallCarrier"), *It);
            break;
        }
    }
}

void APSDefenseController::OnTackleEvent(const FPSTelemetryTackleEvent& Event)
{
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (BB)
    {
        BB->SetValueAsObject(TEXT("BallCarrier"), nullptr);
    }
}

void APSDefenseController::OnFumbleEvent(const FPSTelemetryFumbleEvent& Event)
{
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (BB)
    {
        BB->SetValueAsObject(TEXT("BallCarrier"), nullptr);
    }
}
