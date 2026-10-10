#include "PSOverlayBadgeWidget.h"
#include "PSLocalization.h"
#include "PSOverlayBadgeComponent.h"
#include "PSPlayerController.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"

UPSOverlayBadgeComponent* UPSOverlayBadgeWidget::GetBadgeComponent() const
{
    const APSPlayerController* Player = Cast<APSPlayerController>(GetOwningPlayer());
    return Player ? Player->GetOverlayBadgeComponent() : nullptr;
}

TSharedRef<SWidget> UPSOverlayBadgeWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    return Super::RebuildWidget();
}

void UPSOverlayBadgeWidget::BuildDefaultLayout()
{
    // A full-screen canvas; the plates are made as badges need them.
    Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("BadgeCanvas"));
    Canvas->SetVisibility(ESlateVisibility::HitTestInvisible);
    WidgetTree->RootWidget = Canvas;
}

void UPSOverlayBadgeWidget::EnsurePlates(int32 Count)
{
    while (Plates.Num() < Count && Canvas && WidgetTree)
    {
        UBorder* Plate = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
        Plate->SetHorizontalAlignment(HAlign_Center);
        Plate->SetVerticalAlignment(VAlign_Center);
        Plate->SetPadding(FMargin(2.f));
        UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Label->SetJustification(ETextJustify::Center);
        Plate->SetContent(Label);
        if (UCanvasPanelSlot* PlateSlot = Canvas->AddChildToCanvas(Plate))
        {
            // Placed by its bottom center, sized by the layout.
            PlateSlot->SetAutoSize(false);
            PlateSlot->SetAlignment(FVector2D(0.5, 1.0));
        }
        Plates.Add(Plate);
        PlateLabels.Add(Label);
        PlateFontSizes.Add(0);
    }
}

void UPSOverlayBadgeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    UPSOverlayBadgeComponent* Badges = GetBadgeComponent();
    if (!Badges || !Canvas)
    {
        return;
    }
    Badges->UpdateForCurrentView(InDeltaTime);

    // The layout is in viewport pixels; the canvas is in DPI-scaled units.
    const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
    const float ToCanvas = ViewportScale > KINDA_SMALL_NUMBER ? 1.f / ViewportScale : 1.f;
    const int32 BaseFontSize = Badges->GetStyle().FontSize;

    int32 Used = 0;
    FString Signature;
    for (const FPSPositionBadge& Badge : Badges->GetBadges())
    {
        if (!Badge.bVisible)
        {
            continue;
        }
        EnsurePlates(Used + 1);
        if (!Plates.IsValidIndex(Used))
        {
            break;
        }
        UBorder* Plate = Plates[Used];
        UTextBlock* Label = PlateLabels[Used];
        Plate->SetBrushColor(Badge.Color);
        Plate->SetRenderOpacity(Badge.Opacity);
        Plate->SetVisibility(ESlateVisibility::HitTestInvisible);
        Label->SetText(UPSLocalization::FromLocalized(Badge.Label));
        Label->SetColorAndOpacity(FSlateColor(Badge.TextColor));
        const int32 FontSize = FMath::Max(1, FMath::RoundToInt(BaseFontSize * Badge.Scale));
        if (PlateFontSizes[Used] != FontSize)
        {
            FSlateFontInfo Font = Label->GetFont();
            Font.Size = FontSize;
            Label->SetFont(Font);
            PlateFontSizes[Used] = FontSize;
        }
        if (UCanvasPanelSlot* PlateSlot = Cast<UCanvasPanelSlot>(Plate->Slot))
        {
            PlateSlot->SetPosition(Badge.ScreenPosition * ToCanvas);
            PlateSlot->SetSize(Badge.Size * ToCanvas);
        }
        Signature += Badge.PlayerId.ToString() + TEXT("=") + Badge.Label + TEXT(";");
        ++Used;
    }
    for (int32 Index = Used; Index < Plates.Num(); ++Index)
    {
        Plates[Index]->SetVisibility(ESlateVisibility::Collapsed);
    }

    if (Signature != ShownSignature)
    {
        ShownSignature = Signature;
        OnBadgesChanged(Badges->GetVisibleBadges());
    }
}
