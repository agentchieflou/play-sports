#include "PSAIDebugOverlayWidget.h"
#include "PSAIDecisionLog.h"
#include "PSLocalization.h"
#include "PSOverlayBadgeComponent.h"
#include "PSPlayerController.h"
#include "PSUITeamCatalog.h"
#include "PSWidgetDrawing.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"

UPSAIDecisionLog* UPSAIDebugOverlayWidget::GetDecisionLog() const
{
    return UPSAIDecisionLog::Get(GetWorld());
}

TSharedRef<SWidget> UPSAIDebugOverlayWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    // A debug view over the game: it never takes a click or a finger.
    SetVisibility(ESlateVisibility::HitTestInvisible);
    return Super::RebuildWidget();
}

void UPSAIDebugOverlayWidget::BuildDefaultLayout()
{
    // A full-screen canvas; the plates are made as cards need them, as the badge widget's are.
    Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("AIDebugCanvas"));
    Canvas->SetVisibility(ESlateVisibility::HitTestInvisible);
    WidgetTree->RootWidget = Canvas;
}

void UPSAIDebugOverlayWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (UPSAIDecisionLog* Log = GetDecisionLog())
    {
        Log->RegisterOverlayLayer();
        RegisteredLog = Log;
    }
}

void UPSAIDebugOverlayWidget::NativeDestruct()
{
    if (UPSAIDecisionLog* Log = RegisteredLog.Get())
    {
        Log->UnregisterOverlayLayer();
    }
    RegisteredLog.Reset();
    Super::NativeDestruct();
}

void UPSAIDebugOverlayWidget::EnsurePlates(int32 Count)
{
    while (Plates.Num() < Count && Canvas && WidgetTree)
    {
        UBorder* Plate = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
        Plate->SetHorizontalAlignment(HAlign_Left);
        Plate->SetVerticalAlignment(VAlign_Center);
        UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Label->SetJustification(ETextJustify::Left);
        Plate->SetContent(Label);
        if (UCanvasPanelSlot* PlateSlot = Canvas->AddChildToCanvas(Plate))
        {
            // Placed by its bottom centre, sized by the layout, as a badge is.
            PlateSlot->SetAutoSize(false);
            PlateSlot->SetAlignment(FVector2D(0.5, 1.0));
        }
        Plates.Add(Plate);
        PlateLabels.Add(Label);
        PlateFontSizes.Add(0);
    }
}

void UPSAIDebugOverlayWidget::HidePlates(int32 FromIndex)
{
    for (int32 Index = FromIndex; Index < Plates.Num(); ++Index)
    {
        Plates[Index]->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void UPSAIDebugOverlayWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    Cards.Reset();
    UPSAIDecisionLog* Log = GetDecisionLog();
    const APSPlayerController* Player = Cast<APSPlayerController>(GetOwningPlayer());
    UPSOverlayBadgeComponent* Badges = Player ? Player->GetOverlayBadgeComponent() : nullptr;
    FPSBadgeView View;
    if (!Log || !Log->IsOverlayOn() || !Badges || !Badges->GetCurrentView(View) || !Canvas)
    {
        HidePlates(0);
        return;
    }

    // Laid out in viewport pixels for the camera as it is now; the canvas is in Slate units.
    ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
    const float Pixels = ViewportScale > KINDA_SMALL_NUMBER ? ViewportScale : 1.f;
    Cards = Log->LayoutOverlay(View, Badges->GetStyle(), Pixels);

    const FPSAIDebugTuning& Tuning = Log->GetTuning();
    FLinearColor OffenseColor = FLinearColor::Blue;
    FLinearColor DefenseColor = FLinearColor::Red;
    FLinearColor TextColor = FLinearColor::White;
    UPSUITeamCatalog::ParseHexColor(Tuning.OverlayOffenseColor, OffenseColor);
    UPSUITeamCatalog::ParseHexColor(Tuning.OverlayDefenseColor, DefenseColor);
    UPSUITeamCatalog::ParseHexColor(Tuning.OverlayTextColor, TextColor);
    const float PlatePadding = Tuning.OverlayPadding;
    OffenseLineColor = OffenseColor;
    DefenseLineColor = DefenseColor;
    LineWidth = Tuning.OverlayTargetLineWidth;

    int32 Used = 0;
    for (const FPSAIDebugCard& Card : Cards)
    {
        if (!Card.bVisible)
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
        Plate->SetBrushColor(Card.bOffense ? OffenseColor : DefenseColor);
        Plate->SetRenderOpacity(Card.bCrowdedOut ? Tuning.OverlayCrowdedOpacity : Tuning.OverlayOpacity);
        Plate->SetPadding(FMargin(PlatePadding));
        Plate->SetVisibility(ESlateVisibility::HitTestInvisible);
        // Developer text: player IDs, assignments and reasons, as the AI wrote them.
        Label->SetText(UPSLocalization::Verbatim(Card.Text));
        Label->SetColorAndOpacity(FSlateColor(TextColor));
        if (PlateFontSizes[Used] != Card.FontSize)
        {
            FSlateFontInfo Font = Label->GetFont();
            Font.Size = Card.FontSize;
            Label->SetFont(Font);
            PlateFontSizes[Used] = Card.FontSize;
        }
        if (UCanvasPanelSlot* PlateSlot = Cast<UCanvasPanelSlot>(Plate->Slot))
        {
            PlateSlot->SetPosition(Card.ScreenPosition / Pixels);
            PlateSlot->SetSize(Card.Size / Pixels);
        }
        ++Used;
    }
    HidePlates(Used);
}

int32 UPSAIDebugOverlayWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const int32 TopLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
    if (Cards.Num() == 0)
    {
        return TopLayer;
    }
    // Each player's line to his target, in his side's color.
    const float Pixels = ViewportScale > KINDA_SMALL_NUMBER ? ViewportScale : 1.f;
    TArray<FPSWidgetStroke> Lines;
    for (const FPSAIDebugCard& Card : Cards)
    {
        if (Card.bHasTarget)
        {
            Lines.Add(PSWidgetDrawing::MakeStroke({ Card.PlayerScreen / Pixels, Card.TargetScreen / Pixels }, Card.bOffense ? OffenseLineColor : DefenseLineColor,
                LineWidth, TEXT("Target")));
        }
    }
    return PSWidgetDrawing::Paint(Lines, AllottedGeometry, OutDrawElements, TopLayer, InWidgetStyle.GetColorAndOpacityTint(), LineWidth);
}
