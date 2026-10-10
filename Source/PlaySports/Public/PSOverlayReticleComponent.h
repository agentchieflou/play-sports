// PSOverlayReticleComponent.h - Epic 30: who the selected-player reticle marks, and how
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSOverlayReticle.h"
#include "PSPlatformTiers.h"
#include "PSPlayerPawn.h"
#include "PSOverlayReticleComponent.generated.h"

class APSPlayerController;

/**
 * UPSOverlayReticleComponent puts the selected-player reticle (APSOverlayReticle) under the
 * pawn its APSPlayerController controls (Epic 30), in that human's team color:
 *
 *   PreSnap      before the snap and after the whistle
 *   InPlay       during the play, without the ball
 *   BallCarrier  during the play, with the ball (the emphasised look)
 *   Hidden       while the human controls no football player
 *
 * It reads control from its own controller and whether the play is live from the controller's
 * UPSPlayContextComponent, so it keeps no second copy of either (rules 3 and 6); possession is
 * the pawn's. Looks are data (Data/overlay_reticle.json). Pulses run only on a tier whose
 * OverlayDetail is Full. It re-evaluates every tick; headless tests call Refresh.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSOverlayReticleComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSOverlayReticleComponent();

    static FString GetDefaultStylePath();

    /** The style in use, loaded from the default path on first use. */
    const FPSOverlayReticleStyle& GetStyle();

    /** Replaces the style with JsonFilePath's, read through UPSDataIngestion. A style that
     *  fails ValidateStyle is refused and the current one kept. */
    bool LoadStyleFromJson(const FString& JsonFilePath);

    /** Problems with a style, one line each (empty when sound). */
    static TArray<FString> ValidateStyle(const FPSOverlayReticleStyle& InStyle);

    /** The human's team color, used when the style asks for it. BeginPlay sets it from the
     *  team picked at team select (the level's team= option). */
    UFUNCTION(BlueprintCallable, Category = "Overlay")
    void SetTeamColor(const FLinearColor& InTeamColor);

    UFUNCTION(BlueprintCallable, Category = "Overlay")
    void ClearTeamColor();

    /** Draws at this level of detail; BeginPlay takes the platform tier's. */
    UFUNCTION(BlueprintCallable, Category = "Overlay")
    void SetOverlayDetail(EPSOverlayDetail InDetail);

    UFUNCTION(BlueprintPure, Category = "Overlay")
    EPSOverlayDetail GetOverlayDetail() const { return OverlayDetail; }

    /** What the reticle should show now. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    EPSReticleState ComputeState() const;

    /** The reticle's color for a pawn on Side before the state's brightness. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    FLinearColor ComputeBaseColor(EPSTeamSide Side);

    /** Moves the reticle to the controlled pawn and gives it the current state's look. */
    UFUNCTION(BlueprintCallable, Category = "Overlay")
    void Refresh();

    /** Advances the pulse clock; the tick calls this, then Refresh. */
    void AdvanceAnimation(float DeltaSeconds);

    /** The reticle actor, created on first use. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    APSOverlayReticle* GetReticle() const { return Reticle; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    APSPlayerController* GetPlayerController() const;
    APSOverlayReticle* EnsureReticle();

    /** The team picked at team select, from the level's options; false when none. */
    bool ResolveTeamColorFromLevel(FLinearColor& OutColor) const;

    UPROPERTY(Transient)
    APSOverlayReticle* Reticle;

    FPSOverlayReticleStyle Style;
    bool bStyleLoaded = false;

    FLinearColor TeamColor = FLinearColor::White;
    bool bHasTeamColor = false;

    EPSOverlayDetail OverlayDetail = EPSOverlayDetail::Full;
    float PulseClock = 0.f;
};
