// AgenticLinkEngineTools.cpp - Epic 25: engine reflection tools for MCP agents
#include "AgenticLinkEngineTools.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/OutputDeviceNull.h"
#include "UObject/Script.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

namespace AgenticLinkTools
{
    const TCHAR* const NoWorldError = TEXT("No world is open: open a level in the editor (or start PIE) and retry.");

    /** One argument in a tool's input schema. */
    struct FSchemaParam
    {
        const TCHAR* Name;
        const TCHAR* Type;
        const TCHAR* Description;
        bool bRequired;
    };

    TSharedPtr<FJsonObject> MakeSchema(const TArray<FSchemaParam>& Params)
    {
        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Required;
        for (const FSchemaParam& Param : Params)
        {
            TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
            Property->SetStringField(TEXT("type"), Param.Type);
            Property->SetStringField(TEXT("description"), Param.Description);
            if (FCString::Strcmp(Param.Type, TEXT("array")) == 0)
            {
                TSharedPtr<FJsonObject> Items = MakeShared<FJsonObject>();
                Items->SetStringField(TEXT("type"), TEXT("number"));
                Property->SetObjectField(TEXT("items"), Items);
                Property->SetNumberField(TEXT("minItems"), 3);
                Property->SetNumberField(TEXT("maxItems"), 3);
            }
            Properties->SetObjectField(Param.Name, Property);
            if (Param.bRequired)
            {
                Required.Add(MakeShared<FJsonValueString>(Param.Name));
            }
        }

        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        Schema->SetObjectField(TEXT("properties"), Properties);
        Schema->SetArrayField(TEXT("required"), Required);
        return Schema;
    }

