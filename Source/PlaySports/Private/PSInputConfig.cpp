#include "PSInputConfig.h"
#include "PSDataIngestion.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"

FString UPSInputConfig::GetDefaultCatalogPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/input_actions.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

FString UPSInputConfig::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/input_tuning.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSInputConfig::LoadDefaults()
{
    LoadTuningFromJson(GetDefaultTuningPath());
    const bool bCatalogLoaded = LoadFromJson(GetDefaultCatalogPath());

    if (!Glyphs)
    {
        Glyphs = NewObject<UPSInputGlyphs>(this, TEXT("RuntimeGlyphs"));
    }
    Glyphs->LoadDefaults();
    return bCatalogLoaded;
}

bool UPSInputConfig::GetGlyphForAction(FName ActionId, FName ContextId, EPSInputDevice Device, FPSInputGlyph& OutGlyph) const
{
    return Glyphs && Glyphs->GetGlyphForAction(Catalog, ActionId, ContextId, Device, OutGlyph);
}

bool UPSInputConfig::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FInputTuningRow LoadedTuning;
    if (!Ingestion->LoadInputTuningFromJson(JsonFilePath, LoadedTuning))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSInputConfig: Could not load input tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }

    Tuning = LoadedTuning;
    AuthoredTuning = LoadedTuning;
    return true;
}

void UPSInputConfig::SetStickDeadZoneScale(float Scale)
{
    // Never so large that no deflection is left to read.
    Tuning.StickDeadZoneLower = FMath::Clamp(AuthoredTuning.StickDeadZoneLower * FMath::Max(0.f, Scale), 0.f, Tuning.StickDeadZoneUpper - 0.05f);
    BuildRuntimeObjects();
}

bool UPSInputConfig::LoadFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSInputCatalog LoadedCatalog;
    if (!Ingestion->LoadInputCatalogFromJson(JsonFilePath, LoadedCatalog))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSInputConfig: Could not load input catalog from %s."), *JsonFilePath);
        return false;
    }

    Catalog = MoveTemp(LoadedCatalog);
    AuthoredCatalog = Catalog;
    Remaps.Reset();
    BuildRuntimeObjects();

    UE_LOG(LogTemp, Display, TEXT("UPSInputConfig: Loaded %d input action(s) across %d context(s) from %s."),
        Catalog.Actions.Num(), Catalog.Contexts.Num(), *JsonFilePath);
    return true;
}

