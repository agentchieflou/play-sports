// AgenticLinkMcpServer.h - Epic 25: the Model Context Protocol endpoint into the engine
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/** What a tool handler hands back: a JSON result, or an error the caller can act on. */
struct AGENTICLINK_API FAgenticLinkToolResult
{
    TSharedPtr<FJsonObject> Result;
    FString Error;

    static FAgenticLinkToolResult Success(const TSharedPtr<FJsonObject>& InResult);
    static FAgenticLinkToolResult Failure(const FString& InError);

    bool IsError() const { return !Error.IsEmpty(); }
};

/** One MCP tool: its name, what it does, a JSON Schema for its arguments, and the handler. */
struct AGENTICLINK_API FAgenticLinkTool
{
    FString Name;
    FString Description;
    TSharedPtr<FJsonObject> InputSchema;
    TFunction<FAgenticLinkToolResult(const TSharedPtr<FJsonObject>& Arguments)> Handler;
};

/**
 * FAgenticLinkMcpServer speaks the Model Context Protocol (JSON-RPC 2.0) for the tools
 * registered on it. It is transport-free: FAgenticLinkHttpTransport feeds it the body of each
 * POST to /mcp and returns what it answers.
 *
 *  - initialize answers with the client's protocol version when it is one of
 *    GetSupportedProtocolVersions(), otherwise with the latest, and advertises tools.
 *  - ping, tools/list and tools/call are served; notifications get no answer.
 *  - A tool that fails answers with isError and its message (the agent can correct and retry);
 *    an unknown tool, method or malformed message is a JSON-RPC error.
 *  - Batches (a JSON array) are refused, as the 2025-06-18 revision of the protocol does.
 */
class AGENTICLINK_API FAgenticLinkMcpServer
{
public:
    /** JSON-RPC error codes the server answers with. */
    static constexpr int32 ParseError = -32700;
    static constexpr int32 InvalidRequest = -32600;
    static constexpr int32 MethodNotFound = -32601;
    static constexpr int32 InvalidParams = -32602;

    /** Newest first. */
    static TArray<FString> GetSupportedProtocolVersions();

    /** Adds Tool, replacing a tool of the same name. */
    void RegisterTool(const FAgenticLinkTool& Tool);

    /** Takes the tool named ToolName off the list; false when there was none. */
    bool UnregisterTool(const FString& ToolName);

    const TArray<FAgenticLinkTool>& GetTools() const { return Tools; }

    /** Handles one JSON-RPC message and returns the answer as JSON text, or an empty string
     *  when the message was a notification (or a response) that needs none. */
    FString HandleMessage(const FString& MessageJson);

    /** True once a client has completed initialize. */
    bool IsInitialized() const { return bInitialized; }

    /** The protocol version agreed at initialize (empty before). */
    const FString& GetNegotiatedProtocolVersion() const { return NegotiatedProtocolVersion; }

private:
    TSharedPtr<FJsonObject> HandleInitialize(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleToolsList() const;
    TSharedPtr<FJsonObject> HandleToolsCall(const TSharedPtr<FJsonObject>& Params, FString& OutError) const;

    static FString MakeResult(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result);
    static FString MakeError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message);

    TArray<FAgenticLinkTool> Tools;
    FString NegotiatedProtocolVersion;
    bool bInitialized = false;
};

/** Serializes a JSON object as compact text. */
AGENTICLINK_API FString AgenticLinkJsonToString(const TSharedPtr<FJsonObject>& Object);
