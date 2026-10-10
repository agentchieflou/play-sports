// PSAIDebugOverlayWidget.h - Epic 85.2: the always-on AI debug layer, drawn the way Epic 28 draws badges
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSAIDecisionTypes.h"
#include "PSAIDebugOverlayWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UPSAIDecisionLog;
class UTextBlock;

/**
 * UPSAIDebugOverlayWidget is the on-field AI debug overlay's drawing layer (Epic 85.2), made by
 * APSHUD in development builds. While the overlay is on (ps.AI.DebugOverlay, or
 * UPSAIDecisionLog::SetOverlayEnabled), each frame it asks the decision log to lay out a card
 * for every player with a decision (UPSAIDecisionLog::LayoutOverlay) for the player's camera
 * (UPSOverlayBadgeComponent::GetCurrentView) with Epic 28's badge style. It then draws each card
 * as the badge widget draws a badge: a colored plate with its text at its place over the head,
 * sized for its distance. Unlike the badges, which hide most players during the play, it shows
 * every one; a card with no room left is drawn dimmed where it was. The line from each player to
 * his target is painted with Slate lines (PSWidgetDrawing). While it is up, the decision log's
 * debug text in the world stands down. It decides nothing itself.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSAIDebugOverlayWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** The cards drawn this frame. */
    UFUNCTION(BlueprintPure, Category = "AIDebug")
    TArray<FPSAIDebugCard> GetCards() const { return Cards; }

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
    void BuildDefaultLayout();
    void EnsurePlates(int32 Count);
    void HidePlates(int32 FromIndex);
    UPSAIDecisionLog* GetDecisionLog() const;

    UPROPERTY(Transient)
    UCanvasPanel* Canvas = nullptr;

    UPROPERTY(Transient)
    TArray<UBorder*> Plates;

    UPROPERTY(Transient)
    TArray<UTextBlock*> PlateLabels;

    /** The type size each plate's text was last given, so an unchanged one isn't re-laid out. */
    TArray<int32> PlateFontSizes;

    UPROPERTY(Transient)
    TArray<FPSAIDebugCard> Cards;

    /** Viewport pixels per Slate unit when the cards were laid out. */
    float ViewportScale = 1.f;

    /** The target lines' look, from the tuning as the cards were laid out. */
    FLinearColor OffenseLineColor = FLinearColor::Blue;
    FLinearColor DefenseLineColor = FLinearColor::Red;
    float LineWidth = 2.f;

    /** The decision log this layer registered with, to stand its world text down. */
    TWeakObjectPtr<UPSAIDecisionLog> RegisteredLog;
};
