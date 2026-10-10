#include "PSTelestratorWidget.h"
#include "PSInputConfig.h"
#include "PSLocalization.h"
#include "PSMenuComponent.h"
#include "PSMenuScreenWidget.h"
#include "PSPlayerController.h"
#include "PSTelestratorLayer.h"
#include "PSTelestratorSubsystem.h"
#include "PSUITeamCatalog.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"

namespace PSTelestratorWidgetStyle
{
    // Placeholder look for the code-built toolbar; a Widget Blueprint replaces it.
    static const FLinearColor ToolbarBackdrop(0.f, 0.f, 0.f, 0.6f);
    static const FLinearColor ButtonColor(1.f, 1.f, 1.f, 1.f);
    static const FMargin ToolbarPadding(0.f, 12.f, 0.f, 0.f);
    static const FMargin ButtonPadding(4.f, 0.f);
    static const int32 ButtonFontSize = 18;

    /** The pointer ID the mouse draws with; fingers use their own index (0 and up). */
    static constexpr int32 MousePointer = -2;

    /** A tool's toolbar button: its ID, its text and the tool. */
    struct FPSToolButtonDef
    {
        const TCHAR* Id;
        const TCHAR* TextKey;
        EPSTelestratorTool Tool;
    };

    static const FPSToolButtonDef ToolButtons[] = {
        { TEXT("Tool.Freehand"), TEXT("Telestrator.Freehand"), EPSTelestratorTool::Freehand },
        { TEXT("Tool.Arrow"), TEXT("Telestrator.Arrow"), EPSTelestratorTool::Arrow },
        { TEXT("Tool.Circle"), TEXT("Telestrator.Circle"), EPSTelestratorTool::Circle },
        { TEXT("Tool.Player"), TEXT("Telestrator.Player"), EPSTelestratorTool::Player } };

    /** The tool a toolbar button picks; false for a button that runs an action instead. */
    bool ToolForButton(FName ButtonId, EPSTelestratorTool& OutTool)
    {
        for (const FPSToolButtonDef& Def : ToolButtons)
        {
            if (ButtonId == FName(Def.Id))
            {
                OutTool = Def.Tool;
                return true;
            }
        }
        return false;
    }
}

UPSTelestratorSubsystem* UPSTelestratorWidget::GetTelestrator() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSTelestratorSubsystem>() : nullptr;
}

TSharedRef<SWidget> UPSTelestratorWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    SetIsFocusable(true);
    // Idle, it lets every click and finger through to the game.
    SetVisibility(bLayerActive ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
    return Super::RebuildWidget();
}

void UPSTelestratorWidget::BuildDefaultLayout()
{
    using namespace PSTelestratorWidgetStyle;

    UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("LayerRoot"));
    WidgetTree->RootWidget = Root;

    Toolbar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Toolbar"));
    Toolbar->SetBrushColor(ToolbarBackdrop);
    Toolbar->SetVisibility(ESlateVisibility::Collapsed);
    if (UOverlaySlot* ToolbarSlot = Root->AddChildToOverlay(Toolbar))
    {
        ToolbarSlot->SetHorizontalAlignment(HAlign_Center);
        ToolbarSlot->SetVerticalAlignment(VAlign_Top);
        ToolbarSlot->SetPadding(ToolbarPadding);
    }
    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    Toolbar->SetContent(Row);

    // The tools, then undo, clear, save and done: the layer's own actions (by their catalog IDs),
    // so a tap, a click and the catalog's buttons all do the same.
    const UPSTelestratorSubsystem* Defaults = GetDefault<UPSTelestratorSubsystem>();
    TArray<TPair<FName, FString>> Buttons;
    for (const FPSToolButtonDef& Def : ToolButtons)
    {
        Buttons.Emplace(FName(Def.Id), FString(Def.TextKey));
    }
    Buttons.Emplace(Defaults->UndoActionId, FString(TEXT("Telestrator.Undo")));
    Buttons.Emplace(Defaults->ClearActionId, FString(TEXT("Telestrator.Clear")));
    Buttons.Emplace(Defaults->SaveActionId, FString(TEXT("Telestrator.Save")));
    Buttons.Emplace(Defaults->ExitActionId, FString(TEXT("Telestrator.Done")));

    ToolbarButtons.Reset();
    for (const TPair<FName, FString>& Entry : Buttons)
    {
        UPSMenuButton* Button = WidgetTree->ConstructWidget<UPSMenuButton>(UPSMenuButton::StaticClass());
        Button->OptionId = Entry.Key;
        Button->OnOptionChosen.BindUObject(this, &UPSTelestratorWidget::HandleToolbar);
        UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Label->SetText(UPSLocalization::GetText(Entry.Value));
        FSlateFontInfo Font = Label->GetFont();
        Font.Size = ButtonFontSize;
        Label->SetFont(Font);
        Button->SetContent(Label);
        if (UHorizontalBoxSlot* ButtonSlot = Row->AddChildToHorizontalBox(Button))
        {
            ButtonSlot->SetPadding(ButtonPadding);
        }
        ToolbarButtons.Add(Button);
    }
    RefreshToolbar();
}

