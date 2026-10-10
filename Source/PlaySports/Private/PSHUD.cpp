#include "PSHUD.h"
#include "PSOverlayBadgeWidget.h"
#include "PSOverlayPersonnelWidget.h"
#include "PSOverlayScoreBugWidget.h"
#include "PSPlatformTiers.h"
#include "PSTelestratorWidget.h"

APSHUD::APSHUD()
{
    ScoreboardWidgetClass = UPSOverlayScoreBugWidget::StaticClass();
    ScoreboardWidget = nullptr;
    ChyronWidgetClass = UPSOverlayChyronWidget::StaticClass();
    ChyronWidget = nullptr;
    PersonnelWidgetClass = UPSOverlayPersonnelWidget::StaticClass();
    PersonnelWidget = nullptr;
    BadgeWidgetClass = UPSOverlayBadgeWidget::StaticClass();
    BadgeWidget = nullptr;
    TelestratorWidgetClass = UPSTelestratorWidget::StaticClass();
    TelestratorWidget = nullptr;
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
    if (PersonnelWidgetClass && PSPlatformTiers::GetActiveTier().OverlayDetail != EPSOverlayDetail::Minimal)
    {
        PersonnelWidget = CreateWidget<UUserWidget>(GetOwningPlayerController(), PersonnelWidgetClass);
        if (PersonnelWidget)
        {
            PersonnelWidget->AddToViewport();
        }
    }
    if (ChyronWidgetClass && PSPlatformTiers::GetActiveTier().OverlayDetail != EPSOverlayDetail::Minimal)
    {
        ChyronWidget = CreateWidget<UUserWidget>(GetOwningPlayerController(), ChyronWidgetClass);
        if (ChyronWidget)
        {
            ChyronWidget->AddToViewport();
        }
    }

    // Over the broadcast package, under any menu opened later: the telestrator draws on the frame.
    if (TelestratorWidgetClass)
    {
        TelestratorWidget = CreateWidget<UUserWidget>(GetOwningPlayerController(), TelestratorWidgetClass);
        if (TelestratorWidget)
        {
            TelestratorWidget->AddToViewport();
        }
    }
}
