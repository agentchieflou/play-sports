#include "PSOverlayBallFlightActor.h"
#include "PSUITeamCatalog.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace PSOverlayBallFlightActorPrivate
{
    FLinearColor ParseOr(const FString& Hex, const FLinearColor& Fallback)
    {
        FLinearColor Parsed = Fallback;
        UPSUITeamCatalog::ParseHexColor(Hex, Parsed);
        return Parsed;
    }

    /** Overlay geometry is drawn, never collided with or lit into shadows. */
    void MakeDecorative(UPrimitiveComponent* Component)
    {
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCastShadow(false);
        Component->bReceivesDecals = false;
        Component->SetCanEverAffectNavigation(false);
    }

    template <typename AssetType>
    AssetType* LoadAsset(const FString& Path, const TCHAR* What)
    {
        if (Path.IsEmpty())
        {
            return nullptr;
        }
        AssetType* Asset = Cast<AssetType>(FSoftObjectPath(Path).TryLoad());
        if (!Asset)
        {
            UE_LOG(LogTemp, Warning, TEXT("APSOverlayBallFlight: Could not load the %s %s."), What, *Path);
        }
        return Asset;
    }
}

APSOverlayBallFlight::APSOverlayBallFlight()
{
    using namespace PSOverlayBallFlightActorPrivate;

    PrimaryActorTick.bCanEverTick = false;

    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    ArcSpline = CreateDefaultSubobject<USplineComponent>(TEXT("ArcSpline"));
    ArcSpline->SetupAttachment(RootComponent);
    ArcSpline->ClearSplinePoints(false);

    ArcDots = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ArcDots"));
    ArcDots->SetupAttachment(RootComponent);
    MakeDecorative(ArcDots);

    LandingRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LandingRing"));
    LandingRing->SetupAttachment(RootComponent);
    MakeDecorative(LandingRing);
    LandingRing->SetVisibility(false);

    LeadRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeadRing"));
    LeadRing->SetupAttachment(RootComponent);
    MakeDecorative(LeadRing);
    LeadRing->SetVisibility(false);

    ReadoutText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ReadoutText"));
    ReadoutText->SetupAttachment(RootComponent);
    ReadoutText->SetHorizontalAlignment(EHTA_Center);
    ReadoutText->SetVerticalAlignment(EVRTA_TextCenter);
    ReadoutText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ReadoutText->SetCastShadow(false);
    ReadoutText->SetVisibility(false);

    ArcMaterial = nullptr;
    LandingMaterial = nullptr;
    LeadMaterial = nullptr;
    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
}

void APSOverlayBallFlight::ApplyStyle(const FPSBallFlightStyle& InStyle)
{
    using namespace PSOverlayBallFlightActorPrivate;

    Style = InStyle;
    ArcColor = ParseOr(Style.ArcColor, FLinearColor::White);
    LandingColor = ParseOr(Style.LandingColor, FLinearColor::Yellow);
    LeadOnColor = ParseOr(Style.LeadOnTargetColor, FLinearColor::Green);
    LeadOffColor = ParseOr(Style.LeadOffTargetColor, FLinearColor::Red);
    GoodColor = ParseOr(Style.GoodColor, FLinearColor::Green);
    NoGoodColor = ParseOr(Style.NoGoodColor, FLinearColor::Red);
    LeadColor = LeadOnColor;

    ArcDots->SetStaticMesh(LoadAsset<UStaticMesh>(Style.DotMeshPath, TEXT("arc dot mesh")));
    UStaticMesh* RingMesh = LoadAsset<UStaticMesh>(Style.RingMeshPath, TEXT("ring mesh"));
    LandingRing->SetStaticMesh(RingMesh);
    LeadRing->SetStaticMesh(RingMesh);

    UMaterialInterface* Base = LoadAsset<UMaterialInterface>(Style.MaterialPath, TEXT("material"));
    ArcMaterial = MakeMaterial(Base, ArcDots, ArcColor);
    LandingMaterial = MakeMaterial(Base, LandingRing, LandingColor);
    LeadMaterial = MakeMaterial(Base, LeadRing, LeadColor);

    ReadoutText->SetWorldSize(FMath::Max(Style.ReadoutTextSize, 1.f));

    // Dots are rebuilt at the new size on the next draw.
    DotsFirst = INDEX_NONE;
}

UMaterialInstanceDynamic* APSOverlayBallFlight::MakeMaterial(UMaterialInterface* Base, UPrimitiveComponent* Target, const FLinearColor& Color)
{
    UMaterialInstanceDynamic* Material = Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
    if (Material)
    {
        Target->SetMaterial(0, Material);
        SetMaterialColor(Material, Color);
    }
    return Material;
}

void APSOverlayBallFlight::SetMaterialColor(UMaterialInstanceDynamic* Material, const FLinearColor& Color) const
{
    if (Material)
    {
        Material->SetVectorParameterValue(Style.ColorParameter, Color);
    }
}

