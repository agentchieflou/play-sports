// PSOverlayBadgeWidget.h - Epic 28: draws the floating position badges over the players
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSOverlayBadgeTypes.h"
#include "PSOverlayBadgeWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UPSOverlayBadgeComponent;
class UTextBlock;

/**
 * UPSOverlayBadgeWidget draws the position badges (Epic 28) that its owning player's
 * UPSOverlayBadgeComponent lays out: each a colored plate with its label, at its place over the
 * player's head, sized and faded as laid out. Each frame it asks the component to lay out for
 * the camera as it is now, then moves its plates; it decides nothing itself. With no designer
 * tree it builds its layout in code, like the menus and the score bug. APSHUD shows it.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSOverlayBadgeWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    /** Lets a Widget Blueprint draw the badges its own way (glyph icons, say); called whenever
     *  the set of badges shown, or what they say, changes. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Overlay")
    void OnBadgesChanged(const TArray<FPSPositionBadge>& Shown);

private:
    void BuildDefaultLayout();
    void EnsurePlates(int32 Count);
    UPSOverlayBadgeComponent* GetBadgeComponent() const;

    UPROPERTY(Transient)
    UCanvasPanel* Canvas;

    UPROPERTY(Transient)
    TArray<UBorder*> Plates;

    UPROPERTY(Transient)
    TArray<UTextBlock*> PlateLabels;

    /** The type size each plate's label was last given, so an unchanged one isn't re-laid out. */
    TArray<int32> PlateFontSizes;

    /** What was shown last frame, to call OnBadgesChanged only on a change. */
    FString ShownSignature;
};
