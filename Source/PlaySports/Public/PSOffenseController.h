#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "PSTelemetryBus.h"
#include "PSOffenseController.generated.h"

class UBehaviorTree;
class UBlackboardComponent;
class UPSSkillPlayerAIComponent;
class UPSRouteRunnerComponent;
class UPSPocketComponent;

/**
 * APSOffenseController drives offensive skill players (QB, RB, WR, TE). Its
 * UPSSkillPlayerAIComponent plays the called play in C++ (Epic 14); the blackboard keys are
 * kept in sync for Behavior Tree assets an editor session may add later.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSOffenseController : public AAIController
{
    GENERATED_BODY()

public:
    APSOffenseController();

    /** Assign a route as a sequence of world-space waypoints (formation offsets already
     *  resolved by the play orchestrator, Epic 17). Sets TargetLocation/RouteWaypointIndex
     *  blackboard keys consumed by the BT route-running task. */
    UFUNCTION(BlueprintCallable, Category = "AI|Offense")
    void SetAssignedRoute(const TArray<FVector>& WorldSpaceWaypoints);

    UFUNCTION(BlueprintCallable, Category = "AI|Offense")
    void AdvanceToNextWaypoint();

    /** In-memory mirror of the "PlayPhase" blackboard key (0=PreSnap..7=FieldGoal, see
     *  OnPhaseChanged). This is the authoritative read for game logic/tests; the
     *  Blackboard is a best-effort mirror kept for future editor-authored BT assets. */
    UFUNCTION(BlueprintPure, Category = "AI|Offense")
    int32 GetCurrentPlayPhaseValue() const { return CurrentPlayPhaseValue; }

    UFUNCTION(BlueprintPure, Category = "AI|Offense")
    bool GetHasPossessionValue() const { return bHasPossessionValue; }

    UFUNCTION(BlueprintPure, Category = "AI|Offense")
    FVector GetCurrentTargetLocation() const { return RouteWaypoints.IsValidIndex(CurrentWaypointIndex) ? RouteWaypoints[CurrentWaypointIndex] : FVector::ZeroVector; }

    UFUNCTION(BlueprintPure, Category = "AI|Offense")
    int32 GetRouteWaypointIndex() const { return CurrentWaypointIndex; }

    UFUNCTION(BlueprintPure, Category = "AI|Offense")
    int32 GetRouteWaypointCount() const { return RouteWaypoints.Num(); }

    /** The whole route, world-space. */
    const TArray<FVector>& GetRouteWaypoints() const { return RouteWaypoints; }

    /** Plays the called play for the possessed pawn (Epic 14): routes, reads, hand-offs. */
    UFUNCTION(BlueprintPure, Category = "AI|Offense")
    UPSSkillPlayerAIComponent* GetSkillAI() const { return SkillAI; }

    /** Runs the route as a contested skill: release, breaks, double moves, option reads (Epic 68). */
    UFUNCTION(BlueprintPure, Category = "AI|Offense")
    UPSRouteRunnerComponent* GetRouteRunner() const { return RouteRunner; }

    /** A quarterback's pocket: climb, slide, escape, scramble, and how a sack ends (Epic 71). */
    UFUNCTION(BlueprintPure, Category = "AI|Offense")
    UPSPocketComponent* GetPocket() const { return Pocket; }

protected:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
    UBehaviorTree* BehaviorTreeAsset;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
    UBlackboardComponent* BlackboardComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
    UPSSkillPlayerAIComponent* SkillAI;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
    UPSRouteRunnerComponent* RouteRunner;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
    UPSPocketComponent* Pocket;

private:
    UFUNCTION()
    void OnPhaseChanged(const FPSTelemetryPhaseChangeEvent& Event);

    UFUNCTION()
    void OnSnapEvent(const FPSTelemetrySnapEvent& Event);

    UFUNCTION()
    void OnThrowEvent(const FPSTelemetryThrowEvent& Event);

    UFUNCTION()
    void OnCatchEvent(const FPSTelemetryCatchEvent& Event);

    UFUNCTION()
    void OnFumbleEvent(const FPSTelemetryFumbleEvent& Event);

    UFUNCTION()
    void OnTackleEvent(const FPSTelemetryTackleEvent& Event);

    void InitializeBlackboardState();

    UPROPERTY(Transient)
    TArray<FVector> RouteWaypoints;

    int32 CurrentWaypointIndex = 0;

    UPROPERTY(Transient)
    int32 CurrentPlayPhaseValue = 0;

    UPROPERTY(Transient)
    bool bHasPossessionValue = false;
};
