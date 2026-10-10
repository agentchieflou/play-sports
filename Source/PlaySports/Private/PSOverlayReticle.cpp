#include "PSOverlayReticle.h"
#include "PSPlayerPawn.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

APSOverlayReticle::APSOverlayReticle()
{
    PrimaryActorTick.bCanEverTick = false;

    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    RingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RingMesh"));
    RingMesh->SetupAttachment(RootComponent);
    RingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RingMesh->SetGenerateOverlapEvents(false);
    RingMesh->SetCastShadow(false);
    RingMesh->bReceivesDecals = false;
    RingMesh->SetCanEverAffectNavigation(false);

    RingMaterial = nullptr;
    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
}

void APSOverlayReticle::ApplyStyle(const FPSOverlayReticleStyle& Style)
{
    ColorParameter = Style.ColorParameter;
    MeshDiameter = FMath::Max(Style.MeshDiameter, 1.f);
    Thickness = FMath::Max(Style.Thickness, 0.f);
    GroundClearance = Style.GroundClearance;

    UStaticMesh* Mesh = Style.MeshPath.IsEmpty() ? nullptr : Cast<UStaticMesh>(FSoftObjectPath(Style.MeshPath).TryLoad());
    if (!Mesh && !Style.MeshPath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("APSOverlayReticle: Could not load the reticle mesh %s."), *Style.MeshPath);
    }
    RingMesh->SetStaticMesh(Mesh);

    UMaterialInterface* Material = Style.MaterialPath.IsEmpty() ? nullptr : Cast<UMaterialInterface>(FSoftObjectPath(Style.MaterialPath).TryLoad());
    if (!Material && !Style.MaterialPath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("APSOverlayReticle: Could not load the reticle material %s."), *Style.MaterialPath);
    }
    RingMaterial = Material ? UMaterialInstanceDynamic::Create(Material, this) : nullptr;
    if (RingMaterial)
    {
        RingMesh->SetMaterial(0, RingMaterial);
        RingMaterial->SetVectorParameterValue(ColorParameter, Color);
    }
    UpdateRingTransform();
}

void APSOverlayReticle::Follow(APSPlayerPawn* Target)
{
    if (FollowedPawn.Get() == Target)
    {
        SetActorHiddenInGame(Target == nullptr);
        return;
    }

    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    FollowedPawn = Target;
    if (!Target)
    {
        SetActorHiddenInGame(true);
        return;
    }

    // Attached, the ring rides with the pawn for free; it sits at the bottom of his capsule.
    AttachToActor(Target, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    SetActorRelativeLocation(FVector(0.f, 0.f, -Target->GetSimpleCollisionHalfHeight()));
    SetActorHiddenInGame(false);
}

void APSOverlayReticle::SetLook(EPSReticleState InState, const FLinearColor& InColor, float InRadius)
{
    State = InState;
    Color = InColor;
    Radius = FMath::Max(InRadius, 0.f);
    if (RingMaterial)
    {
        RingMaterial->SetVectorParameterValue(ColorParameter, Color);
    }
    UpdateRingTransform();
}

void APSOverlayReticle::UpdateRingTransform()
{
    // The mesh is MeshDiameter across and as tall at scale 1, centered on its pivot.
    const float Across = (2.f * Radius) / MeshDiameter;
    const float Tall = Thickness / MeshDiameter;
    RingMesh->SetRelativeScale3D(FVector(Across, Across, FMath::Max(Tall, KINDA_SMALL_NUMBER)));
    RingMesh->SetRelativeLocation(FVector(0.f, 0.f, GroundClearance + Thickness * 0.5f));
}
