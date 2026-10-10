// PSOverlayReticle.h - Epic 30: the ring on the ground under the player the human controls
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSOverlayReticle.generated.h"

class APSPlayerPawn;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** What the reticle is showing. */
UENUM(BlueprintType)
enum class EPSReticleState : uint8
{
    /** The human controls no football player. */
    Hidden,
    /** Before the snap, or after the whistle. */
    PreSnap,
    /** During the play, without the ball. */
    InPlay,
    /** During the play, with the ball: the emphasised look. */
    BallCarrier
};

/** How the reticle looks in one state (Data/overlay_reticle.json). */
USTRUCT(BlueprintType)
struct FPSOverlayReticleStateStyle
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    EPSReticleState State = EPSReticleState::PreSnap;

    /** Ring radius on the ground, cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float Radius = 70.f;

    /** Multiplies the team color: above 1 glows, below 1 recedes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float Brightness = 1.f;

    /** Pulses per second; 0 holds still. Tiers without animation hold still anyway. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float PulseHz = 0.f;

    /** How far the radius swells at the top of a pulse, as a fraction of Radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float PulseAmount = 0.f;
};

/**
 * The reticle's look as data (Data/overlay_reticle.json; Architecture rule 4). Colors are
 * "#RRGGBB". Mesh and material are asset paths: engine basic shapes until an editor session
 * authors the broadcast hexagon (Specs/Overlay_Reticle_Spec.md).
 */
USTRUCT(BlueprintType)
struct FPSOverlayReticleStyle
{
    GENERATED_BODY()

    /** The reticle's color under an offensive player when the human's team isn't known. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString OffenseColor = TEXT("#2F80ED");

    /** ... and under a defensive player. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString DefenseColor = TEXT("#EB5757");

    /** Use the human's team's primary color (from team select) when it is known. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    bool bUseTeamColor = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString MeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

    /** The material's vector parameter the color goes into. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FName ColorParameter = TEXT("Color");

    /** The mesh's size at scale 1, cm across (the engine cylinder is 100). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float MeshDiameter = 100.f;

    /** How thick the ring is drawn, cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float Thickness = 2.f;

    /** Gap between the ground and the ring's underside, cm, so it never z-fights the grass. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float GroundClearance = 1.f;

    /** One entry per visible state: PreSnap, InPlay, BallCarrier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    TArray<FPSOverlayReticleStateStyle> ReticleStates;

    const FPSOverlayReticleStateStyle* FindState(EPSReticleState InState) const
    {
        return ReticleStates.FindByPredicate([InState](const FPSOverlayReticleStateStyle& Entry) { return Entry.State == InState; });
    }
};

/**
 * APSOverlayReticle is the selected-player indicator (Epic 30): a flat ring attached under a
 * pawn's feet, so it follows him with no per-frame work. UPSOverlayReticleComponent decides who
 * it marks and how it looks; this actor only draws it.
 */
UCLASS()
class PLAYSPORTS_API APSOverlayReticle : public AActor
{
    GENERATED_BODY()

public:
    APSOverlayReticle();

    /** Loads the style's mesh and material. A path that doesn't load leaves the ring
     *  without that asset (logged); the reticle's logic still runs. */
    void ApplyStyle(const FPSOverlayReticleStyle& Style);

    /** Attaches the ring under Target's feet and shows it; null detaches and hides it. */
    void Follow(APSPlayerPawn* Target);

    /** Sets what the ring shows: its state, color and radius. */
    void SetLook(EPSReticleState InState, const FLinearColor& InColor, float InRadius);

    UFUNCTION(BlueprintPure, Category = "Overlay")
    APSPlayerPawn* GetFollowedPawn() const { return FollowedPawn.Get(); }

    UFUNCTION(BlueprintPure, Category = "Overlay")
    EPSReticleState GetState() const { return State; }

    UFUNCTION(BlueprintPure, Category = "Overlay")
    FLinearColor GetColor() const { return Color; }

    UFUNCTION(BlueprintPure, Category = "Overlay")
    float GetRadius() const { return Radius; }

    UFUNCTION(BlueprintPure, Category = "Overlay")
    UStaticMeshComponent* GetRingMesh() const { return RingMesh; }

private:
    void UpdateRingTransform();

    UPROPERTY(VisibleAnywhere, Category = "Overlay")
    UStaticMeshComponent* RingMesh;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* RingMaterial;

    TWeakObjectPtr<APSPlayerPawn> FollowedPawn;
    EPSReticleState State = EPSReticleState::Hidden;
    FLinearColor Color = FLinearColor::White;
    float Radius = 0.f;
    FName ColorParameter;
    float MeshDiameter = 100.f;
    float Thickness = 2.f;
    float GroundClearance = 1.f;
};
