#pragma once

#include "CoreMinimal.h"
#include "PSPlaybookData.h"
#include "PSPlayOrchestrator.generated.h"

class APSPlayerPawn;
class APSOffenseController;
class APSDefenseController;

/**
 * Resolves one selected FPSPlayDefinition into per-pawn assignments across all 22
 * on-field agents (Epic 17). Offense pawns get world-space route waypoints via
 * APSOffenseController::SetAssignedRoute; defense pawns get coverage/rush/run-fit
 * assignments via APSDefenseController::SetAssignment. Synchronized phase reaction
 * is handled by each controller's own TelemetryBus subscription (Epic C1); this
 * class only performs the one-time distribution at snap time.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSPlayOrchestrator : public UObject
{
    GENERATED_BODY()

public:
    /** Resolves Play into per-pawn assignments for every pawn in OnFieldPawns whose
     *  role matches an assignment slot. RouteLibrary resolves Route assignment kinds
     *  to waypoint offsets; LineOfScrimmage is the world-space origin for offsets. */
    UFUNCTION(BlueprintCallable, Category = "AI|Orchestration")
    void DistributePlayCall(const FPSPlayDefinition& Play, const TArray<APSPlayerPawn*>& OnFieldPawns, const UDataTable* RouteLibrary, const FVector& LineOfScrimmage);

    /** Broken-play adaptation: redirects offensive skill players still running routes
     *  toward space near the scrambling QB's current location. */
    UFUNCTION(BlueprintCallable, Category = "AI|Orchestration")
    void TriggerScrambleDrill(const TArray<APSPlayerPawn*>& OnFieldPawns, const FVector& QBLocation);

    /** Seeds the deterministic RNG used for any orchestration-level randomness
     *  (e.g. coverage jitter), so a play can be re-simulated identically for replay/debug. */
    UFUNCTION(BlueprintCallable, Category = "AI|Orchestration")
    void SeedDeterminism(int32 Seed);

    UFUNCTION(BlueprintPure, Category = "AI|Orchestration")
    int32 GetCurrentSeed() const { return DeterminismStream.GetCurrentSeed(); }

private:
    static EPSDefensiveAssignmentType ToDefensiveAssignmentType(EPSAssignmentKind Kind);

    TArray<FVector> ResolveRouteWaypoints(const FName& RouteId, const UDataTable* RouteLibrary, const FVector& Origin) const;

    UPROPERTY(Transient)
    FRandomStream DeterminismStream;
};
