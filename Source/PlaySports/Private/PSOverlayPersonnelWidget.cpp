#include "PSOverlayPersonnelWidget.h"
#include "PSOverlayPersonnelSubsystem.h"
#include "PSUITeamCatalog.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"

namespace PSOverlayPersonnelWidgetPrivate
{
    // Spacing for the code-built layout; colors and type sizes are the style's. The bottom
    // margin keeps the panels clear of the score bug.
    static const FMargin ScreenPadding(32.f, 24.f, 32.f, 120.f);
    static const FMargin PanelPadding(12.f, 8.f);
    static const float TeamBarWidth = 6.f;
    /** How far a flash moves the panel's fill toward the flash color, at its start. */
    static const float FlashStrength = 0.6f;

    FLinearColor ParseOr(const FString& Hex, const FLinearColor& Fallback)
    {
        FLinearColor Parsed = Fallback;
        UPSUITeamCatalog::ParseHexColor(Hex, Parsed);
        return Parsed;
    }

    void SetFontSize(UTextBlock& Block, int32 Size)
    {
        FSlateFontInfo Font = Block.GetFont();
        if (Font.Size != Size)
        {
            Font.Size = Size;
            Block.SetFont(Font);
        }
    }
}

UPSOverlayPersonnelSubsystem* UPSOverlayPersonnelWidget::GetPersonnel() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSOverlayPersonnelSubsystem>() : nullptr;
}

TSharedRef<SWidget> UPSOverlayPersonnelWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    return Super::RebuildWidget();
}

void UPSOverlayPersonnelWidget::BuildDefaultLayout()
{
    using namespace PSOverlayPersonnelWidgetPrivate;

    // A transparent full-screen frame holds the row: offense, a spacer, defense.
    UBorder* Screen = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PersonnelFrame"));
    Screen->SetBrushColor(FLinearColor::Transparent);
    Screen->SetPadding(ScreenPadding);
    Screen->SetHorizontalAlignment(HAlign_Fill);
    Screen->SetVerticalAlignment(VAlign_Bottom);
    Screen->SetVisibility(ESlateVisibility::HitTestInvisible);
    WidgetTree->RootWidget = Screen;
    Built.Add(Screen);

    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PersonnelRow"));
    Screen->SetContent(Row);
    Built.Add(Row);

    BuildPanel(OffenseWidgets, true);
    Row->AddChildToHorizontalBox(OffenseWidgets.Frame);
    USpacer* Gap = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
    if (UHorizontalBoxSlot* GapSlot = Row->AddChildToHorizontalBox(Gap))
    {
        GapSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }
    Built.Add(Gap);
    BuildPanel(DefenseWidgets, false);
    Row->AddChildToHorizontalBox(DefenseWidgets.Frame);
}

void UPSOverlayPersonnelWidget::BuildPanel(FPanelWidgets& Out, bool bOffense)
{
    using namespace PSOverlayPersonnelWidgetPrivate;

    Out.Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), bOffense ? TEXT("OffensePanel") : TEXT("DefensePanel"));
    Out.Frame->SetPadding(PanelPadding);
    Out.Frame->SetVisibility(ESlateVisibility::Collapsed);
    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    Out.Frame->SetContent(Column);

    // The header: team color bar, logo slot, team label.
    UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    Column->AddChildToVerticalBox(Header);
    Out.TeamBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
    Out.TeamBar->SetPadding(FMargin(TeamBarWidth * 0.5f, 0.f));
    if (UHorizontalBoxSlot* BarSlot = Header->AddChildToHorizontalBox(Out.TeamBar))
    {
        BarSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
        BarSlot->SetVerticalAlignment(VAlign_Fill);
    }
    Out.Logo = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
    Out.Logo->SetVisibility(ESlateVisibility::Collapsed);
    if (UHorizontalBoxSlot* LogoSlot = Header->AddChildToHorizontalBox(Out.Logo))
    {
        LogoSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
        LogoSlot->SetVerticalAlignment(VAlign_Center);
    }
    Out.Team = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    if (UHorizontalBoxSlot* TeamSlot = Header->AddChildToHorizontalBox(Out.Team))
    {
        TeamSlot->SetVerticalAlignment(VAlign_Center);
    }

    Out.Package = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Column->AddChildToVerticalBox(Out.Package);
    Out.CountsRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    Column->AddChildToVerticalBox(Out.CountsRow);

    Built.Add(Out.Frame);
    Built.Add(Column);
    Built.Add(Header);
    Built.Add(Out.TeamBar);
    Built.Add(Out.Logo);
    Built.Add(Out.Team);
    Built.Add(Out.Package);
    Built.Add(Out.CountsRow);
}

