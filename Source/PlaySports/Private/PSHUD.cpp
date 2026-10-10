#include "PSHUD.h"
#include "PSOverlayScoreBugWidget.h"
#include "PSPlatformTiers.h"

APSHUD::APSHUD()
{
    ScoreboardWidgetClass = UPSOverlayScoreBugWidget::StaticClass();
    ScoreboardWidget = nullptr;
    ChyronWidgetClass = UPSOverlayChyronWidget::StaticClass();
    ChyronWidget = nullptr;
}

void APSHUD::BeginPlay()
{
    Super::BeginPlay();

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
