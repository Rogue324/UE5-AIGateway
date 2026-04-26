#pragma once

#include "AIEditorAssistantSettings.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IHttpRequest.h"

struct FAIEditorAssistantChatServiceSettings
{
    FString BaseUrl;
    FString ApiKey;
    FString Model;
    EAIEditorAssistantAPIProvider Provider = EAIEditorAssistantAPIProvider::OpenAI;
    EAIEditorAssistantReasoningIntensity ReasoningIntensity = EAIEditorAssistantReasoningIntensity::ProviderDefault;
};

struct FAIEditorAssistantChatCompletionRequest
{
    TArray<TSharedPtr<FJsonValue>> Messages;
    TArray<TSharedPtr<FJsonValue>> Tools;
    FString ToolChoice;
    bool bStream = true;
    int32 MaxTokens = 0;
};

struct FAIEditorAssistantChatServiceResponse
{
    bool bRequestSucceeded = false;
    int32 ResponseCode = 0;
    FString ResponseBody;
};

struct FAIEditorAssistantModelListResponse
{
    bool bRequestSucceeded = false;
    int32 ResponseCode = 0;
    TArray<FString> Models;
    FString ErrorMessage;
};

struct FAIEditorAssistantReasoningOptionsResponse
{
    bool bRequestSucceeded = false;
    int32 ResponseCode = 0;
    TArray<EAIEditorAssistantReasoningIntensity> Options;
    FString ErrorMessage;
};

class IAIEditorAssistantChatService
{
public:
    using FStreamChunkCallback = TFunction<void(const FString&)>;
    using FRequestCompleteCallback = TFunction<void(const FAIEditorAssistantChatServiceResponse&)>;
    using FModelListCompleteCallback = TFunction<void(const FAIEditorAssistantModelListResponse&)>;
    using FReasoningOptionsCompleteCallback = TFunction<void(const FAIEditorAssistantReasoningOptionsResponse&)>;

    virtual ~IAIEditorAssistantChatService() = default;

    virtual bool SendStreamingChatRequest(
        const FAIEditorAssistantChatServiceSettings& Settings,
        const FAIEditorAssistantChatCompletionRequest& Request,
        FStreamChunkCallback&& OnStreamChunk,
        FRequestCompleteCallback&& OnComplete) = 0;
    virtual bool FetchAvailableModels(
        const FAIEditorAssistantChatServiceSettings& Settings,
        FModelListCompleteCallback&& OnComplete) = 0;
    virtual bool FetchReasoningOptions(
        const FAIEditorAssistantChatServiceSettings& Settings,
        const FString& Model,
        FReasoningOptionsCompleteCallback&& OnComplete) = 0;

    virtual void CancelActiveRequests() = 0;
};

class FAIEditorAssistantOpenAIChatService : public IAIEditorAssistantChatService, public TSharedFromThis<FAIEditorAssistantOpenAIChatService>
{
public:
    virtual bool SendStreamingChatRequest(
        const FAIEditorAssistantChatServiceSettings& Settings,
        const FAIEditorAssistantChatCompletionRequest& Request,
        FStreamChunkCallback&& OnStreamChunk,
        FRequestCompleteCallback&& OnComplete) override;
    virtual bool FetchAvailableModels(
        const FAIEditorAssistantChatServiceSettings& Settings,
        FModelListCompleteCallback&& OnComplete) override;
    virtual bool FetchReasoningOptions(
        const FAIEditorAssistantChatServiceSettings& Settings,
        const FString& Model,
        FReasoningOptionsCompleteCallback&& OnComplete) override;

    virtual void CancelActiveRequests() override;

private:
    struct FPendingServiceRequest;

    bool BeginRequest(
        const FAIEditorAssistantChatServiceSettings& Settings,
        const FAIEditorAssistantChatCompletionRequest& Request,
        const FString& AcceptHeader,
        FStreamChunkCallback&& OnStreamChunk,
        FRequestCompleteCallback&& OnComplete);
    bool BeginFetchModelsRequest(
        const FAIEditorAssistantChatServiceSettings& Settings,
        FModelListCompleteCallback&& OnComplete);
    bool BeginFetchReasoningOptionsRequest(
        const FAIEditorAssistantChatServiceSettings& Settings,
        const FString& Model,
        FReasoningOptionsCompleteCallback&& OnComplete);

    void HandleRequestProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived);
    void HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
    TSharedPtr<FPendingServiceRequest> FindRequestContext(const FHttpRequestPtr& Request) const;
    void RemoveRequestContext(const FHttpRequestPtr& Request);

    TArray<TSharedPtr<FPendingServiceRequest>> ActiveRequests;
};
