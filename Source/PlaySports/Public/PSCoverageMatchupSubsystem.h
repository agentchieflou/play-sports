// PSCoverageMatchupSubsystem.h - Epic 69: DB-vs-WR as a continuous contest
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "PSCoverageMatchupTypes.h"
#include "PSTelemetryBus.h"
#include "PSCoverageMatchupSubsystem.generated.h"

class APSPlayerPawn;
class UPSDefenderAIComponent;
class UPSPlayCallSubsystem;

/**
 * UPSCoverageMatchupSubsystem is the coverage matchup engine (Epic 69): the authority on how
 * each coverage player stands against the receivers, which both sides' AIs play against.
 *
 *  - Press: before the snap, a man-coverage back over a receiver walks up into press when the
 *    defense's shell allows it and his chance to win the jam (Epic 68's release model, from the
 *    presser's side) is good enough. At the snap the receiver's UPSRouteRunnerComponent contests
 *    the release against him; a presser who wins trails his man tight, one who is beaten is out
 *    of phase for PressBeatenSeconds.
 *  - Leverage: every man matchup has a side (inside or outside, from the shell) the defender
 *    plays, shaded LeverageShade that way, and holds until the receiver crosses his face. The
 *    receiver plays against it: a break away from it gains separation (the defender is out of
 *    phase while he makes it up), one into it gains none; a double move's fake toward it bites
 *    more often; an option route breaks away from it.
 *  - Zones: a zone defender picks up the receiver who comes into his zone and carries him; when
 *    the receiver leaves, he hands him to the defender whose zone he runs into, carries him on
 *    when he goes vertical with nobody deeper, and passes him off underneath.
 *  - Safety help: a deep defender stays over the top of the deepest receiver in his area; when
 *    one leaves the deep zones the others re-space to cover them; a man defender left over
 *    takes the shell's free role (deep middle, robber).
 *  - Pass interference: a defender who plays through the targeted receiver while the ball is in
 *    the air draws a flag, which UPSPlaySimulation enforces.
 *
 * It decides; the players act. UPSDefenderAIComponent reads its cover and zone targets, and
 * every contest goes on UPSTelemetryBus as a Coverage event (a defender it puts out of phase
 * freezes on it). The defenders' assignments and their men stay the defense AI's
 * (UPSDefenderAIComponent::GetCoveredReceiver); this holds how they play them.
 *
 * It steps at the AI's decision rate from Tick; headless tests call UpdateCoverage. Tuning:
 * Data/coverage_matchups.json.
 */