    TArray<TSharedPtr<FJsonValue>> MakeVectorArray(const FVector& Vector)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        Values.Add(MakeShared<FJsonValueNumber>(Vector.X));
        Values.Add(MakeShared<FJsonValueNumber>(Vector.Y));
        Values.Add(MakeShared<FJsonValueNumber>(Vector.Z));
        return Values;
    }

    /** Reads a [x, y, z] argument; false when it is present but not three numbers. */
    bool ReadTriple(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Field, FVector& OutValue)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Arguments->HasField(Field))
        {
            return true;
        }
        if (!Arguments->TryGetArrayField(Field, Values) || !Values || Values->Num() != 3)
        {
            return false;
        }
        double Components[3] = { 0.0, 0.0, 0.0 };
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (!(*Values)[Axis].IsValid() || !(*Values)[Axis]->TryGetNumber(Components[Axis]))
            {
                return false;
            }
        }
        OutValue = FVector(Components[0], Components[1], Components[2]);
        return true;
    }

    TSharedPtr<FJsonObject> DescribeActor(const AActor* Actor)
    {
        TSharedPtr<FJsonObject> Description = MakeShared<FJsonObject>();
        Description->SetStringField(TEXT("name"), Actor->GetName());
#if WITH_EDITOR
        Description->SetStringField(TEXT("label"), Actor->GetActorLabel());
#else
        Description->SetStringField(TEXT("label"), Actor->GetName());
#endif
        Description->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
        Description->SetStringField(TEXT("path"), Actor->GetPathName());
        Description->SetArrayField(TEXT("location"), MakeVectorArray(Actor->GetActorLocation()));
        return Description;
    }

    bool IsOfClassNamed(const AActor* Actor, const FString& ClassName)
    {
        for (const UClass* Class = Actor->GetClass(); Class; Class = Class->GetSuperClass())
        {
            if (Class->GetName() == ClassName)
            {
                return true;
            }
        }
        return false;
    }

    /** One agent edit: an undoable editor transaction with Target recorded; outside the
     *  editor, just the edit. */
    class FEditScope
    {
    public:
        FEditScope(const FString& Description, UObject* Target)
        {
#if WITH_EDITOR
            Transaction = MakeUnique<FScopedTransaction>(TEXT("AgenticLink"), FText::FromString(Description), Target);
#endif
            if (Target)
            {
                Target->Modify();
            }
        }

        /** Drops the transaction (the edit failed and changed nothing worth undoing). */
        void Cancel()
        {
#if WITH_EDITOR
            if (Transaction.IsValid())
            {
                Transaction->Cancel();
            }
#endif
        }

        bool IsTransacted() const
        {
#if WITH_EDITOR
            return Transaction.IsValid() && Transaction->IsOutstanding();
#else
            return false;
#endif
        }

    private:
#if WITH_EDITOR
        TUniquePtr<FScopedTransaction> Transaction;
#endif
    };

    /** The actor named by the "actor" argument, or an error saying why not. */
    AActor* ResolveActor(UWorld* World, const TSharedPtr<FJsonObject>& Arguments, FString& OutError)
    {
        if (!World)
        {
            OutError = NoWorldError;
            return nullptr;
        }
        FString ActorName;
        if (!Arguments->TryGetStringField(TEXT("actor"), ActorName) || ActorName.IsEmpty())
        {
            OutError = TEXT("Missing 'actor': an actor name, label or path from list_actors.");
            return nullptr;
        }
        AActor* Actor = FAgenticLinkEngineTools::FindActor(World, ActorName);
        if (!Actor)
        {
            OutError = FString::Printf(TEXT("No actor '%s' in %s; list_actors shows what is there."), *ActorName, *World->GetName());
        }
        return Actor;
    }

    /** The actor's property named by the "property" argument, or an error saying why not. */
    FProperty* ResolveProperty(const AActor* Actor, const TSharedPtr<FJsonObject>& Arguments, FString& OutError)
    {
        FString PropertyName;
        if (!Arguments->TryGetStringField(TEXT("property"), PropertyName) || PropertyName.IsEmpty())
        {
            OutError = TEXT("Missing 'property': the property's name, e.g. Tags.");
            return nullptr;
        }
        FProperty* Property = FindFProperty<FProperty>(Actor->GetClass(), FName(*PropertyName));
        if (!Property)
        {
            OutError = FString::Printf(TEXT("%s has no property '%s'."), *Actor->GetClass()->GetName(), *PropertyName);
            return nullptr;
        }
        if (!Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
        {
            OutError = FString::Printf(TEXT("'%s' is neither editable nor Blueprint-visible."), *PropertyName);
            return nullptr;
        }
        return Property;
    }

    FAgenticLinkToolResult ListActors(UWorld* World, const TSharedPtr<FJsonObject>& Arguments)
    {
        if (!World)
        {
            return FAgenticLinkToolResult::Failure(NoWorldError);
        }
        FString ClassFilter;
        Arguments->TryGetStringField(TEXT("class"), ClassFilter);
        int32 Limit = 200;
        Arguments->TryGetNumberField(TEXT("limit"), Limit);
        Limit = FMath::Clamp(Limit, 1, 2000);

        TArray<TSharedPtr<FJsonValue>> Actors;
        bool bTruncated = false;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            const AActor* Actor = *It;
            if (!ClassFilter.IsEmpty() && !IsOfClassNamed(Actor, ClassFilter))
            {
                continue;
            }
            if (Actors.Num() >= Limit)
            {
                bTruncated = true;
                break;
            }
            Actors.Add(MakeShared<FJsonValueObject>(DescribeActor(Actor)));
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("world"), World->GetName());
        Result->SetArrayField(TEXT("actors"), Actors);
        Result->SetBoolField(TEXT("truncated"), bTruncated);
        return FAgenticLinkToolResult::Success(Result);
    }

    FAgenticLinkToolResult GetProperty(UWorld* World, const TSharedPtr<FJsonObject>& Arguments)
    {
        FString Error;
        AActor* Actor = ResolveActor(World, Arguments, Error);
        FProperty* Property = Actor ? ResolveProperty(Actor, Arguments, Error) : nullptr;
        if (!Property)
        {
            return FAgenticLinkToolResult::Failure(Error);
        }

        FString Value;
        Property->ExportText_InContainer(0, Value, Actor, nullptr, Actor, PPF_None);
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("actor"), Actor->GetName());
        Result->SetStringField(TEXT("property"), Property->GetName());
        Result->SetStringField(TEXT("type"), Property->GetCPPType());
        Result->SetStringField(TEXT("value"), Value);
        return FAgenticLinkToolResult::Success(Result);
    }

    FAgenticLinkToolResult SetProperty(UWorld* World, const TSharedPtr<FJsonObject>& Arguments)
    {
        FString Error;
        AActor* Actor = ResolveActor(World, Arguments, Error);
        FProperty* Property = Actor ? ResolveProperty(Actor, Arguments, Error) : nullptr;
        if (!Property)
        {
            return FAgenticLinkToolResult::Failure(Error);
        }
        if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance))
        {
            return FAgenticLinkToolResult::Failure(FString::Printf(TEXT("'%s' can't be edited on a placed actor."), *Property->GetName()));
        }
        FString Text;
        if (!FAgenticLinkEngineTools::JsonValueToText(Arguments->TryGetField(TEXT("value")), Text))
        {
            return FAgenticLinkToolResult::Failure(TEXT("Missing 'value': a string in Unreal's text format, a number or a boolean."));
        }

        FEditScope Edit(FString::Printf(TEXT("Agent sets %s.%s"), *Actor->GetName(), *Property->GetName()), Actor);
