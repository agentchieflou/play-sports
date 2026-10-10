#include "PSOverlayBallFlight.h"

FVector PSOverlayBallFlight::PositionAt(const FVector& Start, const FVector& Velocity, float GravityZ, float Seconds)
{
    return Start + Velocity * Seconds + FVector(0.0, 0.0, 0.5 * GravityZ * Seconds * Seconds);
}

FVector PSOverlayBallFlight::PositionAt(const FPSBallFlightPrediction& Prediction, float Seconds)
{
    return PositionAt(Prediction.ReleaseLocation, Prediction.ReleaseVelocity, Prediction.GravityZ, Seconds);
}

float PSOverlayBallFlight::SecondsToDescendTo(const FVector& Start, const FVector& Velocity, float GravityZ, float Height)
{
    // 1/2 g t^2 + Vz t + (Z0 - Height) = 0
    const double Above = Start.Z - Height;
    const double Vz = Velocity.Z;
    const double G = GravityZ;
    if (FMath::Abs(G) < KINDA_SMALL_NUMBER)
    {
        // No gravity: a straight line, which comes down through Height only if it heads down.
        if (Vz < -KINDA_SMALL_NUMBER && Above >= 0.0)
        {
            return static_cast<float>(-Above / Vz);
        }
        return -1.f;
    }

    const double Discriminant = Vz * Vz - 2.0 * G * Above;
    if (Discriminant < 0.0)
    {
        return -1.f;
    }
    // Of the two crossings, the one on the way down (vertical speed -sqrt(D) there) is
    // (-Vz - sqrt(D)) / g: the later one under ordinary gravity.
    const double Seconds = (-Vz - FMath::Sqrt(Discriminant)) / G;
    return Seconds >= 0.0 ? static_cast<float>(Seconds) : -1.f;
}

FPSBallFlightPrediction PSOverlayBallFlight::Predict(EPSBallFlightKind Kind, const FVector& Start, const FVector& Velocity, float GravityZ,
    float LandingHeight, float GroundZ, int32 ArcPoints, float MaxSeconds)
{
    FPSBallFlightPrediction Prediction;
    Prediction.bValid = true;
    Prediction.Kind = Kind;
    Prediction.ReleaseLocation = Start;
    Prediction.ReleaseVelocity = Velocity;
    Prediction.GravityZ = GravityZ;

    const float Longest = FMath::Max(MaxSeconds, 0.f);
    float Height = LandingHeight;
    float Landing = SecondsToDescendTo(Start, Velocity, GravityZ, Height);
    if (Landing < 0.f && !FMath::IsNearlyEqual(LandingHeight, GroundZ))
    {
        // Never down to catch height (released under it, and not rising through it): it comes
        // down on the ground instead.
        Height = GroundZ;
        Landing = SecondsToDescendTo(Start, Velocity, GravityZ, Height);
    }

    Prediction.LandingHeight = Height;
    Prediction.bLands = Landing >= 0.f && Landing <= Longest;
    Prediction.LandingSeconds = Prediction.bLands ? Landing : Longest;
    Prediction.LandingLocation = PositionAt(Start, Velocity, GravityZ, Prediction.LandingSeconds);
    if (Prediction.bLands)
    {
        Prediction.LandingLocation.Z = Height;
    }

    Prediction.ApexSeconds = (GravityZ < 0.f && Velocity.Z > 0.0)
        ? FMath::Min(static_cast<float>(-Velocity.Z / GravityZ), Prediction.LandingSeconds)
        : 0.f;
    Prediction.ApexLocation = PositionAt(Start, Velocity, GravityZ, Prediction.ApexSeconds);

    const int32 Count = FMath::Max(ArcPoints, 2);
    Prediction.ArcPoints.Reserve(Count);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const float Seconds = Prediction.LandingSeconds * static_cast<float>(Index) / static_cast<float>(Count - 1);
        Prediction.ArcPoints.Add(PositionAt(Start, Velocity, GravityZ, Seconds));
    }
    Prediction.ArcPoints.Last() = Prediction.LandingLocation;
    return Prediction;
}

