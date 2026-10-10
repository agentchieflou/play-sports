// PSOverlayBadgeTypes.h - Epic 28: the floating position badges, their style and the view they are laid out in
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerPawn.h"
#include "PSOverlayBadgeTypes.generated.h"

/** Who a badge is on, for its colors and when it shows. */
UENUM(BlueprintType)
enum class EPSBadgeGroup : uint8
{
    /** Wide receivers and tight ends. */
    Receiver,
    /** Running backs. */
    Back,
    Quarterback,
    /** Offensive linemen. */
    Line,
    /** Anyone on defense. */
    Defense
};

/** When a group's badges show during the play (before the snap is FPSBadgeGroupStyle::bPreSnap). */
UENUM(BlueprintType)
enum class EPSBadgeInPlay : uint8
{
    Hidden,
    /** While the human's QB can still throw: the pass buttons. */
    WhilePassing,
    Always
};

/** One group's look and timing (Data/overlay_badges.json). */
USTRUCT(BlueprintType)
struct FPSBadgeGroupStyle
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    EPSBadgeGroup Group = EPSBadgeGroup::Receiver;

    /** The badge's fill, "#RRGGBB". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString Color = TEXT("#2F80ED");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString TextColor = TEXT("#FFFFFF");

    /** Shown before the snap and after the whistle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    bool bPreSnap = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    EPSBadgeInPlay InPlay = EPSBadgeInPlay::Hidden;

    /** Kept on a tier whose OverlayDetail is Minimal: what play needs (the pass buttons). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    bool bEssential = false;
};

/** The badge text for a player with no pass button. */
USTRUCT(BlueprintType)
struct FPSBadgeRoleLabel
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    EPlayerRole Role = EPlayerRole::Quarterback;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString Label;
};

/**
 * The position badges' style and layout rules (Data/overlay_badges.json; Architecture rule 4).
 * Sizes are pixels at scale 1; world distances are cm.
 */
USTRUCT(BlueprintType)
struct FPSOverlayBadgeStyle
{
    GENERATED_BODY()

    /** One entry per group: Receiver, Back, Quarterback, Line, Defense. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    TArray<FPSBadgeGroupStyle> Groups;

    /** One label per role, for players without a pass button. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    TArray<FPSBadgeRoleLabel> RoleLabels;

    /** The badge sits this far above the top of the player's capsule. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float HeadClearance = 30.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float BadgeWidth = 44.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float BadgeHeight = 32.f;

    /** The label's type size at scale 1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    int32 FontSize = 18;

    /** A badge this far from the camera is drawn at scale 1; nearer is bigger, farther smaller. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float ReferenceDistance = 2500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float MinScale = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float MaxScale = 1.25f;

    /** Pixels kept clear around the ball on screen, each way. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float BallClearance = 28.f;

    /** A badge that would cover another or the ball moves up this far (times its scale) ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float NudgeStep = 14.f;

    /** ... at most this many times; one with no room even then isn't drawn. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    int32 MaxNudges = 4;

    /** A badge appearing fades in over this long on a Full tier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float FadeInSeconds = 0.2f;

    /** Badge the player the human controls too (he already has the reticle). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    bool bBadgeControlledPlayer = false;

    const FPSBadgeGroupStyle* FindGroup(EPSBadgeGroup InGroup) const
    {
        return Groups.FindByPredicate([InGroup](const FPSBadgeGroupStyle& Entry) { return Entry.Group == InGroup; });
    }

    /** The label for Role, or empty when the table has none. */
    FString LabelForRole(EPlayerRole InRole) const
    {
        const FPSBadgeRoleLabel* Entry = RoleLabels.FindByPredicate([InRole](const FPSBadgeRoleLabel& Candidate) { return Candidate.Role == InRole; });
        return Entry ? Entry->Label : FString();
    }
};

/** The camera the badges are laid out for. */
USTRUCT(BlueprintType)
struct FPSBadgeView
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FVector CameraLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FRotator CameraRotation = FRotator::ZeroRotator;

    /** Horizontal field of view, degrees. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float FOVDegrees = 90.f;

    /** Pixels. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FVector2D ViewportSize = FVector2D(1920.0, 1080.0);
};

/** One badge over one player, laid out on screen. */
USTRUCT(BlueprintType)
struct FPSPositionBadge
{
    GENERATED_BODY()

    UPROPERTY()
    TWeakObjectPtr<APSPlayerPawn> Pawn;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FName PlayerId;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    EPSBadgeGroup Group = EPSBadgeGroup::Receiver;

    /** The text: a pass button's glyph label ("X", "RB", "1") or the role's ("QB", "TE"). */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FString Label;

    /** A pass button's icon ID, for an imported glyph texture; none for a role label. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FName GlyphId;

    /** The receiver slot (0 is the leftmost) whose button throws to him; -1 for none. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    int32 PassSlot = -1;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FLinearColor Color = FLinearColor::White;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FLinearColor TextColor = FLinearColor::Black;

    /** Above his head, in the world. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FVector WorldAnchor = FVector::ZeroVector;

    /** From the camera to WorldAnchor, cm. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float Distance = 0.f;

    /** The badge's bottom center, in viewport pixels, after any nudge. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FVector2D ScreenPosition = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float Scale = 1.f;

    /** Width and height in viewport pixels, scale applied. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FVector2D Size = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float Opacity = 1.f;

    /** In front of the camera and wholly inside the viewport. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bOnScreen = false;

    /** Drawn: shown now, on screen, and with room. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bVisible = false;

    /** Not drawn because no nudge found it room clear of the other badges and the ball. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bCrowdedOut = false;

    /** How many times it moved up to clear the others and the ball. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    int32 Nudges = 0;
};
