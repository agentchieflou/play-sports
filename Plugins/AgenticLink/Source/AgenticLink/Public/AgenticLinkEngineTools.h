// AgenticLinkEngineTools.h - Epic 25: engine reflection tools for MCP agents
#pragma once

#include "CoreMinimal.h"
#include "AgenticLinkMcpServer.h"

class AActor;
class UClass;
class UWorld;

/**
 * The engine-reflection tools AgenticLink serves (Epic 25), each acting on the world the
 * resolver returns when the tool is called:
 *
 *  - list_actors   {class?, limit?}               actors with name, label, class, location
 *  - get_property  {actor, property}              an editable or Blueprint-visible property, as text
 *  - set_property  {actor, property, value}       an instance-editable property, as an undoable edit
 *  - call_function {actor, function, arguments?}  a BlueprintCallable UFUNCTION; outputs as text
 *  - spawn_actor   {class, location?, rotation?, label?}  a new actor, as an undoable edit
 *
 * Values travel in Unreal's text format ("(X=1,Y=2,Z=3)", "(\"Tag\")", "True"); JSON numbers and
 * booleans are accepted too. In the editor every edit is one FScopedTransaction, so Ctrl+Z
 * undoes an agent's change like a person's. Tools run on the game thread (the HTTP server
 * dispatches there).
 */
class AGENTICLINK_API FAgenticLinkEngineTools
{
public:
    /** Registers the five tools on Server. WorldResolver picks the world per call; an empty
     *  resolver means FindDefaultWorld. */
    static void Register(FAgenticLinkMcpServer& Server, TFunction<UWorld*()> WorldResolver = nullptr);

    /** The world an agent means: the PIE world while playing, else the editor's level, else
     *  the first game world. */
    static UWorld* FindDefaultWorld();

    /** The actor in World whose object name, editor label or path name is ActorName. */
    static AActor* FindActor(UWorld* World, const FString& ActorName);

    /** An actor class by path ("/Script/Engine.TargetPoint", a Blueprint's "..._C") or by
     *  native name ("TargetPoint"); null when it isn't a spawnable actor class. */
    static UClass* FindActorClass(const FString& ClassName);

    /** A JSON argument in Unreal's text format: strings as they are, whole numbers without a
     *  fraction, booleans as True/False. False for arrays, objects and null. */
    static bool JsonValueToText(const TSharedPtr<FJsonValue>& Value, FString& OutText);
};
