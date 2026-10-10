#include "PSTouchInputComponent.h"
#include "PSDataIngestion.h"
#include "PSInputConfig.h"
#include "PSMenuComponent.h"
#include "PSPlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/ICursor.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "Widgets/SViewport.h"

namespace PSTouchInputPrivate
{
    /** The mapping the catalog gives ActionId's gamepad key in ContextId: the stick's 2D key
     *  for a 2D action, a digital button for a Boolean one. The touch value goes through it. */
    const FEnhancedActionKeyMapping* FindGamepadMapping(const UPSInputConfig& Config, FName ActionId, FName ContextId)
    {
        const UInputMappingContext* Context = Config.FindContext(ContextId);
        const UInputAction* Action = Config.FindAction(ActionId);
        if (!Context || !Action)
        {
            return nullptr;
        }

        const FEnhancedActionKeyMapping* Fallback = nullptr;
        for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
        {
            if (Mapping.Action != Action || !Mapping.Key.IsGamepadKey())
            {
                continue;
            }
            const bool bPreferred = Action->ValueType == EInputActionValueType::Axis2D ? Mapping.Key.IsAxis2D() : Mapping.Key.IsDigital();
            if (bPreferred)
            {
                return &Mapping;
            }
            if (!Fallback)
            {
                Fallback = &Mapping;
            }
        }
        return Fallback;
    }

    /** RawValue through Mapping's modifiers, in order, as Enhanced Input applies them. */
    FInputActionValue ApplyModifiers(const FEnhancedActionKeyMapping& Mapping, const FInputActionValue& RawValue)
    {
        FInputActionValue Value = RawValue;
        for (UInputModifier* Modifier : Mapping.Modifiers)
        {
            if (Modifier)
            {
                Value = Modifier->ModifyRaw(nullptr, Value, 0.f);
            }
        }
        return Value;
    }

    bool IsInZone(const FVector2D& SafePoint, const FPSTouchZone& Zone)
    {
        return SafePoint.X >= Zone.Min.X && SafePoint.X <= Zone.Max.X && SafePoint.Y >= Zone.Min.Y && SafePoint.Y <= Zone.Max.Y;
    }
}

/** Observe-only Slate pre-processor: hands touch events to the touch layer in viewport pixels
 *  and returns false from every handler, so widgets and the viewport still receive them. */
class FPSTouchPreProcessor : public IInputProcessor
{
public:
    explicit FPSTouchPreProcessor(UPSTouchInputComponent* InOwner)
        : Owner(InOwner)
    {
    }

    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
    {
    }

    virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
    {
        FVector2D Position;
        if (UPSTouchInputComponent* Component = ResolveTouch(MouseEvent, Position))
        {
            Component->TouchStarted(static_cast<int32>(MouseEvent.GetPointerIndex()), Position, FPlatformTime::Seconds());
        }
        return false;
    }

    virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
    {
        FVector2D Position;
        if (UPSTouchInputComponent* Component = ResolveTouch(MouseEvent, Position))
        {
            Component->TouchMoved(static_cast<int32>(MouseEvent.GetPointerIndex()), Position, FPlatformTime::Seconds());
        }
        return false;
    }

    virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
    {
        FVector2D Position;
        if (UPSTouchInputComponent* Component = ResolveTouch(MouseEvent, Position))
        {
            Component->TouchEnded(static_cast<int32>(MouseEvent.GetPointerIndex()), Position, FPlatformTime::Seconds());
        }
        return false;
    }

private:
    /** The owner, for a touch event over the game viewport; null for mouse events. */
    UPSTouchInputComponent* ResolveTouch(const FPointerEvent& MouseEvent, FVector2D& OutPosition) const
    {
        UPSTouchInputComponent* Component = Owner.Get();
        if (!Component || !MouseEvent.IsTouchEvent())
        {
            return nullptr;
        }
        const FVector2D ScreenPosition = MouseEvent.GetScreenSpacePosition();
        return Component->ScreenToViewport(ScreenPosition, OutPosition) ? Component : nullptr;
    }