void UPSOverlayPersonnelWidget::SetTeamLogo(bool bOffense, UTexture2D* Logo)
{
    FPanelWidgets& Widgets = bOffense ? OffenseWidgets : DefenseWidgets;
    if (!Widgets.Logo)
    {
        return;
    }
    if (Logo)
    {
        Widgets.Logo->SetBrushFromTexture(Logo, true);
        Widgets.Logo->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    else
    {
        Widgets.Logo->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void UPSOverlayPersonnelWidget::ApplyPanel(FPanelWidgets& Widgets, const FPSPersonnelPanel& Panel)
{
    using namespace PSOverlayPersonnelWidgetPrivate;

    if (!Widgets.Frame)
    {
        return;
    }
    Widgets.Frame->SetVisibility(Panel.bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (!Panel.bVisible)
    {
        return;
    }

    const UPSOverlayPersonnelSubsystem* Personnel = GetPersonnel();
    const FPSPersonnelPanelStyle Style = Personnel ? Personnel->GetStyle() : FPSPersonnelPanelStyle();
    const FLinearColor Fill = ParseOr(Style.PanelColor, FLinearColor::Black);
    const FLinearColor Text = ParseOr(Style.TextColor, FLinearColor::White);
    const FLinearColor Flash = ParseOr(Style.FlashColor, FLinearColor::Yellow);

    Widgets.Frame->SetBrushColor(FMath::Lerp(Fill, Flash, Panel.FlashAlpha * FlashStrength));
    Widgets.TeamBar->SetBrushColor(Panel.TeamColor);
    Widgets.Team->SetText(FText::FromString(Panel.TeamLabel));
    Widgets.Team->SetColorAndOpacity(FSlateColor(Text));
    SetFontSize(*Widgets.Team, Style.FontSize);
    Widgets.Package->SetText(FText::FromString(Panel.PackageName));
    Widgets.Package->SetColorAndOpacity(FSlateColor(Text));
    SetFontSize(*Widgets.Package, Style.TitleFontSize);

    // One text per count, so a changed one can flash on its own.
    while (Widgets.Counts.Num() < Panel.Counts.Num())
    {
        UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Widgets.CountsRow->AddChildToHorizontalBox(Block);
        Widgets.Counts.Add(Block);
        Built.Add(Block);
    }
    for (int32 Index = 0; Index < Widgets.Counts.Num(); ++Index)
    {
        UTextBlock* Block = Widgets.Counts[Index];
        if (!Panel.Counts.IsValidIndex(Index))
        {
            Block->SetVisibility(ESlateVisibility::Collapsed);
            continue;
        }
        const FPSPersonnelRoleCount& Count = Panel.Counts[Index];
        const FString Separator = Index + 1 < Panel.Counts.Num() ? TEXT(" | ") : TEXT("");
        Block->SetText(FText::FromString(FString::Printf(TEXT("%s %d%s"), *Count.Label, Count.Count, *Separator)));
        Block->SetColorAndOpacity(FSlateColor(Count.bChanged && Panel.FlashAlpha > 0.f ? FMath::Lerp(Text, Flash, Panel.FlashAlpha) : Text));
        SetFontSize(*Block, Style.FontSize);
        Block->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
}

void UPSOverlayPersonnelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    const UPSOverlayPersonnelSubsystem* Personnel = GetPersonnel();
    if (!Personnel)
    {
        return;
    }
    const FPSPersonnelPanel Offense = Personnel->GetPanel(true);
    const FPSPersonnelPanel Defense = Personnel->GetPanel(false);
    ApplyPanel(OffenseWidgets, Offense);
    ApplyPanel(DefenseWidgets, Defense);

    const FString Signature = FString::Printf(TEXT("%d%s%s%s|%d%s%s%s"), Offense.bVisible ? 1 : 0, *Offense.TeamLabel, *Offense.PackageName, *Offense.CountsText,
        Defense.bVisible ? 1 : 0, *Defense.TeamLabel, *Defense.PackageName, *Defense.CountsText);
    if (Signature != ShownSignature)
    {
        ShownSignature = Signature;
        OnPanelsChanged(Offense, Defense);
    }
}
