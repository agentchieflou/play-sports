#include "PSOverlayScoreBugWidget.h"
#include "PSPerfBudget.h"
#include "PSOverlayBroadcastSubsystem.h"
#include "PSUITeamCatalog.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"

namespace PSOverlayWidgetStyle
{
    // Spacing for the code-built layout; colors and type sizes are the theme's.
    static const FMargin ScreenPadding(32.f, 24.f);
    static const FMargin BoxPadding(14.f, 6.f);
    static const float ChyronSlideSeconds = 0.25f;
    static const float ChyronSlideDistance = 60.f;

    FLinearColor Color(const FString& Hex, const FLinearColor& Fallback)
    {
        FLinearColor Parsed = Fallback;
        UPSUITeamCatalog::ParseHexColor(Hex, Parsed);
        return Parsed;
    }

    UTextBlock* MakeText(UWidgetTree& Tree, int32 FontSize)
    {
        UTextBlock* Block = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        FSlateFontInfo Font = Block->GetFont();
        Font.Size = FontSize;
        Block->SetFont(Font);
        return Block;
    }

    UBorder* MakeBox(UWidgetTree& Tree, UHorizontalBox& Row)
    {
        UBorder* Box = Tree.ConstructWidget<UBorder>(UBorder::StaticClass());
        Box->SetPadding(BoxPadding);
        Box->SetVerticalAlignment(VAlign_Center);
        if (UHorizontalBoxSlot* BoxSlot = Row.AddChildToHorizontalBox(Box))
        {
            BoxSlot->SetVerticalAlignment(VAlign_Fill);
        }
        return Box;
    }

    /** "<filled pips><empty pips>" for timeouts: remaining first. */
    FString Pips(int32 Count)
    {
        FString Out;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Out.AppendChar(TEXT('●'));
        }
        return Out;
    }

    void Anchor(UBorder& Root, EPSScoreBugAnchor Where)
    {
        switch (Where)
        {
        case EPSScoreBugAnchor::TopLeft:
            Root.SetHorizontalAlignment(HAlign_Left);
            Root.SetVerticalAlignment(VAlign_Top);
            break;
        case EPSScoreBugAnchor::TopCenter:
            Root.SetHorizontalAlignment(HAlign_Center);
            Root.SetVerticalAlignment(VAlign_Top);
            break;
        default:
            Root.SetHorizontalAlignment(HAlign_Center);
            Root.SetVerticalAlignment(VAlign_Bottom);
            break;
        }
    }
}

UPSOverlayBroadcastSubsystem* UPSOverlayScoreBugWidget::GetBroadcast() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSOverlayBroadcastSubsystem>() : nullptr;
}

TSharedRef<SWidget> UPSOverlayScoreBugWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    return Super::RebuildWidget();
}

void UPSOverlayScoreBugWidget::BuildDefaultLayout()
{
    using namespace PSOverlayWidgetStyle;
    const UPSOverlayBroadcastSubsystem* Broadcast = GetBroadcast();
    const FPSBroadcastOverlayTheme Theme = Broadcast ? Broadcast->GetTheme() : FPSBroadcastOverlayTheme();

    // A transparent full-screen frame places the bar; the bar holds four boxes.
    Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ScoreBugFrame"));
    Frame->SetBrushColor(FLinearColor::Transparent);
    Frame->SetPadding(ScreenPadding);
    PSOverlayWidgetStyle::Anchor(*Frame, Theme.Anchor);
    WidgetTree->RootWidget = Frame;

    UBorder* Bar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ScoreBugBar"));
    Bar->SetBrushColor(Color(Theme.BarColor, FLinearColor::Black));
    Frame->SetContent(Bar);
    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ScoreBugRow"));
    Bar->SetContent(Row);

    auto TeamBox = [this, &Theme, Row](UBorder*& OutBox, UTextBlock*& OutText, UTextBlock*& OutPips)
    {
        OutBox = MakeBox(*WidgetTree, *Row);
        UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        OutBox->SetContent(Line);
        OutText = MakeText(*WidgetTree, Theme.ScoreFontSize);
        Line->AddChildToHorizontalBox(OutText);
        OutPips = MakeText(*WidgetTree, Theme.TextFontSize);
        if (UHorizontalBoxSlot* PipSlot = Line->AddChildToHorizontalBox(OutPips))
        {
            PipSlot->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
            PipSlot->SetVerticalAlignment(VAlign_Center);
        }
    };
    TeamBox(AwayBox, AwayText, AwayTimeoutText);
    TeamBox(HomeBox, HomeText, HomeTimeoutText);

    UBorder* ClockBox = MakeBox(*WidgetTree, *Row);
    ClockBox->SetBrushColor(FLinearColor::Transparent);
    UHorizontalBox* ClockLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    ClockBox->SetContent(ClockLine);
    QuarterText = MakeText(*WidgetTree, Theme.TextFontSize);
    ClockText = MakeText(*WidgetTree, Theme.ScoreFontSize);
    PlayClockText = MakeText(*WidgetTree, Theme.TextFontSize);
    for (UTextBlock* Block : { QuarterText, ClockText, PlayClockText })
    {
        if (UHorizontalBoxSlot* ClockSlot = ClockLine->AddChildToHorizontalBox(Block))
        {
            ClockSlot->SetPadding(FMargin(6.f, 0.f));
            ClockSlot->SetVerticalAlignment(VAlign_Center);
        }
    }

    SituationBox = MakeBox(*WidgetTree, *Row);
    SituationText = MakeText(*WidgetTree, Theme.TextFontSize);
    SituationBox->SetContent(SituationText);

    SetVisibility(ESlateVisibility::HitTestInvisible);
    bShownAny = false;
}

void UPSOverlayScoreBugWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    PS_PERF_SCOPE(Overlays);
    Super::NativeTick(MyGeometry, InDeltaTime);

    const UPSOverlayBroadcastSubsystem* Broadcast = GetBroadcast();
    if (!Broadcast)
    {
        return;
    }
    const FPSScoreBugState State = Broadcast->GetScoreBug();
    if (!State.bValid)
    {
        SetRenderOpacity(0.f);
        return;
    }
    SetRenderOpacity(1.f);

    const bool bChanged = !bShownAny || State.GameClockText != Shown.GameClockText || State.PlayClockText != Shown.PlayClockText
        || State.HomeScore != Shown.HomeScore || State.AwayScore != Shown.AwayScore || State.SituationText != Shown.SituationText
        || State.bHomeHasPossession != Shown.bHomeHasPossession || State.HomeTimeouts != Shown.HomeTimeouts
        || State.AwayTimeouts != Shown.AwayTimeouts || State.QuarterText != Shown.QuarterText || State.bRedZone != Shown.bRedZone
        || State.bTwoMinute != Shown.bTwoMinute || State.HomeLabel != Shown.HomeLabel || State.AwayLabel != Shown.AwayLabel;
    if (bChanged)
    {
        ApplyState(State, Broadcast->GetTheme());
        Shown = State;
        bShownAny = true;
        OnScoreBugChanged(State);
    }
}

void UPSOverlayScoreBugWidget::ApplyState(const FPSScoreBugState& State, const FPSBroadcastOverlayTheme& Theme)
{
    using namespace PSOverlayWidgetStyle;
    if (!HomeText || !AwayText)
    {
        // A Widget Blueprint with its own tree draws the state in OnScoreBugChanged.
        return;
    }
    const FLinearColor Text = Color(Theme.TextColor, FLinearColor::White);
    const FLinearColor Bar = Color(Theme.BarColor, FLinearColor::Black);
    const FString Possession(TEXT(" ◀"));

    AwayBox->SetBrushColor(State.AwayColor);
    HomeBox->SetBrushColor(State.HomeColor);
    AwayText->SetText(FText::FromString(FString::Printf(TEXT("%s  %d%s"), *State.AwayLabel, State.AwayScore, State.bHomeHasPossession ? TEXT("") : *Possession)));
    HomeText->SetText(FText::FromString(FString::Printf(TEXT("%s  %d%s"), *State.HomeLabel, State.HomeScore, State.bHomeHasPossession ? *Possession : TEXT(""))));
    AwayText->SetColorAndOpacity(FSlateColor(Text));
    HomeText->SetColorAndOpacity(FSlateColor(Text));

    // Timeout pips: the ones left in the timeout color, then the used ones dimmed. One text
    // block draws one color, so the used ones are shown as a count of dim pips after it.
    const int32 MaxTimeouts = FMath::Max(State.MaxTimeouts, 0);
    AwayTimeoutText->SetText(FText::FromString(Pips(FMath::Clamp(State.AwayTimeouts, 0, MaxTimeouts))));
    HomeTimeoutText->SetText(FText::FromString(Pips(FMath::Clamp(State.HomeTimeouts, 0, MaxTimeouts))));
    AwayTimeoutText->SetColorAndOpacity(FSlateColor(Color(Theme.TimeoutColor, FLinearColor::Yellow)));
    HomeTimeoutText->SetColorAndOpacity(FSlateColor(Color(Theme.TimeoutColor, FLinearColor::Yellow)));

    QuarterText->SetText(FText::FromString(State.QuarterText));
    QuarterText->SetColorAndOpacity(FSlateColor(Text));
    ClockText->SetText(FText::FromString(State.GameClockText));
    ClockText->SetColorAndOpacity(FSlateColor(State.bTwoMinute ? Color(Theme.TwoMinuteColor, FLinearColor::Yellow) : Text));
    PlayClockText->SetText(FText::FromString(State.PlayClockText.IsEmpty() ? FString() : FString::Printf(TEXT(":%s"), *State.PlayClockText)));
    PlayClockText->SetColorAndOpacity(FSlateColor(Text));

    SituationBox->SetBrushColor(State.bRedZone ? Color(Theme.RedZoneColor, FLinearColor::Red) : Bar);
    SituationText->SetText(FText::FromString(State.SituationText));
    SituationText->SetColorAndOpacity(FSlateColor(Text));
}