void UPSTelestratorWidget::HandleToolbar(FName ButtonId)
{
    UPSTelestratorSubsystem* Telestrator = GetTelestrator();
    if (!Telestrator)
    {
        return;
    }
    EPSTelestratorTool Picked = EPSTelestratorTool::Freehand;
    if (PSTelestratorWidgetStyle::ToolForButton(ButtonId, Picked))
    {
        Telestrator->SetTool(Picked);
    }
    else
    {
        Telestrator->RunLayerAction(ButtonId);
    }
    RefreshToolbar();
}

void UPSTelestratorWidget::RefreshToolbar()
{
    UPSTelestratorSubsystem* Telestrator = GetTelestrator();
    ShownTool = Telestrator ? Telestrator->GetTool() : EPSTelestratorTool::Freehand;
    FLinearColor Selected = PSTelestratorWidgetStyle::ButtonColor;
    if (Telestrator)
    {
        UPSUITeamCatalog::ParseHexColor(Telestrator->GetTuning().MarkColor, Selected);
    }
    for (UPSMenuButton* Button : ToolbarButtons)
    {
        EPSTelestratorTool ButtonTool = EPSTelestratorTool::Freehand;
        if (Button)
        {
            // The tool in hand is lit in the marks' color.
            const bool bInHand = PSTelestratorWidgetStyle::ToolForButton(Button->OptionId, ButtonTool) && ButtonTool == ShownTool;
            Button->SetBackgroundColor(bInHand ? Selected : PSTelestratorWidgetStyle::ButtonColor);
        }
    }
}

bool UPSTelestratorWidget::IsMenuOpen() const
{
    const APSPlayerController* Player = Cast<APSPlayerController>(GetOwningPlayer());
    const UPSMenuComponent* Menus = Player ? Player->GetMenuComponent() : nullptr;
    return Menus && Menus->IsMenuOpen();
}

void UPSTelestratorWidget::ApplyInputMode(bool bActive)
{
    APlayerController* Player = GetOwningPlayer();
    if (!Player || !Player->GetLocalPlayer())
    {
        return;
    }
    if (bActive)
    {
        // Every key, button and finger is the layer's while it is up.
        FInputModeUIOnly Mode;
        Mode.SetWidgetToFocus(TakeWidget());
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Player->SetInputMode(Mode);
        Player->bShowMouseCursor = true;
    }
    else if (!IsMenuOpen())
    {
        Player->SetInputMode(FInputModeGameOnly());
        Player->bShowMouseCursor = false;
    }
}

