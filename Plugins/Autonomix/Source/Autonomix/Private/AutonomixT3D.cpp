// AutonomixT3D.cpp - Core 25.1: level actors spawned or changed from agent-written T3D text
#include "AutonomixT3D.h"
#include "AgenticLinkEngineTools.h"
#include "Components/SceneComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/Parse.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

namespace AutonomixT3DPrivate
{
    bool LineStarts(const FString& Line, const TCHAR* Keyword)
    {
        return Line.StartsWith(Keyword, ESearchCase::IgnoreCase);
    }

    /** Applies a block's lines to Target, collecting warnings; each array property written by
     *  element is emptied the first time. */
    void ApplyLines(UObject* Target, const TArray<FString>& Lines, int32 LineNumber, FAutonomixT3DResult& Result)
    {
        TSet<const FProperty*> ClearedArrays;
        for (const FString& Line : Lines)
        {
            FString Warning;
            if (!FAutonomixT3D::ApplyPropertyLine(Target, Line, Warning, &ClearedArrays))
            {
                Result.Warnings.Add(FString::Printf(TEXT("Actor at line %d: %s"), LineNumber, *Warning));
            }
        }
    }
}

bool FAutonomixT3D::Parse(const FString& Text, TArray<FAutonomixT3DActor>& OutActors, FString& OutError)
{
    using namespace AutonomixT3DPrivate;

    OutActors.Reset();
    TArray<FString> Lines;
    Text.ParseIntoArrayLines(Lines, false);

    int32 Open = INDEX_NONE;
    int32 ObjectDepth = 0;
    bool bInDefinition = false;
    FString DefinitionName;
    TArray<FString> DefinitionLines;
    for (int32 Index = 0; Index < Lines.Num(); ++Index)
    {
        const FString Line = Lines[Index].TrimStartAndEnd();
        const int32 LineNumber = Index + 1;
        if (Line.IsEmpty())
        {
            continue;
        }
        if (Open == INDEX_NONE)
        {
            // Outside an actor: "Begin Map", "Begin Level" and their ends wrap the actors.
            if (LineStarts(Line, TEXT("Begin Actor")))
            {
                FAutonomixT3DActor& Block = OutActors.AddDefaulted_GetRef();
                Block.LineNumber = LineNumber;
                FParse::Value(*Line, TEXT("Class="), Block.ClassName);
                FParse::Value(*Line, TEXT("Name="), Block.Name);
                if (Block.ClassName.IsEmpty() && Block.Name.IsEmpty())
                {
                    OutError = FString::Printf(TEXT("Line %d: Begin Actor needs a Class= or a Name=."), LineNumber);
                    return false;
                }
                Open = OutActors.Num() - 1;
                ObjectDepth = 0;
            }
            continue;
        }

        FAutonomixT3DActor& Current = OutActors[Open];
        if (LineStarts(Line, TEXT("Begin Object")))
        {
            ++ObjectDepth;
            if (ObjectDepth == 1)
            {
                // "Begin Object Class=... Name=X" declares a subobject; "Begin Object Name=X"
                // defines its properties.
                FString ObjectClass;
                DefinitionName.Reset();
                FParse::Value(*Line, TEXT("Class="), ObjectClass);
                FParse::Value(*Line, TEXT("Name="), DefinitionName);
                bInDefinition = ObjectClass.IsEmpty() && !DefinitionName.IsEmpty();
                DefinitionLines.Reset();
            }
            continue;
        }
        if (LineStarts(Line, TEXT("End Object")))
        {
            if (ObjectDepth == 0)
            {
                OutError = FString::Printf(TEXT("Line %d: End Object without a Begin Object."), LineNumber);
                return false;
            }
            --ObjectDepth;
            if (ObjectDepth == 0 && bInDefinition)
            {
                Current.Subobjects.Add(TPair<FString, TArray<FString>>(DefinitionName, DefinitionLines));
                bInDefinition = false;
            }
            continue;
        }
        if (LineStarts(Line, TEXT("End Actor")))
        {
            if (ObjectDepth != 0)
            {
                OutError = FString::Printf(TEXT("Line %d: End Actor inside an unfinished Begin Object."), LineNumber);
                return false;
            }
            Open = INDEX_NONE;
            continue;
        }
        if (ObjectDepth == 0)
        {
            Current.PropertyLines.Add(Line);
        }
        else if (ObjectDepth == 1 && bInDefinition)
        {
            DefinitionLines.Add(Line);
        }
    }
    if (Open != INDEX_NONE)
    {
        OutError = FString::Printf(TEXT("Line %d: Begin Actor has no End Actor."), OutActors[Open].LineNumber);
        return false;
    }
    return true;
}

