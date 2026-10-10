// PSTitleSafeArea.h - Epic 150: HUD and menus stay inside the platform's title-safe area
#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"

class UUserWidget;

/**
 * The title-safe area (Epic 150): the centred share of the screen that HUD and menu text and
 * controls stay inside, so a TV's edge never hides them. It is one setting per platform, the
 * active tier's TitleSafeArea (Data/platform_tiers.json): 1 (the whole screen) on a PC or a
 * phone, 0.9 on Xbox Series X|S.
 *
 * Every widget the game puts on screen goes through here; tools/lint_conventions.py fails a
 * direct AddToViewport anywhere else.
 *  - AddInside: HUD widgets and captions are laid out inside the area.
 *  - AddWholeScreen: widgets that must cover the screen. These are overlays that follow the field
 *    rather than the screen's edge (badges over players, the telestrator's strokes, AI debug
 *    cards), and full-screen backdrops whose own content keeps to the area through MakeMargin
 *    (the menu screens).
 */
namespace PSTitleSafeArea
{
    /** The active tier's title-safe share of the screen, 0.5 to 1. */
    PLAYSPORTS_API float GetActiveFraction();

    /** Fraction clamped to what a title-safe area can be (0.5 to 1). */
    PLAYSPORTS_API float ClampFraction(float Fraction);

    /** The viewport anchors of a centred box Fraction of the screen wide and high: from
     *  OutMin to OutMax, each 0 to 1. */
    PLAYSPORTS_API void GetAnchorBounds(float Fraction, FVector2D& OutMin, FVector2D& OutMax);

    /** The margins inside a Size box that leave a centred box Fraction of it wide and high. */
    PLAYSPORTS_API FMargin MakeMargin(const FVector2D& Size, float Fraction);

    /** Adds Widget to its player's screen inside the active title-safe area. */
    PLAYSPORTS_API void AddInside(UUserWidget* Widget, int32 ZOrder = 0);

    /** Adds Widget over the whole screen: only for widgets that follow the field or keep their own
     *  content inside the area (see above). */
    PLAYSPORTS_API void AddWholeScreen(UUserWidget* Widget, int32 ZOrder = 0);
}