    TWeakObjectPtr<UPSTouchInputComponent> Owner;
};

UPSTouchInputComponent::UPSTouchInputComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UPSTouchInputComponent::BeginPlay()
{
    Super::BeginPlay();

    const APSPlayerController* Controller = GetPlayerController();
    if (Controller && Controller->IsLocalController() && FSlateApplication::IsInitialized())
    {
        PreProcessor = MakeShared<FPSTouchPreProcessor>(this);
        FSlateApplication::Get().RegisterInputPreProcessor(PreProcessor);
    }
}

void UPSTouchInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (PreProcessor.IsValid() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().UnregisterInputPreProcessor(PreProcessor);
    }
    PreProcessor.Reset();
    Pointers.Reset();
    PendingSwipes.Reset();

    Super::EndPlay(EndPlayReason);
}

bool UPSTouchInputComponent::LoadLayoutFromJson(const FString& JsonFilePath)
{
    bLayoutLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSTouchLayout Loaded;
    if (!Ingestion->LoadTouchLayoutFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSTouchInputComponent: Could not load the touch layout from %s; touch has no controls."), *JsonFilePath);
        Layout = FPSTouchLayout();
        return false;
    }
    Layout = MoveTemp(Loaded);
    return true;
}

const FPSTouchLayout& UPSTouchInputComponent::GetLayout()
{
    if (!bLayoutLoaded)
    {
        LoadLayoutFromJson(PSTouchControls::GetDefaultLayoutPath());
    }
    return Layout;
}

void UPSTouchInputComponent::SetViewportSize(const FVector2D& InViewportSize)
{
    ViewportSize = InViewportSize;
}

bool UPSTouchInputComponent::ScreenToViewport(const FVector2D& ScreenPosition, FVector2D& OutPosition)
{
    UWorld* World = GetWorld();
    UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr;
    if (!ViewportClient)
    {
        return false;
    }
    TSharedPtr<SViewport> ViewportWidget = ViewportClient->GetGameViewportWidget();
    if (!ViewportWidget.IsValid())
    {
        return false;
    }

    const FGeometry& Geometry = ViewportWidget->GetCachedGeometry();
    const FVector2D LocalSize = Geometry.GetLocalSize();
    if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
    {
        return false;
    }
    ViewportSize = LocalSize;
    const FVector2D LocalPosition = Geometry.AbsoluteToLocal(ScreenPosition);
    OutPosition = LocalPosition;
    return true;
}

APSPlayerController* UPSTouchInputComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}

TArray<FName> UPSTouchInputComponent::GetActiveContexts() const
{
    const APSPlayerController* Controller = GetPlayerController();
    return Controller ? Controller->GetActiveInputContexts() : TArray<FName>();
}

bool UPSTouchInputComponent::IsStoodDown() const
{
    const APSPlayerController* Controller = GetPlayerController();
    const UPSMenuComponent* Menus = Controller ? Controller->GetMenuComponent() : nullptr;
    return Menus && Menus->IsMenuOpen();
}

void UPSTouchInputComponent::GetSafeArea(FVector2D& OutOrigin, FVector2D& OutSize)
{
    const FPSTouchSafeZone& Safe = GetLayout().SafeZone;
    OutOrigin = FVector2D(Safe.Left * ViewportSize.X, Safe.Top * ViewportSize.Y);
    OutSize = FVector2D(ViewportSize.X * (1.0 - Safe.Left - Safe.Right), ViewportSize.Y * (1.0 - Safe.Top - Safe.Bottom));
}

