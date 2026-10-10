// PSHUDWidget.cpp - Epic 5: Scoreboard and Play Result Widget C++ base classes
#include "PSHUDWidget.h"
#include "PSLocalization.h"
#include "Engine/World.h"

namespace PSHUDDefaults
{
    // What the scoreboard shows before the first snap: a full quarter and play clock.
    static const float QuarterSeconds = 900.f;
    static const int32 PlayClockSeconds = 40;
    static const TCHAR* const FirstPhase = TEXT("PreSnap");
}

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

    GameClockText = MakeGameClockText(PSHUDDefaults::QuarterSeconds);
    PlayClockText = FText::AsNumber(PSHUDDefaults::PlayClockSeconds);
    PlayPhaseText = MakePhaseText(PSHUDDefaults::FirstPhase);
}

FText UPSScoreboardWidget::MakeGameClockText(float GameClockSeconds)
{
    const int32 TotalSeconds = FMath::Max(0, FMath::RoundToInt(GameClockSeconds));
    FNumberFormattingOptions TwoDigits = FNumberFormattingOptions::DefaultNoGrouping();
    TwoDigits.SetMinimumIntegralDigits(2);
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Minutes"), FText::AsNumber(TotalSeconds / 60, &FNumberFormattingOptions::DefaultNoGrouping()));
    Arguments.Add(TEXT("Seconds"), FText::AsNumber(TotalSeconds % 60, &TwoDigits));
    return UPSLocalization::Format(TEXT("HUD.GameClock"), Arguments);
}

FText UPSScoreboardWidget::MakePhaseText(const FString& PhaseName)
{
    const FString Key = FString::Printf(TEXT("HUD.Phase.%s"), *PhaseName);
    return UPSLocalization::HasText(Key) ? UPSLocalization::GetText(Key) : UPSLocalization::Verbatim(PhaseName);
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
    PlayPhaseText = MakePhaseText(Event.NewPhase);
    FormatGameClock(Event.GameClockSeconds);
    FormatPlayClock(Event.PlayClockSeconds);
}

void UPSScoreboardWidget::FormatGameClock(float GameClockSeconds)
{
    GameClockText = MakeGameClockText(GameClockSeconds);
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

FText UPSPlayResultWidget::MakeYardsBanner(int32 YardsGained)
{
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Yards"), FText::AsNumber(YardsGained));
    return YardsGained >= 0
        ? UPSLocalization::Format(TEXT("HUD.YardsGained"), Arguments)
        : UPSLocalization::Format(TEXT("HUD.YardsLost"), Arguments);
}

FText UPSPlayResultWidget::MakeScoreBanner(const FString& ScoreType)
{
    const FString Key = FString::Printf(TEXT("HUD.Score.%s"), *ScoreType);
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Score"), UPSLocalization::HasText(Key) ? UPSLocalization::GetText(Key) : UPSLocalization::Verbatim(ScoreType.ToUpper()));
    return UPSLocalization::Format(TEXT("HUD.Score"), Arguments);
}

FText UPSPlayResultWidget::MakeIncompletePassBanner()
{
    return UPSLocalization::GetText(TEXT("HUD.IncompletePass"));
}

void UPSPlayResultWidget::HandleOnTackle(const FPSTelemetryTackleEvent& Event)
{
    BannerText = MakeYardsBanner(Event.YardsGained);
    OnShowPlayResultBanner();
}

void UPSPlayResultWidget::HandleOnScore(const FPSTelemetryScoreEvent& Event)
{
    BannerText = MakeScoreBanner(Event.ScoreType);
    OnShowPlayResultBanner();
}

void UPSPlayResultWidget::HandleOnPhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("Scoring") && Event.OldPhase == TEXT("PassRush"))
    {
        BannerText = MakeIncompletePassBanner();
        OnShowPlayResultBanner();
    }
}
