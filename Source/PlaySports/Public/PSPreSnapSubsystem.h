// PSPreSnapSubsystem.h - Epic 66: audibles, hot routes, motion and protection before the snap
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "PSPlaybookData.h"
#include "PSPreSnapTypes.h"
#include "PSTelemetryBus.h"
#include "PSPreSnapSubsystem.generated.h"

class APSPlayerPawn;
class UPSPlayCallSubsystem;

/**
 * UPSPreSnapSubsystem is the authority on the offense's changes to its call between the call
 * and the snap (Epic 66, Architecture rule 6), for a human (UPSPreSnapInputComponent) or the
 * CPU quarterback alike:
 *
 *  - Audible: swap to another play in the same formation. The call itself stays
 *    UPSPlayCallSubsystem's; an audible is a new call made there.
 *  - Hot route: one receiver's route swapped for another his alignment allows
 *    (FPreSnapTuningRow::HotRouteSets).
 *  - Motion: a receiver runs across the formation before the snap. In man coverage the
 *    defender over him travels with him, and the motion is announced with that man indicator.
 *  - Protection: the line slides left or right, and a back or tight end is kept in to block or
 *    released into a route.
 *
 * Changes are allowed while the call window is open and the offense has called; a new
 * offensive call (an audible too) clears them, as does the next down. At the snap
 * UPSPlayOrchestrator reads them through ApplyAdjustment while it hands out the call, and the
 * game mode's line pairing reads the slide (ComputeBlockingPairs). Every change is announced
 * on UPSTelemetryBus (PreSnap).
 *
 * When both calls are in and the CPU called the offense, its quarterback reads the defense's
 * look (GetDefensiveLook) once, if his Awareness allows: he checks out of a run into a blitz
 * or a heavy box and out of a pass against a light one, beats a blitz with a hot route and a
 * back kept in, and motions his slot receiver on a pass. Tuning: Data/presnap_tuning.json.
 *
 * Motion moves pawns with AddMovementInput from Tick; headless tests call TickMotion.
 */
