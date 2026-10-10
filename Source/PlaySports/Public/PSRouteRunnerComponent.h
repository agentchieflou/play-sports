// PSRouteRunnerComponent.h - Epic 68: a receiver runs his route as a contested skill
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSPlaybookData.h"
#include "PSPlayerAttributes.h"
#include "PSRouteRunning.h"
#include "PSTelemetryBus.h"
#include "PSRouteRunnerComponent.generated.h"

class APSOffenseController;
class APSPlayerPawn;
class UDataTable;

/**
 * UPSRouteRunnerComponent runs an AI receiver's route the way the route-running model
 * (PSRouteRunning, Epic 68) says, on top of the waypoints APSOffenseController holds:
 *
 *  - Release: a defender pressing him at the snap contests it -- he wins, is held up (delay),
 *    or is pushed toward the sideline (reroute), by the two players' ratings and a roll.
 *  - Breaks: he turns for the next leg before the corner by how stiff he is (Agility), so a
 *    sharp receiver cuts on the spot and a stiff one rounds it.
 *  - Double moves: at a fake break he sells it, and the defender on him may bite (his
 *    Awareness against the receiver's Agility). A defender who bit freezes.
 *  - Option routes: at the read point he reads man or zone and runs the branch for it, away
 *    from a man defender's leverage.
 *
 * UPSPlayOrchestrator hands him the plan (SetRoutePlan) with the route; the plan only applies
 * while the controller still runs those waypoints, so a route replaced mid-play (a scramble
 * drill, a test's own route) is run plainly, rounding and all. Contests are announced on
 * UPSTelemetryBus (RouteRunning) and rolled from the play's seed. UPSSkillPlayerAIComponent
 * steers through Steer; the quarterback times his reads by GetReadTime.
 *
 * APSOffenseController owns one.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSRouteRunnerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSRouteRunnerComponent();

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FRouteRunningTuningRow& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Replaces the tuning (headless tests). */
    void SetTuning(const FRouteRunningTuningRow& InTuning);

    /** This play's tuning for Receiver: the tuning as loaded, scaled by Data/player_dna.json's
     *  RouteRunning bindings for his style (Epic 79), then by the difficulty tier when he plays
     *  for the CPU (Epic 84). SetRoutePlan applies the controlled pawn's. */
    void ApplyPlayTuning(const APSPlayerPawn* Receiver);

    /** This play's route: its definition, the world waypoints it resolved to (already the
     *  controller's), the side's mirror (+1 right of the ball), the library its option branches
     *  come from, and the seed of this receiver's rolls. He runs it in his style, at the CPU's difficulty (ApplyPlayTuning). */
    void SetRoutePlan(const FPSRoute& Route, const TArray<FVector>& WorldWaypoints, float InMirror, const UDataTable* RouteLibrary, int32 Seed);

    /** No pattern this play: a block, a spot, a scramble drill. */
    void ClearRoutePlan();

    /** Starts the play's run afresh (the first tick after the snap). */
    void ResetRun();

    /** True while he runs the route he was planned for (not one replaced since). */
    UFUNCTION(BlueprintPure, Category = "Routes")
    bool HasPlan() const;

    /** When the quarterback's read of this route comes up, seconds after the snap. */
    UFUNCTION(BlueprintPure, Category = "Routes")
    float GetReadTime() const { return ReadTime; }

    /** True when the route has a break (or an option read) still to come or made. */
    UFUNCTION(BlueprintPure, Category = "Routes")
    bool HasBreak() const { return BreakIndex != INDEX_NONE || OptionReadIndex != INDEX_NONE || bBroken; }

    /** True once he has made his break (or his option read). */
    UFUNCTION(BlueprintPure, Category = "Routes")
    bool HasBroken() const { return bBroken; }

    UFUNCTION(BlueprintPure, Category = "Routes")
    EPSReleaseOutcome GetReleaseOutcome() const { return ReleaseOutcome; }

    /** An option route's read, once made. */
    bool GetCoverageRead(EPSCoverageRead& OutRead) const;

    /** One step along Controller's route at TimeSinceSnap: the direction to run, zero while he
     *  is held (a jam, selling a fake). ArrivalRadius is how close counts as reaching a
     *  waypoint, before any rounding. bOutFinished when he reaches the last one. */
    FVector Steer(APSPlayerPawn* Self, APSOffenseController* Controller, const TArray<APSPlayerPawn*>& Pawns, float TimeSinceSnap, float ArrivalRadius, bool& bOutFinished);

private:
    bool IsPlanFor(const APSOffenseController* Controller) const;
    void ContestRelease(APSPlayerPawn* Self, APSOffenseController* Controller, const TArray<APSPlayerPawn*>& Pawns, float TimeSinceSnap);
    void SellFake(APSPlayerPawn* Self, const TArray<APSPlayerPawn*>& Pawns, float TimeSinceSnap);
    void ReadOption(APSPlayerPawn* Self, APSOffenseController* Controller, const TArray<APSPlayerPawn*>& Pawns);
    void Publish(EPSRouteEventKind Kind, const APSPlayerPawn* Self, const APSPlayerPawn* Defender, FName Outcome, float Seconds);

    UPROPERTY(Transient)
    FRouteRunningTuningRow Tuning;

    /** The tuning as loaded (or set); Tuning is this with the receiver's DNA applied. */
    UPROPERTY(Transient)
    FRouteRunningTuningRow BaseTuning;

    /** The plan: the waypoints it was made for, which of them are fakes, the break, the read. */
    TArray<FVector> PlannedWaypoints;
    TArray<bool> FakeWaypoints;
    TWeakObjectPtr<const UDataTable> Library;
    FName VsManBranch;
    FName VsZoneBranch;
    FRandomStream Rolls;
    FVector StartLocation = FVector::ZeroVector;
    float Mirror = 1.f;
    float ReadTime = 0.f;
    float HoldUntil = -1.f;
    int32 BreakIndex = INDEX_NONE;
    int32 OptionReadIndex = INDEX_NONE;
    EPSReleaseOutcome ReleaseOutcome = EPSReleaseOutcome::Unpressed;
    EPSCoverageRead CoverageRead = EPSCoverageRead::Zone;
    bool bHasPlan = false;
    bool bStarted = false;
    bool bBroken = false;
    bool bRead = false;
    bool bTuningLoaded = false;
};