UCLASS()
class PLAYSPORTS_API UPSCoverageMatchupSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FPSCoverageMatchupTuning& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Replaces the tuning (headless tests). */
    void SetTuning(const FPSCoverageMatchupTuning& InTuning);

    /** Problems with InTuning (empty when sound). */
    static TArray<FString> ValidateTuning(const FPSCoverageMatchupTuning& InTuning);

    /** The rule for Shell, or the default one. */
    const FPSCoverageShellRule& GetShellRule(const FString& Shell);

    /** The coverage shell of the defense's call ("" with none). */
    FString GetCalledShell() const;

    /** One step: before the snap, the press alignment; during the play, the matchups, the
     *  zones and the interference watch. */
    void UpdateCoverage(float DeltaSeconds);

    // --- Press ---

    /** Before the snap: the receiver Defender walks up to press, and the spot he takes. */
    bool GetPressPlan(const APSPlayerPawn* Defender, APSPlayerPawn*& OutReceiver, FVector& OutSpot) const;

    /** The defender lined up to press Receiver this down, if any. */
    APSPlayerPawn* FindPlannedPresser(const APSPlayerPawn* Receiver) const;

    /** The receiver Defender lined up to press this down, if any. */
    APSPlayerPawn* GetPlannedReceiver(const APSPlayerPawn* Defender) const;

    /** True when Defender pressed Receiver at the snap and won the jam. */
    bool WonJam(const APSPlayerPawn* Defender, const APSPlayerPawn* Receiver) const;

    // --- Leverage ---

    /** Defender's leverage on Receiver: the side (-1 or +1 across the field) he plays, and
     *  whether he still holds it. False when they are not a matchup. */
    bool GetLeverage(const APSPlayerPawn* Defender, const APSPlayerPawn* Receiver, EPSLeverage& OutLeverage, float& OutSide, bool& bOutHeld) const;

    /** Where Defender plays Receiver in man coverage: ReceiverLead ahead of him, OffCushion
     *  downfield of him (PressCushion after a won jam), LeverageShade to his leverage side.
     *  False when they are not a matchup. */
    bool GetCoverTarget(const APSPlayerPawn* Defender, const APSPlayerPawn* Receiver, const FVector& ReceiverLead, float OffCushion, FVector& OutTarget) const;

    /** How much a double move's fake in FakeDirection changes Defender's chance to bite on
     *  Receiver: +LeverageBiteBonus toward the leverage he holds, minus it away; 0 when they are
     *  not a matchup or he has lost it. */
    float GetLeverageBiteBonus(const APSPlayerPawn* Defender, const APSPlayerPawn* Receiver, const FVector& FakeDirection) const;

    // --- Zones and safety help ---

    /** Where zone defender Defender plays now: his spot (or the one he rotated to, or his free
     *  role's), over the top of his area if he is deep, a robber's jump, or on the receiver he
     *  carries. False when the engine has no zone for him. */
    bool GetZoneTarget(const APSPlayerPawn* Defender, FVector& OutTarget) const;

    /** The receiver zone defender Defender carries, if any. */
    APSPlayerPawn* GetCarriedReceiver(const APSPlayerPawn* Defender) const;

    /** The free role a left-over man defender took this play. */
    EPSFreeRole GetFreeRole(const APSPlayerPawn* Defender) const;

    /** True while Defender plays a deep zone. */
    bool IsDeepDefender(const APSPlayerPawn* Defender) const;

    /** True once a deep zone defender has rotated to cover for one who left. */
    bool HasRotated(const APSPlayerPawn* Defender) const;

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    struct FManMatchup
    {
        TWeakObjectPtr<APSPlayerPawn> Defender;
        TWeakObjectPtr<APSPlayerPawn> Receiver;
        EPSLeverage Leverage = EPSLeverage::Inside;
        /** The side across the field (-1 or +1) the defender plays. */
        float Side = 1.f;
        bool bHeld = true;
    };

    struct FZoneState
    {
        TWeakObjectPtr<APSPlayerPawn> Defender;
        TWeakObjectPtr<APSPlayerPawn> Carried;
        /** His own zone (the call's, or his free role's) and where he plays it now. */
        FVector BaseSpot = FVector::ZeroVector;
        FVector Spot = FVector::ZeroVector;
        float Radius = 0.f;
        EPSFreeRole FreeRole = EPSFreeRole::None;
        bool bActive = false;
        bool bDeep = false;
        bool bVertical = false;
        bool bRotated = false;
    };

    struct FPressPlan
    {
        TWeakObjectPtr<APSPlayerPawn> Defender;
        TWeakObjectPtr<APSPlayerPawn> Receiver;
        /** The side across the field (-1 or +1) he shades to. */
        float Side = 1.f;
    };

    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandleThrow(const FPSTelemetryThrowEvent& Event);
    void HandleCatch(const FPSTelemetryCatchEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);
    void HandleRouteRunning(const FPSTelemetryRouteEvent& Event);

    void PlanPress();
    void WalkToPress();
    void UpdateManMatchups();
    void UpdateFreeRoles();
    void UpdateZones();
    void UpdateRotation();
    void CheckInterference();
    void ResolveBreak(const FPSTelemetryRouteEvent& Event);
    void ResolveRelease(const FPSTelemetryRouteEvent& Event);

    /** True while coverage still matters: the play is live and the quarterback holds the ball
     *  behind the line, or it is in the air. */
    bool IsCoverageLive() const;

    /** The side across the field (-1 or +1) Leverage puts a defender on a receiver at
     *  ReceiverY, with the ball at MiddleY. */
    static float LeverageSide(EPSLeverage Leverage, float ReceiverY, float MiddleY);

    UPSPlayCallSubsystem* GetPlayCall() const;
    const TArray<APSPlayerPawn*>& GetFieldPawns() const;
    APSPlayerPawn* FindPawnByName(const FString& DisplayName, bool bOffense) const;
    UPSDefenderAIComponent* DefenderAIOf(const APSPlayerPawn* Pawn) const;

    /** The rule for the shell the defense plays this snap. */
    const FPSCoverageShellRule& GetPlayedShellRule();

    void Publish(EPSCoverageEventKind Kind, const APSPlayerPawn* Defender, const APSPlayerPawn* Receiver, FName Outcome, float Seconds = 0.f,
        const FVector& Location = FVector::ZeroVector, const APSPlayerPawn* OtherDefender = nullptr, int32 YardsPastLine = 0);

    UPROPERTY(Transient)
    FPSCoverageMatchupTuning Tuning;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;

    /** By defender. */
    TMap<FObjectKey, FManMatchup> ManMatchups;
    TMap<FObjectKey, FZoneState> Zones;
    TMap<FObjectKey, FPressPlan> PressPlans;
    /** By receiver: the presser who won his jam at the snap. */
    TMap<FObjectKey, FObjectKey> WonJams;
    /** Every defender who played a deep zone this play. */
    TSet<FObjectKey> DeepRoster;

    /** The officials' interference calls, seeded from each snap's situation. */
    FRandomStream Rolls;
    FString PlayedShell;
    FString TargetReceiverName;
    FVector LineOfScrimmage = FVector::ZeroVector;
    FVector LandingSpot = FVector::ZeroVector;
    float SinceUpdate = 0.f;
    int32 FreeRolesTaken = 0;
    int32 RotatedFor = 0;
    bool bLive = false;
    bool bShellRead = false;
    bool bBallInAir = false;
    bool bInterferenceSeen = false;
    bool bTuningLoaded = false;
};
