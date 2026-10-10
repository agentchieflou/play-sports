// PSOverlayBadgeComponent.h - Epic 28: which players wear a floating position badge, and where it goes
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSOverlayBadgeTypes.h"
#include "PSPlatformTiers.h"
#include "PSPlayerPawn.h"
#include "PSOverlayBadgeComponent.generated.h"

class APSPlayerController;

/**
 * UPSOverlayBadgeComponent is the position-badge model for its APSPlayerController's human
 * (Epic 28): the letters floating over players' heads in the broadcast frame. UPSOverlayBadgeWidget
 * (made by APSHUD) only draws what it lays out.
 *
 *  - Who: every player on the field but the one the human controls (he has the reticle), by
 *    group: receivers, backs, the quarterback, the line, the defense. Each group's color and
 *    when it shows -- before the snap; during the play hidden, while the human can still throw,
 *    or always -- are data.
 *  - What: when the human's quarterback can throw, his receiver slots (UPSPassingComponent,
 *    left to right as the formation lines up) wear the button that throws to them: the glyph of
 *    the slot's action on the device in use ("X", "Y", "B", "RB", "A" on a gamepad), so a
 *    remapped button shows its new key. Everyone else wears his role's label ("QB", "TE").
 *  - Where: above his head, scaled by distance from the camera, then nudged up clear of the
 *    other badges and of the ball; one with no room isn't drawn.
 *  - Detail: the platform tier's OverlayDetail. Minimal keeps the essential groups only (the
 *    pass buttons); only Full fades badges in.
 *
 * Control, the controller's devices and catalog, whether the play is live (its
 * UPSPlayContextComponent) and the receiver order (its UPSPassingComponent) are all read from
 * where they live (rules 3 and 6). The style is Data/overlay_badges.json. The widget lays it out
 * each frame for the player's camera (UpdateForCurrentView); headless tests call Refresh with a
 * view of their own.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSOverlayBadgeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSOverlayBadgeComponent();

    static FString GetDefaultStylePath();

    /** The style in use, loaded from the default path on first use. */
    const FPSOverlayBadgeStyle& GetStyle();

    /** Replaces the style with JsonFilePath's, read through UPSDataIngestion. A style that fails
     *  ValidateStyle is refused and the current one kept. */
    bool LoadStyleFromJson(const FString& JsonFilePath);

    void SetStyle(const FPSOverlayBadgeStyle& InStyle);

    /** Problems with a style, one line each (empty when sound). */
    static TArray<FString> ValidateStyle(const FPSOverlayBadgeStyle& InStyle);

    /** The group a player of Role on Side belongs to. */
    static EPSBadgeGroup GroupFor(EPlayerRole Role, EPSTeamSide Side);

    /** Lays out at this level of detail; BeginPlay takes the platform tier's. */
    UFUNCTION(BlueprintCallable, Category = "Overlay")
    void SetOverlayDetail(EPSOverlayDetail InDetail);

    UFUNCTION(BlueprintPure, Category = "Overlay")
    EPSOverlayDetail GetOverlayDetail() const { return OverlayDetail; }

    /** Works out every badge for View: who wears one, what it says, and where it goes. */
    void Refresh(const FPSBadgeView& View);

    /** Moves the fade clock on. */
    void AdvanceTime(float DeltaSeconds);

    /** One frame: the fade clock moves on and the badges are laid out for the player's camera
     *  as it is now. UPSOverlayBadgeWidget calls this as it draws, after the camera has moved,
     *  so the badges never trail it by a frame and cost nothing while nothing draws them. */
    UFUNCTION(BlueprintCallable, Category = "Overlay")
    void UpdateForCurrentView(float DeltaSeconds);

    /** The player's camera and viewport now; false without a local camera or viewport. */
    bool GetCurrentView(FPSBadgeView& OutView) const;

    /** Every badge from the last Refresh, drawn or not. */
    const TArray<FPSPositionBadge>& GetBadges() const { return Badges; }

    /** The badges drawn now. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    TArray<FPSPositionBadge> GetVisibleBadges() const;

    /** The badge on Pawn from the last Refresh, or null when he has none. */
    const FPSPositionBadge* FindBadge(const APSPlayerPawn* Pawn) const;

protected:
    virtual void BeginPlay() override;

private:
    APSPlayerController* GetPlayerController() const;

    FPSOverlayBadgeStyle Style;
    bool bStyleLoaded = false;
    EPSOverlayDetail OverlayDetail = EPSOverlayDetail::Full;

    UPROPERTY(Transient)
    TArray<FPSPositionBadge> Badges;

    /** When each shown badge appeared, for the fade-in. */
    TMap<TWeakObjectPtr<APSPlayerPawn>, float> ShownSince;
    float Clock = 0.f;
};
