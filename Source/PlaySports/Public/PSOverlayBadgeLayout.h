// PSOverlayBadgeLayout.h - Epic 28: placing position badges on screen (pure)
#pragma once

#include "CoreMinimal.h"
#include "PSOverlayBadgeTypes.h"

/**
 * The badges' screen arithmetic, with no world access, so headless tests can drive it with any
 * view. UPSOverlayBadgeComponent feeds it the player's camera.
 */
namespace PSOverlayBadgeLayout
{
    /**
     * Where World appears in View, in viewport pixels (origin top left), with a perspective
     * camera of horizontal field of view View.FOVDegrees (the engine's default, which keeps the
     * horizontal view and fits the vertical to the aspect ratio). False when World is behind the
     * camera. OutDepth is its distance along the camera's forward axis.
     */
    PLAYSPORTS_API bool ProjectToScreen(const FPSBadgeView& View, const FVector& World, FVector2D& OutScreen, float& OutDepth);

    /** A badge's scale at Distance from the camera: ReferenceDistance / Distance, kept between
     *  MinScale and MaxScale. */
    PLAYSPORTS_API float ScaleForDistance(const FPSOverlayBadgeStyle& Style, float Distance);

    /** The badge's rectangle on screen: ScreenPosition is its bottom center. */
    PLAYSPORTS_API FBox2D BadgeRect(const FPSPositionBadge& Badge);

    /** True when Rect lies wholly inside the viewport. */
    PLAYSPORTS_API bool IsInsideViewport(const FBox2D& Rect, const FVector2D& ViewportSize);

    /**
     * Keeps the visible badges from covering each other or the ball (BallRect, when the ball is
     * on screen). Badges are placed in order: pass buttons first, then nearer before farther
     * (ties by PlayerId). One that would overlap a placed badge or the ball moves up by
     * NudgeStep x its scale, at most MaxNudges times; one still in the way, or pushed off the
     * top of the screen, isn't drawn (bCrowdedOut).
     */
    PLAYSPORTS_API void ResolveOverlaps(TArray<FPSPositionBadge>& Badges, const FBox2D* BallRect, const FPSOverlayBadgeStyle& Style,
        const FVector2D& ViewportSize);
}
