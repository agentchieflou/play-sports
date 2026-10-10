// PSMenuScreenWidget.h - Epic 101: one menu screen on the screen stack
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "PSMenuTypes.h"
#include "PSMenuScreenWidget.generated.h"

class UBorder;
class UPSMenuComponent;
class UTextBlock;
class UWidget;
class APlayerController;

DECLARE_DELEGATE_OneParam(FPSMenuOptionChosen, FName /* OptionId */);

/** A button that reports which menu option it stands for. */
UCLASS()
class PLAYSPORTS_API UPSMenuButton : public UButton
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, Category = "Menu")
    FName OptionId;

    FPSMenuOptionChosen OnOptionChosen;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    UFUNCTION()
    void HandleClicked();
};

/**
 * UPSMenuScreenWidget presents one FPSMenuScreenDef. A Widget Blueprint subclass can supply
 * its own designer layout and call ChooseOption/GoBack from it; with no designer tree the
 * widget builds a plain, fully working layout in code (title, body, one button per option),
 * so the front end runs before any UMG asset exists. Gamepad and keyboard navigation between
 * buttons is Slate's; Back comes from the input catalog's keys (UPSMenuComponent::IsBackKey).
 *
 * The backdrop covers the whole screen; the content keeps to the platform's title-safe area
 * (Epic 150, PSTitleSafeArea): the code-built layout pads the backdrop by GetTitleSafeMargin, and
 * a Widget Blueprint's layout does the same.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSMenuScreenWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Must be called before the widget is added to the viewport. */
    void SetScreen(const FPSMenuScreenDef& InScreen, UPSMenuComponent* InOwnerMenu, float InFadeSeconds);

    UFUNCTION(BlueprintPure, Category = "Menu")
    FPSMenuScreenDef GetScreen() const { return Screen; }

    UFUNCTION(BlueprintCallable, Category = "Menu")
    void ChooseOption(FName OptionId);

    UFUNCTION(BlueprintCallable, Category = "Menu")
    void GoBack();

    /** Gives the first option user focus so a gamepad can navigate straight away. */
    void FocusFirstOption(APlayerController* Player);

    /** Gives OptionId's button focus (the first option when it is not on the screen). */
    void FocusOption(FName OptionId, APlayerController* Player);

    /** The padding that keeps this screen's content inside the title-safe area, for its current
     *  size (Epic 150). */
    UFUNCTION(BlueprintPure, Category = "Menu")
    FMargin GetTitleSafeMargin() const { return TitleSafeMargin; }

    /** True for an option that calls a play (Epic 102): the code-built layout shows that play's
     *  diagram beside its name (UPSPlayDiagramWidget, Epic 102.1). */
    static bool ShowsPlayPreview(const FPSMenuOptionDef& Option);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    /** Lets a Widget Blueprint refresh its designer layout from GetScreen(). */
    UFUNCTION(BlueprintImplementableEvent, Category = "Menu")
    void OnScreenSet();

private:
    void BuildDefaultLayout();

    /** The play's preview for Option, sized by the play art's Diagram style; null for an option
     *  that calls no play, or a play with nobody on the field to resolve it for. */
    UWidget* MakePlayPreview(const FPSMenuOptionDef& Option);

    UPROPERTY(Transient)
    FPSMenuScreenDef Screen;

    UPROPERTY(Transient)
    TArray<UPSMenuButton*> OptionButtons;

    /** The code-built layout's full-screen backdrop, padded to the title-safe area. */
    UPROPERTY(Transient)
    UBorder* Backdrop = nullptr;

    /** The title-safe padding for the size the widget last had (Epic 150). */
    FMargin TitleSafeMargin;

    /** The live play clock on the play-call screens (Epic 102.5); null elsewhere. */
    UPROPERTY(Transient)
    UTextBlock* ClockText = nullptr;

    TWeakObjectPtr<UPSMenuComponent> OwnerMenu;

    /** The option last narrated as focused (Epic 103.3). */
    FName NarratedOption;

    float FadeSeconds = 0.f;
    float FadeElapsed = 0.f;
};
