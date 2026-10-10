#include "PSHUD.h"
#include "PSAIDebugOverlayWidget.h"
#include "PSOverlayBadgeWidget.h"
#include "PSOverlayPersonnelWidget.h"
#include "PSOverlayScoreBugWidget.h"
#include "PSPlatformTiers.h"
#include "PSTelestratorWidget.h"
#include "PSTitleSafeArea.h"

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
    AIDebugWidgetClass = UPSAIDebugOverlayWidget::StaticClass();
    AIDebugWidget = nullptr;
}

void APSHUD::BeginPlay()
{
    Super::BeginPlay();

#if !UE_BUILD_SHIPPING
    // Lowest of all: the AI debug overlay's cards float over the field, under everything else.
    if (AIDebugWidgetClass)
    {
        AIDebugWidget = CreateWidget<UUserWidget>(GetOwningPlayerController(), AIDebugWidgetClass);
        if (AIDebugWidget)
        {
            // Its cards follow the players, not the screen's edge.
            PSTitleSafeArea::AddWholeScreen(AIDebugWidget);
        }
    }
#endif

    // Under the score bug: badges float over the field. Their own component trims them on a
    // Minimal tier.
    if (BadgeWidgetClass)
    {
        BadgeWidget = CreateWidget<UUserWidget>(GetOwningPlayerController(), BadgeWidgetClass);
        if (BadgeWidget)
        {
            // Badges follow the players, not the screen's edge.
            PSTitleSafeArea::AddWholeScreen(BadgeWidget);
        }
    }

    if (ScoreboardWidgetClass)
    {
        ScoreboardWidget = CreateWidget<UUserWidget>(GetOwningPlayerController(), ScoreboardWidgetClass);
        if (ScoreboardWidget)
        {
            PSTitleSafeArea::AddInside(ScoreboardWidget);
        }
    }

    // The minimal tier draws the score bug only (Specs/Platform_Audit.md section 4).
    if (PersonnelWidgetClass && PSPlatformTiers::GetActiveTier().OverlayDetail != EPSOverlayDetail::Minimal)
    {
        PersonnelWidget = CreateWidget<UUserWidget>(GetOwningPlayerController(), PersonnelWidgetClass);
        if (PersonnelWidget)
        {
            PSTitleSafeArea::AddInside(PersonnelWidget);
        }
    }
    if (ChyronWidgetClass && PSPlatformTiers::GetActiveTier().OverlayDetail != EPSOverlayDetail::Minimal)
    {
        ChyronWidget = CreateWidget<UUserWidget>(GetOwningPlayerController(), ChyronWidgetClass);
        if (ChyronWidget)
        {
            PSTitleSafeArea::AddInside(ChyronWidget);
        }
    }

    // Over the broadcast package, under any menu opened later: the telestrator draws on the frame.
    if (TelestratorWidgetClass)
    {
        TelestratorWidget = CreateWidget<UUserWidget>(GetOwningPlayerController(), TelestratorWidgetClass);
        if (TelestratorWidget)
        {
            // Its strokes are drawn on the frame, edge to edge.
            PSTitleSafeArea::AddWholeScreen(TelestratorWidget);
        }
    }
}
