// AgenticLinkHttpTransport.cpp - Epic 25: MCP over Streamable HTTP, on the engine's HTTP server
#include "AgenticLinkHttpTransport.h"
#include "AgenticLinkMcpServer.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"

namespace AgenticLinkHttp
{
    FString FindHeader(const FHttpServerRequest& Request, const TCHAR* Name)
    {
        for (const TPair<FString, TArray<FString>>& Header : Request.Headers)
        {
            if (Header.Key.Equals(Name, ESearchCase::IgnoreCase) && Header.Value.Num() > 0)
            {
                return Header.Value[0];
            }
        }
        return FString();
    }

    FString BodyToString(const TArray<uint8>& Body)
    {
        if (Body.Num() == 0)
        {
            return FString();
        }
        FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Body.GetData()), Body.Num());
        return FString(Converted.Length(), Converted.Get());
    }
}

FAgenticLinkHttpTransport::FAgenticLinkHttpTransport(TSharedRef<FAgenticLinkMcpServer> InServer)
    : Server(InServer)
{
}

FAgenticLinkHttpTransport::~FAgenticLinkHttpTransport()
{
    Stop();
}

const TCHAR* FAgenticLinkHttpTransport::GetEndpointPath()
{
    return TEXT("/mcp");
}

bool FAgenticLinkHttpTransport::Start(uint32 Port)
{
    Stop();
    Router = FHttpServerModule::Get().GetHttpRouter(Port, /* bFailOnBindFailure */ true);
    if (!Router.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("AgenticLink: port %u is not available; the MCP server is off."), Port);
        return false;
    }

    RouteHandle = Router->BindRoute(FHttpPath(GetEndpointPath()), EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_GET,
        FHttpRequestHandler::CreateRaw(this, &FAgenticLinkHttpTransport::HandleRequest));
    if (!RouteHandle.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("AgenticLink: %s is already bound on port %u; the MCP server is off."), GetEndpointPath(), Port);
        Router.Reset();
        return false;
    }

    FHttpServerModule::Get().StartAllListeners();
    UE_LOG(LogTemp, Display, TEXT("AgenticLink: MCP server listening at http://127.0.0.1:%u%s"), Port, GetEndpointPath());
    return true;
}

void FAgenticLinkHttpTransport::Stop()
{
    if (Router.IsValid() && RouteHandle.IsValid())
    {
        Router->UnbindRoute(RouteHandle);
    }
    RouteHandle.Reset();
    Router.Reset();
}

bool FAgenticLinkHttpTransport::HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
    if (!IsAllowedOrigin(AgenticLinkHttp::FindHeader(Request, TEXT("Origin"))))
    {
        OnComplete(FHttpServerResponse::Error(EHttpServerResponseCodes::Forbidden, TEXT("forbidden_origin"), TEXT("AgenticLink answers only pages on localhost.")));
        return true;
    }
    if (Request.Verb != EHttpServerRequestVerbs::VERB_POST)
    {
        TUniquePtr<FHttpServerResponse> Refusal = FHttpServerResponse::Error(EHttpServerResponseCodes::BadMethod, TEXT("method_not_allowed"),
            TEXT("POST one JSON-RPC message; this server opens no event stream."));
        TArray<FString> Allowed;
        Allowed.Add(TEXT("POST"));
        Refusal->Headers.Add(TEXT("Allow"), Allowed);
        OnComplete(MoveTemp(Refusal));
        return true;
    }

    const FString Answer = Server->HandleMessage(AgenticLinkHttp::BodyToString(Request.Body));
    if (Answer.IsEmpty())
    {
        TUniquePtr<FHttpServerResponse> Acknowledged = MakeUnique<FHttpServerResponse>();
        Acknowledged->Code = EHttpServerResponseCodes::Accepted;
        OnComplete(MoveTemp(Acknowledged));
        return true;
    }
    OnComplete(FHttpServerResponse::Create(Answer, TEXT("application/json")));
    return true;
}

bool FAgenticLinkHttpTransport::IsAllowedOrigin(const FString& Origin)
{
    if (Origin.IsEmpty())
    {
        return true;
    }
    FString Scheme;
    FString Authority;
    if (!Origin.Split(TEXT("://"), &Scheme, &Authority) || !(Scheme.Equals(TEXT("http"), ESearchCase::IgnoreCase) || Scheme.Equals(TEXT("https"), ESearchCase::IgnoreCase)))
    {
        return false;
    }

    FString Host = Authority;
    if (Host.StartsWith(TEXT("[")))
    {
        int32 Close = INDEX_NONE;
        if (!Host.FindChar(TEXT(']'), Close))
        {
            return false;
        }
        Host = Host.Mid(1, Close - 1);
    }
    else
    {
        int32 Colon = INDEX_NONE;
        if (Host.FindChar(TEXT(':'), Colon))
        {
            Host = Host.Left(Colon);
        }
    }
    return Host.Equals(TEXT("localhost"), ESearchCase::IgnoreCase) || Host == TEXT("127.0.0.1") || Host == TEXT("::1");
}