void UPSTelestratorWidget::SetLayerActive(bool bActive)
{
    bLayerActive = bActive;
    StrokePointer = INDEX_NONE;
    bCursorDrawing = false;
    bShowCursor = false;
    HeldKeys.Reset();
    LeftStick = FVector2D::ZeroVector;
    RightStick = FVector2D::ZeroVector;
    DrawCursor = LastSize * 0.5;
    SetVisibility(bActive ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
    if (Toolbar)
    {
        Toolbar->SetVisibility(bActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }
    RefreshToolbar();
    ApplyInputMode(bActive);
    OnAnalysisChanged(bActive);
}

void UPSTelestratorWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    LastSize = MyGeometry.GetLocalSize();

    UPSTelestratorSubsystem* Telestrator = GetTelestrator();
    const bool bActive = Telestrator && Telestrator->IsAnalysisActive();
    if (bActive != bLayerActive)
    {
        SetLayerActive(bActive);
    }
    if (!bActive)
    {
        return;
    }
    // A menu over the layer (the pause menu) took the input; it comes back when the menu goes.
    if (!IsMenuOpen() && !HasAnyUserFocus() && !HasFocusedDescendants())
    {
        ApplyInputMode(true);
    }
    if (Telestrator->GetTool() != ShownTool)
    {
        RefreshToolbar();
    }

    // The cursor follows the Telestrator context's cursor bindings: the keys held and the sticks.
    APSPlayerController* Player = Cast<APSPlayerController>(GetOwningPlayer());
    const UPSInputConfig* Config = Player ? Player->GetInputConfig() : nullptr;
    const FPSInputActionDef* CursorAction = Config
        ? Config->Catalog.Actions.FindByPredicate([Telestrator](const FPSInputActionDef& Def) { return Def.ActionId == Telestrator->CursorActionId; })
        : nullptr;
    if (CursorAction)
    {
        const FPSTelestratorTuning& Tuning = Telestrator->GetTuning();
        const FVector2D Direction = PSTelestratorLayer::CursorDirection(CursorAction->Bindings, HeldKeys, LeftStick, RightStick, Tuning.CursorDeadZone);
        if (!Direction.IsNearlyZero())
        {
            DrawCursor = PSTelestratorLayer::MoveCursor(DrawCursor, Direction, InDeltaTime, LastSize, Tuning);
            bShowCursor = true;
            if (bCursorDrawing)
            {
                Telestrator->ExtendStroke(PSTelestratorLayer::ToFrame(DrawCursor, LastSize));
            }
        }
    }
}

int32 UPSTelestratorWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const int32 TopLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
    UPSTelestratorSubsystem* Telestrator = GetTelestrator();
    if (!bLayerActive || !Telestrator)
    {
        return TopLayer;
    }
    const FPSTelestratorTuning& Tuning = Telestrator->GetTuning();
    const FVector2D Size = AllottedGeometry.GetLocalSize();
    TArray<FPSWidgetStroke> Strokes = PSTelestratorLayer::BuildStrokes(Telestrator->GetTelestrationRef(), Telestrator->GetLiveStroke(), Telestrator->GetTool(), Size, Tuning);
    if (bShowCursor)
    {
        Strokes.Append(PSTelestratorLayer::CursorStrokes(DrawCursor, Size, Tuning));
    }
    return PSWidgetDrawing::Paint(Strokes, AllottedGeometry, OutDrawElements, TopLayer, InWidgetStyle.GetColorAndOpacityTint(), Tuning.MinStrokeWidth);
}

// --- Strokes from the mouse and fingers ---------------------------------------------------

FReply UPSTelestratorWidget::PointerDown(int32 PointerId, const FVector2D& Local)
{
    UPSTelestratorSubsystem* Telestrator = GetTelestrator();
    if (!bLayerActive || !Telestrator || StrokePointer != INDEX_NONE || bCursorDrawing)
    {
        return FReply::Unhandled();
    }
    StrokePointer = PointerId;
    bShowCursor = false;
    Telestrator->BeginStroke(PSTelestratorLayer::ToFrame(Local, LastSize));
    return FReply::Handled().CaptureMouse(TakeWidget());
}

void UPSTelestratorWidget::PointerMove(int32 PointerId, const FVector2D& Local)
{
    UPSTelestratorSubsystem* Telestrator = GetTelestrator();
    if (Telestrator && PointerId == StrokePointer)
    {
        Telestrator->ExtendStroke(PSTelestratorLayer::ToFrame(Local, LastSize));
    }
}

FReply UPSTelestratorWidget::PointerUp(int32 PointerId, const FVector2D& Local)
{
    UPSTelestratorSubsystem* Telestrator = GetTelestrator();
    if (!Telestrator || PointerId != StrokePointer)
    {
        return FReply::Unhandled();
    }
    StrokePointer = INDEX_NONE;
    Telestrator->EndStroke(PSTelestratorLayer::ToFrame(Local, LastSize));
    return FReply::Handled().ReleaseMouseCapture();
}

FReply UPSTelestratorWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        LastSize = InGeometry.GetLocalSize();
        const FReply Reply = PointerDown(PSTelestratorWidgetStyle::MousePointer, InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
        if (Reply.IsEventHandled())
        {
            return Reply;
        }
    }
    return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UPSTelestratorWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (StrokePointer == PSTelestratorWidgetStyle::MousePointer)
    {
        PointerMove(PSTelestratorWidgetStyle::MousePointer, InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
        return FReply::Handled();
    }
    return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UPSTelestratorWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        const FReply Reply = PointerUp(PSTelestratorWidgetStyle::MousePointer, InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
        if (Reply.IsEventHandled())
        {
            return Reply;
        }
    }
    return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UPSTelestratorWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
    LastSize = InGeometry.GetLocalSize();
    const FReply Reply = PointerDown(static_cast<int32>(InGestureEvent.GetPointerIndex()), InGeometry.AbsoluteToLocal(InGestureEvent.GetScreenSpacePosition()));
    return Reply.IsEventHandled() ? Reply : Super::NativeOnTouchStarted(InGeometry, InGestureEvent);
}

