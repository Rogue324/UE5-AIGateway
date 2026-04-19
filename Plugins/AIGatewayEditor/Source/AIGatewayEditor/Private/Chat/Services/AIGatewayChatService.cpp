#include "Chat/Services/AIGatewayChatService.h"

#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

struct FAIGatewayOpenAIChatService::FPendingServiceRequest
{
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest;
    FStreamChunkCallback OnStreamChunk;
    FRequestCompleteCallback OnComplete;
    int32 ProcessedLength = 0;
};

namespace
{
    FString BuildRequestBody(const FAIGatewayChatServiceSettings& Settings, const FAIGatewayChatCompletionRequest& Request)
    {
        TSharedPtr<FJsonObject> RequestBodyObject = MakeShared<FJsonObject>();
        RequestBodyObject->SetStringField(TEXT("model"), Settings.Model);
        RequestBodyObject->SetBoolField(TEXT("stream"), Request.bStream);
        RequestBodyObject->SetArrayField(TEXT("messages"), Request.Messages);

        if (Request.Tools.Num() > 0)
        {
            RequestBodyObject->SetArrayField(TEXT("tools"), Request.Tools);
        }

        if (Request.MaxTokens > 0)
        {
            RequestBodyObject->SetNumberField(TEXT("max_tokens"), Request.MaxTokens);
        }

        FString RequestBody;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
        FJsonSerializer::Serialize(RequestBodyObject.ToSharedRef(), Writer);
        return RequestBody;
    }
}

bool FAIGatewayOpenAIChatService::SendStreamingChatRequest(
    const FAIGatewayChatServiceSettings& Settings,
    const FAIGatewayChatCompletionRequest& Request,
    FStreamChunkCallback&& OnStreamChunk,
    FRequestCompleteCallback&& OnComplete)
{
    return BeginRequest(Settings, Request, TEXT("text/event-stream, application/json"), MoveTemp(OnStreamChunk), MoveTemp(OnComplete));
}

bool FAIGatewayOpenAIChatService::BeginRequest(
    const FAIGatewayChatServiceSettings& Settings,
    const FAIGatewayChatCompletionRequest& Request,
    const FString& AcceptHeader,
    FStreamChunkCallback&& OnStreamChunk,
    FRequestCompleteCallback&& OnComplete)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(Settings.BaseUrl + TEXT("/chat/completions"));
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    HttpRequest->SetHeader(TEXT("Accept"), AcceptHeader);
    HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Settings.ApiKey));
    HttpRequest->SetContentAsString(BuildRequestBody(Settings, Request));
    HttpRequest->OnRequestProgress64().BindSP(AsShared(), &FAIGatewayOpenAIChatService::HandleRequestProgress);
    HttpRequest->OnProcessRequestComplete().BindSP(AsShared(), &FAIGatewayOpenAIChatService::HandleRequestComplete);

    TSharedPtr<FPendingServiceRequest> Context = MakeShared<FPendingServiceRequest>();
    Context->HttpRequest = HttpRequest;
    Context->OnStreamChunk = MoveTemp(OnStreamChunk);
    Context->OnComplete = MoveTemp(OnComplete);
    ActiveRequests.Add(Context);

    if (!HttpRequest->ProcessRequest())
    {
        RemoveRequestContext(HttpRequest);
        return false;
    }

    return true;
}

void FAIGatewayOpenAIChatService::HandleRequestProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived)
{
    TSharedPtr<FPendingServiceRequest> Context = FindRequestContext(Request);
    if (!Context.IsValid() || !Context->OnStreamChunk)
    {
        return;
    }

    const FHttpResponsePtr Response = Request->GetResponse();
    if (!Response.IsValid())
    {
        return;
    }

    const FString CurrentContent = Response->GetContentAsString();
    if (CurrentContent.Len() <= Context->ProcessedLength)
    {
        return;
    }

    const FString NewChunk = CurrentContent.Mid(Context->ProcessedLength);
    Context->ProcessedLength = CurrentContent.Len();
    Context->OnStreamChunk(NewChunk);
}

void FAIGatewayOpenAIChatService::HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    TSharedPtr<FPendingServiceRequest> Context = FindRequestContext(Request);
    if (!Context.IsValid())
    {
        return;
    }

    if (Context->OnStreamChunk && Response.IsValid())
    {
        const FString CurrentContent = Response->GetContentAsString();
        if (CurrentContent.Len() > Context->ProcessedLength)
        {
            Context->OnStreamChunk(CurrentContent.Mid(Context->ProcessedLength));
            Context->ProcessedLength = CurrentContent.Len();
        }
    }

    FAIGatewayChatServiceResponse ServiceResponse;
    ServiceResponse.bRequestSucceeded = bWasSuccessful && Response.IsValid();
    if (Response.IsValid())
    {
        ServiceResponse.ResponseCode = Response->GetResponseCode();
        ServiceResponse.ResponseBody = Response->GetContentAsString();
    }

    FRequestCompleteCallback CompletionCallback = MoveTemp(Context->OnComplete);
    RemoveRequestContext(Request);

    if (CompletionCallback)
    {
        CompletionCallback(ServiceResponse);
    }
}

TSharedPtr<FAIGatewayOpenAIChatService::FPendingServiceRequest> FAIGatewayOpenAIChatService::FindRequestContext(const FHttpRequestPtr& Request) const
{
    const TSharedPtr<FPendingServiceRequest>* FoundContext = ActiveRequests.FindByPredicate([&Request](const TSharedPtr<FPendingServiceRequest>& Context)
    {
        return Context.IsValid() && Context->HttpRequest == Request;
    });

    return FoundContext != nullptr ? *FoundContext : nullptr;
}

void FAIGatewayOpenAIChatService::RemoveRequestContext(const FHttpRequestPtr& Request)
{
    ActiveRequests.RemoveAll([&Request](const TSharedPtr<FPendingServiceRequest>& Context)
    {
        return Context.IsValid() && Context->HttpRequest == Request;
    });
}
