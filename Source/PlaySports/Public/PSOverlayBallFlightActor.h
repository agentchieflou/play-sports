// PSOverlayBallFlightActor.h - Epic 32: draws the ball's predicted arc, landing spot, lead and kick readout
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSOverlayBallFlightTypes.h"
#include "PSPlatformTiers.h"
#include "PSOverlayBallFlightActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class USplineComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * APSOverlayBallFlight draws what UPSOverlayBallFlightSubsystem works out (Epic 32); it decides
 * nothing. In world space:
 *
 *  - the predicted arc as a spline, with a dot at each of its points (a ribbon is an editor
 *    pass, Specs/Ball_Flight_Overlay_Spec.md). On a Full tier the dots the ball has already
 *    passed go away; Simplified keeps the whole arc; Minimal draws no arc;
 *  - the landing-spot ring on the ground, its radius the receiver's catch radius;
 *  - the receiver's lead ring (passes; not on Minimal), green when he gets there in time;
 *  - the kick readout above the crossbar of the posts the kick is judged at.
 */
UCLASS()
class PLAYSPORTS_API APSOverlayBallFlight : public AActor
{
    GENERATED_BODY()

public:
    APSOverlayBallFlight();

    /** Loads the style's meshes and material and keeps its colors and sizes. A path that
     *  doesn't load leaves that part without the asset (logged); the rest still draws. */
    void ApplyStyle(const FPSBallFlightStyle& InStyle);

    /** Draws State at Detail; hides everything when State isn't visible. */
    void ShowFlight(const FPSBallFlightState& State, EPSOverlayDetail Detail);

    UFUNCTION(BlueprintPure, Category = "Overlay")
    USplineComponent* GetArcSpline() const { return ArcSpline; }

    UFUNCTION(BlueprintPure, Category = "Overlay")
    UInstancedStaticMeshComponent* GetArcDots() const { return ArcDots; }

    UFUNCTION(BlueprintPure, Category = "Overlay")
    UStaticMeshComponent* GetLandingRing() const { return LandingRing; }

    UFUNCTION(BlueprintPure, Category = "Overlay")
    UStaticMeshComponent* GetLeadRing() const { return LeadRing; }

    UFUNCTION(BlueprintPure, Category = "Overlay")
    UTextRenderComponent* GetReadoutText() const { return ReadoutText; }

    /** Dots drawn now. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    int32 GetShownDotCount() const;

    /** The lead ring's color now. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    FLinearColor GetLeadColor() const { return LeadColor; }

    /** The readout's text now; empty while none shows. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    FString GetReadoutLabel() const { return ReadoutLabel; }

private:
    UMaterialInstanceDynamic* MakeMaterial(UMaterialInterface* Base, UPrimitiveComponent* Target, const FLinearColor& Color);
    void SetMaterialColor(UMaterialInstanceDynamic* Material, const FLinearColor& Color) const;
    void PlaceRing(UStaticMeshComponent* Ring, const FVector& Center, float Radius) const;
    void RebuildDots(const FPSBallFlightPrediction& Prediction, int32 FirstDot);

    UPROPERTY(VisibleAnywhere, Category = "Overlay")
    USplineComponent* ArcSpline;

    UPROPERTY(VisibleAnywhere, Category = "Overlay")
    UInstancedStaticMeshComponent* ArcDots;

    UPROPERTY(VisibleAnywhere, Category = "Overlay")
    UStaticMeshComponent* LandingRing;

    UPROPERTY(VisibleAnywhere, Category = "Overlay")
    UStaticMeshComponent* LeadRing;

    UPROPERTY(VisibleAnywhere, Category = "Overlay")
    UTextRenderComponent* ReadoutText;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* ArcMaterial;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* LandingMaterial;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* LeadMaterial;

    FPSBallFlightStyle Style;
    FLinearColor ArcColor = FLinearColor::White;
    FLinearColor LandingColor = FLinearColor::Yellow;
    FLinearColor LeadOnColor = FLinearColor::Green;
    FLinearColor LeadOffColor = FLinearColor::Red;
    FLinearColor GoodColor = FLinearColor::Green;
    FLinearColor NoGoodColor = FLinearColor::Red;
    FLinearColor LeadColor = FLinearColor::Green;
    FString ReadoutLabel;

    /** What the dots were last built from, so a frame that changes nothing rebuilds nothing. */
    FVector DotsReleaseLocation = FVector::ZeroVector;
    FVector DotsReleaseVelocity = FVector::ZeroVector;
    int32 DotsFirst = INDEX_NONE;
};