#if WITH_EDITOR
        Actor->PreEditChange(Property);
#endif
        FOutputDeviceNull Quiet;
        const bool bImported = Property->ImportText_InContainer(*Text, Actor, Actor, PPF_None, &Quiet) != nullptr;
#if WITH_EDITOR
        FPropertyChangedEvent ChangeEvent(Property, EPropertyChangeType::ValueSet);
        Actor->PostEditChangeProperty(ChangeEvent);
#endif
        if (!bImported)
        {
            Edit.Cancel();
            return FAgenticLinkToolResult::Failure(FString::Printf(TEXT("'%s' is not a valid %s."), *Text, *Property->GetCPPType()));
        }

        FString Value;
        Property->ExportText_InContainer(0, Value, Actor, nullptr, Actor, PPF_None);
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("actor"), Actor->GetName());
        Result->SetStringField(TEXT("property"), Property->GetName());
        Result->SetStringField(TEXT("value"), Value);
        Result->SetBoolField(TEXT("undoable"), Edit.IsTransacted());
        return FAgenticLinkToolResult::Success(Result);
    }

    FAgenticLinkToolResult CallFunction(UWorld* World, const TSharedPtr<FJsonObject>& Arguments)
    {
        FString Error;
        AActor* Actor = ResolveActor(World, Arguments, Error);
        if (!Actor)
        {
            return FAgenticLinkToolResult::Failure(Error);
        }
        FString FunctionName;
        if (!Arguments->TryGetStringField(TEXT("function"), FunctionName) || FunctionName.IsEmpty())
        {
            return FAgenticLinkToolResult::Failure(TEXT("Missing 'function': a BlueprintCallable function's name."));
        }
        UFunction* Function = Actor->FindFunction(FName(*FunctionName));
        if (!Function)
        {
            return FAgenticLinkToolResult::Failure(FString::Printf(TEXT("%s has no function '%s'."), *Actor->GetClass()->GetName(), *FunctionName));
        }
        if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable))
        {
            return FAgenticLinkToolResult::Failure(FString::Printf(TEXT("'%s' is not BlueprintCallable."), *FunctionName));
        }

        const TSharedPtr<FJsonObject>* CallArgumentsPtr = nullptr;
        const TSharedPtr<FJsonObject> CallArguments = Arguments->TryGetObjectField(TEXT("arguments"), CallArgumentsPtr) && CallArgumentsPtr
            ? *CallArgumentsPtr
            : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>());

        FStructOnScope Parameters(Function);
        uint8* ParameterMemory = Parameters.GetStructMemory();
        FOutputDeviceNull Quiet;
        for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
        {
            const FProperty* Parameter = *It;
            const bool bOutputOnly = Parameter->HasAnyPropertyFlags(CPF_ReturnParm)
                || (Parameter->HasAnyPropertyFlags(CPF_OutParm) && !Parameter->HasAnyPropertyFlags(CPF_ReferenceParm));
            if (bOutputOnly)
            {
                continue;
            }

            FString Text;
            const TSharedPtr<FJsonValue> Supplied = CallArguments->TryGetField(Parameter->GetName());
            bool bHaveText = Supplied.IsValid() && FAgenticLinkEngineTools::JsonValueToText(Supplied, Text);
#if WITH_EDITORONLY_DATA
            const FName DefaultKey(*(FString(TEXT("CPP_Default_")) + Parameter->GetName()));
            if (!Supplied.IsValid() && Function->HasMetaData(DefaultKey))
            {
                Text = Function->GetMetaData(DefaultKey);
                bHaveText = true;
            }
#endif
            if (!bHaveText)
            {
                return FAgenticLinkToolResult::Failure(FString::Printf(TEXT("Argument '%s' (%s) is missing or not a string, number or boolean."),
                    *Parameter->GetName(), *Parameter->GetCPPType()));
            }
            if (!Parameter->ImportText_InContainer(*Text, ParameterMemory, nullptr, PPF_None, &Quiet))
            {
                return FAgenticLinkToolResult::Failure(FString::Printf(TEXT("Argument '%s': '%s' is not a valid %s."),
                    *Parameter->GetName(), *Text, *Parameter->GetCPPType()));
            }
        }

        FEditScope Edit(FString::Printf(TEXT("Agent calls %s.%s"), *Actor->GetName(), *FunctionName), Actor);
        {
            // AActor::ProcessEvent does nothing in a world whose actors aren't initialized
            // for play (the editor's level) unless script execution is allowed, as it is
            // for a CallInEditor button.
            FEditorScriptExecutionGuard ScriptGuard;
            Actor->ProcessEvent(Function, ParameterMemory);
        }

        TSharedPtr<FJsonObject> Outputs = MakeShared<FJsonObject>();
        for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
        {
            const FProperty* Parameter = *It;
            if (Parameter->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
            {
                FString Value;
                Parameter->ExportText_InContainer(0, Value, ParameterMemory, nullptr, nullptr, PPF_None);
                Outputs->SetStringField(Parameter->GetName(), Value);
            }
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("actor"), Actor->GetName());
        Result->SetStringField(TEXT("function"), Function->GetName());
        Result->SetObjectField(TEXT("outputs"), Outputs);
        Result->SetBoolField(TEXT("undoable"), Edit.IsTransacted());
        return FAgenticLinkToolResult::Success(Result);
    }

    FAgenticLinkToolResult SpawnActor(UWorld* World, const TSharedPtr<FJsonObject>& Arguments)
    {
        if (!World || !World->PersistentLevel)
        {
            return FAgenticLinkToolResult::Failure(NoWorldError);
        }
        FString ClassName;
        if (!Arguments->TryGetStringField(TEXT("class"), ClassName) || ClassName.IsEmpty())
        {
            return FAgenticLinkToolResult::Failure(TEXT("Missing 'class': e.g. TargetPoint or /Script/Engine.TargetPoint."));
        }
        UClass* Class = FAgenticLinkEngineTools::FindActorClass(ClassName);
        if (!Class)
        {
            return FAgenticLinkToolResult::Failure(FString::Printf(TEXT("'%s' is not a spawnable actor class."), *ClassName));
        }
        FVector Location = FVector::ZeroVector;
        FVector RotationValues = FVector::ZeroVector;
        if (!ReadTriple(Arguments, TEXT("location"), Location) || !ReadTriple(Arguments, TEXT("rotation"), RotationValues))
        {
            return FAgenticLinkToolResult::Failure(TEXT("'location' and 'rotation' are arrays of three numbers."));
        }
        const FRotator Rotation(RotationValues.X, RotationValues.Y, RotationValues.Z);

        FEditScope Edit(FString::Printf(TEXT("Agent spawns %s"), *Class->GetName()), World->PersistentLevel);
        FActorSpawnParameters SpawnParameters;
        SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AActor* Actor = World->SpawnActor(Class, &Location, &Rotation, SpawnParameters);
        if (!Actor)
        {
            Edit.Cancel();
            return FAgenticLinkToolResult::Failure(FString::Printf(TEXT("%s could not be spawned."), *Class->GetName()));
        }
#if WITH_EDITOR
        FString Label;
        if (Arguments->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty())
        {
            Actor->SetActorLabel(Label);
        }
#endif

        TSharedPtr<FJsonObject> Result = DescribeActor(Actor);
        Result->SetBoolField(TEXT("undoable"), Edit.IsTransacted());
        return FAgenticLinkToolResult::Success(Result);
    }

    /** A tool handler bound to the world the resolver picks when it runs. */
    TFunction<FAgenticLinkToolResult(const TSharedPtr<FJsonObject>&)> Bind(
        TFunction<UWorld*()> WorldResolver, FAgenticLinkToolResult (*Handler)(UWorld*, const TSharedPtr<FJsonObject>&))
    {
        return [WorldResolver, Handler](const TSharedPtr<FJsonObject>& Arguments)
        {
            UWorld* World = WorldResolver ? WorldResolver() : FAgenticLinkEngineTools::FindDefaultWorld();
            return Handler(World, Arguments);
        };
    }
}

