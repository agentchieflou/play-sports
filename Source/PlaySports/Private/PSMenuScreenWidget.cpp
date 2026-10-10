#include "PSMenuScreenWidget.h"
#include "PSMenuComponent.h"
#include "PSLocalization.h"
#include "PSOverlayPlayArtSubsystem.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayDiagramWidget.h"
#include "PSTitleSafeArea.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"

namespace PSMenuStyle
{
    // Placeholder look for the code-built layout; a Widget Blueprint replaces all of it.
    static const FLinearColor Backdrop(0.f, 0.f, 0.f, 0.8f);
    static const int32 TitleFontSize = 40;
    static const int32 BodyFontSize = 18;
    static const int32 OptionFontSize = 24;
    static const int32 DetailFontSize = 14;
    static const FMargin OptionPadding(0.f, 6.f);
    static const FMargin TitlePadding(0.f, 0.f, 0.f, 24.f);
    static const FMargin PreviewPadding(0.f, 0.f, 12.f, 0.f);
}

TSharedRef<SWidget> UPSMenuButton::RebuildWidget()
{
    OnClicked.AddUniqueDynamic(this, &UPSMenuButton::HandleClicked);
    return Super::RebuildWidget();
}

void UPSMenuButton::HandleClicked()
{
    OnOptionChosen.ExecuteIfBound(OptionId);
}

void UPSMenuScreenWidget::SetScreen(const FPSMenuScreenDef& InScreen, UPSMenuComponent* InOwnerMenu, float InFadeSeconds)
{
    Screen = InScreen;
    OwnerMenu = InOwnerMenu;
    FadeSeconds = FMath::Max(0.f, InFadeSeconds);
    FadeElapsed = 0.f;
    SetIsFocusable(true);
    if (FadeSeconds > 0.f)
    {
        SetRenderOpacity(0.f);
    }
    OnScreenSet();
}

TSharedRef<SWidget> UPSMenuScreenWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    return Super::RebuildWidget();
}

void UPSMenuScreenWidget::BuildDefaultLayout()
{
    UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
    Background->SetBrushColor(PSMenuStyle::Backdrop);
    Background->SetHorizontalAlignment(HAlign_Center);
    Background->SetVerticalAlignment(VAlign_Center);
    Background->SetPadding(TitleSafeMargin);
    WidgetTree->RootWidget = Background;
    Backdrop = Background;

    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    Background->SetContent(Column);

    // The screen arrives localized (UPSMenuComponent::GetPresentedScreen, Epic 106).
    auto AddText = [this, Column](const FString& Text, int32 FontSize, const FMargin& TextPadding)
    {
        UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Block->SetText(UPSLocalization::FromLocalized(Text));
        FSlateFontInfo Font = Block->GetFont();
        Font.Size = FontSize;
        Block->SetFont(Font);
        Block->SetJustification(ETextJustify::Center);
        if (UVerticalBoxSlot* TextSlot = Column->AddChildToVerticalBox(Block))
        {
            TextSlot->SetPadding(TextPadding);
            TextSlot->SetHorizontalAlignment(HAlign_Center);
        }
        return Block;
    };

    AddText(Screen.Title, PSMenuStyle::TitleFontSize, PSMenuStyle::TitlePadding);
    if (!Screen.Body.IsEmpty())
    {
        AddText(Screen.Body, PSMenuStyle::BodyFontSize, PSMenuStyle::TitlePadding);
    }
    ClockText = UPSMenuComponent::IsPlayCallContent(Screen.Content)
        ? AddText(FString(), PSMenuStyle::BodyFontSize, PSMenuStyle::TitlePadding)
        : nullptr;

    // A redrawn screen says its focused option again (a stepped setting's new value).
    OptionButtons.Reset();
    NarratedOption = NAME_None;
    for (const FPSMenuOptionDef& Option : Screen.Options)
    {
        UPSMenuButton* Button = WidgetTree->ConstructWidget<UPSMenuButton>(UPSMenuButton::StaticClass());
        Button->OptionId = Option.OptionId;
        Button->OnOptionChosen.BindUObject(this, &UPSMenuScreenWidget::ChooseOption);
        if (Option.AccentColor.A > 0.f)
        {
            Button->SetBackgroundColor(Option.AccentColor);
        }

        UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Label->SetText(UPSLocalization::FromLocalized(Option.Label));
        FSlateFontInfo Font = Label->GetFont();
        Font.Size = PSMenuStyle::OptionFontSize;
        Label->SetFont(Font);
        UWidget* Text = Label;
        if (!Option.Detail.IsEmpty())
        {
            // A second, smaller line under the label (a play's assignments, Epic 102).
            UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
            Lines->AddChildToVerticalBox(Label);
            UTextBlock* Detail = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
            Detail->SetText(UPSLocalization::FromLocalized(Option.Detail));
            FSlateFontInfo DetailFont = Detail->GetFont();
            DetailFont.Size = PSMenuStyle::DetailFontSize;
            Detail->SetFont(DetailFont);
            Lines->AddChildToVerticalBox(Detail);
            Text = Lines;
        }
        if (UWidget* Preview = MakePlayPreview(Option))
        {
            // The play's diagram beside its name (Epic 102.1).
            UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
            if (UHorizontalBoxSlot* PreviewSlot = Row->AddChildToHorizontalBox(Preview))
            {
                PreviewSlot->SetPadding(PSMenuStyle::PreviewPadding);
                PreviewSlot->SetVerticalAlignment(VAlign_Center);
            }
            if (UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text))
            {
                TextSlot->SetVerticalAlignment(VAlign_Center);
            }
            Text = Row;
        }
        Button->SetContent(Text);

        if (UVerticalBoxSlot* ButtonSlot = Column->AddChildToVerticalBox(Button))
        {
            ButtonSlot->SetPadding(PSMenuStyle::OptionPadding);
            ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
        }
        OptionButtons.Add(Button);
    }
}

