#include "PSTouchControls.h"
#include "PSInputGlyphs.h"
#include "Misc/Paths.h"

namespace PSTouchControlsPrivate
{
    FString KindName(EPSTouchControlKind Kind)
    {
        return StaticEnum<EPSTouchControlKind>()->GetNameStringByValue(static_cast<int64>(Kind));
    }

    bool IsUnitInterval(double Value)
    {
        return Value >= 0.0 && Value <= 1.0;
    }

    void ValidateZone(const FPSTouchZone& Zone, const TCHAR* Name, TArray<FString>& Problems)
    {
        const bool bInside = IsUnitInterval(Zone.Min.X) && IsUnitInterval(Zone.Min.Y) && IsUnitInterval(Zone.Max.X) && IsUnitInterval(Zone.Max.Y);
        if (!bInside || Zone.Min.X >= Zone.Max.X || Zone.Min.Y >= Zone.Max.Y)
        {
            Problems.Add(FString::Printf(TEXT("%s must satisfy 0 <= Min < Max <= 1 on both axes."), Name));
        }
    }
}

FString PSTouchControls::GetDefaultLayoutPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/touch_controls.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSTouchControlDef* PSTouchControls::FindControl(const FPSTouchLayout& Layout, FName ControlId)
{
    return Layout.TouchControls.FindByPredicate([ControlId](const FPSTouchControlDef& Def) { return Def.ControlId == ControlId; });
}

const FPSTouchControlDef* PSTouchControls::FindSwipe(const FPSTouchLayout& Layout, EPSSwipeDirection Direction)
{
    return Layout.TouchControls.FindByPredicate([Direction](const FPSTouchControlDef& Def)
    {
        return Def.Kind == EPSTouchControlKind::Swipe && Def.Direction == Direction;
    });
}

bool PSTouchControls::ResolveControl(const FPSTouchLayout& Layout, const FPSInputCatalog& Catalog, FName ControlId,
    const TArray<FName>& ActiveContexts, FName& OutActionId, FName& OutContextId)
{
    bool bFound = false;
    int32 BestPriority = 0;
    for (const FName& ActiveContext : ActiveContexts)
    {
        const FPSTouchContextDef* TouchContext = Layout.TouchContexts.FindByPredicate([ActiveContext](const FPSTouchContextDef& Def)
        {
            return Def.ContextId == ActiveContext;
        });
        const FPSTouchBindingDef* Binding = TouchContext
            ? TouchContext->Bindings.FindByPredicate([ControlId](const FPSTouchBindingDef& Def) { return Def.ControlId == ControlId; })
            : nullptr;
        if (!Binding)
        {
            continue;
        }

        const FPSInputContextDef* CatalogContext = Catalog.Contexts.FindByPredicate([ActiveContext](const FPSInputContextDef& Def)
        {
            return Def.ContextId == ActiveContext;
        });
        const int32 Priority = CatalogContext ? CatalogContext->Priority : 0;
        if (!bFound || Priority >= BestPriority)
        {
            bFound = true;
            BestPriority = Priority;
            OutActionId = Binding->ActionId;
            OutContextId = ActiveContext;
        }
    }
    return bFound;
}

EPSSwipeDirection PSTouchControls::ClassifySwipe(const FVector2D& Delta, double Seconds, float MinDistance, float MaxSeconds)
{
    if (Seconds > MaxSeconds || Delta.Size() < MinDistance)
    {
        return EPSSwipeDirection::None;
    }
    if (FMath::Abs(Delta.X) >= FMath::Abs(Delta.Y))
    {
        return Delta.X > 0.0 ? EPSSwipeDirection::Right : EPSSwipeDirection::Left;
    }
    return Delta.Y > 0.0 ? EPSSwipeDirection::Down : EPSSwipeDirection::Up;
}

FVector2D PSTouchControls::StickValue(const FVector2D& Delta, float Radius)
{
    if (Radius <= 0.f)
    {
        return FVector2D::ZeroVector;
    }
    // Screen Y runs down; the Move action's Y is forward, up the screen.
    const FVector2D Value(Delta.X / Radius, -Delta.Y / Radius);
    return Value.SizeSquared() > 1.0 ? Value.GetSafeNormal() : Value;
}

