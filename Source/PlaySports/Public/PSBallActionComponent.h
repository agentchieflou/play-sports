// PSBallActionComponent.h - Epic C3: extracted ball-action logic from APSPlayerPawn
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSBallActionComponent.generated.h"

class APSPlayerPawn;
class APSBall;
class UPSTelemetryBus;
struct FPSTelemetrySnapEvent;

/**
 * UPSBallActionComponent encapsulates ball-action mechanics (passing, handoffs, lateral tosses,
 * kicking, fumbles, and tackle resolution) previously inlined in APSPlayerPawn.
 * Kept Blueprint-accessible to mirror the original pawn API.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSBallActionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSBallActionComponent();

    /** Throw the ball to a target location. IntendedTarget, when provided, is
     *  published on the TelemetryBus as the pass's intended receiver (Epic 140: an
     *  interception on this pass auto-kills IntendedTarget, not whoever the ball
     *  happens to hit). SpeedScale (0-1] throws softer than the passer's full arm: a touch
     *  pass (Epic 104); a target out of reach that softly is thrown at full speed. The ball
     *  comes down off target by the passer's inaccuracy (his Awareness, the CPU difficulty's
     *  scale), rolled on his stream of the play's seeded streams (UPSNetRandomStreams, Epic 108):
     *  the same match seed and snap throw the same ball. */
    UFUNCTION(BlueprintCallable, Category = "BallAction")
    bool ThrowPass(APSBall* Ball, const FVector& TargetLocation, bool bHighArc = false, APSPlayerPawn* IntendedTarget = nullptr, float SpeedScale = 1.f);

    /** Perform an instant handoff of the ball to a target player pawn */
    /** Hands the carried ball to TargetPlayer (within 200 cm). */
    UFUNCTION(BlueprintCallable, Category = "BallAction")
    bool ExecuteHandoff(APSPlayerPawn* TargetPlayer);

    /** The ball attached to the owning pawn, or null when it isn't carrying one. */
    UFUNCTION(BlueprintPure, Category = "BallAction")
    APSBall* GetCarriedBall() const;

    /** Perform a lateral/pitch toss of the ball to a target player pawn */
    UFUNCTION(BlueprintCallable, Category = "BallAction")
    bool ExecutePitch(APSPlayerPawn* TargetPlayer);

    /** Kick the ball with a specified power and angle */
    UFUNCTION(BlueprintCallable, Category = "BallAction")
    bool ExecuteKick(APSBall* Ball, float KickPower, float LaunchAngle);

    /** Fumble the ball, launching it with a pop-out velocity */
    UFUNCTION(BlueprintCallable, Category = "BallAction")
    void FumbleBall();

    /** Resolves a physical tackle contest against an incoming defender. A carrier it downs is
     *  announced as a Tackle event on the telemetry bus (tackler, carrier, spot, sack); the
     *  play simulation, the outcome authority, rules on the play from that event and measures
     *  its yards from the line of scrimmage. True when the tackle succeeded, even if the
     *  carrier survived the hit or fumbled. */
    UFUNCTION(BlueprintCallable, Category = "BallAction")
    bool ResolveTackle(APSPlayerPawn* Defender);

    /** Hears the snap's line of scrimmage on the bus, for the sack call. BeginPlay binds;
     *  headless tests call it. Idempotent. */
    void BindToBus();

    void UnbindFromBus();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);

    /** The last snap's line of scrimmage (world X; the offense attacks +X), once one is heard.
     *  A quarterback with the ball brought down behind it is sacked. */
    float LineOfScrimmageX = 0.f;
    bool bHasLineOfScrimmage = false;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
};
