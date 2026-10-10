// PSTouchHudWidget.cpp - Epic 146.4: draws the on-screen touch controls, built in code
#include "PSTouchHudWidget.h"
#include "PSInputConfig.h"
#include "PSInputDeviceComponent.h"
#include "PSLocalization.h"
#include "PSPlayerController.h"
#include "PSTouchInputComponent.h"
#include "PSUIAccessibilitySubsystem.h"
#include "PSWidgetDrawing.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

void UPSTouchHudWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    Style = PSTouchHud::LoadStyle(PSTouchHud::GetDefaultStylePath());
}

TSharedRef<SWidget> UPSTouchHudWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    // Fingers go straight through to the touch layer, which does the hit-testing.
    SetVisibility(ESlateVisibility::HitTestInvisible);
    return Super::RebuildWidget();
}

void UPSTouchHudWidget::BuildDefaultLayout()
{
    // A full-screen canvas for the labels; the rings are painted.
    Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TouchHudCanvas"));
    Canvas->SetVisibility(ESlateVisibility::HitTestInvisible);
    WidgetTree->RootWidget = Canvas;
}

UPSTouchInputComponent* UPSTouchHudWidget::GetTouchLayer() const
{
    const APSPlayerController* Player = Cast<APSPlayerController>(GetOwningPlayer());
    return Player ? Player->GetTouchInputComponent() : nullptr;
}

bool UPSTouchHudWidget::IsTouchActive() const
{
    const APSPlayerController* Player = Cast<APSPlayerController>(GetOwningPlayer());
    const UPSInputDeviceComponent* Devices = Player ? Player->GetInputDeviceComponent() : nullptr;
    return Devices && Devices->GetActiveDevice() == EPSInputDevice::Touch;
}

bool UPSTouchHudWidget::IsReducedMotion() const
{
    UWorld* World = GetWorld();
    UPSUIAccessibilitySubsystem* Accessibility = World ? World->GetSubsystem<UPSUIAccessibilitySubsystem>() : nullptr;
    return Accessibility && Accessibility->IsReducedMotion();
}

void UPSTouchHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    Drawing = FPSTouchHudDrawing();
    UPSTouchInputComponent* Touch = GetTouchLayer();
    if (Touch && IsTouchActive())
    {
        // The touch layer works in viewport pixels; this widget in DPI-scaled units.
        const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
        const float PixelsToLocal = ViewportScale > KINDA_SMALL_NUMBER ? 1.f / ViewportScale : 1.f;
        const FVector2D LocalSize = MyGeometry.GetLocalSize();
        Drawing = PSTouchHud::BuildDrawing(Touch->GetControlViews(), Touch->GetLastSwipe(), FPlatformTime::Seconds(), PixelsToLocal,
            static_cast<float>(LocalSize.Y), IsReducedMotion(), Style);
    }
    ShowLabels(Drawing.Labels);
}

void UPSTouchHudWidget::ShowLabels(const TArray<FPSTouchHudLabel>& Labels)
{
    if (!Canvas || !WidgetTree)
    {
        return;
    }
    while (LabelBlocks.Num() < Labels.Num())
    {
        UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Block->SetJustification(ETextJustify::Center);
        Block->SetVisibility(ESlateVisibility::HitTestInvisible);
        if (UCanvasPanelSlot* BlockSlot = Canvas->AddChildToCanvas(Block))
        {
            // Centred on the button.
            BlockSlot->SetAutoSize(true);
            BlockSlot->SetAlignment(FVector2D(0.5, 0.5));
        }
        LabelBlocks.Add(Block);
        LabelFontSizes.Add(0);
    }

    APSPlayerController* Player = Cast<APSPlayerController>(GetOwningPlayer());
    const UPSInputConfig* Config = Player ? Player->GetInputConfig() : nullptr;
    for (int32 Index = 0; Index < LabelBlocks.Num(); ++Index)
    {
        UTextBlock* Block = LabelBlocks[Index];
        FPSInputGlyph Glyph;
        if (!Labels.IsValidIndex(Index) || !Config
            || !Config->GetGlyphForAction(Labels[Index].ActionId, Labels[Index].ContextId, EPSInputDevice::Touch, Glyph))
        {
            Block->SetVisibility(ESlateVisibility::Collapsed);
            continue;
        }
        const FPSTouchHudLabel& Label = Labels[Index];
        // A control's glyph is a button's name, not text of ours to translate (Epic 106).
        Block->SetText(UPSLocalization::FromLocalized(UPSLocalization::Verbatim(Glyph.Label).ToString()));
        Block->SetColorAndOpacity(FSlateColor(Label.Color));
        Block->SetVisibility(ESlateVisibility::HitTestInvisible);
        const int32 FontSize = FMath::Max(1, FMath::RoundToInt(Label.FontSize));
        if (LabelFontSizes[Index] != FontSize)
        {
            FSlateFontInfo Font = Block->GetFont();
            Font.Size = FontSize;
            Block->SetFont(Font);
            LabelFontSizes[Index] = FontSize;
        }
        if (UCanvasPanelSlot* BlockSlot = Cast<UCanvasPanelSlot>(Block->Slot))
        {
            BlockSlot->SetPosition(Label.Center);
        }
    }
}

int32 UPSTouchHudWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const int32 TopLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
    if (Drawing.Strokes.Num() == 0)
    {
        return TopLayer;
    }
    return PSWidgetDrawing::Paint(Drawing.Strokes, AllottedGeometry, OutDrawElements, TopLayer, InWidgetStyle.GetColorAndOpacityTint(), 1.f);
}
