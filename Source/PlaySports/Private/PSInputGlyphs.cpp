#include "PSInputGlyphs.h"
#include "PSDataIngestion.h"
#include "Misc/Paths.h"

namespace PSInputGlyphsPrivate
{
    FString DeviceName(EPSInputDevice Device)
    {
        return StaticEnum<EPSInputDevice>()->GetNameStringByValue(static_cast<int64>(Device));
    }
}

FString UPSInputGlyphs::GetDefaultGlyphsPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/input_glyphs.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSInputGlyphs::LoadFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSInputGlyphCatalog Loaded;
    if (!Ingestion->LoadInputGlyphsFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSInputGlyphs: Could not load the glyph table from %s."), *JsonFilePath);
        return false;
    }

    Table = MoveTemp(Loaded);
    return true;
}

EPSInputDevice UPSInputGlyphs::GetDeviceForKey(const FKey& Key)
{
    if (Key.IsGamepadKey())
    {
        return EPSInputDevice::Gamepad;
    }
    return Key.IsTouch() ? EPSInputDevice::Touch : EPSInputDevice::KeyboardMouse;
}

const FPSInputGlyphSetDef* UPSInputGlyphs::GetDefaultSet(EPSInputDevice Device) const
{
    return Table.GlyphSets.FindByPredicate([Device](const FPSInputGlyphSetDef& Set)
    {
        return Set.Device == Device && Set.bDefaultForDevice;
    });
}

bool UPSInputGlyphs::GetGlyphForKey(const FKey& Key, EPSInputDevice Device, FPSInputGlyph& OutGlyph) const
{
    const FPSInputGlyphSetDef* Set = GetDefaultSet(Device);
    if (!Set || !Key.IsValid() || GetDeviceForKey(Key) != Device)
    {
        return false;
    }

    const FName KeyName = Key.GetFName();
    if (const FPSKeyGlyphDef* Entry = Set->Keys.FindByPredicate([KeyName](const FPSKeyGlyphDef& Def) { return Def.Key == KeyName; }))
    {
        OutGlyph.GlyphSetId = Set->GlyphSetId;
        OutGlyph.GlyphId = Entry->GlyphId;
        OutGlyph.Label = Entry->Label;
        OutGlyph.Key = Key;
        return true;
    }

    if (Set->bFallbackToKeyName)
    {
        OutGlyph.GlyphSetId = Set->GlyphSetId;
        OutGlyph.GlyphId = FName(*FString::Printf(TEXT("Key_%s"), *KeyName.ToString()));
        OutGlyph.Label = Key.GetDisplayName(false).ToString();
        OutGlyph.Key = Key;
        return true;
    }
    return false;
}

bool UPSInputGlyphs::GetGlyphForAction(const FPSInputCatalog& InCatalog, FName ActionId, FName ContextId, EPSInputDevice Device, FPSInputGlyph& OutGlyph) const
{
    const FPSInputGlyphSetDef* Set = GetDefaultSet(Device);
    const FPSInputActionDef* Action = InCatalog.Actions.FindByPredicate([ActionId](const FPSInputActionDef& Def) { return Def.ActionId == ActionId; });
    if (!Set || !Action || !Action->Contexts.Contains(ContextId))
    {
        return false;
    }

    // Touch controls name actions, not keys (Epic 130): the set's action glyph is the glyph.
    if (Device == EPSInputDevice::Touch)
    {
        const FPSActionGlyphDef* TouchGlyph = Set->Actions.FindByPredicate([ActionId](const FPSActionGlyphDef& Def) { return Def.ActionId == ActionId; });
        if (!TouchGlyph)
        {
            return false;
        }
        OutGlyph.GlyphSetId = Set->GlyphSetId;
        OutGlyph.GlyphId = TouchGlyph->GlyphId;
        OutGlyph.Label = TouchGlyph->Label;
        OutGlyph.Key = FKey();
        return true;
    }

    // The catalog decides which key the action uses on this device.
    const FPSInputKeyBinding* Binding = Action->Bindings.FindByPredicate([Device](const FPSInputKeyBinding& Def)
    {
        const FKey Key(Def.Key);
        return Key.IsValid() && GetDeviceForKey(Key) == Device;
    });
    if (!Binding)
    {
        return false;
    }

    if (const FPSActionGlyphDef* ActionGlyph = Set->Actions.FindByPredicate([ActionId](const FPSActionGlyphDef& Def) { return Def.ActionId == ActionId; }))
    {
        OutGlyph.GlyphSetId = Set->GlyphSetId;
        OutGlyph.GlyphId = ActionGlyph->GlyphId;
        OutGlyph.Label = ActionGlyph->Label;
        OutGlyph.Key = FKey();
        return true;
    }
    return GetGlyphForKey(FKey(Binding->Key), Device, OutGlyph);
}

