#include "PSUICaptionWidget.h"
#include "PSLocalization.h"
#include "PSUIAccessibilitySubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"

namespace PSCaptionStyle
{
    // Placeholder look for the code-built layout; a Widget Blueprint replaces all of it.
    static const FLinearColor BandColor(0.f, 0.f, 0.f, 0.7f);
    static const FMargin BandPadding(16.f, 8.f);
    static const FMargin ScreenMargin(0.f, 0.f, 0.f, 48.f);
}

TSharedRef<SWidget> UPSUICaptionWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    return Super::RebuildWidget();
}

void UPSUICaptionWidget::BuildDefaultLayout()
{
    // A transparent full-screen root holds the band at the bottom centre.
    UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CaptionRoot"));
    Root->SetBrushColor(FLinearColor::Transparent);
    Root->SetHorizontalAlignment(HAlign_Center);
    Root->SetVerticalAlignment(VAlign_Bottom);
    Root->SetPadding(PSCaptionStyle::ScreenMargin);
    Root->SetVisibility(ESlateVisibility::HitTestInvisible);
    WidgetTree->RootWidget = Root;

    Band = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CaptionBand"));
    Band->SetBrushColor(PSCaptionStyle::BandColor);
    Band->SetPadding(PSCaptionStyle::BandPadding);
    Band->SetVisibility(ESlateVisibility::Collapsed);
    Root->SetContent(Band);

    CaptionText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CaptionText"));
    CaptionText->SetJustification(ETextJustify::Center);
    CaptionText->SetAutoWrapText(true);
    Band->SetContent(CaptionText);
}

void UPSUICaptionWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    UWorld* World = GetWorld();
    UPSUIAccessibilitySubsystem* Accessibility = World ? World->GetSubsystem<UPSUIAccessibilitySubsystem>() : nullptr;
    if (!Accessibility || !Band || !CaptionText)
    {
        return;
    }

    TArray<FText> Lines;
    for (const FPSCaptionLine& Line : Accessibility->GetActiveCaptions(World->GetTimeSeconds()))
    {
        Lines.Add(UPSUIAccessibilitySubsystem::FormatCaptionText(Line));
    }
    const FText Shown = UPSLocalization::JoinLines(Lines);
    const FString Text = Shown.ToString();
    const int32 FontSize = Accessibility->GetCaptionFontSize();
    if (Text == ShownText && FontSize == ShownFontSize)
    {
        return;
    }
    ShownText = Text;
    ShownFontSize = FontSize;
    CaptionText->SetText(Shown);
    FSlateFontInfo Font = CaptionText->GetFont();
    Font.Size = FontSize;
    CaptionText->SetFont(Font);
    Band->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}
