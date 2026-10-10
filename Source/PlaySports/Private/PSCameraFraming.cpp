#include "PSCameraFraming.h"

namespace PSCameraFramingPrivate
{
    /** Any wider and the perspective math stops meaning anything. */
    static constexpr double MaxSupportedFieldOfView = 170.0;

    /** Used when a shot or the tuning carries no usable aspect ratio. */
    static constexpr double FallbackAspectRatio = 1.7778;

    /** A projected point this far outside [0, 1] still counts as in frame (float rounding at
     *  the frame's edge). */
    static constexpr double FrameEdgeTolerance = 1e-3;

    /** Closer than this along the line of sight (cm) counts as beside or behind the camera. */
    static constexpr double MinDepth = 1e-3;

    static double SafeAspect(double AspectRatio)
    {
        return AspectRatio > 0.0 ? AspectRatio : FallbackAspectRatio;
    }

    static FVector BoxCorner(const FBox& Box, int32 Index)
    {
        return FVector(
            (Index & 1) ? Box.Max.X : Box.Min.X,
            (Index & 2) ? Box.Max.Y : Box.Min.Y,
            (Index & 4) ? Box.Max.Z : Box.Min.Z);
    }
}

FBox UPSCameraFraming::ComputePlayerBox(const TArray<FVector>& Locations, const FPSAll22CameraTuning& Tuning)
{
    FBox Box(ForceInit);
    for (const FVector& Location : Locations)
    {
        Box += Location;
    }
    if (!Box.IsValid)
    {
        return Box;
    }

    const double Margin = FMath::Max(0.0, static_cast<double>(Tuning.FramingMarginCm));
    const double HalfHeight = FMath::Max(0.0, static_cast<double>(Tuning.PlayerHeightCm)) * 0.5;
    return Box.ExpandBy(FVector(Margin, Margin, HalfHeight));
}

FPSCameraShot UPSCameraFraming::FrameBox(const FPSAll22RigDef& Rig, const FBox& Box, float AttackDirection, float AspectRatio)
{
    using namespace PSCameraFramingPrivate;

    const FBox Framed = Box.IsValid ? Box : FBox(FVector::ZeroVector, FVector::ZeroVector);
    const FVector Aim = Framed.GetCenter();
    const double Direction = AttackDirection < 0.f ? -1.0 : 1.0;
    const double Rail = FMath::Max(0.0, static_cast<double>(Rig.RailHalfLengthCm));
    const double Aspect = SafeAspect(AspectRatio);

    // The rig's fixed spot: its height and standoff never change; a tracking rig slides along
    // its rail to stay square to the play.
    FVector RigLocation;
    FVector FallbackForward;
    if (Rig.Placement == EPSAll22RigPlacement::EndZone)
    {
        const double RailY = Rig.bTrackPlay ? FMath::Clamp<double>(Aim.Y, -Rail, Rail) : 0.0;
        RigLocation = FVector(-Direction * Rig.StandoffCm, RailY, Rig.HeightCm);
        FallbackForward = FVector(Direction, 0.0, 0.0);
    }
    else
    {
        const double RailX = Rig.bTrackPlay ? FMath::Clamp<double>(Aim.X, -Rail, Rail) : 0.0;
        RigLocation = FVector(RailX, -Rig.StandoffCm, Rig.HeightCm);
        FallbackForward = FVector(0.0, 1.0, 0.0);
    }

    FVector LookDirection = Aim - RigLocation;
    if (!LookDirection.Normalize())
    {
        LookDirection = FallbackForward;
    }
    const FRotator Rotation = LookDirection.Rotation();
    const FRotationMatrix Axes(Rotation);
    const FVector Forward = Axes.GetScaledAxis(EAxis::X);
    const FVector Right = Axes.GetScaledAxis(EAxis::Y);
    const FVector Up = Axes.GetScaledAxis(EAxis::Z);

    const double MinFov = FMath::Clamp<double>(Rig.MinFieldOfView, 1.0, MaxSupportedFieldOfView);
    const double MaxFov = FMath::Clamp<double>(Rig.MaxFieldOfView, MinFov, MaxSupportedFieldOfView);
    const double TanHalfMax = FMath::Tan(FMath::DegreesToRadians(MaxFov * 0.5));

    // Each corner needs the frame's half-width to reach |right| and its half-height to reach
    // |up| at that corner's depth. NeededTan is the widest of those at the rig's spot; PullBack
    // is how far back along the line of sight the widest zoom would hold them all.
    double NeededTan = 0.0;
    double PullBack = 0.0;
    bool bAllInFront = true;
    for (int32 Index = 0; Index < 8; ++Index)
    {
        const FVector Offset = BoxCorner(Framed, Index) - RigLocation;
        const double Depth = FVector::DotProduct(Offset, Forward);
        const double Reach = FMath::Max(
            FMath::Abs(FVector::DotProduct(Offset, Right)),
            FMath::Abs(FVector::DotProduct(Offset, Up)) * Aspect);
        PullBack = FMath::Max(PullBack, Reach / TanHalfMax - Depth);
        if (Depth <= MinDepth)
        {
            bAllInFront = false;
        }
        else
        {
            NeededTan = FMath::Max(NeededTan, Reach / Depth);
        }
    }

    FPSCameraShot Shot;
    Shot.Rotation = Rotation;
    Shot.AspectRatio = static_cast<float>(Aspect);
    if (bAllInFront && NeededTan <= TanHalfMax)
    {
        const double NeededFov = FMath::RadiansToDegrees(2.0 * FMath::Atan(NeededTan));
        Shot.Location = RigLocation;
        Shot.FieldOfView = static_cast<float>(FMath::Clamp<double>(NeededFov, MinFov, MaxFov));
    }
    else
    {
        // Too spread for the widest zoom (or the box reaches past the rig): back away along the
        // line of sight, which keeps the aim, by just enough (plus a centimetre of slack).
        Shot.Location = RigLocation - Forward * (PullBack + 1.0);
        Shot.FieldOfView = static_cast<float>(MaxFov);
    }
    return Shot;
}

