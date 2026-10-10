// PSMenuScreenWidget.h - Epic 101: one menu screen on the screen stack
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "PSMenuTypes.h"
#include "PSMenuScreenWidget.generated.h"

class UPSMenuComponent;
class UTextBlock;
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

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    /** Lets a Widget Blueprint refresh its designer layout from GetScreen(). */
    UFUNCTION(BlueprintImplementableEvent, Category = "Menu")
    void OnScreenSet();

private:
    void BuildDefaultLayout();

    UPROPERTY(Transient)
    FPSMenuScreenDef Screen;

    UPROPERTY(Transient)
    TArray<UPSMenuButton*> OptionButtons;

    /** The live play clock on the play-call screens (Epic 102.5); null elsewhere. */
    UPROPERTY(Transient)
    UTextBlock* ClockText = nullptr;

    TWeakObjectPtr<UPSMenuComponent> OwnerMenu;

    float FadeSeconds = 0.f;
    float FadeElapsed = 0.f;
};
