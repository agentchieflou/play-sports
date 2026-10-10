// AgenticLinkMcpServer.cpp - Epic 25: the Model Context Protocol endpoint into the engine
#include "AgenticLinkMcpServer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace AgenticLinkMcp
{
    const TCHAR* const ServerName = TEXT("AgenticLink");
    const TCHAR* const ServerVersion = TEXT("1.0");
    const TCHAR* const Instructions = TEXT(
        "Unreal Editor bridge for play-sports. list_actors finds actors in the open level (the PIE world "
        "while playing); get_property, set_property and call_function work on one actor by name; "
        "spawn_actor places one. Edits are undoable editor transactions. Property values use Unreal's "
        "text format, e.g. (X=1,Y=2,Z=3) or (\"Tag\").");
}

FAgenticLinkToolResult FAgenticLinkToolResult::Success(const TSharedPtr<FJsonObject>& InResult)
{
    FAgenticLinkToolResult Outcome;
    Outcome.Result = InResult.IsValid() ? InResult : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>());
    return Outcome;
}

FAgenticLinkToolResult FAgenticLinkToolResult::Failure(const FString& InError)
{
    FAgenticLinkToolResult Outcome;
    Outcome.Error = InError.IsEmpty() ? FString(TEXT("The tool failed.")) : InError;
    return Outcome;
}

FString AgenticLinkJsonToString(const TSharedPtr<FJsonObject>& Object)
{
    FString Text;
    if (Object.IsValid())
    {
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
        FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
    }
    return Text;
}

TArray<FString> FAgenticLinkMcpServer::GetSupportedProtocolVersions()
{
    return { TEXT("2025-06-18"), TEXT("2025-03-26"), TEXT("2024-11-05") };
}

void FAgenticLinkMcpServer::RegisterTool(const FAgenticLinkTool& Tool)
{
    Tools.RemoveAll([&Tool](const FAgenticLinkTool& Existing) { return Existing.Name == Tool.Name; });
    Tools.Add(Tool);
}

bool FAgenticLinkMcpServer::UnregisterTool(const FString& ToolName)
{
    return Tools.RemoveAll([&ToolName](const FAgenticLinkTool& Existing) { return Existing.Name == ToolName; }) > 0;
}

FString FAgenticLinkMcpServer::HandleMessage(const FString& MessageJson)
{
    TSharedPtr<FJsonValue> Parsed;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MessageJson);
    if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
    {
        return MakeError(MakeShared<FJsonValueNull>(), ParseError, TEXT("Parse error: the body is not JSON."));
    }
    if (Parsed->Type == EJson::Array)
    {
        return MakeError(MakeShared<FJsonValueNull>(), InvalidRequest, TEXT("Batches are not supported; send one message per request."));
    }

    const TSharedPtr<FJsonObject>* MessagePtr = nullptr;
    if (!Parsed->TryGetObject(MessagePtr) || !MessagePtr || !MessagePtr->IsValid())
    {
        return MakeError(MakeShared<FJsonValueNull>(), InvalidRequest, TEXT("A message must be a JSON object."));
    }
    const TSharedPtr<FJsonObject>& Message = *MessagePtr;

    const TSharedPtr<FJsonValue> Id = Message->TryGetField(TEXT("id"));
    const bool bHasId = Id.IsValid() && !Id->IsNull();
    FString Method;
    if (!Message->TryGetStringField(TEXT("method"), Method))
    {
        // A response from the client (result/error with an id) needs no answer.
        if (bHasId && (Message->HasField(TEXT("result")) || Message->HasField(TEXT("error"))))
        {
            return FString();
        }
        return MakeError(bHasId ? Id : TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>()), InvalidRequest, TEXT("Invalid request: no method."));
    }

    FString Version;
    if (!Message->TryGetStringField(TEXT("jsonrpc"), Version) || Version != TEXT("2.0"))
    {
        return bHasId ? MakeError(Id, InvalidRequest, TEXT("Invalid request: jsonrpc must be \"2.0\".")) : FString();
    }

    const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
    const TSharedPtr<FJsonObject> Params = Message->TryGetObjectField(TEXT("params"), ParamsPtr) && ParamsPtr ? *ParamsPtr : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>());

    if (!bHasId)
    {
        // Notifications (notifications/initialized, notifications/cancelled, ...) get no answer.
        return FString();
    }

    if (Method == TEXT("initialize"))
    {
        return MakeResult(Id, HandleInitialize(Params));
    }
    if (Method == TEXT("ping"))
    {
        return MakeResult(Id, MakeShared<FJsonObject>());
    }
    if (Method == TEXT("tools/list"))
    {
        return MakeResult(Id, HandleToolsList());
    }
    if (Method == TEXT("tools/call"))
    {
        FString Error;
        const TSharedPtr<FJsonObject> Result = HandleToolsCall(Params, Error);
        return Result.IsValid() ? MakeResult(Id, Result) : MakeError(Id, InvalidParams, Error);
    }
    return MakeError(Id, MethodNotFound, FString::Printf(TEXT("Method not found: %s"), *Method));
}