void UPSInputConfig::BuildRuntimeObjects()
{
    // Keep the actions already handed out: input components bound them.
    TMap<FName, UInputAction*> PreviousActions = MoveTemp(RuntimeActions);
    RuntimeActions.Reset();
    RuntimeContexts.Reset();

    for (const FPSInputContextDef& ContextDef : Catalog.Contexts)
    {
        if (ContextDef.ContextId.IsNone() || RuntimeContexts.Contains(ContextDef.ContextId))
        {
            continue;
        }
        const FName ObjectName = MakeUniqueObjectName(this, UInputMappingContext::StaticClass(),
            *FString::Printf(TEXT("IMC_%s"), *ContextDef.ContextId.ToString()));
        RuntimeContexts.Add(ContextDef.ContextId, NewObject<UInputMappingContext>(this, ObjectName, RF_Transient));
    }

    for (const FPSInputActionDef& ActionDef : Catalog.Actions)
    {
        if (ActionDef.ActionId.IsNone() || RuntimeActions.Contains(ActionDef.ActionId))
        {
            continue;
        }
        UInputAction* Action = PreviousActions.FindRef(ActionDef.ActionId);
        if (!Action || Action->ValueType != ActionDef.ValueType)
        {
            const FName ObjectName = MakeUniqueObjectName(this, UInputAction::StaticClass(),
                *FString::Printf(TEXT("IA_%s"), *ActionDef.ActionId.ToString()));
            Action = NewObject<UInputAction>(this, ObjectName, RF_Transient);
            Action->ValueType = ActionDef.ValueType;
        }
        Action->bTriggerWhenPaused = ActionDef.bTriggerWhenPaused;
        RuntimeActions.Add(ActionDef.ActionId, Action);

        for (const FName& ContextId : ActionDef.Contexts)
        {
            UInputMappingContext* Context = RuntimeContexts.FindRef(ContextId);
            if (!Context)
            {
                continue;
            }

            for (const FPSInputKeyBinding& Binding : ActionDef.Bindings)
            {
                const FKey Key(Binding.Key);
                if (!Key.IsValid())
                {
                    continue;
                }

                FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
                if (Key.IsGamepadKey() && Key.IsAxis2D())
                {
                    UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(Context);
                    DeadZone->Type = EDeadZoneType::Radial;
                    DeadZone->LowerThreshold = Tuning.StickDeadZoneLower;
                    DeadZone->UpperThreshold = Tuning.StickDeadZoneUpper;
                    Mapping.Modifiers.Add(DeadZone);

                    UInputModifierResponseCurveExponential* Curve = NewObject<UInputModifierResponseCurveExponential>(Context);
                    Curve->CurveExponent = FVector(Tuning.StickResponseExponent);
                    Mapping.Modifiers.Add(Curve);
                }
                if (Binding.bSwizzleYX)
                {
                    UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Context);
                    Swizzle->Order = EInputAxisSwizzle::YXZ;
                    Mapping.Modifiers.Add(Swizzle);
                }
                if (Binding.bNegate)
                {
                    Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
                }
            }
        }
    }
}

TArray<FString> UPSInputConfig::Validate() const
{
    return ValidateCatalog(Catalog, Tuning);
}

