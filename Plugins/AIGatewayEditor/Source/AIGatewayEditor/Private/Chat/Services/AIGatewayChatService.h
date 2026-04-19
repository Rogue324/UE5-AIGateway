#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IHttpRequest.h"

struct FAIGatewayChatServiceSettings
{
    FString BaseUrl;
    FString ApiKey;
    FString Model;
};

struct FAIGatewayChatCompletionRequest
{
    TArray<TSharedPtr<FJsonValue>> Messages;
    TArray<TSharedPtr<FJsonValue>> Tools;
    bool bStream = true;
    int32 MaxTokens = 0;
};

struct FAIGatewayChatServiceResponse
{
    bool bRequestSucceeded = false;
    int32 ResponseCode = 0;
    FString ResponseBody;
};

class IAIGatewayChatService
{
public:
    using FStreamChunkCallback = TFunction<void(const FString&)>;
    using FRequestCompleteCallback = TFunction<void(const FAIGatewayChatServiceResponse&)>;

    virtual ~IAIGatewayChatService() = default;

    virtual bool SendStreamingChatRequest(
        const FAIGatewayChatServiceSettings& Settings,
        const FAIGatewayChatCompletionRequest& Request,
        FStreamChunkCallback&& OnStreamChunk,
        FRequestCompleteCallback&& OnComplete) = 0;
};

class FAIGatewayOpenAIChatService : public IAIGatewayChatService, public TSharedFromThis<FAIGatewayOpenAIChatService>
{
public:
    virtual bool SendStreamingChatRequest(
        const FAIGatewayChatServiceSettings& Settings,
        const FAIGatewayChatCompletionRequest& Request,
        FStreamChunkCallback&& OnStreamChunk,
        FRequestCompleteCallback&& OnComplete) override;

private:
    struct FPendingServiceRequest;

    bool BeginRequest(
        const FAIGatewayChatServiceSettings& Settings,
        const FAIGatewayChatCompletionRequest& Request,
        const FString& AcceptHeader,
        FStreamChunkCallback&& OnStreamChunk,
        FRequestCompleteCallback&& OnComplete);

    void HandleRequestProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived);
    void HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
    TSharedPtr<FPendingServiceRequest> FindRequestContext(const FHttpRequestPtr& Request) const;
    void RemoveRequestContext(const FHttpRequestPtr& Request);

    TArray<TSharedPtr<FPendingServiceRequest>> ActiveRequests;
};
