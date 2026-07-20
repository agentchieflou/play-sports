#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "PSTelemetryBus.h"
#include "PSDefenseController.generated.h"

class UBehaviorTree;
class APSPlayerPawn;

UENUM(BlueprintType)
enum class EPSDefensiveAssignmentType : uint8
{
    PassRush,
    Contain,
    ManCoverage,
    ZoneCoverage,
    RunFit,
    Block
};

/**
 * APSDefenseController drives defensive positions (DL, LB, DB) and OL run/pass
 * blocking assignments using Behavior Trees synchronized via UPSTelemetryBus events.
 * Assignment (man/zone/rush/run-fit) is set externally per-play (Epic 16/17 feed this;
 * defaults to a sensible per-role assignment when unset).
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSDefenseController : public AAIController
{
    GENERATED_BODY()

public:
    APSDefenseController();

    /** Assign this defender's responsibility for the current play. Called by the
     *  play orchestrator (Epic 17) or defaulted from role if never called. */
    UFUNCTION(BlueprintCallable, Category = "AI|Defense")
    void SetAssignment(EPSDefensiveAssignmentType NewAssignment, AActor* CoverageTarget = nullptr, FVector ZoneLocation = FVector::ZeroVector);

    UFUNCTION(BlueprintPure, Category = "AI|Defense")
    EPSDefensiveAssignmentType GetAssignment() const { return CurrentAssignment; }

    /** Attribute-scaled pursuit angle: predicts an intercept point ahead of the ball
     *  carrier's current velocity, scaled by this controller's Awareness attribute
     *  (higher awareness reads the carrier's path more accurately). */
    UFUNCTION(BlueprintPure, Category = "AI|Defense")
    FVector ComputePursuitInterceptPoint(const FVector& CarrierLocation, const FVector& CarrierVelocity, const FVector& SelfLocation) const;

protected:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
    UBehaviorTree* BehaviorTreeAsset;

private:
    UFUNCTION()
    void OnPhaseChanged(const FPSTelemetryPhaseChangeEvent& Event);

    UFUNCTION()
    void OnSnapEvent(const FPSTelemetrySnapEvent& Event);

    UFUNCTION()
    void OnCatchEvent(const FPSTelemetryCatchEvent& Event);

    UFUNCTION()
    void OnTackleEvent(const FPSTelemetryTackleEvent& Event);

    UFUNCTION()
    void OnFumbleEvent(const FPSTelemetryFumbleEvent& Event);

    void InitializeBlackboardState();
    EPSDefensiveAssignmentType DefaultAssignmentForRole() const;

    EPSDefensiveAssignmentType CurrentAssignment;

    UPROPERTY(Transient)
    AActor* AssignmentCoverageTarget;

    FVector AssignmentZoneLocation;
};