bool FAutonomixT3D::ApplyPropertyLine(UObject* Target, const FString& Line, FString& OutWarning, TSet<const FProperty*>* ClearedArrays)
{
    int32 Equals = INDEX_NONE;
    if (!Target || !Line.FindChar(TEXT('='), Equals) || Equals == 0)
    {
        OutWarning = FString::Printf(TEXT("'%s' is not Property=Value."), *Line.Left(80));
        return false;
    }
    FString PropertyName = Line.Left(Equals).TrimStartAndEnd();
    const FString ValueText = Line.Mid(Equals + 1).TrimStartAndEnd();

    // "Tags(0)=..." writes one element of an array.
    int32 ElementIndex = INDEX_NONE;
    int32 Open = INDEX_NONE;
    if (PropertyName.FindChar(TEXT('('), Open) && PropertyName.EndsWith(TEXT(")")))
    {
        const FString IndexText = PropertyName.Mid(Open + 1, PropertyName.Len() - Open - 2);
        if (!IndexText.IsNumeric())
        {
            OutWarning = FString::Printf(TEXT("'%s' has no element index."), *PropertyName);
            return false;
        }
        ElementIndex = FCString::Atoi(*IndexText);
        PropertyName = PropertyName.Left(Open);
    }

    FProperty* Property = FindFProperty<FProperty>(Target->GetClass(), FName(*PropertyName));
    if (!Property)
    {
        OutWarning = FString::Printf(TEXT("%s has no property '%s'."), *Target->GetClass()->GetName(), *PropertyName);
        return false;
    }
    if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
    {
        OutWarning = FString::Printf(TEXT("'%s' is transient or deprecated; an import doesn't write it."), *PropertyName);
        return false;
    }

    void* ValuePtr = nullptr;
    FProperty* Writing = Property;
    if (ElementIndex == INDEX_NONE)
    {
        ValuePtr = Property->ContainerPtrToValuePtr<void>(Target);
    }
    else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        if (ElementIndex < 0 || ElementIndex > 4096)
        {
            OutWarning = FString::Printf(TEXT("'%s(%d)' is out of range."), *PropertyName, ElementIndex);
            return false;
        }
        FScriptArrayHelper Array(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Target));
        if (ClearedArrays && !ClearedArrays->Contains(ArrayProperty))
        {
            Array.EmptyValues();
            ClearedArrays->Add(ArrayProperty);
        }
        if (ElementIndex >= Array.Num())
        {
            Array.AddValues(ElementIndex + 1 - Array.Num());
        }
        ValuePtr = Array.GetRawPtr(ElementIndex);
        Writing = ArrayProperty->Inner;
    }
    else if (ElementIndex >= 0 && ElementIndex < Property->ArrayDim)
    {
        ValuePtr = Property->ContainerPtrToValuePtr<void>(Target, ElementIndex);
    }
    else
    {
        OutWarning = FString::Printf(TEXT("'%s(%d)' is out of range."), *PropertyName, ElementIndex);
        return false;
    }

#if WITH_EDITOR
    Target->PreEditChange(Property);
#endif
    FOutputDeviceNull Quiet;
    const bool bImported = Writing->ImportText_Direct(*ValueText, ValuePtr, Target, PPF_Delimited, &Quiet) != nullptr;
#if WITH_EDITOR
    FPropertyChangedEvent ChangeEvent(Property, EPropertyChangeType::ValueSet);
    Target->PostEditChangeProperty(ChangeEvent);
#endif
    if (!bImported)
    {
        OutWarning = FString::Printf(TEXT("'%s' is not a valid %s for %s."), *ValueText.Left(80), *Writing->GetCPPType(), *PropertyName);
    }
    return bImported;
}

