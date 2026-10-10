// PSUICaptionWidget.h - Epic 103.3: captions at the bottom of the screen
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSUICaptionWidget.generated.h"

class UBorder;
class UTextBlock;

/**
 * UPSUICaptionWidget draws the captions UPSUIAccessibilitySubsystem holds: bottom centre, white
 * on a dark band, at the CaptionSize setting's size, gone when there are none. Like the menus
 * it builds a plain layout in code; a Widget Blueprint subclass can restyle it and read the
 * same subsystem.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSUICaptionWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void BuildDefaultLayout();

    UPROPERTY(Transient)
    UBorder* Band = nullptr;

    UPROPERTY(Transient)
    UTextBlock* CaptionText = nullptr;

    FString ShownText;
    int32 ShownFontSize = 0;
};