bool UPSTouchInputComponent::GetControlPlacement(FName ControlId, FVector2D& OutCenter, float& OutRadius)
{
    const FPSTouchControlDef* Control = PSTouchControls::FindControl(GetLayout(), ControlId);
    if (!Control || Control->Kind == EPSTouchControlKind::Swipe || ViewportSize.X <= 0.0 || ViewportSize.Y <= 0.0)
    {
        return false;
    }

    FVector2D SafeOrigin;
    FVector2D SafeSize;
    GetSafeArea(SafeOrigin, SafeSize);
    OutCenter = SafeOrigin + Control->Position * SafeSize;
    OutRadius = static_cast<float>(Control->Radius * SafeSize.Y);
    return true;
}

TArray<FPSTouchBindingDef> UPSTouchInputComponent::GetActiveControls()
{
    TArray<FPSTouchBindingDef> Active;
    APSPlayerController* Controller = GetPlayerController();
    const UPSInputConfig* Config = Controller ? Controller->GetInputConfig() : nullptr;
    if (!Config || IsStoodDown())
    {
        return Active;
    }

    const TArray<FName> Contexts = GetActiveContexts();
    for (const FPSTouchControlDef& Control : GetLayout().TouchControls)
    {
        FPSTouchBindingDef Entry;
        FName WinningContext;
        if (PSTouchControls::ResolveControl(GetLayout(), Config->Catalog, Control.ControlId, Contexts, Entry.ActionId, WinningContext))
        {
            Entry.ControlId = Control.ControlId;
            Active.Add(Entry);
        }
    }
    return Active;
}

void UPSTouchInputComponent::TouchStarted(int32 FingerId, const FVector2D& Position, double TimeSeconds)
{
    APSPlayerController* Controller = GetPlayerController();
    const UPSInputConfig* Config = Controller ? Controller->GetInputConfig() : nullptr;
    if (!Config || IsStoodDown() || ViewportSize.X <= 0.0 || ViewportSize.Y <= 0.0)
    {
        return;
    }

    const FPSTouchLayout& CurrentLayout = GetLayout();
    const TArray<FName> Contexts = GetActiveContexts();
    FVector2D SafeOrigin;
    FVector2D SafeSize;
    GetSafeArea(SafeOrigin, SafeSize);
    const FVector2D SafePoint = (Position - SafeOrigin) / SafeSize;

    FPSTouchPointer Pointer;
    Pointer.Origin = Position;
    Pointer.Current = Position;
    Pointer.StartSeconds = TimeSeconds;

    // A button first: the nearest one under the finger that an active context binds.
    const FPSTouchControlDef* Pressed = nullptr;
    FName PressedAction;
    double PressedDistance = TNumericLimits<double>::Max();
    for (const FPSTouchControlDef& Control : CurrentLayout.TouchControls)
    {
        FName ActionId;
        FName ContextId;
        if (Control.Kind != EPSTouchControlKind::Button
            || !PSTouchControls::ResolveControl(CurrentLayout, Config->Catalog, Control.ControlId, Contexts, ActionId, ContextId))
        {
            continue;
        }
        const double Distance = FVector2D::Distance(Position, SafeOrigin + Control.Position * SafeSize);
        if (Distance <= Control.Radius * SafeSize.Y && Distance < PressedDistance)
        {
            Pressed = &Control;
            PressedAction = ActionId;
            PressedDistance = Distance;
        }
    }
    if (Pressed)
    {
        Pointer.Kind = EPSTouchControlKind::Button;
        Pointer.ControlId = Pressed->ControlId;
        Pointer.LatchedAction = PressedAction;
        Pointers.Add(FingerId, Pointer);
        return;
    }

    // Then the stick, when no other finger holds it.
    const FPSTouchControlDef* StickControl = CurrentLayout.TouchControls.FindByPredicate([](const FPSTouchControlDef& Def)
    {
        return Def.Kind == EPSTouchControlKind::Stick;
    });
    bool bStickHeld = false;
    for (const TPair<int32, FPSTouchPointer>& Held : Pointers)
    {
        if (Held.Key != FingerId && Held.Value.Kind == EPSTouchControlKind::Stick)
        {
            bStickHeld = true;
        }
    }
    FName StickAction;
    FName StickContext;
    if (StickControl && !bStickHeld
        && PSTouchControls::ResolveControl(CurrentLayout, Config->Catalog, StickControl->ControlId, Contexts, StickAction, StickContext))
    {
        const FVector2D StickCenter = SafeOrigin + StickControl->Position * SafeSize;
        const bool bOnStick = CurrentLayout.bFloatingStick
            ? PSTouchInputPrivate::IsInZone(SafePoint, CurrentLayout.StickZone)
            : FVector2D::Distance(Position, StickCenter) <= StickControl->Radius * SafeSize.Y;
        if (bOnStick)
        {
            Pointer.Kind = EPSTouchControlKind::Stick;
            Pointer.ControlId = StickControl->ControlId;
            Pointer.LatchedAction = StickAction;
            Pointer.Origin = CurrentLayout.bFloatingStick ? Position : StickCenter;
            Pointers.Add(FingerId, Pointer);
            return;
        }
    }

    // Otherwise a possible swipe.
    if (PSTouchInputPrivate::IsInZone(SafePoint, CurrentLayout.GestureZone))
    {
        Pointer.Kind = EPSTouchControlKind::Swipe;
        Pointers.Add(FingerId, Pointer);
    }
}