FPSCameraShot UPSCameraFraming::FrameAll22(const FPSAll22RigDef& Rig, const FPSAll22CameraTuning& Tuning,
    const TArray<FVector>& Locations, float AttackDirection, float AspectRatio)
{
    return FrameBox(Rig, ComputePlayerBox(Locations, Tuning), AttackDirection, AspectRatio);
}

bool UPSCameraFraming::ProjectToShot(const FPSCameraShot& Shot, const FVector& Point, FVector2D& OutScreen)
{
    using namespace PSCameraFramingPrivate;

    const FRotationMatrix Axes(Shot.Rotation);
    const FVector Offset = Point - Shot.Location;
    const double Depth = FVector::DotProduct(Offset, Axes.GetScaledAxis(EAxis::X));
    if (Depth <= MinDepth)
    {
        OutScreen = FVector2D(-1.0, -1.0);
        return false;
    }

    const double TanHalf = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp<double>(Shot.FieldOfView, 1.0, MaxSupportedFieldOfView) * 0.5));
    const double Aspect = SafeAspect(Shot.AspectRatio);
    const double ScreenX = 0.5 + 0.5 * FVector::DotProduct(Offset, Axes.GetScaledAxis(EAxis::Y)) / (Depth * TanHalf);
    const double ScreenY = 0.5 - 0.5 * FVector::DotProduct(Offset, Axes.GetScaledAxis(EAxis::Z)) * Aspect / (Depth * TanHalf);
    OutScreen = FVector2D(ScreenX, ScreenY);
    return ScreenX >= -FrameEdgeTolerance && ScreenX <= 1.0 + FrameEdgeTolerance
        && ScreenY >= -FrameEdgeTolerance && ScreenY <= 1.0 + FrameEdgeTolerance;
}

bool UPSCameraFraming::IsPointInShot(const FPSCameraShot& Shot, const FVector& Point)
{
    FVector2D Screen;
    return ProjectToShot(Shot, Point, Screen);
}

TArray<FString> UPSCameraFraming::ValidateTuning(const FPSAll22CameraTuning& Tuning)
{
    TArray<FString> Problems;
    if (Tuning.All22Rigs.Num() == 0)
    {
        Problems.Add(TEXT("All22Rigs is empty: the film view needs at least one rig"));
    }

    TSet<FName> RigIds;
    for (int32 Index = 0; Index < Tuning.All22Rigs.Num(); ++Index)
    {
        const FPSAll22RigDef& Rig = Tuning.All22Rigs[Index];
        const FString Where = FString::Printf(TEXT("All22Rigs[%d]"), Index);
        if (Rig.RigId.IsNone() || RigIds.Contains(Rig.RigId))
        {
            Problems.Add(Where + TEXT(": RigId is empty or used twice"));
        }
        RigIds.Add(Rig.RigId);
        if (Rig.HeightCm <= 0.f)
        {
            Problems.Add(Where + TEXT(": HeightCm must be positive (the rig is elevated)"));
        }
        if (Rig.StandoffCm <= 0.f)
        {
            Problems.Add(Where + TEXT(": StandoffCm must be positive"));
        }
        if (Rig.RailHalfLengthCm < 0.f)
        {
            Problems.Add(Where + TEXT(": RailHalfLengthCm must be 0 or more"));
        }
        if (Rig.MinFieldOfView <= 0.f || Rig.MaxFieldOfView >= PSCameraFramingPrivate::MaxSupportedFieldOfView || Rig.MinFieldOfView > Rig.MaxFieldOfView)
        {
            Problems.Add(Where + TEXT(": the zoom range must satisfy 0 < MinFieldOfView <= MaxFieldOfView < 170"));
        }
    }

    if (Tuning.FramingMarginCm < 0.f)
    {
        Problems.Add(TEXT("FramingMarginCm must be 0 or more"));
    }
    if (Tuning.PlayerHeightCm < 0.f)
    {
        Problems.Add(TEXT("PlayerHeightCm must be 0 or more"));
    }
    if (Tuning.AspectRatio <= 0.f)
    {
        Problems.Add(TEXT("AspectRatio must be positive"));
    }
    if (Tuning.ReframeSpeed < 0.f)
    {
        Problems.Add(TEXT("ReframeSpeed must be 0 or more"));
    }
    return Problems;
}