TArray<FString> UPSInputConfig::ValidateCatalog(const FPSInputCatalog& InCatalog, const FInputTuningRow& InTuning)
{
    TArray<FString> Errors;

    TSet<FName> ContextIds;
    for (int32 Index = 0; Index < InCatalog.Contexts.Num(); ++Index)
    {
        const FName ContextId = InCatalog.Contexts[Index].ContextId;
        if (ContextId.IsNone())
        {
            Errors.Add(FString::Printf(TEXT("Contexts[%d]: empty ContextId"), Index));
        }
        else if (ContextIds.Contains(ContextId))
        {
            Errors.Add(FString::Printf(TEXT("Contexts[%d]: duplicate ContextId '%s'"), Index, *ContextId.ToString()));
        }
        ContextIds.Add(ContextId);
    }

    TSet<FName> ActionIds;
    // Context -> keys already bound in it, to catch one button meaning two things.
    TMap<FName, TMap<FName, FName>> KeysByContext;
    for (int32 Index = 0; Index < InCatalog.Actions.Num(); ++Index)
    {
        const FPSInputActionDef& ActionDef = InCatalog.Actions[Index];
        const FString ActionLabel = ActionDef.ActionId.IsNone()
            ? FString::Printf(TEXT("Actions[%d]"), Index)
            : FString::Printf(TEXT("Action '%s'"), *ActionDef.ActionId.ToString());

        if (ActionDef.ActionId.IsNone())
        {
            Errors.Add(FString::Printf(TEXT("%s: empty ActionId"), *ActionLabel));
        }
        else if (ActionIds.Contains(ActionDef.ActionId))
        {
            Errors.Add(FString::Printf(TEXT("%s: duplicate ActionId"), *ActionLabel));
        }
        ActionIds.Add(ActionDef.ActionId);

        if (ActionDef.Contexts.Num() == 0)
        {
            Errors.Add(FString::Printf(TEXT("%s: declares no context, so nothing can trigger it"), *ActionLabel));
        }

        bool bHasKeyboardMouse = false;
        bool bHasGamepad = false;
        for (const FPSInputKeyBinding& Binding : ActionDef.Bindings)
        {
            const FKey Key(Binding.Key);
            if (!Key.IsValid())
            {
                Errors.Add(FString::Printf(TEXT("%s: '%s' is not a valid engine key"), *ActionLabel, *Binding.Key.ToString()));
                continue;
            }
            if (Key.IsGamepadKey())
            {
                bHasGamepad = true;
            }
            else
            {
                bHasKeyboardMouse = true;
            }
        }

        for (const FName& ContextId : ActionDef.Contexts)
        {
            if (!ContextIds.Contains(ContextId))
            {
                Errors.Add(FString::Printf(TEXT("%s: unknown context '%s'"), *ActionLabel, *ContextId.ToString()));
                continue;
            }
            if (!bHasKeyboardMouse)
            {
                Errors.Add(FString::Printf(TEXT("%s: no keyboard/mouse binding in context '%s'"), *ActionLabel, *ContextId.ToString()));
            }
            if (!bHasGamepad)
            {
                Errors.Add(FString::Printf(TEXT("%s: no gamepad binding in context '%s'"), *ActionLabel, *ContextId.ToString()));
            }

            TMap<FName, FName>& BoundKeys = KeysByContext.FindOrAdd(ContextId);
            for (const FPSInputKeyBinding& Binding : ActionDef.Bindings)
            {
                if (const FName* OtherAction = BoundKeys.Find(Binding.Key))
                {
                    if (*OtherAction != ActionDef.ActionId)
                    {
                        Errors.Add(FString::Printf(TEXT("%s: key '%s' is already bound to '%s' in context '%s'"),
                            *ActionLabel, *Binding.Key.ToString(), *OtherAction->ToString(), *ContextId.ToString()));
                    }
                }
                else
                {
                    BoundKeys.Add(Binding.Key, ActionDef.ActionId);
                }
            }
        }
    }

    if (InTuning.StickDeadZoneLower < 0.f || InTuning.StickDeadZoneLower >= InTuning.StickDeadZoneUpper || InTuning.StickDeadZoneUpper > 1.f)
    {
        Errors.Add(FString::Printf(TEXT("Tuning: stick dead zone must satisfy 0 <= lower (%.2f) < upper (%.2f) <= 1"),
            InTuning.StickDeadZoneLower, InTuning.StickDeadZoneUpper));
    }
    if (InTuning.StickResponseExponent <= 0.f)
    {
        Errors.Add(FString::Printf(TEXT("Tuning: StickResponseExponent (%.2f) must be positive"), InTuning.StickResponseExponent));
    }
    if (InTuning.DeviceSwitchAnalogThreshold <= 0.f || InTuning.DeviceSwitchAnalogThreshold > 1.f)
    {
        Errors.Add(FString::Printf(TEXT("Tuning: DeviceSwitchAnalogThreshold (%.2f) must be in (0, 1]"), InTuning.DeviceSwitchAnalogThreshold));
    }

    return Errors;
}

UInputAction* UPSInputConfig::FindAction(FName ActionId) const
{
    return RuntimeActions.FindRef(ActionId);
}

UInputMappingContext* UPSInputConfig::FindContext(FName ContextId) const
{
    return RuntimeContexts.FindRef(ContextId);
}

int32 UPSInputConfig::GetContextPriority(FName ContextId) const
{
    for (const FPSInputContextDef& ContextDef : Catalog.Contexts)
    {
        if (ContextDef.ContextId == ContextId)
        {
            return ContextDef.Priority;
        }
    }
    return INDEX_NONE;
}

FName UPSInputConfig::FindActionId(const UInputAction* Action) const
{
    for (const TPair<FName, UInputAction*>& Pair : RuntimeActions)
    {
        if (Pair.Value == Action)
        {
            return Pair.Key;
        }
    }
    return NAME_None;
}

