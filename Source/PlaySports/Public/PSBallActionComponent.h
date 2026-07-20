// PSBallActionComponent.h - Epic C3: extracted ball-action logic from APSPlayerPawn
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSBallActionComponent.generated.h"

class APSPlayerPawn;
class APSBall;

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
     *  happens to hit). */
    UFUNCTION(BlueprintCallable, Category = "BallAction")
    bool ThrowPass(APSBall* Ball, const FVector& TargetLocation, bool bHighArc = false, APSPlayerPawn* IntendedTarget = nullptr);

    /** Perform an instant handoff of the ball to a target player pawn */
    UFUNCTION(BlueprintCallable, Category = "BallAction")
    bool ExecuteHandoff(APSPlayerPawn* TargetPlayer);

    /** Perform a lateral/pitch toss of the ball to a target player pawn */
    UFUNCTION(BlueprintCallable, Category = "BallAction")
    bool ExecutePitch(APSPlayerPawn* TargetPlayer);

    /** Kick the ball with a specified power and angle */
    UFUNCTION(BlueprintCallable, Category = "BallAction")
    bool ExecuteKick(APSBall* Ball, float KickPower, float LaunchAngle);

    /** Fumble the ball, launching it with a pop-out velocity */
    UFUNCTION(BlueprintCallable, Category = "BallAction")
    void FumbleBall();

    /** Resolve a physical tackle collision contest against an incoming defender */
    UFUNCTION(BlueprintCallable, Category = "BallAction")
    bool ResolveTackle(APSPlayerPawn* Defender);
};
