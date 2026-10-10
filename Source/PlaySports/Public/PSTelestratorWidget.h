// PSTelestratorWidget.h - Epic 44: the telestrator's drawing layer: paints the marks, turns strokes into calls
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSTelestratorTypes.h"
#include "PSWidgetDrawing.h"
#include "PSTelestratorWidget.generated.h"

class UBorder;
class UPSMenuButton;
class UPSTelestratorSubsystem;

/**
 * UPSTelestratorWidget is the telestrator's drawing layer (Epic 44), made by APSHUD for its
 * player and idle until analysis mode is on (UPSTelestratorSubsystem, entered with the
 * Telestrator button).
 *
 *  - While analysis is on it covers the screen, takes the input (UI input mode, so nothing
 *    reaches the camera or the pawn under it; the touch layer stands down too) and shows a
 *    toolbar: the four tools, undo, clear, save and done.
 *  - Strokes: a mouse drag or a finger becomes BeginStroke, ExtendStroke and EndStroke in the
 *    frame's normalized screen space. With a gamepad or the keys, a cursor moves with the
 *    Telestrator context's TelestratorCursor bindings (the Move stick, WASD, the arrows) and
 *    TelestratorDraw (A, Space) draws at it while held. The context's other buttons go to
 *    UPSTelestratorSubsystem::RunLayerAction; which key means what is read from the input catalog
 *    (the Telestrator context), as menus read theirs.
 *  - Painting: every mark and the stroke in progress (PSTelestratorLayer::BuildStrokes) with
 *    Slate lines, sized from the screen's shorter side, plus the cursor once it has moved.
 *
 * With no designer tree it builds its toolbar in code, like the menus.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSTelestratorWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** The drawing cursor, in the widget's local space. */
    UFUNCTION(BlueprintPure, Category = "Telestrator")
    FVector2D GetDrawCursor() const { return DrawCursor; }

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
    virtual FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
    virtual FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent) override;

    /** Lets a Widget Blueprint show or hide its own chrome as analysis starts and ends. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Telestrator")
    void OnAnalysisChanged(bool bActive);

private:
    UPSTelestratorSubsystem* GetTelestrator() const;
    void BuildDefaultLayout();
    void SetLayerActive(bool bActive);
    void ApplyInputMode(bool bActive);
    bool IsMenuOpen() const;
    void HandleToolbar(FName ButtonId);
    void RefreshToolbar();

    /** A pointer (the mouse, or a finger by its index) starts, moves or ends a stroke at Local. */
    FReply PointerDown(int32 PointerId, const FVector2D& Local);
    void PointerMove(int32 PointerId, const FVector2D& Local);
    FReply PointerUp(int32 PointerId, const FVector2D& Local);

    /** The catalog action a key means in the layer's context; none when it means nothing. */
    FName ActionForKey(const FKey& Key) const;

    UPROPERTY(Transient)
    UBorder* Toolbar = nullptr;

    UPROPERTY(Transient)
    TArray<UPSMenuButton*> ToolbarButtons;

    /** Analysis was on last tick. */
    bool bLayerActive = false;

    /** The pointer drawing now: the mouse (MousePointer), a finger's index, or none. */
    int32 StrokePointer = INDEX_NONE;

    /** The cursor's stroke is being drawn (TelestratorDraw held). */
    bool bCursorDrawing = false;

    /** The cursor has moved since the mouse or a finger last drew, so it is shown. */
    bool bShowCursor = false;

    FVector2D DrawCursor = FVector2D::ZeroVector;
    FVector2D LeftStick = FVector2D::ZeroVector;
    FVector2D RightStick = FVector2D::ZeroVector;

    /** Keys held now, by name, for the cursor's bindings. */
    TSet<FName> HeldKeys;

    /** The widget's size when it last ticked, for the cursor and key presses. */
    FVector2D LastSize = FVector2D::ZeroVector;

    EPSTelestratorTool ShownTool = EPSTelestratorTool::Freehand;
};
