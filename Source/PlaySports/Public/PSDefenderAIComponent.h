// PSDefenderAIComponent.h - Epic 15 made live: defenders that actually play their assignment
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "PSTelemetryBus.h"
#include "PSDefenderAIComponent.generated.h"

class APSDefenseController;
class APSPlayerPawn;

/** What a defensive AI player is doing this moment of the play. */
UENUM(BlueprintType)
enum class EPSDefenderAction : uint8
{
    /** Before the snap, or out of the play. */
    Idle,
    /** Going after the passer (PassRush, and a blitz, which the play data maps to it). */
    Rush,
    /** Rushing wide of the passer to keep him inside. */
    Contain,
    /** Man coverage: mirroring one receiver from a cushion. */
    Cover,
    /** Zone coverage: holding a spot, shading to the receiver who enters it. */
    Zone,
    /** Run fit: holding, reading run or pass. */
    Read,
    /** Chasing the ball carrier on an intercept angle. */
    Pursue,
    /** Breaking on a thrown ball. */
    BallHawk,
    /** Carrying the ball back after a takeaway. */
    Return
};

/** Defensive AI tuning (Data/defense_ai_tuning.json; Architecture rule 4). Distances in cm. */
USTRUCT(BlueprintType)
struct FDefenderAITuningRow : public FTableRowBase
{
    GENERATED_BODY()

    /** How close counts as reaching a spot (a zone, the drop, the ball). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float ArrivalRadius = 100.f;

    /** How far downfield of his man a cover defender stays. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float ManCushion = 150.f;

    /** At Awareness 100, how far ahead of his man's movement a cover defender reads (none at 0). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float ManAnticipationSeconds = 0.4f;

    /** A zone defender plays receivers within this distance of his spot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float ZoneRadius = 700.f;

    /** How far toward a receiver in his zone he leaves his spot (0 holds it, 1 goes to him). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float ZoneShadeWeight = 0.5f;

    /** A contain rusher aims this far outside the passer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float ContainWidth = 400.f;

    /** A passer this far behind the line is a pass read for a run-fit defender. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float PassReadDepth = 250.f;

    /** On a pass read, a run-fit defender drops to this depth past the line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float PassDropDepth = 600.f;

    /** A defender with 0 Awareness takes this long to react to a read or a throw (none at 100). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float MaxReactionSeconds = 0.6f;

    /** Coverage defenders within this distance of where a pass comes down break on it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float BallHawkRadius = 1200.f;

    /** A coverage defender with 0 Awareness freezes this long on a pump fake (not at all at
     *  100; Epic 104). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    float PumpFakeFreezeSeconds = 0.5f;
};

/**
 * UPSDefenderAIComponent plays the defensive call for an AI-controlled defender: rushers go
 * after the passer (a contain rusher stays outside him), cover defenders mirror their man
 * from a cushion, zone defenders hold their spot and shade to the receiver in it, run-fit
 * defenders read run or pass, coverage defenders break on a thrown ball, and once the ball is
 * out -- a hand-off, a catch, a QB past the line -- everyone pursues the carrier on
 * APSDefenseController's intercept angle. Awareness sets how fast each read happens, and how
 * long a pump fake freezes coverage.
 *
 * It is the defensive twin of UPSSkillPlayerAIComponent and works the same way: it moves the
 * pawn with AddMovementInput (so FMovementTuningRow applies), takes the assignment from
 * APSDefenseController (set by UPSPlayOrchestrator at the snap), hears the snap and the throw
 * on UPSTelemetryBus (rule 5), and reads FDefenderAITuningRow (rule 4). A defender held by a
 * blocker is left to the engagement steering in APSPlayerPawn::Tick.
 *
 * APSDefenseController owns one. It ticks with the controller; headless tests call TickAI.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSDefenderAIComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSDefenderAIComponent();

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FDefenderAITuningRow& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Listens for the snap, throws, pump fakes and the end of the play. Idempotent. */
    void BindToBus();

    void UnbindFromBus();

    /** One decision step: reads the play, picks the action, steers the pawn. */
    void TickAI(float DeltaSeconds);

    UFUNCTION(BlueprintPure, Category = "AI")
    EPSDefenderAction GetAction() const { return Action; }

    /** The direction (unit, on the ground) the pawn was last steered; zero when standing. */
    UFUNCTION(BlueprintPure, Category = "AI")
    FVector GetDesiredDirection() const { return DesiredDirection; }

    /** The receiver this defender has in man coverage this play, if any. */
    UFUNCTION(BlueprintPure, Category = "AI")
    APSPlayerPawn* GetCoveredReceiver() const;

    /** The spot this defender is playing in zone (or dropped to on a pass read). */
    UFUNCTION(BlueprintPure, Category = "AI")
    FVector GetZoneSpot() const { return ZoneSpot; }

    /** How long this defender takes to react to a read or a throw: MaxReactionSeconds at
     *  Awareness 0, none at 100. */
    float GetReactionSeconds();

    /** True while a pump fake has this defender frozen. */
    UFUNCTION(BlueprintPure, Category = "AI")
    bool IsFrozen() const { return bPlayLive && TimeSinceSnap < FrozenUntil; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandleThrow(const FPSTelemetryThrowEvent& Event);
    void HandleCatch(const FPSTelemetryCatchEvent& Event);
    void HandlePumpFake(const FPSTelemetryPumpFakeEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    void StartAssignment(APSPlayerPawn* Self);
    APSPlayerPawn* PickReceiverToCover(const APSPlayerPawn* Self) const;
    bool IsBallOut(const APSPlayerPawn* Carrier) const;

    FVector SteerToward(const APSPlayerPawn* Self, const FVector& Target) const;
    FVector SteerToRush(const APSPlayerPawn* Self) const;
    FVector SteerToContain(const APSPlayerPawn* Self) const;
    FVector SteerToCover(const APSPlayerPawn* Self) const;
    FVector SteerInZone(const APSPlayerPawn* Self) const;
    FVector SteerToPursue(const APSPlayerPawn* Self, const APSPlayerPawn* Carrier) const;

    APSDefenseController* GetDefenseController() const;
    APSPlayerPawn* GetSelf() const;
    APSPlayerPawn* FindCarrier() const;
    APSPlayerPawn* FindOpponent(EPlayerRole Role) const;
    TArray<APSPlayerPawn*> GetFieldPawns() const;

    UPROPERTY(Transient)
    FDefenderAITuningRow Tuning;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    TWeakObjectPtr<APSPlayerPawn> CoveredReceiver;
    EPSDefenderAction Action = EPSDefenderAction::Idle;
    FVector DesiredDirection = FVector::ZeroVector;
    FVector LineOfScrimmage = FVector::ZeroVector;
    FVector ZoneSpot = FVector::ZeroVector;
    FVector LandingSpot = FVector::ZeroVector;
    float TimeSinceSnap = 0.f;
    /** When this defender may act on the ball being out, the pass read and the throw
     *  (TimeSinceSnap plus his reaction); negative: not seen yet. */
    float PursueAt = -1.f;
    float PassReadAt = -1.f;
    float BallHawkAt = -1.f;
    /** Until when a pump fake holds this defender (TimeSinceSnap). */
    float FrozenUntil = -1.f;
    bool bPlayLive = false;
    bool bSnapPending = false;
    bool bBallInAir = false;
    bool bTuningLoaded = false;
};
