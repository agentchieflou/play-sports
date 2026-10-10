// AgenticLinkHttpTransport.h - Epic 25: MCP over Streamable HTTP, on the engine's HTTP server
#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "HttpServerRequest.h"

class FAgenticLinkMcpServer;
class IHttpRouter;

/**
 * FAgenticLinkHttpTransport serves an FAgenticLinkMcpServer at http://127.0.0.1:<port>/mcp, the
 * MCP Streamable HTTP transport without server-sent streams:
 *
 *  - POST carries one JSON-RPC message. A request is answered 200 with application/json; a
 *    notification or a response is acknowledged 202 with no body.
 *  - GET (a server-to-client stream) is refused with 405, which the protocol allows.
 *  - A request whose Origin header isn't localhost is refused with 403, against DNS rebinding.
 *
 * The engine's HTTP server runs on the game thread, so every tool call does too. Which address
 * it binds comes from [HTTPServer.Listeners] in the engine config; the project's
 * DefaultEngine.ini pins it to 127.0.0.1.
 */
class AGENTICLINK_API FAgenticLinkHttpTransport
{
public:
    static constexpr uint32 DefaultPort = 8790;

    explicit FAgenticLinkHttpTransport(TSharedRef<FAgenticLinkMcpServer> InServer);
    ~FAgenticLinkHttpTransport();

    /** Binds /mcp on Port and starts listening. False when the port can't be had. */
    bool Start(uint32 Port);

    void Stop();

    bool IsListening() const { return RouteHandle.IsValid(); }

    /** The /mcp route; public so it can be exercised without a socket. */
    bool HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

    /** True for no Origin, or an http(s) origin on localhost / 127.0.0.1 / [::1]. */
    static bool IsAllowedOrigin(const FString& Origin);

    /** The endpoint's path, "/mcp". */
    static const TCHAR* GetEndpointPath();

private:
    TSharedRef<FAgenticLinkMcpServer> Server;
    TSharedPtr<IHttpRouter> Router;
    FHttpRouteHandle RouteHandle;
};