void UPSTouchInputComponent::TouchMoved(int32 FingerId, const FVector2D& Position, double TimeSeconds)
{
    if (FPSTouchPointer* Pointer = Pointers.Find(FingerId))
    {
        Pointer->Current = Position;
    }
}

void UPSTouchInputComponent::TouchEnded(int32 FingerId, const FVector2D& Position, double TimeSeconds)
{
    FPSTouchPointer Pointer;
    if (!Pointers.RemoveAndCopyValue(FingerId, Pointer) || Pointer.Kind != EPSTouchControlKind::Swipe || IsStoodDown())
    {
        return;
    }

    FVector2D SafeOrigin;
    FVector2D SafeSize;
    GetSafeArea(SafeOrigin, SafeSize);
    if (SafeSize.Y <= 0.0)
    {
        return;
    }

    const FPSTouchLayout& CurrentLayout = GetLayout();
    const FVector2D Delta = (Position - Pointer.Origin) / SafeSize.Y;
    const EPSSwipeDirection Direction = PSTouchControls::ClassifySwipe(Delta, TimeSeconds - Pointer.StartSeconds,
        CurrentLayout.SwipeMinDistance, CurrentLayout.SwipeMaxSeconds);
    const FPSTouchControlDef* SwipeControl = Direction != EPSSwipeDirection::None ? PSTouchControls::FindSwipe(CurrentLayout, Direction) : nullptr;

    FPSTouchActionSample Sample;
    if (SwipeControl && MakeSample(SwipeControl->ControlId, FInputActionValue(true), GetActiveContexts(), Sample))
    {
        PendingSwipes.Add(Sample);
    }
}

bool UPSTouchInputComponent::MakeSample(FName ControlId, const FInputActionValue& RawValue, const TArray<FName>& Contexts, FPSTouchActionSample& OutSample)
{
    APSPlayerController* Controller = GetPlayerController();
    const UPSInputConfig* Config = Controller ? Controller->GetInputConfig() : nullptr;
    FName ActionId;
    FName ContextId;
    if (!Config || !PSTouchControls::ResolveControl(GetLayout(), Config->Catalog, ControlId, Contexts, ActionId, ContextId))
    {
        return false;
    }

    const FEnhancedActionKeyMapping* Mapping = PSTouchInputPrivate::FindGamepadMapping(*Config, ActionId, ContextId);
    if (!Mapping)
    {
        return false;
    }

    OutSample.ControlId = ControlId;
    OutSample.ActionId = ActionId;
    OutSample.ContextId = ContextId;
    OutSample.GamepadKey = Mapping->Key;
    OutSample.RawValue = RawValue;
    OutSample.Value = PSTouchInputPrivate::ApplyModifiers(*Mapping, RawValue);
    return true;
}