float PSOverlayBallFlight::ProgressSeconds(const FPSBallFlightPrediction& Prediction, const FVector& Location, float FallbackSeconds)
{
    const FVector Heading(Prediction.ReleaseVelocity.X, Prediction.ReleaseVelocity.Y, 0.0);
    const double SpeedSquared = Heading.SizeSquared();
    if (SpeedSquared < 1.0)
    {
        return FallbackSeconds;
    }
    const FVector Moved = Location - Prediction.ReleaseLocation;
    const double Seconds = FVector::DotProduct(FVector(Moved.X, Moved.Y, 0.0), Heading) / SpeedSquared;
    return FMath::Max(0.f, static_cast<float>(Seconds));
}

float PSOverlayBallFlight::DeviationAt(const FPSBallFlightPrediction& Prediction, const FVector& Location, float Seconds)
{
    return static_cast<float>(FVector::Dist(Location, PositionAt(Prediction, Seconds)));
}

FPSKickReadout PSOverlayBallFlight::JudgeKick(const FPSBallFlightPrediction& Prediction, const FPSBallFlightStyle& Style)
{
    FPSKickReadout Readout;
    const FVector& Velocity = Prediction.ReleaseVelocity;
    if (!Prediction.bValid || FMath::Abs(Velocity.X) < 1.0)
    {
        // Not heading down the field.
        return Readout;
    }

    // The first posts ahead of the ball.
    double Crossing = -1.0;
    for (const float PostX : Style.GoalPostX)
    {
        const double Seconds = (PostX - Prediction.ReleaseLocation.X) / Velocity.X;
        if (Seconds > 0.0 && (Crossing < 0.0 || Seconds < Crossing))
        {
            Crossing = Seconds;
            Readout.GoalPostX = PostX;
        }
    }
    if (Crossing < 0.0)
    {
        return Readout;
    }

    Readout.CrossingSeconds = static_cast<float>(Crossing);
    Readout.CrossingLocation = PositionAt(Prediction, Readout.CrossingSeconds);
    // UE's axes: X forward, Y right. A kicker facing +X has +Y on his right; facing -X, -Y.
    const double Toward = Velocity.X > 0.0 ? 1.0 : -1.0;
    Readout.LateralOffset = static_cast<float>((Readout.CrossingLocation.Y - Style.GoalPostY) * Toward);
    Readout.HeightOverBar = static_cast<float>(Readout.CrossingLocation.Z - Style.GroundZ) - Style.CrossbarHeight;
    Readout.MarginInside = Style.UprightWidth * 0.5f - FMath::Abs(Readout.LateralOffset);

    const float Grounded = SecondsToDescendTo(Prediction.ReleaseLocation, Velocity, Prediction.GravityZ, Style.GroundZ);
    Readout.bReachesGoal = Grounded < 0.f || Grounded >= Readout.CrossingSeconds;

    if (!Readout.bReachesGoal)
    {
        Readout.Verdict = EPSKickVerdict::Short;
    }
    else if (Readout.MarginInside < 0.f)
    {
        Readout.Verdict = Readout.LateralOffset < 0.f ? EPSKickVerdict::WideLeft : EPSKickVerdict::WideRight;
    }
    else if (Readout.HeightOverBar < 0.f)
    {
        Readout.Verdict = EPSKickVerdict::Short;
    }
    else
    {
        Readout.Verdict = EPSKickVerdict::Good;
    }
    Readout.Label = Style.LabelFor(Readout.Verdict);
    return Readout;
}

FPSPassLead PSOverlayBallFlight::ComputeLead(const FVector& Location, const FVector& Velocity, float ArrivalSeconds, const FVector& LandingLocation,
    float CatchRadius, float GroundZ)
{
    FPSPassLead Lead;
    Lead.bValid = true;
    Lead.ArrivalSeconds = FMath::Max(ArrivalSeconds, 0.f);
    Lead.LeadLocation = FVector(Location.X + Velocity.X * Lead.ArrivalSeconds, Location.Y + Velocity.Y * Lead.ArrivalSeconds, GroundZ);
    Lead.MissDistance = static_cast<float>(FVector::Dist2D(Lead.LeadLocation, LandingLocation));
    Lead.CatchRadius = CatchRadius;
    Lead.bOnTarget = Lead.MissDistance <= CatchRadius;
    return Lead;
}