FAutonomixT3DResult FAutonomixT3D::Import(UWorld* World, const FString& Text)
{
    using namespace AutonomixT3DPrivate;

    FAutonomixT3DResult Result;
    if (!World || !World->PersistentLevel)
    {
        Result.Error = TEXT("No world is open: open a level in the editor (or start PIE) and retry.");
        return Result;
    }
    TArray<FAutonomixT3DActor> Blocks;
    if (!Parse(Text, Blocks, Result.Error))
    {
        return Result;
    }
    if (Blocks.Num() == 0)
    {
        Result.Error = TEXT("The text has no Begin Actor ... End Actor block.");
        return Result;
    }

#if WITH_EDITOR
    FScopedTransaction Transaction(TEXT("Autonomix"), FText::FromString(TEXT("Agent imports T3D")), World->PersistentLevel);
#endif
    World->PersistentLevel->Modify();

    for (const FAutonomixT3DActor& Block : Blocks)
    {
        AActor* Actor = Block.Name.IsEmpty() ? nullptr : FAgenticLinkEngineTools::FindActor(World, Block.Name);
        const bool bChanging = Actor != nullptr;
        if (Actor)
        {
            Actor->Modify();
        }
        else
        {
            UClass* Class = FAgenticLinkEngineTools::FindActorClass(Block.ClassName);
            if (!Class)
            {
                Result.Warnings.Add(FString::Printf(TEXT("Actor at line %d: '%s' is not a spawnable actor class (and no actor is named '%s'); skipped."),
                    Block.LineNumber, *Block.ClassName, *Block.Name));
                continue;
            }
            FActorSpawnParameters SpawnParameters;
            SpawnParameters.OverrideLevel = World->PersistentLevel;
            SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            if (!Block.Name.IsEmpty() && !StaticFindObjectFast(nullptr, World->PersistentLevel, FName(*Block.Name)))
            {
                SpawnParameters.Name = FName(*Block.Name);
                SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
            }
            Actor = World->SpawnActor(Class, nullptr, nullptr, SpawnParameters);
            if (!Actor)
            {
                Result.Warnings.Add(FString::Printf(TEXT("Actor at line %d: %s could not be spawned."), Block.LineNumber, *Class->GetName()));
                continue;
            }
        }

        // The subobjects (its components) first, then the actor's own lines.
        for (const TPair<FString, TArray<FString>>& Subobject : Block.Subobjects)
        {
            UObject* Target = FindObject<UObject>(Actor, *Subobject.Key);
            if (!Target)
            {
                Result.Warnings.Add(FString::Printf(TEXT("Actor at line %d: %s has no subobject '%s' (a component its class doesn't make isn't created)."),
                    Block.LineNumber, *Actor->GetName(), *Subobject.Key));
                continue;
            }
            Target->Modify();
            ApplyLines(Target, Subobject.Value, Block.LineNumber, Result);
            if (USceneComponent* Scene = Cast<USceneComponent>(Target))
            {
                Scene->UpdateComponentToWorld();
            }
        }
        TArray<FString> OwnLines;
        for (const FString& Line : Block.PropertyLines)
        {
            // The root is the one its class made: an exported "RootComponent=..." names it.
            if (LineStarts(Line, TEXT("RootComponent=")))
            {
                continue;
            }
            if (LineStarts(Line, TEXT("ActorLabel=")))
            {
#if WITH_EDITOR
                Actor->SetActorLabel(Line.Mid(FCString::Strlen(TEXT("ActorLabel="))).TrimStartAndEnd().TrimQuotes());
#endif
                continue;
            }
            OwnLines.Add(Line);
        }
        ApplyLines(Actor, OwnLines, Block.LineNumber, Result);
#if WITH_EDITOR
        Actor->PostEditChange();
#endif
        (bChanging ? Result.Changed : Result.Spawned).Add(Actor->GetName());
    }

#if WITH_EDITOR
    if (Result.Spawned.Num() + Result.Changed.Num() == 0)
    {
        Transaction.Cancel();
    }
    Result.bUndoable = Transaction.IsOutstanding();
#endif
    if (Result.Spawned.Num() + Result.Changed.Num() == 0)
    {
        Result.Error = Result.Warnings.Num() > 0 ? Result.Warnings[0] : FString(TEXT("Nothing was imported."));
    }
    return Result;
}