TSharedRef<SWidget> UPSOverlayChyronWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    return Super::RebuildWidget();
}

void UPSOverlayChyronWidget::BuildDefaultLayout()
{
    using namespace PSOverlayWidgetStyle;
    const UWorld* World = GetWorld();
    const UPSOverlayBroadcastSubsystem* Broadcast = World ? World->GetSubsystem<UPSOverlayBroadcastSubsystem>() : nullptr;
    const FPSBroadcastOverlayTheme Theme = Broadcast ? Broadcast->GetTheme() : FPSBroadcastOverlayTheme();

    // The lower third: bottom left, clear of a bottom-centered score bug.
    UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ChyronFrame"));
    Root->SetBrushColor(FLinearColor::Transparent);
    Root->SetPadding(FMargin(ScreenPadding.Left, 0.f, 0.f, ScreenPadding.Bottom + 3.f * Theme.ScoreFontSize));
    Root->SetHorizontalAlignment(HAlign_Left);
    Root->SetVerticalAlignment(VAlign_Bottom);
    WidgetTree->RootWidget = Root;

    Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ChyronPanel"));
    Panel->SetBrushColor(Color(Theme.ChyronColor, FLinearColor::Black));
    Panel->SetPadding(FMargin(18.f, 8.f));
    Root->SetContent(Panel);

    UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    Panel->SetContent(Lines);
    const FLinearColor Text = Color(Theme.TextColor, FLinearColor::White);
    HeadlineText = MakeText(*WidgetTree, Theme.ScoreFontSize);
    HeadlineText->SetColorAndOpacity(FSlateColor(Text));
    Lines->AddChildToVerticalBox(HeadlineText);
    DetailText = MakeText(*WidgetTree, Theme.TextFontSize);
    DetailText->SetColorAndOpacity(FSlateColor(Text));
    Lines->AddChildToVerticalBox(DetailText);

    SetVisibility(ESlateVisibility::HitTestInvisible);
    SetRenderOpacity(0.f);
}

void UPSOverlayChyronWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    PS_PERF_SCOPE(Overlays);
    Super::NativeTick(MyGeometry, InDeltaTime);

    const UWorld* World = GetWorld();
    const UPSOverlayBroadcastSubsystem* Broadcast = World ? World->GetSubsystem<UPSOverlayBroadcastSubsystem>() : nullptr;
    if (!Broadcast)
    {
        return;
    }

    FPSChyron Chyron;
    const bool bShowing = Broadcast->GetCurrentChyron(Chyron);
    const int32 Order = bShowing ? Chyron.Order : INDEX_NONE;
    if (Order != ShownOrder)
    {
        ShownOrder = Order;
        if (HeadlineText && DetailText)
        {
            HeadlineText->SetText(FText::FromString(Chyron.Headline));
            DetailText->SetText(FText::FromString(Chyron.Detail));
        }
        OnChyronChanged(Chyron, bShowing);
    }

    if (!bShowing)
    {
        SetRenderOpacity(0.f);
        return;
    }

    // Slides and fades in on a Full tier; simply appears otherwise.
    const float Shown = Broadcast->GetChyronShownSeconds();
    const bool bAnimate = Broadcast->GetOverlayDetail() == EPSOverlayDetail::Full;
    const float Alpha = bAnimate ? FMath::Clamp(Shown / PSOverlayWidgetStyle::ChyronSlideSeconds, 0.f, 1.f) : 1.f;
    SetRenderOpacity(Alpha);
    SetRenderTranslation(FVector2D(-PSOverlayWidgetStyle::ChyronSlideDistance * (1.f - Alpha), 0.f));
}