bool UPSMenuScreenWidget::ShowsPlayPreview(const FPSMenuOptionDef& Option)
{
    return Option.Command == EPSMenuCommand::CallPlay && !Option.Payload.IsNone();
}

UWidget* UPSMenuScreenWidget::MakePlayPreview(const FPSMenuOptionDef& Option)
{
    if (!ShowsPlayPreview(Option))
    {
        return nullptr;
    }
    UWorld* World = GetWorld();
    UPSOverlayPlayArtSubsystem* PlayArt = World ? World->GetSubsystem<UPSOverlayPlayArtSubsystem>() : nullptr;
    FPSPlayDiagram Diagram;
    if (!PlayArt || !PlayArt->BuildPlayDiagram(Option.Payload, Diagram))
    {
        return nullptr;
    }
    const FPSPlayDiagramStyle& Look = PlayArt->GetStyle().Diagram;
    UPSPlayDiagramWidget* Preview = WidgetTree->ConstructWidget<UPSPlayDiagramWidget>(UPSPlayDiagramWidget::StaticClass());
    Preview->SetDiagram(Diagram, Look);
    Preview->SetVisibility(ESlateVisibility::HitTestInvisible);
    // Sized in Slate units, which scale with the display: the same share of a phone's screen.
    USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
    Box->SetWidthOverride(Look.PreviewWidth);
    Box->SetHeightOverride(Look.PreviewHeight);
    Box->SetContent(Preview);
    return Box;
}

void UPSMenuScreenWidget::FocusFirstOption(APlayerController* Player)
{
    if (OptionButtons.Num() > 0 && OptionButtons[0])
    {
        OptionButtons[0]->SetUserFocus(Player);
    }
    else
    {
        SetUserFocus(Player);
    }
}

void UPSMenuScreenWidget::FocusOption(FName OptionId, APlayerController* Player)
{
    for (UPSMenuButton* Button : OptionButtons)
    {
        if (Button && Button->OptionId == OptionId)
        {
            Button->SetUserFocus(Player);
            return;
        }
    }
    FocusFirstOption(Player);
}

void UPSMenuScreenWidget::ChooseOption(FName OptionId)
{
    if (UPSMenuComponent* Menu = OwnerMenu.Get())
    {
        Menu->ChooseOption(OptionId);
    }
}

void UPSMenuScreenWidget::GoBack()
{
    if (UPSMenuComponent* Menu = OwnerMenu.Get())
    {
        Menu->HandleBack();
    }
}

FReply UPSMenuScreenWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    // A remap waiting for its key takes the very next one, before a button can press on it.
    UPSMenuComponent* Menu = OwnerMenu.Get();
    if (Menu && Menu->HandleRemapKey(InKeyEvent.GetKey()))
    {
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UPSMenuScreenWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    UPSMenuComponent* Menu = OwnerMenu.Get();
    if (Menu && Menu->IsBackKey(InKeyEvent.GetKey()))
    {
        Menu->HandleBack();
        return FReply::Handled();
    }
    if (Menu && Menu->IsFavoriteKey(InKeyEvent.GetKey()))
    {
        // Star the play under focus (Epic 102.3); the menu redraws this screen.
        for (UPSMenuButton* Button : OptionButtons)
        {
            if (Button && Button->HasAnyUserFocus())
            {
                const FName FocusedOption = Button->OptionId;
                Menu->ToggleFavoriteOption(FocusedOption);
                return FReply::Handled();
            }
        }
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UPSMenuScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    // The content keeps to the title-safe area at whatever size the screen has (Epic 150).
    const FMargin SafeMargin = PSTitleSafeArea::MakeMargin(MyGeometry.GetLocalSize(), PSTitleSafeArea::GetActiveFraction());
    if (SafeMargin != TitleSafeMargin)
    {
        TitleSafeMargin = SafeMargin;
        if (Backdrop)
        {
            Backdrop->SetPadding(TitleSafeMargin);
        }
    }

    if (FadeSeconds > 0.f && FadeElapsed < FadeSeconds)
    {
        FadeElapsed += InDeltaTime;
        SetRenderOpacity(FMath::Clamp(FadeElapsed / FadeSeconds, 0.f, 1.f));
    }

    // The UI narration hook (Epic 103.3): an option is said as it takes focus.
    if (UPSMenuComponent* Menu = OwnerMenu.Get())
    {
        for (const UPSMenuButton* Button : OptionButtons)
        {
            if (Button && Button->HasAnyUserFocus() && Button->OptionId != NarratedOption)
            {
                NarratedOption = Button->OptionId;
                Menu->NarrateOption(NarratedOption);
                break;
            }
        }
    }

    // The play clock keeps running while the player picks (Epic 102.5).
    const UWorld* World = GetWorld();
    const UPSPlayCallSubsystem* PlayCall = (ClockText && World) ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    if (PlayCall && PlayCall->GetPlayClockSeconds() >= 0.f)
    {
        FFormatNamedArguments Arguments;
        Arguments.Add(TEXT("Seconds"), FText::AsNumber(FMath::CeilToInt(PlayCall->GetPlayClockSeconds())));
        ClockText->SetText(UPSLocalization::Format(TEXT("Menu.PlayClock"), Arguments));
    }
}