FReply UPSTelestratorWidget::NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
    const int32 Finger = static_cast<int32>(InGestureEvent.GetPointerIndex());
    if (Finger == StrokePointer)
    {
        PointerMove(Finger, InGeometry.AbsoluteToLocal(InGestureEvent.GetScreenSpacePosition()));
        return FReply::Handled();
    }
    return Super::NativeOnTouchMoved(InGeometry, InGestureEvent);
}

FReply UPSTelestratorWidget::NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
    const FReply Reply = PointerUp(static_cast<int32>(InGestureEvent.GetPointerIndex()), InGeometry.AbsoluteToLocal(InGestureEvent.GetScreenSpacePosition()));
    return Reply.IsEventHandled() ? Reply : Super::NativeOnTouchEnded(InGeometry, InGestureEvent);
}

// --- Keys, buttons and the sticks ---------------------------------------------------------

FName UPSTelestratorWidget::ActionForKey(const FKey& Key) const
{
    APSPlayerController* Player = Cast<APSPlayerController>(GetOwningPlayer());
    const UPSInputConfig* Config = Player ? Player->GetInputConfig() : nullptr;
    const UPSTelestratorSubsystem* Telestrator = GetTelestrator();
    return (Config && Telestrator) ? Config->FindActionForKey(Key, Telestrator->LayerContextId) : NAME_None;
}

FReply UPSTelestratorWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    UPSTelestratorSubsystem* Telestrator = GetTelestrator();
    if (!bLayerActive || !Telestrator)
    {
        return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
    }
    const FKey Key = InKeyEvent.GetKey();
    const FName ActionId = ActionForKey(Key);
    if (ActionId == Telestrator->CursorActionId)
    {
        HeldKeys.Add(Key.GetFName());
        return FReply::Handled();
    }
    if (ActionId == Telestrator->DrawActionId)
    {
        if (!InKeyEvent.IsRepeat() && !bCursorDrawing && StrokePointer == INDEX_NONE)
        {
            bCursorDrawing = true;
            bShowCursor = true;
            Telestrator->BeginStroke(PSTelestratorLayer::ToFrame(DrawCursor, LastSize));
        }
        return FReply::Handled();
    }
    if (!ActionId.IsNone())
    {
        if (!InKeyEvent.IsRepeat())
        {
            Telestrator->RunLayerAction(ActionId);
            RefreshToolbar();
        }
        return FReply::Handled();
    }
    // A pad button that means nothing here does nothing, rather than moving Slate's focus.
    return Key.IsGamepadKey() ? FReply::Handled() : Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UPSTelestratorWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    UPSTelestratorSubsystem* Telestrator = GetTelestrator();
    const FKey Key = InKeyEvent.GetKey();
    HeldKeys.Remove(Key.GetFName());
    if (Telestrator && bCursorDrawing && ActionForKey(Key) == Telestrator->DrawActionId)
    {
        bCursorDrawing = false;
        Telestrator->EndStroke(PSTelestratorLayer::ToFrame(DrawCursor, LastSize));
        return FReply::Handled();
    }
    return bLayerActive && Key.IsGamepadKey() ? FReply::Handled() : Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

FReply UPSTelestratorWidget::NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent)
{
    if (!bLayerActive)
    {
        return Super::NativeOnAnalogValueChanged(InGeometry, InAnalogEvent);
    }
    const FKey Key = InAnalogEvent.GetKey();
    const float Value = InAnalogEvent.GetAnalogValue();
    if (Key == EKeys::Gamepad_LeftX)
    {
        LeftStick.X = Value;
    }
    else if (Key == EKeys::Gamepad_LeftY)
    {
        LeftStick.Y = Value;
    }
    else if (Key == EKeys::Gamepad_RightX)
    {
        RightStick.X = Value;
    }
    else if (Key == EKeys::Gamepad_RightY)
    {
        RightStick.Y = Value;
    }
    // The sticks steer the cursor, never Slate's focus.
    return FReply::Handled();
}
