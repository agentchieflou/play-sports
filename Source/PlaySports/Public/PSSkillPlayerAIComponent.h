// PSSkillPlayerAIComponent.h - Epic 14: offensive players that actually play the called play
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "PSTelemetryBus.h"
#include "PSSkillPlayerAIComponent.generated.h"

class APSOffenseController;
class APSPlayerPawn;

/** What an offensive AI player is doing this moment of the play. */
UENUM(BlueprintType)
enum class EPSSkillPlayerAction : uint8
{
    /** Before the snap, or out of the play. */
    Idle,
    /** Following the assigned route: a receiver's pattern, the QB's drop, the RB's mesh. */
    RunRoute,
    /** QB with the ball, scanning receivers. */
    ReadDefense,
    /** RB at the mesh point waiting for the handoff. */
    WaitHandoff,
    /** Running with the ball: upfield, away from defenders (a QB scramble too). */
    CarryBall,
    /** Converging on a pass thrown to this player. */
    TrackBall,
    /** No route: holding in front of the QB and taking on the nearest rusher. */
    Block
};

/** Offensive AI tuning (Data/skill_ai_tuning.json; Architecture rule 4). Distances in cm. */
USTRUCT(BlueprintType)
struct FSkillPlayerAITuningRow : public FTableRowBase
{
    GENERATED_BODY()

    /** How close counts as reaching a route waypoint. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float WaypointArrivalRadius = 100.f;

    /** Distance from the nearest defender at which a receiver counts as open. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float OpenSeparation = 300.f;

    /** Extra separation a QB with 0 Awareness needs to see a receiver as open (none at 100). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float AwarenessMisreadSeparation = 150.f;

    /** The QB reads no sooner than this after the snap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float MinReadSeconds = 0.6f;

    /** By this long after the snap the QB throws to his best receiver or scrambles. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float MaxReadSeconds = 3.f;

    /** A defender this close to the QB forces the decision now. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float PressureRadius = 250.f;

    /** Under pressure the QB still throws to a receiver at least this open; otherwise he runs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float PressuredThrowSeparation = 150.f;

    /** The QB hands off when the RB is this close (the hand-off itself allows 200). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float HandoffRadius = 150.f;

    /** A run play's QB keeps the ball and runs if no hand-off happened by then. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float HandoffTimeoutSeconds = 1.5f;

    /** A ball carrier steers away from defenders within this distance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float CarrierAvoidRadius = 500.f;

    /** How strongly the carrier veers from the nearest defender (1 = as much as upfield). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float CarrierAvoidWeight = 1.f;

    /** Ball speed used to lead a receiver: lead time = distance / this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float ThrowLeadSpeed = 2000.f;

    /** How far in front of the QB a pass blocker sets up. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float BlockSetDistance = 150.f;

    /** A blocker takes on rushers within this distance of the QB. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float BlockEngageRadius = 500.f;
};

/**
 * UPSSkillPlayerAIComponent plays the called play for an AI-controlled offensive pawn
 * (Epic 14): receivers run their routes and converge on a ball thrown to them, the QB drops,
 * reads and throws (or hands off on a run, or scrambles), the RB takes the hand-off and hits
 * the run lane, blockers set up in front of the QB, and whoever has the ball runs upfield away
 * from the nearest defender.
 *
 * It moves the pawn the same way a human does -- AddMovementInput, so the pawn's acceleration,
 * turning and cutting rules (FMovementTuningRow) apply -- and needs no Behavior Tree asset,
 * which this repo can't author without an editor. The route comes from APSOffenseController
 * (set by UPSPlayOrchestrator at the snap); the call, the snap and the throw come from
 * UPSTelemetryBus (rule 5); tuning comes from FSkillPlayerAITuningRow (rule 4).
 *
 * APSOffenseController owns one. It ticks with the controller; headless tests call TickAI.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSSkillPlayerAIComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSSkillPlayerAIComponent();

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FSkillPlayerAITuningRow& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Listens for the call, the snap, throws and the end of the play. Idempotent. */
    void BindToBus();

    void UnbindFromBus();

    /** One decision step: picks the action, acts (throw, hand off), steers the pawn. */
    void TickAI(float DeltaSeconds);

    UFUNCTION(BlueprintPure, Category = "AI")
    EPSSkillPlayerAction GetAction() const { return Action; }

    /** The direction (unit, on the ground) the pawn was last steered; zero when standing. */
    UFUNCTION(BlueprintPure, Category = "AI")
    FVector GetDesiredDirection() const { return DesiredDirection; }

    /** True while the offense's call for this snap is a run. */
    bool IsRunPlay() const { return bRunPlay; }

    /** The receiver the QB would throw to now and whether he reads him as open: the most
     *  separated eligible receiver, with the separation the QB's Awareness lets him see. */
    APSPlayerPawn* ChooseReceiver(bool& bOutOpen, float& OutSeparation);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    void HandlePlayCall(const FPSTelemetryPlayCallEvent& Event);
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandleThrow(const FPSTelemetryThrowEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    void TickQuarterback(APSPlayerPawn* Self);
    void ThrowTo(APSPlayerPawn* Self, APSPlayerPawn* Receiver);
    FVector SteerAlongRoute(APSPlayerPawn* Self);
    FVector SteerAsCarrier(APSPlayerPawn* Self) const;
    FVector SteerAsBlocker(APSPlayerPawn* Self) const;

    APSOffenseController* GetOffenseController() const;
    APSPlayerPawn* GetSelf() const;
    APSPlayerPawn* FindTeammate(EPlayerRole Role) const;
    TArray<APSPlayerPawn*> GetFieldPawns() const;

    UPROPERTY(Transient)
    FSkillPlayerAITuningRow Tuning;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    EPSSkillPlayerAction Action = EPSSkillPlayerAction::Idle;
    FVector DesiredDirection = FVector::ZeroVector;
    FVector LineOfScrimmage = FVector::ZeroVector;
    FVector TrackTarget = FVector::ZeroVector;
    float TimeSinceSnap = 0.f;
    bool bPlayLive = false;
    bool bSnapPending = false;
    bool bRunPlay = false;
    bool bTuningLoaded = false;
};
