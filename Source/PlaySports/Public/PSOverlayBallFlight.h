// PSOverlayBallFlight.h - Epic 32: ball flight prediction, kick judging and receiver lead (pure)
#pragma once

#include "CoreMinimal.h"
#include "PSOverlayBallFlightTypes.h"

/**
 * The ball-flight overlay's arithmetic, with no world access. A ball in flight is a drag-free
 * projectile under constant gravity (APSBall's UProjectileMovementComponent integrates exactly
 * that), so its whole flight follows from one instant of its physics state:
 *
 *     P(t) = P0 + V0 t + 1/2 (0, 0, g) t^2
 *
 * UPSOverlayBallFlightSubsystem reads that instant from the ball and draws what these return.
 */
namespace PSOverlayBallFlight
{
    /** Where the ball is Seconds after it was at Start going Velocity. */
    PLAYSPORTS_API FVector PositionAt(const FVector& Start, const FVector& Velocity, float GravityZ, float Seconds);

    /** The prediction's position Seconds after its release. */
    PLAYSPORTS_API FVector PositionAt(const FPSBallFlightPrediction& Prediction, float Seconds);

    /** When the ball comes down through Height (the later crossing), in seconds from Start;
     *  -1 when it never does. */
    PLAYSPORTS_API float SecondsToDescendTo(const FVector& Start, const FVector& Velocity, float GravityZ, float Height);

    /**
     * The flight from Start at Velocity: where it lands at LandingHeight (at GroundZ when it
     * never comes down to LandingHeight), its apex, and ArcPoints points from release to
     * landing. A ball that doesn't land within MaxSeconds is drawn to MaxSeconds.
     */
    PLAYSPORTS_API FPSBallFlightPrediction Predict(EPSBallFlightKind Kind, const FVector& Start, const FVector& Velocity, float GravityZ,
        float LandingHeight, float GroundZ, int32 ArcPoints, float MaxSeconds);

    /**
     * How far along the prediction a ball now at Location is, in seconds since release. Read
     * from its progress along the launch's heading, which a drag-free ball covers at a constant
     * speed: it doesn't depend on whether the ball has moved yet this frame. FallbackSeconds
     * when the launch has no speed across the ground (a ball kicked straight up).
     */
    PLAYSPORTS_API float ProgressSeconds(const FPSBallFlightPrediction& Prediction, const FVector& Location, float FallbackSeconds);

    /** How far Location is from where the prediction puts the ball at Seconds. */
    PLAYSPORTS_API float DeviationAt(const FPSBallFlightPrediction& Prediction, const FVector& Location, float Seconds);

    /**
     * A kick judged at the first goal posts ahead of it (Style.GoalPostX): wide of the uprights
     * left or right (as the kicker sees it), short (down before the goal, or under the bar), or
     * good. The posts stand across the field's X axis. Verdict None with no posts ahead.
     */
    PLAYSPORTS_API FPSKickReadout JudgeKick(const FPSBallFlightPrediction& Prediction, const FPSBallFlightStyle& Style);

    /**
     * Where a receiver at Location moving at Velocity will be on the ground after
     * ArrivalSeconds, and whether that is within CatchRadius of where the ball comes down.
     * Constant velocity: what he is doing now, carried on.
     */
    PLAYSPORTS_API FPSPassLead ComputeLead(const FVector& Location, const FVector& Velocity, float ArrivalSeconds, const FVector& LandingLocation,
        float CatchRadius, float GroundZ);
}