TSharedPtr<FJsonObject> FAgenticLinkMcpServer::HandleInitialize(const TSharedPtr<FJsonObject>& Params)
{
    const TArray<FString> Supported = GetSupportedProtocolVersions();
    FString Requested;
    Params->TryGetStringField(TEXT("protocolVersion"), Requested);
    NegotiatedProtocolVersion = Supported.Contains(Requested) ? Requested : Supported[0];
    bInitialized = true;

    TSharedPtr<FJsonObject> ToolsCapability = MakeShared<FJsonObject>();
    ToolsCapability->SetBoolField(TEXT("listChanged"), false);
    TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
    Capabilities->SetObjectField(TEXT("tools"), ToolsCapability);

    TSharedPtr<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
    ServerInfo->SetStringField(TEXT("name"), AgenticLinkMcp::ServerName);
    ServerInfo->SetStringField(TEXT("version"), AgenticLinkMcp::ServerVersion);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("protocolVersion"), NegotiatedProtocolVersion);
    Result->SetObjectField(TEXT("capabilities"), Capabilities);
    Result->SetObjectField(TEXT("serverInfo"), ServerInfo);
    Result->SetStringField(TEXT("instructions"), AgenticLinkMcp::Instructions);
    return Result;
}

TSharedPtr<FJsonObject> FAgenticLinkMcpServer::HandleToolsList() const
{
    TArray<TSharedPtr<FJsonValue>> ToolValues;
    for (const FAgenticLinkTool& Tool : Tools)
    {
        TSharedPtr<FJsonObject> ToolObject = MakeShared<FJsonObject>();
        ToolObject->SetStringField(TEXT("name"), Tool.Name);
        ToolObject->SetStringField(TEXT("description"), Tool.Description);
        TSharedPtr<FJsonObject> Schema = Tool.InputSchema;
        if (!Schema.IsValid())
        {
            Schema = MakeShared<FJsonObject>();
            Schema->SetStringField(TEXT("type"), TEXT("object"));
        }
        ToolObject->SetObjectField(TEXT("inputSchema"), Schema);
        ToolValues.Add(MakeShared<FJsonValueObject>(ToolObject));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("tools"), ToolValues);
    return Result;
}

TSharedPtr<FJsonObject> FAgenticLinkMcpServer::HandleToolsCall(const TSharedPtr<FJsonObject>& Params, FString& OutError) const
{
    FString ToolName;
    if (!Params->TryGetStringField(TEXT("name"), ToolName))
    {
        OutError = TEXT("tools/call needs a tool name.");
        return nullptr;
    }
    const FAgenticLinkTool* Tool = Tools.FindByPredicate([&ToolName](const FAgenticLinkTool& Candidate) { return Candidate.Name == ToolName; });
    if (!Tool || !Tool->Handler)
    {
        OutError = FString::Printf(TEXT("Unknown tool: %s"), *ToolName);
        return nullptr;
    }

    const TSharedPtr<FJsonObject>* ArgumentsPtr = nullptr;
    const TSharedPtr<FJsonObject> Arguments = Params->TryGetObjectField(TEXT("arguments"), ArgumentsPtr) && ArgumentsPtr ? *ArgumentsPtr : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>());
    const FAgenticLinkToolResult Outcome = Tool->Handler(Arguments);

    // The text block carries the result for every client; structuredContent is the same
    // object for clients on 2025-06-18 and later.
    TSharedPtr<FJsonObject> TextBlock = MakeShared<FJsonObject>();
    TextBlock->SetStringField(TEXT("type"), TEXT("text"));
    TextBlock->SetStringField(TEXT("text"), Outcome.IsError() ? Outcome.Error : AgenticLinkJsonToString(Outcome.Result));
    TArray<TSharedPtr<FJsonValue>> Content;
    Content.Add(MakeShared<FJsonValueObject>(TextBlock));

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("content"), Content);
    Result->SetBoolField(TEXT("isError"), Outcome.IsError());
    if (!Outcome.IsError())
    {
        Result->SetObjectField(TEXT("structuredContent"), Outcome.Result);
    }
    return Result;
}

FString FAgenticLinkMcpServer::MakeResult(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result)
{
    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Response->SetField(TEXT("id"), Id);
    Response->SetObjectField(TEXT("result"), Result.IsValid() ? Result : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
    return AgenticLinkJsonToString(Response);
}

FString FAgenticLinkMcpServer::MakeError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message)
{
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetNumberField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message);

    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Response->SetField(TEXT("id"), Id);
    Response->SetObjectField(TEXT("error"), Error);
    return AgenticLinkJsonToString(Response);
}