UCLASS()
class PLAYSPORTS_API UPSPreSnapSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FPreSnapTuningRow& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** True while the call window is open and the offense has a call to change. */
    UFUNCTION(BlueprintPure, Category = "PreSnap")
    bool IsAdjustable() const;

    // --- Audibles ---

    /** The other plays in the formation of the offense's call, in playbook order. */
    TArray<FPSPlayDefinition> GetAudibles() const;

    /** Calls PlayId instead, if it is one of GetAudibles. */
    bool Audible(FName PlayId, bool bHuman);

    /** Calls the next play of the formation after the current call (wrapping). */
    bool AudibleToNext(bool bHuman);

    // --- Hot routes ---

    /** Where Player lines up: wide or slot for a receiver, tight for a tight end, backfield
     *  for a back. */
    EPSReceiverAlignment GetAlignment(const APSPlayerPawn* Player);

    /** The routes Player may be hot-routed to (empty for a player who can't be). */
    TArray<FName> GetAllowedHotRoutes(const APSPlayerPawn* Player);

    /** Sends Player on RouteId instead of his called route. He must run a route on this play
     *  (released, if the play has him blocking) and RouteId must be in his allowed set. */
    bool HotRoute(APSPlayerPawn* Player, FName RouteId, bool bHuman);

    /** Hot-routes Player to the next route in his allowed set after the one he runs now. */
    bool CycleHotRoute(APSPlayerPawn* Player, bool bHuman);

    // --- Motion ---

    /** Sends Player (an AI receiver, tight end or back) across the formation. One man moves
     *  at a time. In man coverage the AI defender lined up over him travels with him. */
    bool StartMotion(APSPlayerPawn* Player, bool bHuman);

    /** One motion step: the man in motion and any travelling defender move toward their spots
     *  until they arrive or the ball is snapped. */
    void TickMotion(float DeltaSeconds);

    UFUNCTION(BlueprintPure, Category = "PreSnap")
    APSPlayerPawn* GetMotionPlayer() const { return MotionPlayer.Get(); }

    /** The defender travelling with the man in motion, if one is. */
    UFUNCTION(BlueprintPure, Category = "PreSnap")
    APSPlayerPawn* GetTravellingDefender() const { return TravellingDefender.Get(); }

    /** True while the man in motion is still moving. */
    UFUNCTION(BlueprintPure, Category = "PreSnap")
    bool IsMotionActive() const { return bMotionActive; }

    // --- Protection ---

    /** Keeps Player (a back or tight end) in to block, releases him, or puts him back as
     *  called. Block needs a play that gives him a route; Release one that has him blocking.
     *  No protection calls on a run. */
    bool SetProtection(APSPlayerPawn* Player, EPSProtectionCall Call, bool bHuman);

    /** Block if the play gives Player a route, Release if it has him blocking; as called if
     *  he is already changed. */
    bool ToggleProtection(APSPlayerPawn* Player, bool bHuman);

    bool SetSlide(EPSSlideDirection Direction, bool bHuman);

    /** None, then Left, then Right, then None again. */
    bool CycleSlide(bool bHuman);

    UFUNCTION(BlueprintPure, Category = "PreSnap")
    EPSSlideDirection GetSlide() const { return Slide; }

    /** Where a sliding lineman aims for his man, relative to himself (zero with no slide). */
    FVector GetSlideAimOffset();

    /** Pairs each lineman with the nearest unclaimed rusher to his aim point (his spot plus
     *  AimOffset). With no slide the linemen pick in the order given; on a slide the ones
     *  furthest toward it pick first, leaving the backside rusher over. Shared with the game
     *  mode's PairLinemen. */
    static TArray<TPair<APSPlayerPawn*, APSPlayerPawn*>> ComputeBlockingPairs(const TArray<APSPlayerPawn*>& Linemen, const TArray<APSPlayerPawn*>& Rushers, const FVector& AimOffset);

    // --- What the play-out reads ---

    /** Player's changes this snap, or null. */
    const FPSPreSnapPlayerAdjustment* FindAdjustment(const APSPlayerPawn* Player) const;

    /** Applies Player's changes to the assignment the call gives him: a block, a release
     *  route, or a hot route. Called by UPSPlayOrchestrator at the snap. */
    void ApplyAdjustment(const APSPlayerPawn* Player, FPSPlayAssignment& InOutAssignment);

    /** What the offense sees of the defense now (see FPSDefensiveLook). */
    FPSDefensiveLook GetDefensiveLook();

    /** The CPU quarterback's read (see the class comment). Runs once per call window. */
    void RunCpuRead();

    /** Whether the CPU quarterback's read may check out of the call (an audible); his hot
     *  routes, protection, slide and motion are his either way. On by default; a demo that must
     *  show its call turns it off. */
    void SetCpuAudiblesAllowed(bool bAllowed) { bCpuAudiblesAllowed = bAllowed; }

    /** Drops every change: the next down, or a new call. */
    void ResetAdjustments();

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandlePlayCall(const FPSTelemetryPlayCallEvent& Event);
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    UPSPlayCallSubsystem* GetPlayCall() const;
    bool GetOffensePlay(FPSPlayDefinition& OutPlay) const;
    TArray<APSPlayerPawn*> GetFieldPawns() const;
    APSPlayerPawn* FindAIQuarterback() const;

    /** The assignment Player's call gives him, before any change. False if none. */
    bool GetCalledAssignment(const APSPlayerPawn* Player, const FPSPlayDefinition& Play, FPSPlayAssignment& OutAssignment) const;

    /** The route Player runs now (his hot route, release route or called route). */
    FName GetCurrentRouteId(const APSPlayerPawn* Player);

    /** Whether Player runs a route on the current call, after his protection call. */
    bool RunsRoute(const APSPlayerPawn* Player);

    APSPlayerPawn* FindManDefenderOver(const APSPlayerPawn* Player);
    void Publish(EPSPreSnapAction Action, const APSPlayerPawn* Player, FName Detail, bool bHuman, const APSPlayerPawn* Defender = nullptr);

    UPROPERTY(Transient)
    FPreSnapTuningRow Tuning;

    /** Each changed player's adjustments, by pawn. */
    TMap<FObjectKey, FPSPreSnapPlayerAdjustment> Adjustments;
    TWeakObjectPtr<APSPlayerPawn> MotionPlayer;
    TWeakObjectPtr<APSPlayerPawn> TravellingDefender;
    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    FVector MotionTarget = FVector::ZeroVector;
    EPSSlideDirection Slide = EPSSlideDirection::None;
    bool bMotionActive = false;
    /** The travelling defender keeps over the man in motion until the snap. */
    bool bTravelActive = false;
    bool bCpuReadDone = false;
    bool bCpuAudiblesAllowed = true;
    bool bTuningLoaded = false;
};
