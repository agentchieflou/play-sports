#include "PSHUD.h"
#include "PSOverlayBadgeWidget.h"
#include "PSOverlayScoreBugWidget.h"
#include "PSPlatformTiers.h"

APSHUD::APSHUD()
{
    ScoreboardWidgetClass = UPSOverlayScoreBugWidget::StaticClass();
    ScoreboardWidget = nullptr;
    ChyronWidgetClass = UPSOverlayChyronWidget::StaticClass();
    ChyronWidget = nullptr;
    BadgeWidgetClass = UPSOverlayBadgeWidget::StaticClass();
    BadgeWidget = nullptr;
}

void APSHUD::BeginPlay()
{
    Super::BeginPlay();

    // Under the score bug: badges float over the field. Their own component trims them on a
    // Minimal tier.
    if (BadgeWidgetClass)
    {
        BadgeWidget = CreateWidget<UUserWidget>(GetOwningPlayerController(), BadgeWidgetClass);
        if (BadgeWidget)
        {
            BadgeWidget->AddToViewport();
        }
    }

    if (ScoreboardWidgetClass)
    {
        ScoreboardWidget = CreateWidget<UUserWidget>(GetOwningPlayerController(), ScoreboardWidgetClass);
        if (ScoreboardWidget)
        {
            ScoreboardWidget->AddToViewport();
        }
    }

    // The minimal tier draws the score bug only (Specs/Platform_Audit.md section 4).
    if (ChyronWidgetClass && PSPlatformTiers::GetActiveTier().OverlayDetail != EPSOverlayDetail::Minimal)
    {
        ChyronWidget = CreateWidget<UUserWidget>(GetOwningPlayerController(), ChyronWidgetClass);
        if (ChyronWidget)
        {
            ChyronWidget->AddToViewport();
        }
    }
}
