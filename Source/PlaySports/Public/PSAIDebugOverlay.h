// PSAIDebugOverlay.h - Epic 85.2: the AI debug overlay's cards, laid out with Epic 28's badge rules (pure)
#pragma once

#include "CoreMinimal.h"
#include "PSAIDecisionTypes.h"
#include "PSOverlayBadgeTypes.h"

/** One player as the overlay shows him: who, what he decided, where he is and what he goes at. */
struct FPSAIDebugCardSource
{
    FName PlayerId;

    /** UPSAIDecisionLog::DescribeForOverlay of his latest decision. */
    FString Text;

    /** Where he stands (his actor's location): the card sits OverlayHeightCm above it. */
    FVector Location = FVector::ZeroVector;

    bool bOffense = true;

    bool bHasTarget = false;
    FVector TargetLocation = FVector::ZeroVector;
};

/**
 * The AI debug overlay's arithmetic (Epic 85.2), with no world access so headless tests drive it
 * with any camera. It reuses Track A's badge rendering rules (PSOverlayBadgeLayout, Epic 28):
 * the same projection, the same scale by distance and the same nudging clear of each other. Unlike
 * the badges, which hide most players during the play, every player with a decision has a card.
 */
namespace PSAIDebugOverlay
{
    /** Problems with the overlay's settings, one line each (empty when sound). */
    PLAYSPORTS_API TArray<FString> ValidateTuning(const FPSAIDebugTuning& Tuning);

    /** Text with each line longer than MaxLineChars broken at its last space before the limit
     *  (a word longer than a line is cut). */
    PLAYSPORTS_API FString WrapText(const FString& Text, int32 MaxLineChars);

    /** A card's size for Text at FontSize, in Slate units: its longest line times OverlayCharWidth
     *  times the type size across, its lines times OverlayLineHeight times the type size down,
     *  plus OverlayPadding all round. */
    PLAYSPORTS_API FVector2D MeasureText(const FString& Text, int32 FontSize, const FPSAIDebugTuning& Tuning);

    /**
     * Every source's card for View, in viewport pixels. PixelsPerUnit is the viewport's DPI scale
     * (Slate units to pixels), so a card is the same size to the eye on a phone as on a monitor.
     *  - A card sits OverlayHeightCm above the player, scaled by its distance from the camera as a
     *    badge is (BadgeStyle's ReferenceDistance, MinScale, MaxScale); its type is OverlayFontSize
     *    times OverlayFontScale times that scale.
     *  - It is visible when it is in front of the camera and wholly on the screen.
     *  - Visible cards are nudged up clear of each other, nearer before farther
     *    (PSOverlayBadgeLayout::ResolveOverlaps with OverlayNudgeStep and OverlayMaxNudges); one
     *    with no room is bCrowdedOut and stays where it was.
     *  - A player with a target in front of the camera gets the two screen points for a line.
     */
    PLAYSPORTS_API TArray<FPSAIDebugCard> LayoutCards(const TArray<FPSAIDebugCardSource>& Sources, const FPSBadgeView& View,
        const FPSOverlayBadgeStyle& BadgeStyle, const FPSAIDebugTuning& Tuning, float PixelsPerUnit);
}
