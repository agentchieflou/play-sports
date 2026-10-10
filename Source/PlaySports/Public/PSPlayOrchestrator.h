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
     *  role has an assignment slot; a role with more players than slots repeats its last
     *  slot. RouteLibrary resolves Route assignment kinds to waypoint offsets from the
     *  player's own split (LineOfScrimmage's X, the pawn's Y), mirrored for a player left
     *  of the ball; a zone is played on the defender's own side of the field. An offensive
     *  player's pre-snap changes (UPSPreSnapSubsystem: hot route, kept in, released) apply
     *  first, and an offensive player with no route this play has his old one cleared, so
     *  he blocks. */
    UFUNCTION(BlueprintCallable, Category = "AI|Orchestration")
    void DistributePlayCall(const FPSPlayDefinition& Play, const TArray<APSPlayerPawn*>& OnFieldPawns, const UDataTable* RouteLibrary, const FVector& LineOfScrimmage);

    /** The play's assignment for the RoleIndex-th player (0-based) of Role: the role's slot of
     *  that index, or its last slot when the role has more players than slots. Null when the
     *  play has no slot for Role. */
    static const FPSPlayAssignment* FindAssignmentSlot(const FPSPlayDefinition& Play, EPlayerRole Role, int32 RoleIndex);

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

    TArray<FVector> ResolveRouteWaypoints(const FName& RouteId, const UDataTable* RouteLibrary, const FVector& Origin, float MirrorY = 1.f) const;

    UPROPERTY(Transient)
    FRandomStream DeterminismStream;
};