void APSOverlayBallFlight::PlaceRing(UStaticMeshComponent* Ring, const FVector& Center, float Radius) const
{
    // The ring mesh is MeshDiameter across and as tall at scale 1, centered on its pivot.
    const float Diameter = FMath::Max(Style.MeshDiameter, 1.f);
    const float Across = (2.f * FMath::Max(Radius, 0.f)) / Diameter;
    const float Tall = FMath::Max(Style.RingThickness / Diameter, KINDA_SMALL_NUMBER);
    Ring->SetWorldScale3D(FVector(Across, Across, Tall));
    Ring->SetWorldLocation(FVector(Center.X, Center.Y, Style.GroundZ + Style.GroundClearance + Style.RingThickness * 0.5f));
}

void APSOverlayBallFlight::RebuildDots(const FPSBallFlightPrediction& Prediction, int32 FirstDot)
{
    ArcDots->ClearInstances();
    const float Scale = FMath::Max(Style.ArcDotDiameter, 0.f) / FMath::Max(Style.MeshDiameter, 1.f);
    for (int32 Index = FMath::Max(FirstDot, 0); Index < Prediction.ArcPoints.Num(); ++Index)
    {
        ArcDots->AddInstance(FTransform(FRotator::ZeroRotator, Prediction.ArcPoints[Index], FVector(Scale)), true);
    }
    DotsFirst = FirstDot;
}

int32 APSOverlayBallFlight::GetShownDotCount() const
{
    return ArcDots && ArcDots->IsVisible() && !IsHidden() ? ArcDots->GetInstanceCount() : 0;
}

void APSOverlayBallFlight::ShowFlight(const FPSBallFlightState& State, EPSOverlayDetail Detail)
{
    const FPSBallFlightPrediction& Prediction = State.Prediction;
    if (!State.bVisible || !Prediction.bValid)
    {
        SetActorHiddenInGame(true);
        ArcDots->ClearInstances();
        DotsFirst = INDEX_NONE;
        ArcSpline->ClearSplinePoints(true);
        LandingRing->SetVisibility(false);
        LeadRing->SetVisibility(false);
        ReadoutText->SetVisibility(false);
        ReadoutLabel.Reset();
        return;
    }
    SetActorHiddenInGame(false);

    // The arc. The spline always carries it; the dots depend on the tier.
    const bool bNewArc = !Prediction.ReleaseLocation.Equals(DotsReleaseLocation) || !Prediction.ReleaseVelocity.Equals(DotsReleaseVelocity)
        || ArcSpline->GetNumberOfSplinePoints() != Prediction.ArcPoints.Num();
    if (bNewArc)
    {
        ArcSpline->SetSplinePoints(Prediction.ArcPoints, ESplineCoordinateSpace::World, true);
        DotsReleaseLocation = Prediction.ReleaseLocation;
        DotsReleaseVelocity = Prediction.ReleaseVelocity;
        DotsFirst = INDEX_NONE;
    }

    const int32 PointCount = Prediction.ArcPoints.Num();
    int32 FirstDot = 0;
    if (Detail == EPSOverlayDetail::Minimal)
    {
        FirstDot = PointCount;
    }
    else if (Detail == EPSOverlayDetail::Full && Prediction.LandingSeconds > KINDA_SMALL_NUMBER && PointCount > 1)
    {
        // Points sit evenly in time; the ones the ball has already passed go.
        const float Along = FMath::Clamp(State.ElapsedSeconds / Prediction.LandingSeconds, 0.f, 1.f);
        FirstDot = FMath::Clamp(FMath::CeilToInt(Along * static_cast<float>(PointCount - 1) - KINDA_SMALL_NUMBER), 0, PointCount);
    }
    if (FirstDot != DotsFirst)
    {
        RebuildDots(Prediction, FirstDot);
    }
    ArcDots->SetVisibility(FirstDot < PointCount);

    // Where it comes down.
    if (Prediction.bLands)
    {
        SetMaterialColor(LandingMaterial, LandingColor);
        PlaceRing(LandingRing, Prediction.LandingLocation, State.LandingRadius);
    }
    LandingRing->SetVisibility(Prediction.bLands);

    // Where the receiver will be.
    const bool bLead = State.Lead.bValid && Detail != EPSOverlayDetail::Minimal;
    if (bLead)
    {
        LeadColor = State.Lead.bOnTarget ? LeadOnColor : LeadOffColor;
        SetMaterialColor(LeadMaterial, LeadColor);
        PlaceRing(LeadRing, State.Lead.LeadLocation, Style.LeadRadius);
    }
    LeadRing->SetVisibility(bLead);

    // The kick against the posts, above the crossbar, facing back down the field at the kick.
    const bool bReadout = State.Kind == EPSBallFlightKind::Kick && State.Kick.Verdict != EPSKickVerdict::None;
    if (bReadout)
    {
        ReadoutLabel = State.Kick.Label;
        ReadoutText->SetText(FText::FromString(ReadoutLabel));
        ReadoutText->SetTextRenderColor((State.Kick.Verdict == EPSKickVerdict::Good ? GoodColor : NoGoodColor).ToFColor(true));
        const float FacingYaw = Prediction.ReleaseVelocity.X >= 0.0 ? 180.f : 0.f;
        ReadoutText->SetWorldLocationAndRotation(
            FVector(State.Kick.GoalPostX, Style.GoalPostY, Style.GroundZ + Style.CrossbarHeight + Style.ReadoutHeight),
            FRotator(0.f, FacingYaw, 0.f));
    }
    else
    {
        ReadoutLabel.Reset();
    }
    ReadoutText->SetVisibility(bReadout);
}
