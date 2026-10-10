// AutonomixT3D.h - Core 25.1: level actors spawned or changed from agent-written T3D text
#pragma once

#include "CoreMinimal.h"

class AActor;
class FProperty;
class UObject;
class UWorld;

/** One "Begin Actor ... End Actor" block of T3D text. */
struct AUTONOMIX_API FAutonomixT3DActor
{
    /** Class= (a path such as /Script/Engine.TargetPoint, or a class name). */
    FString ClassName;

    /** Name=: the actor to change when the level has it, else the new actor's name. */
    FString Name;

    /** The block's own property lines, "Property=Value" in Unreal's text format. */
    TArray<FString> PropertyLines;

    /** Each "Begin Object Name=X" definition (no Class=): the subobject's name and its lines. */
    TArray<TPair<FString, TArray<FString>>> Subobjects;

    /** The header line, for messages. */
    int32 LineNumber = 0;
};

/** What an import did. */
struct AUTONOMIX_API FAutonomixT3DResult
{
    /** The names of the actors spawned and of those changed. */
    TArray<FString> Spawned;
    TArray<FString> Changed;

    /** Lines that couldn't be applied, one message each (the rest of the import stands). */
    TArray<FString> Warnings;

    /** Why nothing was imported; empty on success. */
    FString Error;

    /** True when the import is one editor transaction (Ctrl+Z undoes all of it). */
    bool bUndoable = false;

    bool IsError() const { return !Error.IsEmpty(); }
};

/**
 * FAutonomixT3D imports agent-written T3D text (the format the editor copies actors as) into a
 * world's persistent level (Core 25.1):
 *
 *  - Each "Begin Actor Class=... Name=..." block, inside "Begin Map"/"Begin Level" or not, either
 *    changes the actor of that name (or editor label) the level already has, or spawns a new one
 *    of its class under that name.
 *  - Its property lines ("Tags=(\"A\")", "bHidden=True", "ActorLabel=\"X\"") are imported into the
 *    actor as the engine imports text; each "Begin Object Name=X ... End Object" definition into
 *    the actor's subobject X (its components: "RelativeLocation=(X=1,Y=2,Z=3)" on its root moves
 *    it). Subobject declarations ("Begin Object Class=...") name what the class already makes;
 *    a component the class doesn't make is not created, and the definition says so in a warning.
 *    An exported "RootComponent=..." line is passed over: the root stays the one the class made.
 *  - The whole text is one undoable transaction in the editor. Text that names no actor, or a
 *    block left open, imports nothing and says why.
 *
 * Transient and deprecated properties are never written.
 */
class AUTONOMIX_API FAutonomixT3D
{
public:
    /** The actor blocks of Text; false with OutError when a block is left open or has no class
     *  and no name. */
    static bool Parse(const FString& Text, TArray<FAutonomixT3DActor>& OutActors, FString& OutError);

    /** Imports Text into World's persistent level. */
    static FAutonomixT3DResult Import(UWorld* World, const FString& Text);

    /** Applies "Property=Value", or "Property(Index)=Value" for one element of an array, to
     *  Target. With ClearedArrays, an array written by element is emptied the first time (so
     *  "Tags(0)=..." alone leaves one tag, as the engine's own import does). False with
     *  OutWarning when the property is unknown, can't be written or the value doesn't parse. */
    static bool ApplyPropertyLine(UObject* Target, const FString& Line, FString& OutWarning, TSet<const FProperty*>* ClearedArrays = nullptr);
};