void FAgenticLinkEngineTools::Register(FAgenticLinkMcpServer& Server, TFunction<UWorld*()> WorldResolver)
{
    using namespace AgenticLinkTools;

    FAgenticLinkTool ListActorsTool;
    ListActorsTool.Name = TEXT("list_actors");
    ListActorsTool.Description = TEXT("Lists the actors in the open level (the PIE world while playing): name, label, class, path and location.");
    ListActorsTool.InputSchema = MakeSchema({
        { TEXT("class"), TEXT("string"), TEXT("Only actors of this class or a subclass, by class name, e.g. TargetPoint."), false },
        { TEXT("limit"), TEXT("integer"), TEXT("At most this many actors (default 200)."), false } });
    ListActorsTool.Handler = Bind(WorldResolver, &ListActors);
    Server.RegisterTool(ListActorsTool);

    FAgenticLinkTool GetPropertyTool;
    GetPropertyTool.Name = TEXT("get_property");
    GetPropertyTool.Description = TEXT("Reads an editable or Blueprint-visible property of an actor, in Unreal's text format.");
    GetPropertyTool.InputSchema = MakeSchema({
        { TEXT("actor"), TEXT("string"), TEXT("Actor name, label or path (see list_actors)."), true },
        { TEXT("property"), TEXT("string"), TEXT("Property name, e.g. Tags."), true } });
    GetPropertyTool.Handler = Bind(WorldResolver, &GetProperty);
    Server.RegisterTool(GetPropertyTool);

    FAgenticLinkTool SetPropertyTool;
    SetPropertyTool.Name = TEXT("set_property");
    SetPropertyTool.Description = TEXT("Sets an instance-editable property of an actor as one undoable editor transaction.");
    SetPropertyTool.InputSchema = MakeSchema({
        { TEXT("actor"), TEXT("string"), TEXT("Actor name, label or path (see list_actors)."), true },
        { TEXT("property"), TEXT("string"), TEXT("Property name, e.g. Tags."), true },
        { TEXT("value"), TEXT("string"), TEXT("The new value in Unreal's text format, e.g. (\"Tag\") or (X=1,Y=2,Z=3); numbers and booleans also work."), true } });
    SetPropertyTool.Handler = Bind(WorldResolver, &SetProperty);
    Server.RegisterTool(SetPropertyTool);

    FAgenticLinkTool CallFunctionTool;
    CallFunctionTool.Name = TEXT("call_function");
    CallFunctionTool.Description = TEXT("Calls a BlueprintCallable function on an actor, as one undoable editor transaction, and returns its outputs as text.");
    CallFunctionTool.InputSchema = MakeSchema({
        { TEXT("actor"), TEXT("string"), TEXT("Actor name, label or path (see list_actors)."), true },
        { TEXT("function"), TEXT("string"), TEXT("Function name, e.g. K2_GetActorLocation."), true },
        { TEXT("arguments"), TEXT("object"), TEXT("Parameter name to value (Unreal's text format, a number or a boolean)."), false } });
    CallFunctionTool.Handler = Bind(WorldResolver, &CallFunction);
    Server.RegisterTool(CallFunctionTool);

    FAgenticLinkTool SpawnActorTool;
    SpawnActorTool.Name = TEXT("spawn_actor");
    SpawnActorTool.Description = TEXT("Spawns an actor in the open level as one undoable editor transaction.");
    SpawnActorTool.InputSchema = MakeSchema({
        { TEXT("class"), TEXT("string"), TEXT("Actor class name (TargetPoint) or path (/Script/Engine.TargetPoint, /Game/.../BP_X.BP_X_C)."), true },
        { TEXT("location"), TEXT("array"), TEXT("[x, y, z] in cm (default origin)."), false },
        { TEXT("rotation"), TEXT("array"), TEXT("[pitch, yaw, roll] in degrees (default none)."), false },
        { TEXT("label"), TEXT("string"), TEXT("Outliner label (editor only)."), false } });
    SpawnActorTool.Handler = Bind(WorldResolver, &SpawnActor);
    Server.RegisterTool(SpawnActorTool);
}