TArray<FPSTouchActionSample> UPSTouchInputComponent::GatherActionSamples()
{
    TArray<FPSTouchActionSample> Samples;
    if (IsStoodDown())
    {
        PendingSwipes.Reset();
        return Samples;
    }

    const TArray<FName> Contexts = GetActiveContexts();
    FVector2D SafeOrigin;
    FVector2D SafeSize;
    GetSafeArea(SafeOrigin, SafeSize);

    TArray<FPSTouchActionSample> Candidates;
    for (TPair<int32, FPSTouchPointer>& Held : Pointers)
    {
        FPSTouchPointer& Pointer = Held.Value;
        if (Pointer.bSilenced || Pointer.Kind == EPSTouchControlKind::Swipe)
        {
            continue;
        }

        FInputActionValue RawValue(true);
        if (Pointer.Kind == EPSTouchControlKind::Stick)
        {
            const FPSTouchControlDef* StickControl = PSTouchControls::FindControl(GetLayout(), Pointer.ControlId);
            const FVector2D Deflection = SafeSize.Y > 0.0
                ? PSTouchControls::StickValue((Pointer.Current - Pointer.Origin) / SafeSize.Y, StickControl ? StickControl->Radius : 0.f)
                : FVector2D::ZeroVector;
            RawValue = FInputActionValue(Deflection);
        }

        // A held control whose meaning changed under it (a context came on or went off) stops
        // driving its old action and stays silent until the finger lifts: a held button never
        // turns into a different press, as a held key doesn't on the pad.
        FPSTouchActionSample Sample;
        if (!MakeSample(Pointer.ControlId, RawValue, Contexts, Sample) || Sample.ActionId != Pointer.LatchedAction)
        {
            Pointer.bSilenced = true;
            continue;
        }
        Candidates.Add(Sample);
    }
    Candidates.Append(PendingSwipes);
    PendingSwipes.Reset();

    // One value per action a frame, as one key per action would give.
    for (const FPSTouchActionSample& Candidate : Candidates)
    {
        const FName CandidateAction = Candidate.ActionId;
        if (!Samples.ContainsByPredicate([CandidateAction](const FPSTouchActionSample& Kept) { return Kept.ActionId == CandidateAction; }))
        {
            Samples.Add(Candidate);
        }
    }
    return Samples;
}

void UPSTouchInputComponent::DeliverSample(const FPSTouchActionSample& Sample)
{
    APSPlayerController* Controller = GetPlayerController();
    const UPSInputConfig* Config = Controller ? Controller->GetInputConfig() : nullptr;
    const FEnhancedActionKeyMapping* Mapping = Config ? PSTouchInputPrivate::FindGamepadMapping(*Config, Sample.ActionId, Sample.ContextId) : nullptr;
    if (!Mapping)
    {
        return;
    }

    // Raw value plus the gamepad mapping's own modifiers and triggers: Enhanced Input then
    // treats the press exactly as it treats that gamepad key.
    TArray<UInputModifier*> Modifiers;
    for (UInputModifier* Modifier : Mapping->Modifiers)
    {
        Modifiers.Add(Modifier);
    }
    TArray<UInputTrigger*> Triggers;
    for (UInputTrigger* Trigger : Mapping->Triggers)
    {
        Triggers.Add(Trigger);
    }
    Controller->InjectCatalogInput(Sample.ActionId, Sample.RawValue, Modifiers, Triggers);
}

void UPSTouchInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (Pointers.Num() == 0 && PendingSwipes.Num() == 0)
    {
        return;
    }
    for (const FPSTouchActionSample& Sample : GatherActionSamples())
    {
        DeliverSample(Sample);
    }
}
