// PSOverlayEmphasisTypes.h - Epic 36: player emphasis requests, their looks, and the style
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerPawn.h"
#include "PSOverlayEmphasisTypes.generated.h"

/** Why a player is emphasized; each kind has its own look and priority (data). */
UENUM(BlueprintType)
enum class EPSEmphasisKind : uint8
{
    /** A key-player callout: commentary naming him, a coaching tip about him. */
    Highlight,
    /** A mismatch alert: the receiver and the defender who can't cover him. */
    Mismatch,
    /** Replay focus: the player a replay is about. */
    Focus
};

/** What a player looks like now, all requests weighed. */
UENUM(BlueprintType)
enum class EPSEmphasisLook : uint8
{
    None,
    Highlight,
    Mismatch,
    Focus,
    /** Another player is in the spotlight: this one recedes. */
    Dimmed
};

/** One kind's look and weight (Data/player_emphasis.json). */
USTRUCT(BlueprintType)
struct FPSEmphasisKindStyle
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    EPSEmphasisKind Kind = EPSEmphasisKind::Highlight;

    /** The custom-depth stencil value the emphasis post-process material draws this kind's
     *  outline and glow for (1-255, one per look). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    int32 Stencil = 1;

    /** Of several requests on one player the highest wins, and under the budget the highest
     *  players are drawn first. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    int32 Priority = 1;
};

/**
 * Player emphasis rules (Data/player_emphasis.json; Architecture rule 4). The looks themselves --
 * outline, glow, dimming -- are the emphasis post-process material's, which reads the custom-depth
 * stencil values below (Specs/Player_Emphasis_Spec.md).
 */
USTRUCT(BlueprintType)
struct FPSEmphasisStyle
{
    GENERATED_BODY()

    /** One entry per kind: Highlight, Mismatch, Focus. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    TArray<FPSEmphasisKindStyle> Kinds;

    /** The stencil for players dimmed by another's spotlight. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    int32 DimStencil = 4;

    /** At most this many players emphasized at once (each costs custom-depth draws); the
     *  highest priority, newest first, are drawn. Dimmed players don't count. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    int32 MaxEmphasized = 4;

    /** In a spotlight, dim the other emphasized players too (false keeps their own look). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    bool bSpotlightDimsEmphasized = false;

    const FPSEmphasisKindStyle* FindKind(EPSEmphasisKind InKind) const
    {
        return Kinds.FindByPredicate([InKind](const FPSEmphasisKindStyle& Entry) { return Entry.Kind == InKind; });
    }
};

/** One request to emphasize one player. */
USTRUCT(BlueprintType)
struct FPSEmphasisRequest
{
    GENERATED_BODY()

    /** What ClearEmphasis takes. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    int32 Handle = 0;

    UPROPERTY()
    TWeakObjectPtr<APSPlayerPawn> Pawn;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    EPSEmphasisKind Kind = EPSEmphasisKind::Highlight;

    /** Who asked: "Commentary", "Replay", "Coaching", ... ClearSource drops all of one's. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FName Source;

    /** Dims everyone else while it lasts. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bSpotlight = false;

    /** Seconds left; 0 or less lasts until cleared. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float RemainingSeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bTimed = false;
};

/** How one player is drawn now. */
USTRUCT(BlueprintType)
struct FPSPawnEmphasis
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    EPSEmphasisLook Look = EPSEmphasisLook::None;

    /** The stencil his meshes carry; 0 while nothing is drawn on him. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    int32 Stencil = 0;

    /** Drawn: within the budget, on a tier that draws emphasis. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bRendered = false;

    /** The winning request's source; none for a dimmed player. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FName Source;
};
