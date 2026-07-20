// PSHUDWidget.cpp - Epic 5: Scoreboard and Play Result Widget C++ base classes
#include "PSHUDWidget.h"
#include "Engine/World.h"

void UPSScoreboardWidget::NativeConstruct()
{
    Super::NativeConstruct();

    UWorld* World = GetWorld();
    if (World)
    {
        UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
        if (Bus)
        {
            Bus->OnSnap.AddDynamic(this, &UPSScoreboardWidget::HandleOnSnap);
            Bus->OnScore.AddDynamic(this, &UPSScoreboardWidget::HandleOnScore);
            Bus->OnPhaseChange.AddDynamic(this, &UPSScoreboardWidget::HandleOnPhaseChange);
        }
    }

    GameClockText = FText::FromString(TEXT("15:00"));
    PlayClockText = FText::FromString(TEXT("40"));
    PlayPhaseText = FText::FromString(TEXT("PreSnap"));
}

void UPSScoreboardWidget::HandleOnSnap(const FPSTelemetrySnapEvent& Event)
{
    YardLine = Event.YardLine;
    Down = Event.Down;
    Distance = Event.Distance;
    FormatGameClock(Event.GameClockSeconds);
}

void UPSScoreboardWidget::HandleOnScore(const FPSTelemetryScoreEvent& Event)
{
    HomeScore = Event.HomeScore;
    AwayScore = Event.AwayScore;
}

void UPSScoreboardWidget::HandleOnPhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    PlayPhaseText = FText::FromString(Event.NewPhase);
    FormatGameClock(Event.GameClockSeconds);
    FormatPlayClock(Event.PlayClockSeconds);
}

void UPSScoreboardWidget::FormatGameClock(float GameClockSeconds)
{
    int32 TotalSeconds = FMath::Max(0, FMath::RoundToInt(GameClockSeconds));
    int32 Minutes = TotalSeconds / 60;
    int32 Seconds = TotalSeconds % 60;
    GameClockText = FText::FromString(FString::Printf(TEXT("%02d:%02d"), Minutes, Seconds));
}

void UPSScoreboardWidget::FormatPlayClock(float PlayClockSeconds)
{
    int32 TotalSeconds = FMath::Max(0, FMath::RoundToInt(PlayClockSeconds));
    PlayClockText = FText::AsNumber(TotalSeconds);
}

void UPSPlayResultWidget::NativeConstruct()
{
    Super::NativeConstruct();

    UWorld* World = GetWorld();
    if (World)
    {
        UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
        if (Bus)
        {
            Bus->OnTackle.AddDynamic(this, &UPSPlayResultWidget::HandleOnTackle);
            Bus->OnScore.AddDynamic(this, &UPSPlayResultWidget::HandleOnScore);
            Bus->OnPhaseChange.AddDynamic(this, &UPSPlayResultWidget::HandleOnPhaseChange);
        }
    }
}

void UPSPlayResultWidget::HandleOnTackle(const FPSTelemetryTackleEvent& Event)
{
    BannerText = FText::Format(FText::FromString(TEXT("{0}{1} YARDS")), 
        Event.YardsGained >= 0 ? FText::FromString(TEXT("+")) : FText::FromString(TEXT("")), 
        Event.YardsGained);
    OnShowPlayResultBanner();
}

void UPSPlayResultWidget::HandleOnScore(const FPSTelemetryScoreEvent& Event)
{
    BannerText = FText::FromString(Event.ScoreType.ToUpper() + TEXT("!"));
    OnShowPlayResultBanner();
}

void UPSPlayResultWidget::HandleOnPhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("Scoring") && Event.OldPhase == TEXT("PassRush"))
    {
        BannerText = FText::FromString(TEXT("INCOMPLETE PASS"));
        OnShowPlayResultBanner();
    }
}
