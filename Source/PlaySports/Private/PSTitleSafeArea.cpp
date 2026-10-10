#include "PSTitleSafeArea.h"
#include "PSPlatformTiers.h"
#include "Blueprint/UserWidget.h"
#include "Widgets/Layout/Anchors.h"

float PSTitleSafeArea::ClampFraction(float Fraction)
{
    return FMath::Clamp(Fraction, 0.5f, 1.f);
}

float PSTitleSafeArea::GetActiveFraction()
{
    return ClampFraction(PSPlatformTiers::GetActiveTier().TitleSafeArea);
}

void PSTitleSafeArea::GetAnchorBounds(float Fraction, FVector2D& OutMin, FVector2D& OutMax)
{
    const float Inset = (1.f - ClampFraction(Fraction)) * 0.5f;
    OutMin = FVector2D(Inset, Inset);
    OutMax = FVector2D(1.f - Inset, 1.f - Inset);
}

FMargin PSTitleSafeArea::MakeMargin(const FVector2D& Size, float Fraction)
{
    const float Inset = (1.f - ClampFraction(Fraction)) * 0.5f;
    const float Horizontal = FMath::Max(static_cast<float>(Size.X), 0.f) * Inset;
    const float Vertical = FMath::Max(static_cast<float>(Size.Y), 0.f) * Inset;
    return FMargin(Horizontal, Vertical, Horizontal, Vertical);
}

void PSTitleSafeArea::AddInside(UUserWidget* Widget, int32 ZOrder)
{
    if (!Widget)
    {
        return;
    }
    FVector2D Min;
    FVector2D Max;
    GetAnchorBounds(GetActiveFraction(), Min, Max);
    // Stretched between the anchors with no offsets, the widget fills the area at any resolution.
    Widget->SetAnchorsInViewport(FAnchors(static_cast<float>(Min.X), static_cast<float>(Min.Y), static_cast<float>(Max.X), static_cast<float>(Max.Y)));
    Widget->AddToViewport(ZOrder);
}

void PSTitleSafeArea::AddWholeScreen(UUserWidget* Widget, int32 ZOrder)
{
    if (Widget)
    {
        Widget->AddToViewport(ZOrder);
    }
}