UWorld* FAgenticLinkEngineTools::FindDefaultWorld()
{
    if (!GEngine)
    {
        return nullptr;
    }
    UWorld* EditorWorld = nullptr;
    UWorld* GameWorld = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        UWorld* ContextWorld = Context.World();
        if (!ContextWorld)
        {
            continue;
        }
        if (Context.WorldType == EWorldType::PIE)
        {
            return ContextWorld;
        }
        if (Context.WorldType == EWorldType::Editor && !EditorWorld)
        {
            EditorWorld = ContextWorld;
        }
        else if (Context.WorldType == EWorldType::Game && !GameWorld)
        {
            GameWorld = ContextWorld;
        }
    }
    return EditorWorld ? EditorWorld : GameWorld;
}

AActor* FAgenticLinkEngineTools::FindActor(UWorld* World, const FString& ActorName)
{
    if (!World || ActorName.IsEmpty())
    {
        return nullptr;
    }
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (Actor->GetName() == ActorName || Actor->GetPathName() == ActorName)
        {
            return Actor;
        }
#if WITH_EDITOR
        if (Actor->GetActorLabel() == ActorName)
        {
            return Actor;
        }
#endif
    }
    return nullptr;
}

UClass* FAgenticLinkEngineTools::FindActorClass(const FString& ClassName)
{
    UClass* Class = ClassName.StartsWith(TEXT("/"))
        ? StaticLoadClass(AActor::StaticClass(), nullptr, *ClassName, nullptr, LOAD_NoWarn | LOAD_Quiet)
        : FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
    const bool bSpawnable = Class && Class->IsChildOf(AActor::StaticClass())
        && !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists);
    return bSpawnable ? Class : nullptr;
}

bool FAgenticLinkEngineTools::JsonValueToText(const TSharedPtr<FJsonValue>& Value, FString& OutText)
{
    if (!Value.IsValid())
    {
        return false;
    }
    switch (Value->Type)
    {
    case EJson::String:
        OutText = Value->AsString();
        return true;
    case EJson::Boolean:
        OutText = Value->AsBool() ? TEXT("True") : TEXT("False");
        return true;
    case EJson::Number:
    {
        const double Number = Value->AsNumber();
        const double Whole = FMath::RoundToDouble(Number);
        OutText = (Number == Whole && FMath::Abs(Whole) < 9.0e15)
            ? FString::Printf(TEXT("%lld"), static_cast<int64>(Whole))
            : FString::SanitizeFloat(Number);
        return true;
    }
    default:
        return false;
    }
}
