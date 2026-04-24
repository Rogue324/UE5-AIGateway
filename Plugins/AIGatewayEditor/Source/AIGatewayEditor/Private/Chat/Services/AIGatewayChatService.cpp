#include "Chat/Services/AIGatewayChatService.h"

#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogAIGatewayChatService, Log, All);

struct FAIGatewayOpenAIChatService::FPendingServiceRequest
{
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest;
    FStreamChunkCallback OnStreamChunk;
    FRequestCompleteCallback OnComplete;
    int32 ProcessedLength = 0;
};

namespace
{
    constexpr int32 MaxLoggedResponseBodyChars = 2048;

    FString SerializeJsonObject(const TSharedPtr<FJsonObject>& Object)
    {
        if (!Object.IsValid())
        {
            return TEXT("{}");
        }

        FString Output;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
        return Output;
    }

    TSharedPtr<FJsonValue> CloneJsonValueSanitized(const TSharedPtr<FJsonValue>& Value)
    {
        if (!Value.IsValid())
        {
            return MakeShared<FJsonValueNull>();
        }

        const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
        if (Value->TryGetObject(ObjectValue) && ObjectValue != nullptr && (*ObjectValue).IsValid())
        {
            TSharedPtr<FJsonObject> ClonedObject = MakeShared<FJsonObject>();
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ObjectValue)->Values)
            {
                if (Pair.Key.Equals(TEXT("url"), ESearchCase::CaseSensitive))
                {
                    FString UrlText;
                    if (Pair.Value.IsValid() && Pair.Value->TryGetString(UrlText) && UrlText.StartsWith(TEXT("data:"), ESearchCase::CaseSensitive))
                    {
                        const int32 CommaIndex = UrlText.Find(TEXT(","));
                        const FString Prefix = CommaIndex >= 0 ? UrlText.Left(CommaIndex) : UrlText;
                        const int32 PayloadLength = CommaIndex >= 0 ? UrlText.Len() - CommaIndex - 1 : 0;
                        ClonedObject->SetStringField(Pair.Key, FString::Printf(TEXT("%s,<omitted %d chars>"), *Prefix, PayloadLength));
                        continue;
                    }
                }

                ClonedObject->SetField(Pair.Key, CloneJsonValueSanitized(Pair.Value));
            }

            return MakeShared<FJsonValueObject>(ClonedObject);
        }

        const TArray<TSharedPtr<FJsonValue>>* ArrayValue = nullptr;
        if (Value->TryGetArray(ArrayValue) && ArrayValue != nullptr)
        {
            TArray<TSharedPtr<FJsonValue>> ClonedArray;
            for (const TSharedPtr<FJsonValue>& Element : *ArrayValue)
            {
                ClonedArray.Add(CloneJsonValueSanitized(Element));
            }
            return MakeShared<FJsonValueArray>(ClonedArray);
        }

        FString StringValue;
        if (Value->TryGetString(StringValue))
        {
            return MakeShared<FJsonValueString>(StringValue);
        }

        bool BoolValue = false;
        if (Value->TryGetBool(BoolValue))
        {
            return MakeShared<FJsonValueBoolean>(BoolValue);
        }

        double NumberValue = 0.0;
        if (Value->TryGetNumber(NumberValue))
        {
            return MakeShared<FJsonValueNumber>(NumberValue);
        }

        return MakeShared<FJsonValueNull>();
    }

    FString BuildSanitizedRequestBody(const TSharedPtr<FJsonObject>& RequestBodyObject)
    {
        TSharedPtr<FJsonObject> SanitizedObject = MakeShared<FJsonObject>();
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : RequestBodyObject->Values)
        {
            SanitizedObject->SetField(Pair.Key, CloneJsonValueSanitized(Pair.Value));
        }

        return SerializeJsonObject(SanitizedObject);
    }

    void LogRequestDebugSummary(const TSharedPtr<FJsonObject>& RequestBodyObject)
    {
        const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
        int32 MessageCount = 0;
        int32 ImagePartCount = 0;
        int32 DataUrlCount = 0;

        if (RequestBodyObject->TryGetArrayField(TEXT("messages"), Messages) && Messages != nullptr)
        {
            MessageCount = Messages->Num();
            for (const TSharedPtr<FJsonValue>& MessageValue : *Messages)
            {
                const TSharedPtr<FJsonObject>* MessageObject = nullptr;
                if (!MessageValue.IsValid() || !MessageValue->TryGetObject(MessageObject) || MessageObject == nullptr || !(*MessageObject).IsValid())
                {
                    continue;
                }

                const TArray<TSharedPtr<FJsonValue>>* ContentParts = nullptr;
                if (!(*MessageObject)->TryGetArrayField(TEXT("content"), ContentParts) || ContentParts == nullptr)
                {
                    continue;
                }

                for (const TSharedPtr<FJsonValue>& PartValue : *ContentParts)
                {
                    const TSharedPtr<FJsonObject>* PartObject = nullptr;
                    if (!PartValue.IsValid() || !PartValue->TryGetObject(PartObject) || PartObject == nullptr || !(*PartObject).IsValid())
                    {
                        continue;
                    }

                    FString PartType;
                    (*PartObject)->TryGetStringField(TEXT("type"), PartType);
                    if (!PartType.Equals(TEXT("image_url"), ESearchCase::CaseSensitive))
                    {
                        continue;
                    }

                    ++ImagePartCount;

                    const TSharedPtr<FJsonObject>* ImageUrlObject = nullptr;
                    if ((*PartObject)->TryGetObjectField(TEXT("image_url"), ImageUrlObject) && ImageUrlObject != nullptr && (*ImageUrlObject).IsValid())
                    {
                        FString Url;
                        if ((*ImageUrlObject)->TryGetStringField(TEXT("url"), Url) && Url.StartsWith(TEXT("data:"), ESearchCase::CaseSensitive))
                        {
                            ++DataUrlCount;
                        }
                    }
                }
            }
        }

        UE_LOG(LogAIGatewayChatService, Log, TEXT("AIGateway request summary: messages=%d image_parts=%d data_urls=%d"), MessageCount, ImagePartCount, DataUrlCount);
        UE_LOG(LogAIGatewayChatService, Log, TEXT("AIGateway sanitized request body: %s"), *BuildSanitizedRequestBody(RequestBodyObject));
    }

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

        LogRequestDebugSummary(RequestBodyObject);

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
    const FString RequestBody = BuildRequestBody(Settings, Request);
    UE_LOG(LogAIGatewayChatService, Log, TEXT("AIGateway request target: %s"), *(Settings.BaseUrl + TEXT("/chat/completions")));
    UE_LOG(LogAIGatewayChatService, Verbose, TEXT("AIGateway raw request body length: %d"), RequestBody.Len());
    HttpRequest->SetContentAsString(RequestBody);
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
        const FString LoggedResponseBody = ServiceResponse.ResponseBody.Len() > MaxLoggedResponseBodyChars
            ? ServiceResponse.ResponseBody.Left(MaxLoggedResponseBodyChars) + TEXT("...(truncated)")
            : ServiceResponse.ResponseBody;
        UE_LOG(
            LogAIGatewayChatService,
            Log,
            TEXT("AIGateway response: success=%s code=%d body=%s"),
            bWasSuccessful ? TEXT("true") : TEXT("false"),
            ServiceResponse.ResponseCode,
            *LoggedResponseBody);
    }
    else
    {
        UE_LOG(LogAIGatewayChatService, Warning, TEXT("AIGateway response: success=%s with no HTTP response object."), bWasSuccessful ? TEXT("true") : TEXT("false"));
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
