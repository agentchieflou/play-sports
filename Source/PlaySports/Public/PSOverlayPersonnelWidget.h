// PSOverlayPersonnelWidget.h - Epic 29: the offense and defense personnel panels, drawn
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSOverlayPersonnelTypes.h"
#include "PSOverlayPersonnelWidget.generated.h"

class UBorder;
class UHorizontalBox;
class UImage;
class UTexture2D;
class UTextBlock;
class UPSOverlayPersonnelSubsystem;

/**
 * UPSOverlayPersonnelWidget draws UPSOverlayPersonnelSubsystem's two panels (Epic 29): the
 * offense on the left, the defense on the right, each with its team's color bar, a logo slot,
 * the package name and the counts ("RB 1 | TE 3 | WR 1"). A substitution flashes the panel and
 * the counts it changed. Colors and type sizes are the style's (Data/personnel_panel.json). With
 * no designer tree it builds its layout in code, like the score bug. APSHUD shows it, except on a
 * Minimal tier.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSOverlayPersonnelWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Puts a team logo in a panel's logo slot; null empties the slot. */
    UFUNCTION(BlueprintCallable, Category = "Overlay")
    void SetTeamLogo(bool bOffense, UTexture2D* Logo);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    /** Lets a Widget Blueprint draw the panels its own way; called whenever what either panel
     *  says changes. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Overlay")
    void OnPanelsChanged(const FPSPersonnelPanel& Offense, const FPSPersonnelPanel& Defense);

private:
    /** The widgets of one panel. */
    struct FPanelWidgets
    {
        UBorder* Frame = nullptr;
        UBorder* TeamBar = nullptr;
        UImage* Logo = nullptr;
        UTextBlock* Team = nullptr;
        UTextBlock* Package = nullptr;
        UHorizontalBox* CountsRow = nullptr;
        TArray<UTextBlock*> Counts;
    };

    void BuildDefaultLayout();
    void BuildPanel(FPanelWidgets& Out, bool bOffense);
    void ApplyPanel(FPanelWidgets& Widgets, const FPSPersonnelPanel& Panel);
    UPSOverlayPersonnelSubsystem* GetPersonnel() const;

    /** Keeps the code-built widgets alive; the panel structs point into it. */
    UPROPERTY(Transient)
    TArray<UWidget*> Built;

    FPanelWidgets OffenseWidgets;
    FPanelWidgets DefenseWidgets;
    FString ShownSignature;
};
