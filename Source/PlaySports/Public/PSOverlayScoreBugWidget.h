// PSOverlayScoreBugWidget.h - Epic 33: the score bug and the lower-third chyron, drawn
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSOverlayBroadcastTypes.h"
#include "PSOverlayScoreBugWidget.generated.h"

class UBorder;
class UTextBlock;
class UPSOverlayBroadcastSubsystem;

/**
 * UPSOverlayScoreBugWidget draws UPSOverlayBroadcastSubsystem's score bug (Epic 33): both teams
 * with score, possession marker and timeout pips; the quarter, game clock and play clock; down
 * and distance, in the red-zone color inside the red zone, the clock in the two-minute color
 * in the last two minutes of a half. Colors, sizes and placement are the theme's
 * (Data/broadcast_overlay.json). With no designer tree it builds its layout in code, like the
 * menus (Epic 101), so it works before any UMG asset exists. APSHUD shows it; it replaces
 * Epic 5's debug scoreboard.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSOverlayScoreBugWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    /** Lets a Widget Blueprint draw the state its own way; called whenever it changes. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Broadcast")
    void OnScoreBugChanged(const FPSScoreBugState& State);

private:
    void BuildDefaultLayout();
    void ApplyState(const FPSScoreBugState& State, const FPSBroadcastOverlayTheme& Theme);
    UPSOverlayBroadcastSubsystem* GetBroadcast() const;

    UPROPERTY(Transient)
    UBorder* Frame;

    UPROPERTY(Transient)
    UBorder* AwayBox;

    UPROPERTY(Transient)
    UBorder* HomeBox;

    UPROPERTY(Transient)
    UBorder* SituationBox;

    UPROPERTY(Transient)
    UTextBlock* AwayText;

    UPROPERTY(Transient)
    UTextBlock* AwayTimeoutText;

    UPROPERTY(Transient)
    UTextBlock* HomeText;

    UPROPERTY(Transient)
    UTextBlock* HomeTimeoutText;

    UPROPERTY(Transient)
    UTextBlock* QuarterText;

    UPROPERTY(Transient)
    UTextBlock* ClockText;

    UPROPERTY(Transient)
    UTextBlock* PlayClockText;

    UPROPERTY(Transient)
    UTextBlock* SituationText;

    FPSScoreBugState Shown;
    bool bShownAny = false;
};

/**
 * UPSOverlayChyronWidget draws the chyron UPSOverlayBroadcastSubsystem has on screen: a lower
 * third with a headline and a detail line. On a tier whose OverlayDetail is Full it slides and
 * fades in; otherwise it simply appears. Built in code when it has no designer tree.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSOverlayChyronWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    /** Lets a Widget Blueprint draw a new chyron its own way (none when bShowing is false). */
    UFUNCTION(BlueprintImplementableEvent, Category = "Broadcast")
    void OnChyronChanged(const FPSChyron& Chyron, bool bShowing);

private:
    void BuildDefaultLayout();

    UPROPERTY(Transient)
    UBorder* Panel;

    UPROPERTY(Transient)
    UTextBlock* HeadlineText;

    UPROPERTY(Transient)
    UTextBlock* DetailText;

    int32 ShownOrder = INDEX_NONE;
};