TArray<FString> UPSInputGlyphs::Validate(const FPSInputCatalog* InCatalog) const
{
    TArray<FString> Problems;
    const UEnum* DeviceEnum = StaticEnum<EPSInputDevice>();

    TSet<FName> SetIds;
    TMap<EPSInputDevice, int32> DefaultCounts;
    for (const FPSInputGlyphSetDef& Set : Table.GlyphSets)
    {
        const FString SetName = Set.GlyphSetId.ToString();
        if (Set.GlyphSetId.IsNone())
        {
            Problems.Add(TEXT("A glyph set has no GlyphSetId."));
        }
        else if (SetIds.Contains(Set.GlyphSetId))
        {
            Problems.Add(FString::Printf(TEXT("Glyph set '%s' is declared twice."), *SetName));
        }
        SetIds.Add(Set.GlyphSetId);

        if (Set.bDefaultForDevice)
        {
            DefaultCounts.FindOrAdd(Set.Device)++;
        }

        TSet<FName> KeysSeen;
        for (const FPSKeyGlyphDef& Entry : Set.Keys)
        {
            const FKey Key(Entry.Key);
            if (!Key.IsValid())
            {
                Problems.Add(FString::Printf(TEXT("Glyph set '%s': '%s' is not an engine key."), *SetName, *Entry.Key.ToString()));
            }
            else if (GetDeviceForKey(Key) != Set.Device)
            {
                Problems.Add(FString::Printf(TEXT("Glyph set '%s': key '%s' is a %s key, not %s."),
                    *SetName, *Entry.Key.ToString(), *PSInputGlyphsPrivate::DeviceName(GetDeviceForKey(Key)), *PSInputGlyphsPrivate::DeviceName(Set.Device)));
            }
            if (KeysSeen.Contains(Entry.Key))
            {
                Problems.Add(FString::Printf(TEXT("Glyph set '%s': key '%s' has two glyphs."), *SetName, *Entry.Key.ToString()));
            }
            KeysSeen.Add(Entry.Key);
            if (Entry.GlyphId.IsNone() || Entry.Label.IsEmpty())
            {
                Problems.Add(FString::Printf(TEXT("Glyph set '%s', key '%s': needs a GlyphId and a Label."), *SetName, *Entry.Key.ToString()));
            }
        }

        TSet<FName> ActionsSeen;
        for (const FPSActionGlyphDef& Entry : Set.Actions)
        {
            if (Entry.ActionId.IsNone() || ActionsSeen.Contains(Entry.ActionId))
            {
                Problems.Add(FString::Printf(TEXT("Glyph set '%s': empty or duplicate action glyph '%s'."), *SetName, *Entry.ActionId.ToString()));
            }
            ActionsSeen.Add(Entry.ActionId);
            if (Entry.GlyphId.IsNone() || Entry.Label.IsEmpty())
            {
                Problems.Add(FString::Printf(TEXT("Glyph set '%s', action '%s': needs a GlyphId and a Label."), *SetName, *Entry.ActionId.ToString()));
            }
            const FName ActionId = Entry.ActionId;
            if (InCatalog && !InCatalog->Actions.ContainsByPredicate([ActionId](const FPSInputActionDef& Def) { return Def.ActionId == ActionId; }))
            {
                Problems.Add(FString::Printf(TEXT("Glyph set '%s': action glyph for '%s', which is not in the input catalog."), *SetName, *ActionId.ToString()));
            }
        }
    }

    // NumEnums() includes the generated _MAX entry.
    for (int32 Index = 0; Index < DeviceEnum->NumEnums() - 1; ++Index)
    {
        const EPSInputDevice Device = static_cast<EPSInputDevice>(DeviceEnum->GetValueByIndex(Index));
        const int32 Count = DefaultCounts.FindRef(Device);
        if (Count != 1)
        {
            Problems.Add(FString::Printf(TEXT("Device '%s' needs exactly one default glyph set (has %d)."), *PSInputGlyphsPrivate::DeviceName(Device), Count));
        }
    }

    if (InCatalog)
    {
        // Every key the catalog binds must be drawable on its device's default set.
        TSet<FName> Reported;
        for (const FPSInputActionDef& Action : InCatalog->Actions)
        {
            for (const FPSInputKeyBinding& Binding : Action.Bindings)
            {
                const FKey Key(Binding.Key);
                if (!Key.IsValid() || Reported.Contains(Binding.Key))
                {
                    continue;
                }
                const EPSInputDevice Device = GetDeviceForKey(Key);
                const FPSInputGlyphSetDef* Set = GetDefaultSet(Device);
                FPSInputGlyph Unused;
                if (Set && !GetGlyphForKey(Key, Device, Unused))
                {
                    Problems.Add(FString::Printf(TEXT("Glyph set '%s' has no glyph for '%s', which the input catalog binds to '%s'."),
                        *Set->GlyphSetId.ToString(), *Binding.Key.ToString(), *Action.ActionId.ToString()));
                    Reported.Add(Binding.Key);
                }
            }
        }
    }
    return Problems;
}