TArray<FKey> UPSInputConfig::GetKeysFor(FName ActionId, FName ContextId) const
{
    TArray<FKey> Keys;
    for (const FPSInputActionDef& ActionDef : Catalog.Actions)
    {
        if (ActionDef.ActionId != ActionId || !ActionDef.Contexts.Contains(ContextId))
        {
            continue;
        }
        for (const FPSInputKeyBinding& Binding : ActionDef.Bindings)
        {
            const FKey Key(Binding.Key);
            if (Key.IsValid())
            {
                Keys.Add(Key);
            }
        }
    }
    return Keys;
}

FName UPSInputConfig::FindActionForKey(const FKey& Key, FName ContextId) const
{
    for (const FPSInputActionDef& ActionDef : Catalog.Actions)
    {
        if (!ActionDef.Contexts.Contains(ContextId))
        {
            continue;
        }
        for (const FPSInputKeyBinding& Binding : ActionDef.Bindings)
        {
            if (FKey(Binding.Key) == Key)
            {
                return ActionDef.ActionId;
            }
        }
    }
    return NAME_None;
}

bool UPSInputConfig::IsRemappable(FName ActionId) const
{
    const FPSInputActionDef* Action = AuthoredCatalog.Actions.FindByPredicate(
        [ActionId](const FPSInputActionDef& Candidate) { return Candidate.ActionId == ActionId; });
    if (!Action || Action->ValueType != EInputActionValueType::Boolean)
    {
        return false;
    }
    for (const FName& ContextId : Action->Contexts)
    {
        const FPSInputContextDef* Context = AuthoredCatalog.Contexts.FindByPredicate(
            [ContextId](const FPSInputContextDef& Candidate) { return Candidate.ContextId == ContextId; });
        if (Context && !Context->bRemappable)
        {
            return false;
        }
    }
    return true;
}

bool UPSInputConfig::ApplyRemaps(const TArray<FPSInputRemap>& InRemaps, TArray<FString>& OutProblems)
{
    OutProblems.Reset();
    FPSInputCatalog Remapped = AuthoredCatalog;
    for (const FPSInputRemap& Remap : InRemaps)
    {
        const FKey Key(Remap.Key);
        const FString Device = Remap.bGamepad ? TEXT("gamepad") : TEXT("keyboard or mouse");
        FPSInputActionDef* Action = Remapped.Actions.FindByPredicate(
            [&Remap](const FPSInputActionDef& Def) { return Def.ActionId == Remap.ActionId; });
        if (!Action || !IsRemappable(Remap.ActionId))
        {
            OutProblems.Add(FString::Printf(TEXT("'%s' can't be given another key"), *Remap.ActionId.ToString()));
            continue;
        }
        if (!Key.IsValid() || Key.IsGamepadKey() != Remap.bGamepad || Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D())
        {
            OutProblems.Add(FString::Printf(TEXT("'%s' is not a %s button"), *Remap.Key.ToString(), *Device));
            continue;
        }
        FPSInputGlyph Glyph;
        if (Glyphs && !Glyphs->GetGlyphForKey(Key, Remap.bGamepad ? EPSInputDevice::Gamepad : EPSInputDevice::KeyboardMouse, Glyph))
        {
            OutProblems.Add(FString::Printf(TEXT("There is no button picture for '%s'"), *Remap.Key.ToString()));
            continue;
        }
        // The player's key replaces every key the action had on that kind of device.
        Action->Bindings.RemoveAll([&Remap](const FPSInputKeyBinding& Binding)
        {
            const FKey Bound(Binding.Key);
            return !Bound.IsValid() || Bound.IsGamepadKey() == Remap.bGamepad;
        });
        FPSInputKeyBinding Binding;
        Binding.Key = Remap.Key;
        Action->Bindings.Add(Binding);
    }
    if (OutProblems.Num() == 0)
    {
        OutProblems = ValidateCatalog(Remapped, Tuning);
    }
    if (OutProblems.Num() > 0)
    {
        return false;
    }

    Catalog = Remapped;
    Remaps = InRemaps;
    BuildRuntimeObjects();
    return true;
}