TArray<FString> PSTouchControls::ValidateLayout(const FPSTouchLayout& Layout, const FPSInputCatalog* Catalog, const UPSInputGlyphs* Glyphs)
{
    using namespace PSTouchControlsPrivate;
    TArray<FString> Problems;

    const FPSTouchSafeZone& Safe = Layout.SafeZone;
    const bool bMarginsInRange = Safe.Left >= 0.f && Safe.Top >= 0.f && Safe.Right >= 0.f && Safe.Bottom >= 0.f
        && Safe.Left + Safe.Right < 1.f && Safe.Top + Safe.Bottom < 1.f;
    if (!bMarginsInRange)
    {
        Problems.Add(TEXT("SafeZone: margins must be 0 or more and leave part of the screen on each axis."));
    }
    if (Layout.LayoutAspect <= 0.f)
    {
        Problems.Add(TEXT("LayoutAspect must be positive."));
    }
    ValidateZone(Layout.StickZone, TEXT("StickZone"), Problems);
    ValidateZone(Layout.GestureZone, TEXT("GestureZone"), Problems);
    if (Layout.SwipeMinDistance <= 0.f || Layout.SwipeMaxSeconds <= 0.f)
    {
        Problems.Add(TEXT("SwipeMinDistance and SwipeMaxSeconds must be positive."));
    }

    // The controls: where they are and that they fit.
    const double Aspect = Layout.LayoutAspect > 0.f ? Layout.LayoutAspect : 1.0;
    TSet<FName> ControlIds;
    TSet<EPSSwipeDirection> SwipeDirections;
    int32 StickCount = 0;
    for (const FPSTouchControlDef& Control : Layout.TouchControls)
    {
        const FString Name = Control.ControlId.ToString();
        if (Control.ControlId.IsNone() || ControlIds.Contains(Control.ControlId))
        {
            Problems.Add(FString::Printf(TEXT("Control '%s': empty or duplicate ControlId."), *Name));
        }
        ControlIds.Add(Control.ControlId);

        if (Control.Kind == EPSTouchControlKind::Swipe)
        {
            if (Control.Direction == EPSSwipeDirection::None || SwipeDirections.Contains(Control.Direction))
            {
                Problems.Add(FString::Printf(TEXT("Control '%s': a swipe needs a Direction no other swipe uses."), *Name));
            }
            SwipeDirections.Add(Control.Direction);
            continue;
        }

        if (Control.Kind == EPSTouchControlKind::Stick)
        {
            ++StickCount;
        }
        if (Control.Radius <= 0.f || !IsUnitInterval(Control.Position.X) || !IsUnitInterval(Control.Position.Y))
        {
            Problems.Add(FString::Printf(TEXT("Control '%s': needs a Position inside the safe area and a positive Radius."), *Name));
            continue;
        }
        const double HalfWidth = Control.Radius / Aspect;
        const bool bFits = Control.Position.Y - Control.Radius >= 0.0 && Control.Position.Y + Control.Radius <= 1.0
            && Control.Position.X - HalfWidth >= 0.0 && Control.Position.X + HalfWidth <= 1.0;
        if (Control.Kind == EPSTouchControlKind::Button && !bFits)
        {
            Problems.Add(FString::Printf(TEXT("Control '%s': the button reaches outside the safe area."), *Name));
        }
    }
    if (StickCount > 1)
    {
        Problems.Add(FString::Printf(TEXT("The layout has %d sticks; the touch layer drives one."), StickCount));
    }

    // Buttons must not overlap, or a press could land on two of them.
    for (int32 First = 0; First < Layout.TouchControls.Num(); ++First)
    {
        const FPSTouchControlDef& A = Layout.TouchControls[First];
        for (int32 Second = First + 1; Second < Layout.TouchControls.Num(); ++Second)
        {
            const FPSTouchControlDef& B = Layout.TouchControls[Second];
            if (A.Kind != EPSTouchControlKind::Button || B.Kind != EPSTouchControlKind::Button)
            {
                continue;
            }
            const FVector2D Offset((A.Position.X - B.Position.X) * Aspect, A.Position.Y - B.Position.Y);
            if (Offset.Size() < A.Radius + B.Radius)
            {
                Problems.Add(FString::Printf(TEXT("Buttons '%s' and '%s' overlap."), *A.ControlId.ToString(), *B.ControlId.ToString()));
            }
        }
    }

    // The per-context button sets.
    TSet<FName> ContextIds;
    TSet<FName> BoundActions;
    for (const FPSTouchContextDef& TouchContext : Layout.TouchContexts)
    {
        const FString ContextName = TouchContext.ContextId.ToString();
        if (TouchContext.ContextId.IsNone() || ContextIds.Contains(TouchContext.ContextId))
        {
            Problems.Add(FString::Printf(TEXT("Touch context '%s': empty or listed twice."), *ContextName));
        }
        ContextIds.Add(TouchContext.ContextId);

        const FName ContextId = TouchContext.ContextId;
        const FPSInputContextDef* CatalogContext = Catalog
            ? Catalog->Contexts.FindByPredicate([ContextId](const FPSInputContextDef& Def) { return Def.ContextId == ContextId; })
            : nullptr;
        if (Catalog && !CatalogContext)
        {
            Problems.Add(FString::Printf(TEXT("Touch context '%s' is not a context in the input catalog."), *ContextName));
        }

        TSet<FName> BoundControls;
        for (const FPSTouchBindingDef& Binding : TouchContext.Bindings)
        {
            const FString Where = FString::Printf(TEXT("Touch context '%s', control '%s'"), *ContextName, *Binding.ControlId.ToString());
            const FPSTouchControlDef* Control = FindControl(Layout, Binding.ControlId);
            if (!Control)
            {
                Problems.Add(FString::Printf(TEXT("%s: no such control in TouchControls."), *Where));
            }
            if (BoundControls.Contains(Binding.ControlId))
            {
                Problems.Add(FString::Printf(TEXT("%s: bound twice in one context."), *Where));
            }
            BoundControls.Add(Binding.ControlId);
            if (Binding.ActionId.IsNone())
            {
                Problems.Add(FString::Printf(TEXT("%s: no ActionId."), *Where));
                continue;
            }
            BoundActions.Add(Binding.ActionId);

            if (!Catalog)
            {
                continue;
            }
            const FName ActionId = Binding.ActionId;
            const FPSInputActionDef* Action = Catalog->Actions.FindByPredicate([ActionId](const FPSInputActionDef& Def) { return Def.ActionId == ActionId; });
            if (!Action)
            {
                Problems.Add(FString::Printf(TEXT("%s: '%s' is not an action in the input catalog."), *Where, *ActionId.ToString()));
                continue;
            }
            if (CatalogContext && !Action->Contexts.Contains(ContextId))
            {
                Problems.Add(FString::Printf(TEXT("%s: '%s' does not live in context '%s'."), *Where, *ActionId.ToString(), *ContextName));
            }
            if (Control)
            {
                const EInputActionValueType Expected = Control->Kind == EPSTouchControlKind::Stick ? EInputActionValueType::Axis2D : EInputActionValueType::Boolean;
                if (Action->ValueType != Expected)
                {
                    Problems.Add(FString::Printf(TEXT("%s: a %s control needs a %s action, and '%s' is not one."), *Where, *KindName(Control->Kind),
                        Expected == EInputActionValueType::Axis2D ? TEXT("2D axis") : TEXT("Boolean"), *ActionId.ToString()));
                }
            }
            // Touch values go through the action's gamepad mapping, so it must have one.
            const bool bHasGamepadKey = Action->Bindings.ContainsByPredicate([](const FPSInputKeyBinding& Def) { return FKey(Def.Key).IsGamepadKey(); });
            if (!bHasGamepadKey)
            {
                Problems.Add(FString::Printf(TEXT("%s: '%s' has no gamepad binding for the touch value to go through."), *Where, *ActionId.ToString()));
            }
        }

        // Touch must reach every action of a context it covers.
        if (Catalog && CatalogContext)
        {
            for (const FPSInputActionDef& Action : Catalog->Actions)
            {
                const FName ActionId = Action.ActionId;
                const bool bReached = TouchContext.Bindings.ContainsByPredicate([ActionId](const FPSTouchBindingDef& Def) { return Def.ActionId == ActionId; });
                if (Action.Contexts.Contains(ContextId) && !bReached)
                {
                    Problems.Add(FString::Printf(TEXT("Touch context '%s': action '%s' has no touch control."), *ContextName, *ActionId.ToString()));
                }
            }
        }
    }

    // Every catalog context has a touch button set or is deliberately listed without one.
    for (const FName& Uncovered : Layout.ContextsWithoutTouch)
    {
        if (ContextIds.Contains(Uncovered))
        {
            Problems.Add(FString::Printf(TEXT("ContextsWithoutTouch: '%s' also has a touch button set."), *Uncovered.ToString()));
        }
        else if (Catalog && !Catalog->Contexts.ContainsByPredicate([Uncovered](const FPSInputContextDef& Def) { return Def.ContextId == Uncovered; }))
        {
            Problems.Add(FString::Printf(TEXT("ContextsWithoutTouch: '%s' is not a context in the input catalog."), *Uncovered.ToString()));
        }
    }
    if (Catalog)
    {
        for (const FPSInputContextDef& InputContext : Catalog->Contexts)
        {
            if (!ContextIds.Contains(InputContext.ContextId) && !Layout.ContextsWithoutTouch.Contains(InputContext.ContextId))
            {
                Problems.Add(FString::Printf(TEXT("Context '%s' has no touch button set: add it to TouchContexts, or deliberately to ContextsWithoutTouch."),
                    *InputContext.ContextId.ToString()));
            }
        }
    }

    if (Glyphs)
    {
        const FPSInputGlyphSetDef* TouchSet = Glyphs->GetDefaultSet(EPSInputDevice::Touch);
        if (!TouchSet)
        {
            Problems.Add(TEXT("The glyph table has no default Touch glyph set."));
        }
        else
        {
            for (const FName& ActionId : BoundActions)
            {
                if (!TouchSet->Actions.ContainsByPredicate([ActionId](const FPSActionGlyphDef& Def) { return Def.ActionId == ActionId; }))
                {
                    Problems.Add(FString::Printf(TEXT("Glyph set '%s' has no glyph for '%s', which a touch control drives."), *TouchSet->GlyphSetId.ToString(), *ActionId.ToString()));
                }
            }
        }
    }
    return Problems;
}
