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
    BuildRuntimeObjects();

    UE_LOG(LogTemp, Display, TEXT("UPSInputConfig: Loaded %d input action(s) across %d context(s) from %s."),
        Catalog.Actions.Num(), Catalog.Contexts.Num(), *JsonFilePath);
    return true;
}

void UPSInputConfig::BuildRuntimeObjects()
{
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
        const FName ObjectName = MakeUniqueObjectName(this, UInputAction::StaticClass(),
            *FString::Printf(TEXT("IA_%s"), *ActionDef.ActionId.ToString()));
        UInputAction* Action = NewObject<UInputAction>(this, ObjectName, RF_Transient);
        Action->ValueType = ActionDef.ValueType;
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
    TArray<FString> Errors;

    TSet<FName> ContextIds;
    for (int32 Index = 0; Index < Catalog.Contexts.Num(); ++Index)
    {
        const FName ContextId = Catalog.Contexts[Index].ContextId;
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
    for (int32 Index = 0; Index < Catalog.Actions.Num(); ++Index)
    {
        const FPSInputActionDef& ActionDef = Catalog.Actions[Index];
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
