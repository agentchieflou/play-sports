#include "PSOverlayBadgeLayout.h"

namespace PSOverlayBadgeLayoutPrivate
{
    /** Strict overlap: rectangles that only touch don't cover each other. */
    bool Overlaps(const FBox2D& A, const FBox2D& B)
    {
        return A.Min.X < B.Max.X && A.Max.X > B.Min.X && A.Min.Y < B.Max.Y && A.Max.Y > B.Min.Y;
    }
}

bool PSOverlayBadgeLayout::ProjectToScreen(const FPSBadgeView& View, const FVector& World, FVector2D& OutScreen, float& OutDepth)
{
    const FRotationMatrix Axes(View.CameraRotation);
    const FVector Delta = World - View.CameraLocation;
    const double Depth = FVector::DotProduct(Delta, Axes.GetUnitAxis(EAxis::X));
    OutDepth = static_cast<float>(Depth);
    if (Depth <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    // The engine's perspective keeps the horizontal field of view: the focal length in pixels
    // is half the viewport's width over tan(FOV / 2), the same both ways.
    const double HalfFOV = FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(View.FOVDegrees), 1.0, 170.0) * 0.5);
    const double Focal = (View.ViewportSize.X * 0.5) / FMath::Tan(HalfFOV);
    OutScreen.X = View.ViewportSize.X * 0.5 + FVector::DotProduct(Delta, Axes.GetUnitAxis(EAxis::Y)) / Depth * Focal;
    OutScreen.Y = View.ViewportSize.Y * 0.5 - FVector::DotProduct(Delta, Axes.GetUnitAxis(EAxis::Z)) / Depth * Focal;
    return true;
}

float PSOverlayBadgeLayout::ScaleForDistance(const FPSOverlayBadgeStyle& Style, float Distance)
{
    if (Distance <= KINDA_SMALL_NUMBER)
    {
        return Style.MaxScale;
    }
    return FMath::Clamp(Style.ReferenceDistance / Distance, Style.MinScale, Style.MaxScale);
}

FBox2D PSOverlayBadgeLayout::BadgeRect(const FPSPositionBadge& Badge)
{
    const FVector2D Min(Badge.ScreenPosition.X - Badge.Size.X * 0.5, Badge.ScreenPosition.Y - Badge.Size.Y);
    const FVector2D Max(Badge.ScreenPosition.X + Badge.Size.X * 0.5, Badge.ScreenPosition.Y);
    return FBox2D(Min, Max);
}

bool PSOverlayBadgeLayout::IsInsideViewport(const FBox2D& Rect, const FVector2D& ViewportSize)
{
    return Rect.Min.X >= 0.0 && Rect.Min.Y >= 0.0 && Rect.Max.X <= ViewportSize.X && Rect.Max.Y <= ViewportSize.Y;
}

void PSOverlayBadgeLayout::ResolveOverlaps(TArray<FPSPositionBadge>& Badges, const FBox2D* BallRect, const FPSOverlayBadgeStyle& Style,
    const FVector2D& ViewportSize)
{
    using namespace PSOverlayBadgeLayoutPrivate;

    TArray<int32> Order;
    for (int32 Index = 0; Index < Badges.Num(); ++Index)
    {
        if (Badges[Index].bVisible)
        {
            Order.Add(Index);
        }
    }
    // Pass buttons first, then nearer before farther, then by PlayerId so the result is stable.
    Order.Sort([&Badges](int32 A, int32 B)
    {
        const FPSPositionBadge& Left = Badges[A];
        const FPSPositionBadge& Right = Badges[B];
        const bool bLeftButton = Left.PassSlot >= 0;
        const bool bRightButton = Right.PassSlot >= 0;
        if (bLeftButton != bRightButton)
        {
            return bLeftButton;
        }
        if (!FMath::IsNearlyEqual(Left.Distance, Right.Distance))
        {
            return Left.Distance < Right.Distance;
        }
        return Left.PlayerId.LexicalLess(Right.PlayerId);
    });

    TArray<FBox2D> Placed;
    for (const int32 Index : Order)
    {
        FPSPositionBadge& Badge = Badges[Index];
        const FVector2D Origin = Badge.ScreenPosition;
        bool bPlaced = false;
        for (int32 Nudge = 0; Nudge <= FMath::Max(Style.MaxNudges, 0); ++Nudge)
        {
            Badge.ScreenPosition = Origin - FVector2D(0.0, Nudge * Style.NudgeStep * Badge.Scale);
            const FBox2D Rect = BadgeRect(Badge);
            if (!IsInsideViewport(Rect, ViewportSize))
            {
                // Pushed off the top of the screen.
                break;
            }
            bool bClear = !(BallRect && Overlaps(Rect, *BallRect));
            for (int32 Other = 0; bClear && Other < Placed.Num(); ++Other)
            {
                bClear = !Overlaps(Rect, Placed[Other]);
            }
            if (bClear)
            {
                Badge.Nudges = Nudge;
                Placed.Add(Rect);
                bPlaced = true;
                break;
            }
        }
        if (!bPlaced)
        {
            Badge.ScreenPosition = Origin;
            Badge.Nudges = 0;
            Badge.bVisible = false;
            Badge.bCrowdedOut = true;
        }
    }
}
