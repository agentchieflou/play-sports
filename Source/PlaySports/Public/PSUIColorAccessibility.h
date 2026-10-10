// PSUIColorAccessibility.h - Epic 103.2: colorblind-safe colors for teams and overlays
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PSUIColorAccessibility.generated.h"

/** The player's color vision setting (the ColorblindMode setting's choices, in order). */
UENUM(BlueprintType)
enum class EPSColorblindMode : uint8
{
    Off,
    /** Red-blind. */
    Protanopia,
    /** Green-blind, the most common. */
    Deuteranopia,
    /** Blue-blind. */
    Tritanopia
};

/**
 * UPSUIColorLibrary is the one place a color is made safe for the player's color vision
 * (Epic 103.2). Anything that draws a team or overlay color -- team select today, Epic 37's
 * team-color resolution and the Track A overlay palettes as they arrive -- passes it through
 * ResolveColor, and picks a matchup's two colors with ResolveMatchupColors.
 *
 *   - Simulate: how a color looks with the deficiency (Machado et al. 2009, full severity,
 *     applied to linear RGB).
 *   - ResolveColor: daltonization. The detail the deficiency loses is shifted into channels
 *     the player still sees (Fidaner et al.), so red and green stop looking alike.
 *   - PerceivedDistance: CIE76 delta E between two colors as the player sees them.
 *   - ResolveMatchupColors: home and away must be told apart. When their primaries look too
 *     close (under MinDistance), the away side, then the home side, falls back to its
 *     secondary color, choosing the pair that looks the most different.
 */
UCLASS()
class PLAYSPORTS_API UPSUIColorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Color as a player with Mode sees it. Off returns it unchanged. */
    UFUNCTION(BlueprintPure, Category = "Accessibility")
    static FLinearColor Simulate(const FLinearColor& Color, EPSColorblindMode Mode);

    /** Color made easier to tell apart for a player with Mode. Off returns it unchanged. */
    UFUNCTION(BlueprintPure, Category = "Accessibility")
    static FLinearColor ResolveColor(const FLinearColor& Color, EPSColorblindMode Mode);

    /** How different A and B look to a player with Mode (CIE76 delta E; about 2.3 is just
     *  noticeable). */
    UFUNCTION(BlueprintPure, Category = "Accessibility")
    static float PerceivedDistance(const FLinearColor& A, const FLinearColor& B, EPSColorblindMode Mode);

    /** The two colors a home and an away side are drawn in, resolved for Mode. The primaries
     *  unless they look closer than MinDistance; then the pair (primary or secondary each)
     *  that looks the most different. */
    UFUNCTION(BlueprintCallable, Category = "Accessibility")
    static void ResolveMatchupColors(const FLinearColor& HomePrimary, const FLinearColor& HomeSecondary,
        const FLinearColor& AwayPrimary, const FLinearColor& AwaySecondary, EPSColorblindMode Mode, float MinDistance,
        FLinearColor& OutHome, FLinearColor& OutAway);
};
