#include "PSHUD.h"

APSHUD::APSHUD()
{
    ScoreboardWidgetClass = nullptr;
    ScoreboardWidget = nullptr;
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
}
