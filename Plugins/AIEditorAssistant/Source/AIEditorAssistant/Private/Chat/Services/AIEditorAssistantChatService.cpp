#include "Chat/Services/AIEditorAssistantChatService.h"

#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Misc/Base64.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogAIEditorAssistantChatService, Log, All);

struct FAIEditorAssistantOpenAIChatService::FPendingServiceRequest
{
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest;
    FStreamChunkCallback OnStreamChunk;
    FRequestCompleteCallback OnComplete;
    FModelListCompleteCallback OnModelListComplete;
    FReasoningOptionsCompleteCallback OnReasoningOptionsComplete;
    int32 ProcessedLength = 0;
    EAIEditorAssistantAPIProvider Provider = EAIEditorAssistantAPIProvider::OpenAI;
    bool bTreatProgressAsEventStream = true;
    bool bIsModelListRequest = false;
    bool bIsReasoningOptionsRequest = false;
    FString OwnerId;
};

namespace
{
    constexpr int32 AnthropicDefaultMaxTokens = 4096;

    struct FProviderRequestData
    {
        FString Url;
        FString AcceptHeader = TEXT("application/json");
        FString RequestBody;
        bool bTreatProgressAsEventStream = false;
    };

    FString SerializeServiceJsonObject(const TSharedPtr<FJsonObject>& Object)
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

    TSharedPtr<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& Object)
    {
        TSharedPtr<FJsonObject> ParsedObject;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SerializeServiceJsonObject(Object));
        if (FJsonSerializer::Deserialize(Reader, ParsedObject) && ParsedObject.IsValid())
        {
            return ParsedObject;
        }

        return MakeShared<FJsonObject>();
    }

    FString BuildJsonString(const TSharedPtr<FJsonObject>& Object)
    {
        return SerializeServiceJsonObject(Object);
    }

    TSharedPtr<FJsonObject> ParseObjectString(const FString& JsonText)
    {
        if (JsonText.IsEmpty())
        {
            return nullptr;
        }

        TSharedPtr<FJsonObject> ParsedObject;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
        if (FJsonSerializer::Deserialize(Reader, ParsedObject) && ParsedObject.IsValid())
        {
            return ParsedObject;
        }

        return nullptr;
    }

    TSharedPtr<FJsonValue> ParseStringAsJsonValue(const FString& JsonText)
    {
        const FString Trimmed = JsonText.TrimStartAndEnd();
        if (Trimmed.IsEmpty())
        {
            return MakeShared<FJsonValueString>(JsonText);
        }

        if (Trimmed.StartsWith(TEXT("{")))
        {
            if (const TSharedPtr<FJsonObject> ParsedObject = ParseObjectString(Trimmed))
            {
                return MakeShared<FJsonValueObject>(ParsedObject);
            }
        }
        else if (Trimmed.StartsWith(TEXT("[")))
        {
            TArray<TSharedPtr<FJsonValue>> ParsedArray;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
            if (FJsonSerializer::Deserialize(Reader, ParsedArray))
            {
                return MakeShared<FJsonValueArray>(ParsedArray);
            }
        }

        return MakeShared<FJsonValueString>(JsonText);
    }

    FString JoinStrings(const TArray<FString>& Parts, const FString& Separator)
    {
        FString Joined;
        for (int32 Index = 0; Index < Parts.Num(); ++Index)
        {
            if (Index > 0)
            {
                Joined.Append(Separator);
            }

            Joined.Append(Parts[Index]);
        }
        return Joined;
    }

    FString ExtractMessageText(const TSharedPtr<FJsonObject>& MessageObject)
    {
        if (!MessageObject.IsValid())
        {
            return FString();
        }

        FString ContentText;
        if (MessageObject->TryGetStringField(TEXT("content"), ContentText))
        {
            return ContentText;
        }

        const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
        if (!MessageObject->TryGetArrayField(TEXT("content"), ContentArray) || ContentArray == nullptr)
        {
            return FString();
        }

        FString CombinedText;
        for (const TSharedPtr<FJsonValue>& PartValue : *ContentArray)
        {
            const TSharedPtr<FJsonObject>* PartObject = nullptr;
            if (!PartValue.IsValid() || !PartValue->TryGetObject(PartObject) || PartObject == nullptr || !(*PartObject).IsValid())
            {
                continue;
            }

            FString PartText;
            if ((*PartObject)->TryGetStringField(TEXT("text"), PartText) && !PartText.IsEmpty())
            {
                CombinedText.Append(PartText);
            }
        }

        return CombinedText;
    }

    FString MapOpenAIReasoningEffort(const EAIEditorAssistantReasoningIntensity Intensity)
    {
        switch (Intensity)
        {
        case EAIEditorAssistantReasoningIntensity::Disabled:
            return TEXT("none");
        case EAIEditorAssistantReasoningIntensity::Minimal:
            return TEXT("minimal");
        case EAIEditorAssistantReasoningIntensity::Low:
            return TEXT("low");
        case EAIEditorAssistantReasoningIntensity::Medium:
            return TEXT("medium");
        case EAIEditorAssistantReasoningIntensity::High:
            return TEXT("high");
        case EAIEditorAssistantReasoningIntensity::Maximum:
            return TEXT("xhigh");
        case EAIEditorAssistantReasoningIntensity::ProviderDefault:
        default:
            return FString();
        }
    }

    int32 MapAnthropicThinkingBudget(const EAIEditorAssistantReasoningIntensity Intensity)
    {
        switch (Intensity)
        {
        case EAIEditorAssistantReasoningIntensity::Minimal:
            return 1024;
        case EAIEditorAssistantReasoningIntensity::Low:
            return 2048;
        case EAIEditorAssistantReasoningIntensity::Medium:
            return 4096;
        case EAIEditorAssistantReasoningIntensity::High:
            return 8192;
        case EAIEditorAssistantReasoningIntensity::Maximum:
            return 16384;
        default:
            return 0;
        }
    }

    int32 MapGeminiThinkingBudget(const EAIEditorAssistantReasoningIntensity Intensity)
    {
        switch (Intensity)
        {
        case EAIEditorAssistantReasoningIntensity::Minimal:
            return 256;
        case EAIEditorAssistantReasoningIntensity::Low:
            return 1024;
        case EAIEditorAssistantReasoningIntensity::Medium:
            return 2048;
        case EAIEditorAssistantReasoningIntensity::High:
            return 4096;
        case EAIEditorAssistantReasoningIntensity::Maximum:
            return 8192;
        default:
            return 0;
        }
    }

    FString BuildChatCompletionsUrl(const FString& BaseUrl)
    {
        return BaseUrl.EndsWith(TEXT("/chat/completions")) ? BaseUrl : BaseUrl + TEXT("/chat/completions");
    }

    FString BuildModelsUrl(const FString& BaseUrl)
    {
        return BaseUrl.EndsWith(TEXT("/models")) ? BaseUrl : BaseUrl + TEXT("/models");
    }

    FString BuildAnthropicMessagesUrl(const FString& BaseUrl)
    {
        return BaseUrl.EndsWith(TEXT("/messages")) ? BaseUrl : BaseUrl + TEXT("/messages");
    }

    FString BuildGeminiGenerateContentUrl(const FAIEditorAssistantChatServiceSettings& Settings)
    {
        if (Settings.BaseUrl.Contains(TEXT(":generateContent")))
        {
            return Settings.BaseUrl;
        }

        FString BaseUrl = Settings.BaseUrl;
        if (!BaseUrl.EndsWith(TEXT("/")))
        {
            BaseUrl.Append(TEXT("/"));
        }

        return FString::Printf(TEXT("%smodels/%s:generateContent"), *BaseUrl, *Settings.Model);
    }

    bool ParseDataUrl(const FString& DataUrl, FString& OutMimeType, FString& OutBase64Data)
    {
        static const FString Prefix = TEXT("data:");
        if (!DataUrl.StartsWith(Prefix))
        {
            return false;
        }

        const int32 CommaIndex = INDEX_NONE;
        int32 SeparatorIndex = INDEX_NONE;
        if (!DataUrl.FindChar(TEXT(','), SeparatorIndex))
        {
            return false;
        }

        const FString Header = DataUrl.Mid(Prefix.Len(), SeparatorIndex - Prefix.Len());
        OutBase64Data = DataUrl.Mid(SeparatorIndex + 1);

        TArray<FString> HeaderParts;
        Header.ParseIntoArray(HeaderParts, TEXT(";"), true);
        if (HeaderParts.Num() == 0)
        {
            return false;
        }

        OutMimeType = HeaderParts[0];
        return !OutMimeType.IsEmpty() && !OutBase64Data.IsEmpty();
    }

    TSharedPtr<FJsonObject> BuildOpenAIChatBody(const FAIEditorAssistantChatServiceSettings& Settings, const FAIEditorAssistantChatCompletionRequest& Request)
    {
        TSharedPtr<FJsonObject> RequestBodyObject = MakeShared<FJsonObject>();
        RequestBodyObject->SetStringField(TEXT("model"), Settings.Model);
        RequestBodyObject->SetBoolField(TEXT("stream"), Request.bStream);
        RequestBodyObject->SetArrayField(TEXT("messages"), Request.Messages);

        if (Request.Tools.Num() > 0)
        {
            RequestBodyObject->SetArrayField(TEXT("tools"), Request.Tools);
        }

        if (!Request.ToolChoice.IsEmpty())
        {
            RequestBodyObject->SetStringField(TEXT("tool_choice"), Request.ToolChoice);
        }

        if (Request.MaxTokens > 0)
        {
            RequestBodyObject->SetNumberField(TEXT("max_tokens"), Request.MaxTokens);
        }

        const FString ReasoningEffort = MapOpenAIReasoningEffort(Settings.ReasoningIntensity);
        if (!ReasoningEffort.IsEmpty())
        {
            RequestBodyObject->SetStringField(TEXT("reasoning_effort"), ReasoningEffort);
        }

        return RequestBodyObject;
    }

    TSharedPtr<FJsonObject> ConvertOpenAIToolDefinitionToAnthropic(const TSharedPtr<FJsonObject>& ToolObject)
    {
        if (!ToolObject.IsValid())
        {
            return nullptr;
        }

        const TSharedPtr<FJsonObject>* FunctionObject = nullptr;
        if (!ToolObject->TryGetObjectField(TEXT("function"), FunctionObject) || FunctionObject == nullptr || !(*FunctionObject).IsValid())
        {
            return nullptr;
        }

        TSharedPtr<FJsonObject> AnthropicTool = MakeShared<FJsonObject>();
        FString Name;
        FString Description;
        (*FunctionObject)->TryGetStringField(TEXT("name"), Name);
        (*FunctionObject)->TryGetStringField(TEXT("description"), Description);
        AnthropicTool->SetStringField(TEXT("name"), Name);
        AnthropicTool->SetStringField(TEXT("description"), Description);

        const TSharedPtr<FJsonObject>* ParametersObject = nullptr;
        if ((*FunctionObject)->TryGetObjectField(TEXT("parameters"), ParametersObject) && ParametersObject != nullptr && (*ParametersObject).IsValid())
        {
            AnthropicTool->SetObjectField(TEXT("input_schema"), CloneJsonObject(*ParametersObject));
        }
        else
        {
            AnthropicTool->SetObjectField(TEXT("input_schema"), MakeShared<FJsonObject>());
        }

        return AnthropicTool;
    }

    TSharedPtr<FJsonObject> ConvertOpenAIToolDefinitionToGemini(const TSharedPtr<FJsonObject>& ToolObject)
    {
        if (!ToolObject.IsValid())
        {
            return nullptr;
        }

        const TSharedPtr<FJsonObject>* FunctionObject = nullptr;
        if (!ToolObject->TryGetObjectField(TEXT("function"), FunctionObject) || FunctionObject == nullptr || !(*FunctionObject).IsValid())
        {
            return nullptr;
        }

        TSharedPtr<FJsonObject> DeclarationObject = MakeShared<FJsonObject>();
        FString Name;
        FString Description;
        (*FunctionObject)->TryGetStringField(TEXT("name"), Name);
        (*FunctionObject)->TryGetStringField(TEXT("description"), Description);
        DeclarationObject->SetStringField(TEXT("name"), Name);
        DeclarationObject->SetStringField(TEXT("description"), Description);

        const TSharedPtr<FJsonObject>* ParametersObject = nullptr;
        if ((*FunctionObject)->TryGetObjectField(TEXT("parameters"), ParametersObject) && ParametersObject != nullptr && (*ParametersObject).IsValid())
        {
            DeclarationObject->SetObjectField(TEXT("parameters"), CloneJsonObject(*ParametersObject));
        }
        else
        {
            DeclarationObject->SetObjectField(TEXT("parameters"), MakeShared<FJsonObject>());
        }

        return DeclarationObject;
    }

    TSharedPtr<FJsonObject> ConvertOpenAIUserTextPartToAnthropic(const TSharedPtr<FJsonObject>& PartObject)
    {
        FString Text;
        if (!PartObject.IsValid() || !PartObject->TryGetStringField(TEXT("text"), Text))
        {
            return nullptr;
        }

        TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
        TextPart->SetStringField(TEXT("type"), TEXT("text"));
        TextPart->SetStringField(TEXT("text"), Text);
        return TextPart;
    }

    TSharedPtr<FJsonObject> ConvertOpenAIImagePartToAnthropic(const TSharedPtr<FJsonObject>& PartObject)
    {
        if (!PartObject.IsValid())
        {
            return nullptr;
        }

        const TSharedPtr<FJsonObject>* ImageUrlObject = nullptr;
        if (!PartObject->TryGetObjectField(TEXT("image_url"), ImageUrlObject) || ImageUrlObject == nullptr || !(*ImageUrlObject).IsValid())
        {
            return nullptr;
        }

        FString Url;
        if (!(*ImageUrlObject)->TryGetStringField(TEXT("url"), Url))
        {
            return nullptr;
        }

        FString MimeType;
        FString Base64Data;
        if (!ParseDataUrl(Url, MimeType, Base64Data))
        {
            return nullptr;
        }

        TSharedPtr<FJsonObject> SourceObject = MakeShared<FJsonObject>();
        SourceObject->SetStringField(TEXT("type"), TEXT("base64"));
        SourceObject->SetStringField(TEXT("media_type"), MimeType);
        SourceObject->SetStringField(TEXT("data"), Base64Data);

        TSharedPtr<FJsonObject> ImagePart = MakeShared<FJsonObject>();
        ImagePart->SetStringField(TEXT("type"), TEXT("image"));
        ImagePart->SetObjectField(TEXT("source"), SourceObject);
        return ImagePart;
    }

    void ConvertOpenAIContentArrayToAnthropicParts(const TArray<TSharedPtr<FJsonValue>>& ContentArray, TArray<TSharedPtr<FJsonValue>>& OutParts)
    {
        for (const TSharedPtr<FJsonValue>& PartValue : ContentArray)
        {
            const TSharedPtr<FJsonObject>* PartObject = nullptr;
            if (!PartValue.IsValid() || !PartValue->TryGetObject(PartObject) || PartObject == nullptr || !(*PartObject).IsValid())
            {
                continue;
            }

            FString Type;
            (*PartObject)->TryGetStringField(TEXT("type"), Type);

            TSharedPtr<FJsonObject> ConvertedPart;
            if (Type.Equals(TEXT("text"), ESearchCase::IgnoreCase))
            {
                ConvertedPart = ConvertOpenAIUserTextPartToAnthropic(*PartObject);
            }
            else if (Type.Equals(TEXT("image_url"), ESearchCase::IgnoreCase))
            {
                ConvertedPart = ConvertOpenAIImagePartToAnthropic(*PartObject);
            }

            if (ConvertedPart.IsValid())
            {
                OutParts.Add(MakeShared<FJsonValueObject>(ConvertedPart));
            }
        }
    }

    TSharedPtr<FJsonObject> BuildAnthropicMessageFromOpenAIMessage(const TSharedPtr<FJsonObject>& MessageObject)
    {
        if (!MessageObject.IsValid())
        {
            return nullptr;
        }

        FString Role;
        MessageObject->TryGetStringField(TEXT("role"), Role);
        if (Role.Equals(TEXT("system"), ESearchCase::IgnoreCase))
        {
            return nullptr;
        }

        TSharedPtr<FJsonObject> AnthropicMessage = MakeShared<FJsonObject>();

        if (Role.Equals(TEXT("tool"), ESearchCase::IgnoreCase))
        {
            AnthropicMessage->SetStringField(TEXT("role"), TEXT("user"));

            FString ToolCallId;
            FString Content;
            MessageObject->TryGetStringField(TEXT("tool_call_id"), ToolCallId);
            MessageObject->TryGetStringField(TEXT("content"), Content);

            TSharedPtr<FJsonObject> ToolResultBlock = MakeShared<FJsonObject>();
            ToolResultBlock->SetStringField(TEXT("type"), TEXT("tool_result"));
            ToolResultBlock->SetStringField(TEXT("tool_use_id"), ToolCallId);
            ToolResultBlock->SetStringField(TEXT("content"), Content);

            TArray<TSharedPtr<FJsonValue>> ContentBlocks;
            ContentBlocks.Add(MakeShared<FJsonValueObject>(ToolResultBlock));
            AnthropicMessage->SetArrayField(TEXT("content"), ContentBlocks);
            return AnthropicMessage;
        }

        const TSharedPtr<FJsonObject>* ProviderPayloadObject = nullptr;
        if (Role.Equals(TEXT("assistant"), ESearchCase::IgnoreCase)
            && MessageObject->TryGetObjectField(TEXT("_ai-editor-assistant_provider_payload"), ProviderPayloadObject)
            && ProviderPayloadObject != nullptr
            && (*ProviderPayloadObject).IsValid())
        {
            FString ProviderName;
            (*ProviderPayloadObject)->TryGetStringField(TEXT("provider"), ProviderName);
            const TArray<TSharedPtr<FJsonValue>>* NativeContentBlocks = nullptr;
            if (ProviderName.Equals(TEXT("anthropic"), ESearchCase::IgnoreCase)
                && (*ProviderPayloadObject)->TryGetArrayField(TEXT("native_content"), NativeContentBlocks)
                && NativeContentBlocks != nullptr)
            {
                AnthropicMessage->SetStringField(TEXT("role"), TEXT("assistant"));
                AnthropicMessage->SetArrayField(TEXT("content"), *NativeContentBlocks);
                return AnthropicMessage;
            }
        }

        AnthropicMessage->SetStringField(TEXT("role"), Role.Equals(TEXT("assistant"), ESearchCase::IgnoreCase) ? TEXT("assistant") : TEXT("user"));

        TArray<TSharedPtr<FJsonValue>> ContentBlocks;
        FString ContentString;
        if (MessageObject->TryGetStringField(TEXT("content"), ContentString) && !ContentString.IsEmpty())
        {
            TSharedPtr<FJsonObject> TextBlock = MakeShared<FJsonObject>();
            TextBlock->SetStringField(TEXT("type"), TEXT("text"));
            TextBlock->SetStringField(TEXT("text"), ContentString);
            ContentBlocks.Add(MakeShared<FJsonValueObject>(TextBlock));
        }
        else
        {
            const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
            if (MessageObject->TryGetArrayField(TEXT("content"), ContentArray) && ContentArray != nullptr)
            {
                ConvertOpenAIContentArrayToAnthropicParts(*ContentArray, ContentBlocks);
            }
        }

        if (Role.Equals(TEXT("assistant"), ESearchCase::IgnoreCase))
        {
            const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
            if (MessageObject->TryGetArrayField(TEXT("tool_calls"), ToolCalls) && ToolCalls != nullptr)
            {
                for (const TSharedPtr<FJsonValue>& ToolCallValue : *ToolCalls)
                {
                    const TSharedPtr<FJsonObject>* ToolCallObject = nullptr;
                    if (!ToolCallValue.IsValid() || !ToolCallValue->TryGetObject(ToolCallObject) || ToolCallObject == nullptr || !(*ToolCallObject).IsValid())
                    {
                        continue;
                    }

                    const TSharedPtr<FJsonObject>* FunctionObject = nullptr;
                    if (!(*ToolCallObject)->TryGetObjectField(TEXT("function"), FunctionObject) || FunctionObject == nullptr || !(*FunctionObject).IsValid())
                    {
                        continue;
                    }

                    FString ToolCallId;
                    FString FunctionName;
                    FString FunctionArguments;
                    (*ToolCallObject)->TryGetStringField(TEXT("id"), ToolCallId);
                    (*FunctionObject)->TryGetStringField(TEXT("name"), FunctionName);
                    (*FunctionObject)->TryGetStringField(TEXT("arguments"), FunctionArguments);

                    TSharedPtr<FJsonObject> ToolUseBlock = MakeShared<FJsonObject>();
                    ToolUseBlock->SetStringField(TEXT("type"), TEXT("tool_use"));
                    ToolUseBlock->SetStringField(TEXT("id"), ToolCallId);
                    ToolUseBlock->SetStringField(TEXT("name"), FunctionName);

                    TSharedPtr<FJsonObject> ParsedArguments = ParseObjectString(FunctionArguments);
                    ToolUseBlock->SetObjectField(TEXT("input"), ParsedArguments.IsValid() ? ParsedArguments : MakeShared<FJsonObject>());
                    ContentBlocks.Add(MakeShared<FJsonValueObject>(ToolUseBlock));
                }
            }
        }

        AnthropicMessage->SetArrayField(TEXT("content"), ContentBlocks);
        return AnthropicMessage;
    }

    TSharedPtr<FJsonObject> BuildAnthropicRequestBody(const FAIEditorAssistantChatServiceSettings& Settings, const FAIEditorAssistantChatCompletionRequest& Request)
    {
        TArray<FString> SystemParts;
        TArray<TSharedPtr<FJsonValue>> MessagesArray;
        for (const TSharedPtr<FJsonValue>& MessageValue : Request.Messages)
        {
            const TSharedPtr<FJsonObject>* MessageObject = nullptr;
            if (!MessageValue.IsValid() || !MessageValue->TryGetObject(MessageObject) || MessageObject == nullptr || !(*MessageObject).IsValid())
            {
                continue;
            }

            FString Role;
            (*MessageObject)->TryGetStringField(TEXT("role"), Role);
            if (Role.Equals(TEXT("system"), ESearchCase::IgnoreCase))
            {
                const FString SystemText = ExtractMessageText(*MessageObject).TrimStartAndEnd();
                if (!SystemText.IsEmpty())
                {
                    SystemParts.Add(SystemText);
                }
                continue;
            }

            if (const TSharedPtr<FJsonObject> AnthropicMessage = BuildAnthropicMessageFromOpenAIMessage(*MessageObject))
            {
                MessagesArray.Add(MakeShared<FJsonValueObject>(AnthropicMessage));
            }
        }

        TSharedPtr<FJsonObject> RequestBody = MakeShared<FJsonObject>();
        RequestBody->SetStringField(TEXT("model"), Settings.Model);
        RequestBody->SetNumberField(TEXT("max_tokens"), Request.MaxTokens > 0 ? Request.MaxTokens : AnthropicDefaultMaxTokens);
        RequestBody->SetBoolField(TEXT("stream"), false);
        RequestBody->SetArrayField(TEXT("messages"), MessagesArray);

        const FString JoinedSystem = JoinStrings(SystemParts, TEXT("\n\n"));
        if (!JoinedSystem.IsEmpty())
        {
            RequestBody->SetStringField(TEXT("system"), JoinedSystem);
        }

        if (Request.Tools.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> AnthropicTools;
            for (const TSharedPtr<FJsonValue>& ToolValue : Request.Tools)
            {
                const TSharedPtr<FJsonObject>* ToolObject = nullptr;
                if (!ToolValue.IsValid() || !ToolValue->TryGetObject(ToolObject) || ToolObject == nullptr || !(*ToolObject).IsValid())
                {
                    continue;
                }

                if (const TSharedPtr<FJsonObject> AnthropicTool = ConvertOpenAIToolDefinitionToAnthropic(*ToolObject))
                {
                    AnthropicTools.Add(MakeShared<FJsonValueObject>(AnthropicTool));
                }
            }

            if (AnthropicTools.Num() > 0)
            {
                RequestBody->SetArrayField(TEXT("tools"), AnthropicTools);
            }

            TSharedPtr<FJsonObject> ToolChoiceObject = MakeShared<FJsonObject>();
            ToolChoiceObject->SetStringField(TEXT("type"), Request.ToolChoice.Equals(TEXT("none"), ESearchCase::IgnoreCase) ? TEXT("none") : TEXT("auto"));
            RequestBody->SetObjectField(TEXT("tool_choice"), ToolChoiceObject);
        }

        const int32 ThinkingBudget = MapAnthropicThinkingBudget(Settings.ReasoningIntensity);
        if (ThinkingBudget > 0)
        {
            TSharedPtr<FJsonObject> ThinkingObject = MakeShared<FJsonObject>();
            ThinkingObject->SetStringField(TEXT("type"), TEXT("enabled"));
            ThinkingObject->SetNumberField(TEXT("budget_tokens"), ThinkingBudget);
            RequestBody->SetObjectField(TEXT("thinking"), ThinkingObject);
        }

        return RequestBody;
    }

    void ConvertOpenAIContentArrayToGeminiParts(const TArray<TSharedPtr<FJsonValue>>& ContentArray, TArray<TSharedPtr<FJsonValue>>& OutParts)
    {
        for (const TSharedPtr<FJsonValue>& PartValue : ContentArray)
        {
            const TSharedPtr<FJsonObject>* PartObject = nullptr;
            if (!PartValue.IsValid() || !PartValue->TryGetObject(PartObject) || PartObject == nullptr || !(*PartObject).IsValid())
            {
                continue;
            }

            FString Type;
            (*PartObject)->TryGetStringField(TEXT("type"), Type);
            if (Type.Equals(TEXT("text"), ESearchCase::IgnoreCase))
            {
                FString Text;
                if ((*PartObject)->TryGetStringField(TEXT("text"), Text) && !Text.IsEmpty())
                {
                    TSharedPtr<FJsonObject> GeminiPart = MakeShared<FJsonObject>();
                    GeminiPart->SetStringField(TEXT("text"), Text);
                    OutParts.Add(MakeShared<FJsonValueObject>(GeminiPart));
                }
            }
            else if (Type.Equals(TEXT("image_url"), ESearchCase::IgnoreCase))
            {
                const TSharedPtr<FJsonObject>* ImageUrlObject = nullptr;
                if (!(*PartObject)->TryGetObjectField(TEXT("image_url"), ImageUrlObject) || ImageUrlObject == nullptr || !(*ImageUrlObject).IsValid())
                {
                    continue;
                }

                FString Url;
                if (!(*ImageUrlObject)->TryGetStringField(TEXT("url"), Url))
                {
                    continue;
                }

                FString MimeType;
                FString Base64Data;
                if (!ParseDataUrl(Url, MimeType, Base64Data))
                {
                    continue;
                }

                TSharedPtr<FJsonObject> InlineData = MakeShared<FJsonObject>();
                InlineData->SetStringField(TEXT("mimeType"), MimeType);
                InlineData->SetStringField(TEXT("data"), Base64Data);

                TSharedPtr<FJsonObject> GeminiPart = MakeShared<FJsonObject>();
                GeminiPart->SetObjectField(TEXT("inlineData"), InlineData);
                OutParts.Add(MakeShared<FJsonValueObject>(GeminiPart));
            }
        }
    }

    TSharedPtr<FJsonObject> BuildGeminiRequestContentFromOpenAIMessage(const TSharedPtr<FJsonObject>& MessageObject)
    {
        if (!MessageObject.IsValid())
        {
            return nullptr;
        }

        FString Role;
        MessageObject->TryGetStringField(TEXT("role"), Role);
        if (Role.Equals(TEXT("system"), ESearchCase::IgnoreCase))
        {
            return nullptr;
        }

        if (Role.Equals(TEXT("assistant"), ESearchCase::IgnoreCase))
        {
            const TSharedPtr<FJsonObject>* ProviderPayloadObject = nullptr;
            if (MessageObject->TryGetObjectField(TEXT("_ai-editor-assistant_provider_payload"), ProviderPayloadObject)
                && ProviderPayloadObject != nullptr
                && (*ProviderPayloadObject).IsValid())
            {
                FString ProviderName;
                (*ProviderPayloadObject)->TryGetStringField(TEXT("provider"), ProviderName);
                const TSharedPtr<FJsonObject>* NativeContentObject = nullptr;
                if (ProviderName.Equals(TEXT("gemini"), ESearchCase::IgnoreCase)
                    && (*ProviderPayloadObject)->TryGetObjectField(TEXT("native_content"), NativeContentObject)
                    && NativeContentObject != nullptr
                    && (*NativeContentObject).IsValid())
                {
                    return CloneJsonObject(*NativeContentObject);
                }
            }
        }

        TSharedPtr<FJsonObject> GeminiContent = MakeShared<FJsonObject>();
        GeminiContent->SetStringField(TEXT("role"), Role.Equals(TEXT("assistant"), ESearchCase::IgnoreCase) ? TEXT("model") : TEXT("user"));

        TArray<TSharedPtr<FJsonValue>> Parts;
        if (Role.Equals(TEXT("tool"), ESearchCase::IgnoreCase))
        {
            GeminiContent->SetStringField(TEXT("role"), TEXT("user"));

            FString ToolName;
            FString ToolContent;
            MessageObject->TryGetStringField(TEXT("name"), ToolName);
            MessageObject->TryGetStringField(TEXT("content"), ToolContent);

            TSharedPtr<FJsonObject> FunctionResponseObject = MakeShared<FJsonObject>();
            FunctionResponseObject->SetStringField(TEXT("name"), ToolName.IsEmpty() ? TEXT("tool_result") : ToolName);

            TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
            const TSharedPtr<FJsonValue> ParsedValue = ParseStringAsJsonValue(ToolContent);
            if (ParsedValue.IsValid() && ParsedValue->Type == EJson::Object)
            {
                ResponseObject = ParsedValue->AsObject();
            }
            else
            {
                ResponseObject->SetField(TEXT("content"), ParsedValue.IsValid() ? ParsedValue : MakeShared<FJsonValueString>(ToolContent));
            }
            FunctionResponseObject->SetObjectField(TEXT("response"), ResponseObject);

            TSharedPtr<FJsonObject> PartObject = MakeShared<FJsonObject>();
            PartObject->SetObjectField(TEXT("functionResponse"), FunctionResponseObject);
            Parts.Add(MakeShared<FJsonValueObject>(PartObject));
            GeminiContent->SetArrayField(TEXT("parts"), Parts);
            return GeminiContent;
        }

        FString ContentString;
        if (MessageObject->TryGetStringField(TEXT("content"), ContentString) && !ContentString.IsEmpty())
        {
            TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
            TextPart->SetStringField(TEXT("text"), ContentString);
            Parts.Add(MakeShared<FJsonValueObject>(TextPart));
        }
        else
        {
            const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
            if (MessageObject->TryGetArrayField(TEXT("content"), ContentArray) && ContentArray != nullptr)
            {
                ConvertOpenAIContentArrayToGeminiParts(*ContentArray, Parts);
            }
        }

        if (Role.Equals(TEXT("assistant"), ESearchCase::IgnoreCase))
        {
            const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
            if (MessageObject->TryGetArrayField(TEXT("tool_calls"), ToolCalls) && ToolCalls != nullptr)
            {
                for (const TSharedPtr<FJsonValue>& ToolCallValue : *ToolCalls)
                {
                    const TSharedPtr<FJsonObject>* ToolCallObject = nullptr;
                    if (!ToolCallValue.IsValid() || !ToolCallValue->TryGetObject(ToolCallObject) || ToolCallObject == nullptr || !(*ToolCallObject).IsValid())
                    {
                        continue;
                    }

                    const TSharedPtr<FJsonObject>* FunctionObject = nullptr;
                    if (!(*ToolCallObject)->TryGetObjectField(TEXT("function"), FunctionObject) || FunctionObject == nullptr || !(*FunctionObject).IsValid())
                    {
                        continue;
                    }

                    FString FunctionName;
                    FString ArgumentsJson;
                    (*FunctionObject)->TryGetStringField(TEXT("name"), FunctionName);
                    (*FunctionObject)->TryGetStringField(TEXT("arguments"), ArgumentsJson);

                    TSharedPtr<FJsonObject> FunctionCallObject = MakeShared<FJsonObject>();
                    FunctionCallObject->SetStringField(TEXT("name"), FunctionName);

                    TSharedPtr<FJsonObject> ParsedArguments = ParseObjectString(ArgumentsJson);
                    FunctionCallObject->SetObjectField(TEXT("args"), ParsedArguments.IsValid() ? ParsedArguments : MakeShared<FJsonObject>());

                    TSharedPtr<FJsonObject> PartObject = MakeShared<FJsonObject>();
                    PartObject->SetObjectField(TEXT("functionCall"), FunctionCallObject);
                    Parts.Add(MakeShared<FJsonValueObject>(PartObject));
                }
            }
        }

        GeminiContent->SetArrayField(TEXT("parts"), Parts);
        return GeminiContent;
    }

    TSharedPtr<FJsonObject> BuildGeminiRequestBody(const FAIEditorAssistantChatServiceSettings& Settings, const FAIEditorAssistantChatCompletionRequest& Request)
    {
        TArray<FString> SystemParts;
        TArray<TSharedPtr<FJsonValue>> ContentsArray;

        for (const TSharedPtr<FJsonValue>& MessageValue : Request.Messages)
        {
            const TSharedPtr<FJsonObject>* MessageObject = nullptr;
            if (!MessageValue.IsValid() || !MessageValue->TryGetObject(MessageObject) || MessageObject == nullptr || !(*MessageObject).IsValid())
            {
                continue;
            }

            FString Role;
            (*MessageObject)->TryGetStringField(TEXT("role"), Role);
            if (Role.Equals(TEXT("system"), ESearchCase::IgnoreCase))
            {
                const FString SystemText = ExtractMessageText(*MessageObject).TrimStartAndEnd();
                if (!SystemText.IsEmpty())
                {
                    SystemParts.Add(SystemText);
                }
                continue;
            }

            if (const TSharedPtr<FJsonObject> GeminiContent = BuildGeminiRequestContentFromOpenAIMessage(*MessageObject))
            {
                ContentsArray.Add(MakeShared<FJsonValueObject>(GeminiContent));
            }
        }

        TSharedPtr<FJsonObject> RequestBody = MakeShared<FJsonObject>();
        RequestBody->SetArrayField(TEXT("contents"), ContentsArray);

        const FString JoinedSystem = JoinStrings(SystemParts, TEXT("\n\n"));
        if (!JoinedSystem.IsEmpty())
        {
            TSharedPtr<FJsonObject> SystemInstruction = MakeShared<FJsonObject>();
            TArray<TSharedPtr<FJsonValue>> SystemPartsArray;
            TSharedPtr<FJsonObject> SystemTextPart = MakeShared<FJsonObject>();
            SystemTextPart->SetStringField(TEXT("text"), JoinedSystem);
            SystemPartsArray.Add(MakeShared<FJsonValueObject>(SystemTextPart));
            SystemInstruction->SetArrayField(TEXT("parts"), SystemPartsArray);
            RequestBody->SetObjectField(TEXT("systemInstruction"), SystemInstruction);
        }

        TSharedPtr<FJsonObject> GenerationConfig = MakeShared<FJsonObject>();
        if (Request.MaxTokens > 0)
        {
            GenerationConfig->SetNumberField(TEXT("maxOutputTokens"), Request.MaxTokens);
        }

        const int32 ThinkingBudget = MapGeminiThinkingBudget(Settings.ReasoningIntensity);
        if (ThinkingBudget > 0)
        {
            TSharedPtr<FJsonObject> ThinkingConfig = MakeShared<FJsonObject>();
            ThinkingConfig->SetNumberField(TEXT("thinkingBudget"), ThinkingBudget);
            GenerationConfig->SetObjectField(TEXT("thinkingConfig"), ThinkingConfig);
        }

        RequestBody->SetObjectField(TEXT("generationConfig"), GenerationConfig);

        if (Request.Tools.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> FunctionDeclarations;
            for (const TSharedPtr<FJsonValue>& ToolValue : Request.Tools)
            {
                const TSharedPtr<FJsonObject>* ToolObject = nullptr;
                if (!ToolValue.IsValid() || !ToolValue->TryGetObject(ToolObject) || ToolObject == nullptr || !(*ToolObject).IsValid())
                {
                    continue;
                }

                if (const TSharedPtr<FJsonObject> FunctionDeclaration = ConvertOpenAIToolDefinitionToGemini(*ToolObject))
                {
                    FunctionDeclarations.Add(MakeShared<FJsonValueObject>(FunctionDeclaration));
                }
            }

            if (FunctionDeclarations.Num() > 0)
            {
                TSharedPtr<FJsonObject> ToolCollection = MakeShared<FJsonObject>();
                ToolCollection->SetArrayField(TEXT("functionDeclarations"), FunctionDeclarations);
                TArray<TSharedPtr<FJsonValue>> ToolsArray;
                ToolsArray.Add(MakeShared<FJsonValueObject>(ToolCollection));
                RequestBody->SetArrayField(TEXT("tools"), ToolsArray);

                TSharedPtr<FJsonObject> FunctionCallingConfig = MakeShared<FJsonObject>();
                FunctionCallingConfig->SetStringField(TEXT("mode"), Request.ToolChoice.Equals(TEXT("none"), ESearchCase::IgnoreCase) ? TEXT("NONE") : TEXT("AUTO"));
                TSharedPtr<FJsonObject> ToolConfig = MakeShared<FJsonObject>();
                ToolConfig->SetObjectField(TEXT("functionCallingConfig"), FunctionCallingConfig);
                RequestBody->SetObjectField(TEXT("toolConfig"), ToolConfig);
            }
        }

        return RequestBody;
    }

    bool BuildProviderRequestData(const FAIEditorAssistantChatServiceSettings& Settings, const FAIEditorAssistantChatCompletionRequest& Request, FProviderRequestData& OutRequestData)
    {
        TSharedPtr<FJsonObject> RequestBody;

        switch (Settings.Provider)
        {
        case EAIEditorAssistantAPIProvider::Anthropic:
            OutRequestData.Url = BuildAnthropicMessagesUrl(Settings.BaseUrl);
            OutRequestData.AcceptHeader = TEXT("application/json");
            OutRequestData.bTreatProgressAsEventStream = false;
            RequestBody = BuildAnthropicRequestBody(Settings, Request);
            break;

        case EAIEditorAssistantAPIProvider::Gemini:
            OutRequestData.Url = BuildGeminiGenerateContentUrl(Settings);
            OutRequestData.AcceptHeader = TEXT("application/json");
            OutRequestData.bTreatProgressAsEventStream = false;
            RequestBody = BuildGeminiRequestBody(Settings, Request);
            break;

        case EAIEditorAssistantAPIProvider::DeepSeek:
        case EAIEditorAssistantAPIProvider::OpenAI:
        case EAIEditorAssistantAPIProvider::Custom:
        case EAIEditorAssistantAPIProvider::OpenAICompatible:
        default:
            OutRequestData.Url = BuildChatCompletionsUrl(Settings.BaseUrl);
            OutRequestData.AcceptHeader = TEXT("text/event-stream, application/json");
            OutRequestData.bTreatProgressAsEventStream = true;
            RequestBody = BuildOpenAIChatBody(Settings, Request);

            if (Settings.Provider == EAIEditorAssistantAPIProvider::DeepSeek)
            {
                if (Settings.ReasoningIntensity != EAIEditorAssistantReasoningIntensity::ProviderDefault)
                {
                    TSharedPtr<FJsonObject> ThinkingObject = MakeShared<FJsonObject>();
                    ThinkingObject->SetStringField(TEXT("type"), Settings.ReasoningIntensity == EAIEditorAssistantReasoningIntensity::Disabled ? TEXT("disabled") : TEXT("enabled"));
                    RequestBody->SetObjectField(TEXT("thinking"), ThinkingObject);
                }
            }
            break;
        }

        OutRequestData.RequestBody = BuildJsonString(RequestBody);
        return !OutRequestData.Url.IsEmpty() && !OutRequestData.RequestBody.IsEmpty();
    }

    void ApplyProviderHeaders(const FAIEditorAssistantChatServiceSettings& Settings, const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& HttpRequest, const FString& AcceptHeader)
    {
        HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        HttpRequest->SetHeader(TEXT("Accept"), AcceptHeader);

        switch (Settings.Provider)
        {
        case EAIEditorAssistantAPIProvider::Anthropic:
            HttpRequest->SetHeader(TEXT("x-api-key"), Settings.ApiKey);
            HttpRequest->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));
            break;

        case EAIEditorAssistantAPIProvider::Gemini:
            HttpRequest->SetHeader(TEXT("x-goog-api-key"), Settings.ApiKey);
            break;

        case EAIEditorAssistantAPIProvider::DeepSeek:
        case EAIEditorAssistantAPIProvider::OpenAI:
        case EAIEditorAssistantAPIProvider::Custom:
        case EAIEditorAssistantAPIProvider::OpenAICompatible:
        default:
            HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Settings.ApiKey));
            break;
        }
    }

    FString ExtractServiceErrorMessage(const FString& ResponseBody)
    {
        const TSharedPtr<FJsonObject> ResponseObject = ParseObjectString(ResponseBody);
        if (!ResponseObject.IsValid())
        {
            return FString();
        }

        FString ErrorMessage;
        if (ResponseObject->TryGetStringField(TEXT("message"), ErrorMessage) && !ErrorMessage.IsEmpty())
        {
            return ErrorMessage;
        }

        const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
        if (ResponseObject->TryGetObjectField(TEXT("error"), ErrorObject) && ErrorObject != nullptr && (*ErrorObject).IsValid())
        {
            if ((*ErrorObject)->TryGetStringField(TEXT("message"), ErrorMessage) && !ErrorMessage.IsEmpty())
            {
                return ErrorMessage;
            }

            if ((*ErrorObject)->TryGetStringField(TEXT("error"), ErrorMessage) && !ErrorMessage.IsEmpty())
            {
                return ErrorMessage;
            }
        }

        return FString();
    }

    TArray<FString> ParseModelIdsFromListResponse(const FString& ResponseBody)
    {
        TArray<FString> Models;
        const TSharedPtr<FJsonObject> ResponseObject = ParseObjectString(ResponseBody);
        if (!ResponseObject.IsValid())
        {
            return Models;
        }

        const TArray<TSharedPtr<FJsonValue>>* DataArray = nullptr;
        if (!ResponseObject->TryGetArrayField(TEXT("data"), DataArray) || DataArray == nullptr)
        {
            return Models;
        }

        TSet<FString> UniqueModels;
        for (const TSharedPtr<FJsonValue>& ItemValue : *DataArray)
        {
            const TSharedPtr<FJsonObject>* ItemObject = nullptr;
            if (!ItemValue.IsValid() || !ItemValue->TryGetObject(ItemObject) || ItemObject == nullptr || !(*ItemObject).IsValid())
            {
                continue;
            }

            FString ModelId;
            if ((*ItemObject)->TryGetStringField(TEXT("id"), ModelId))
            {
                ModelId = ModelId.TrimStartAndEnd();
                if (!ModelId.IsEmpty())
                {
                    UniqueModels.Add(ModelId);
                }
            }
        }

        for (const FString& ModelId : UniqueModels)
        {
            Models.Add(ModelId);
        }
        Models.Sort([](const FString& A, const FString& B)
        {
            return A < B;
        });
        return Models;
    }

    TArray<EAIEditorAssistantReasoningIntensity> BuildDefaultReasoningOptions(const EAIEditorAssistantAPIProvider Provider)
    {
        (void)Provider;
        return {
            EAIEditorAssistantReasoningIntensity::ProviderDefault,
            EAIEditorAssistantReasoningIntensity::Disabled,
            EAIEditorAssistantReasoningIntensity::Minimal,
            EAIEditorAssistantReasoningIntensity::Low,
            EAIEditorAssistantReasoningIntensity::Medium,
            EAIEditorAssistantReasoningIntensity::High,
            EAIEditorAssistantReasoningIntensity::Maximum
        };
    }

    void AddReasoningOptionByName(const FString& RawName, TSet<EAIEditorAssistantReasoningIntensity>& OutSet)
    {
        const FString Name = RawName.TrimStartAndEnd().ToLower();
        if (Name.IsEmpty())
        {
            return;
        }

        if (Name == TEXT("default") || Name == TEXT("provider_default") || Name == TEXT("providerdefault") || Name == TEXT("auto"))
        {
            OutSet.Add(EAIEditorAssistantReasoningIntensity::ProviderDefault);
            return;
        }
        if (Name == TEXT("disabled") || Name == TEXT("none") || Name == TEXT("off"))
        {
            OutSet.Add(EAIEditorAssistantReasoningIntensity::Disabled);
            return;
        }
        if (Name == TEXT("minimal"))
        {
            OutSet.Add(EAIEditorAssistantReasoningIntensity::Minimal);
            return;
        }
        if (Name == TEXT("low"))
        {
            OutSet.Add(EAIEditorAssistantReasoningIntensity::Low);
            return;
        }
        if (Name == TEXT("medium") || Name == TEXT("enabled") || Name == TEXT("on"))
        {
            OutSet.Add(EAIEditorAssistantReasoningIntensity::Medium);
            return;
        }
        if (Name == TEXT("high"))
        {
            OutSet.Add(EAIEditorAssistantReasoningIntensity::High);
            return;
        }
        if (Name == TEXT("maximum") || Name == TEXT("max") || Name == TEXT("xhigh"))
        {
            OutSet.Add(EAIEditorAssistantReasoningIntensity::Maximum);
            return;
        }
    }

    void AddReasoningOptionsFromArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, TSet<EAIEditorAssistantReasoningIntensity>& OutSet)
    {
        if (!Object.IsValid())
        {
            return;
        }

        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Object->TryGetArrayField(FieldName, Values) || Values == nullptr)
        {
            return;
        }

        for (const TSharedPtr<FJsonValue>& Value : *Values)
        {
            if (!Value.IsValid())
            {
                continue;
            }

            if (Value->Type == EJson::String)
            {
                AddReasoningOptionByName(Value->AsString(), OutSet);
            }
        }
    }

    TArray<EAIEditorAssistantReasoningIntensity> ParseReasoningOptionsFromModelDetail(
        const FString& ResponseBody,
        const EAIEditorAssistantAPIProvider Provider)
    {
        TSet<EAIEditorAssistantReasoningIntensity> OptionsSet;
        OptionsSet.Add(EAIEditorAssistantReasoningIntensity::ProviderDefault);

        const TSharedPtr<FJsonObject> RootObject = ParseObjectString(ResponseBody);
        if (RootObject.IsValid())
        {
            AddReasoningOptionsFromArrayField(RootObject, TEXT("supported_reasoning_efforts"), OptionsSet);
            AddReasoningOptionsFromArrayField(RootObject, TEXT("reasoning_effort_values"), OptionsSet);
            AddReasoningOptionsFromArrayField(RootObject, TEXT("reasoning_modes"), OptionsSet);
            AddReasoningOptionsFromArrayField(RootObject, TEXT("thinking_modes"), OptionsSet);

            const TSharedPtr<FJsonObject>* ReasoningObject = nullptr;
            if (RootObject->TryGetObjectField(TEXT("reasoning"), ReasoningObject) && ReasoningObject != nullptr && (*ReasoningObject).IsValid())
            {
                AddReasoningOptionsFromArrayField(*ReasoningObject, TEXT("efforts"), OptionsSet);
                AddReasoningOptionsFromArrayField(*ReasoningObject, TEXT("supported_efforts"), OptionsSet);
            }

            const TSharedPtr<FJsonObject>* ThinkingObject = nullptr;
            if (RootObject->TryGetObjectField(TEXT("thinking"), ThinkingObject) && ThinkingObject != nullptr && (*ThinkingObject).IsValid())
            {
                bool bEnabled = false;
                if ((*ThinkingObject)->TryGetBoolField(TEXT("enabled"), bEnabled))
                {
                    OptionsSet.Add(EAIEditorAssistantReasoningIntensity::Disabled);
                    if (bEnabled)
                    {
                        OptionsSet.Add(EAIEditorAssistantReasoningIntensity::Medium);
                    }
                }
                AddReasoningOptionsFromArrayField(*ThinkingObject, TEXT("modes"), OptionsSet);
            }
        }

        if (OptionsSet.Num() <= 1)
        {
            for (const EAIEditorAssistantReasoningIntensity Option : BuildDefaultReasoningOptions(Provider))
            {
                OptionsSet.Add(Option);
            }
        }

        TArray<EAIEditorAssistantReasoningIntensity> OutOptions;
        const TArray<EAIEditorAssistantReasoningIntensity> PreferredOrder = {
            EAIEditorAssistantReasoningIntensity::ProviderDefault,
            EAIEditorAssistantReasoningIntensity::Disabled,
            EAIEditorAssistantReasoningIntensity::Minimal,
            EAIEditorAssistantReasoningIntensity::Low,
            EAIEditorAssistantReasoningIntensity::Medium,
            EAIEditorAssistantReasoningIntensity::High,
            EAIEditorAssistantReasoningIntensity::Maximum
        };

        for (const EAIEditorAssistantReasoningIntensity Candidate : PreferredOrder)
        {
            if (OptionsSet.Contains(Candidate))
            {
                OutOptions.Add(Candidate);
            }
        }

        return OutOptions;
    }

    FString ExtractAnthropicTextBlock(const TSharedPtr<FJsonObject>& BlockObject)
    {
        FString Text;
        if (BlockObject.IsValid() && BlockObject->TryGetStringField(TEXT("text"), Text))
        {
            return Text;
        }

        return FString();
    }

    FString NormalizeAnthropicResponseBody(const FString& RawResponseBody)
    {
        const TSharedPtr<FJsonObject> ResponseObject = ParseObjectString(RawResponseBody);
        if (!ResponseObject.IsValid())
        {
            return RawResponseBody;
        }

        const TArray<TSharedPtr<FJsonValue>>* ContentBlocks = nullptr;
        if (!ResponseObject->TryGetArrayField(TEXT("content"), ContentBlocks) || ContentBlocks == nullptr)
        {
            return RawResponseBody;
        }

        FString AssistantText;
        FString ReasoningText;
        TArray<TSharedPtr<FJsonValue>> ToolCalls;
        TArray<TSharedPtr<FJsonValue>> NativeContent;

        int32 ToolIndex = 0;
        for (const TSharedPtr<FJsonValue>& BlockValue : *ContentBlocks)
        {
            const TSharedPtr<FJsonObject>* BlockObject = nullptr;
            if (!BlockValue.IsValid() || !BlockValue->TryGetObject(BlockObject) || BlockObject == nullptr || !(*BlockObject).IsValid())
            {
                continue;
            }

            NativeContent.Add(MakeShared<FJsonValueObject>(CloneJsonObject(*BlockObject)));

            FString BlockType;
            (*BlockObject)->TryGetStringField(TEXT("type"), BlockType);
            if (BlockType.Equals(TEXT("text"), ESearchCase::IgnoreCase))
            {
                AssistantText.Append(ExtractAnthropicTextBlock(*BlockObject));
            }
            else if (BlockType.Equals(TEXT("thinking"), ESearchCase::IgnoreCase))
            {
                FString ThinkingText;
                if ((*BlockObject)->TryGetStringField(TEXT("thinking"), ThinkingText) && !ThinkingText.IsEmpty())
                {
                    ReasoningText.Append(ThinkingText);
                }
            }
            else if (BlockType.Equals(TEXT("tool_use"), ESearchCase::IgnoreCase))
            {
                FString ToolId;
                FString ToolName;
                (*BlockObject)->TryGetStringField(TEXT("id"), ToolId);
                (*BlockObject)->TryGetStringField(TEXT("name"), ToolName);

                TSharedPtr<FJsonObject> FunctionObject = MakeShared<FJsonObject>();
                FunctionObject->SetStringField(TEXT("name"), ToolName);

                const TSharedPtr<FJsonObject>* InputObject = nullptr;
                if ((*BlockObject)->TryGetObjectField(TEXT("input"), InputObject) && InputObject != nullptr && (*InputObject).IsValid())
                {
                    FunctionObject->SetStringField(TEXT("arguments"), SerializeServiceJsonObject(*InputObject));
                }
                else
                {
                    FunctionObject->SetStringField(TEXT("arguments"), TEXT("{}"));
                }

                TSharedPtr<FJsonObject> ToolCallObject = MakeShared<FJsonObject>();
                ToolCallObject->SetStringField(TEXT("id"), ToolId.IsEmpty() ? FString::Printf(TEXT("anthropic-call-%d"), ++ToolIndex) : ToolId);
                ToolCallObject->SetStringField(TEXT("type"), TEXT("function"));
                ToolCallObject->SetObjectField(TEXT("function"), FunctionObject);
                ToolCalls.Add(MakeShared<FJsonValueObject>(ToolCallObject));
            }
        }

        TSharedPtr<FJsonObject> ProviderPayload = MakeShared<FJsonObject>();
        ProviderPayload->SetStringField(TEXT("provider"), TEXT("anthropic"));
        ProviderPayload->SetArrayField(TEXT("native_content"), NativeContent);

        TSharedPtr<FJsonObject> MessageObject = MakeShared<FJsonObject>();
        MessageObject->SetStringField(TEXT("role"), TEXT("assistant"));
        MessageObject->SetStringField(TEXT("content"), AssistantText);
        if (!ReasoningText.IsEmpty())
        {
            MessageObject->SetStringField(TEXT("reasoning_content"), ReasoningText);
        }
        if (ToolCalls.Num() > 0)
        {
            MessageObject->SetArrayField(TEXT("tool_calls"), ToolCalls);
        }
        MessageObject->SetObjectField(TEXT("_ai-editor-assistant_provider_payload"), ProviderPayload);

        TSharedPtr<FJsonObject> ChoiceObject = MakeShared<FJsonObject>();
        ChoiceObject->SetObjectField(TEXT("message"), MessageObject);

        TArray<TSharedPtr<FJsonValue>> Choices;
        Choices.Add(MakeShared<FJsonValueObject>(ChoiceObject));

        TSharedPtr<FJsonObject> NormalizedObject = MakeShared<FJsonObject>();
        NormalizedObject->SetArrayField(TEXT("choices"), Choices);
        return SerializeServiceJsonObject(NormalizedObject);
    }

    FString NormalizeGeminiResponseBody(const FString& RawResponseBody)
    {
        const TSharedPtr<FJsonObject> ResponseObject = ParseObjectString(RawResponseBody);
        if (!ResponseObject.IsValid())
        {
            return RawResponseBody;
        }

        const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
        if (!ResponseObject->TryGetArrayField(TEXT("candidates"), Candidates) || Candidates == nullptr || Candidates->Num() == 0)
        {
            return RawResponseBody;
        }

        const TSharedPtr<FJsonObject>* CandidateObject = nullptr;
        if (!(*Candidates)[0]->TryGetObject(CandidateObject) || CandidateObject == nullptr || !(*CandidateObject).IsValid())
        {
            return RawResponseBody;
        }

        const TSharedPtr<FJsonObject>* ContentObject = nullptr;
        if (!(*CandidateObject)->TryGetObjectField(TEXT("content"), ContentObject) || ContentObject == nullptr || !(*ContentObject).IsValid())
        {
            return RawResponseBody;
        }

        const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
        if (!(*ContentObject)->TryGetArrayField(TEXT("parts"), Parts) || Parts == nullptr)
        {
            return RawResponseBody;
        }

        FString AssistantText;
        FString ReasoningText;
        TArray<TSharedPtr<FJsonValue>> ToolCalls;
        int32 ToolIndex = 0;

        for (const TSharedPtr<FJsonValue>& PartValue : *Parts)
        {
            const TSharedPtr<FJsonObject>* PartObject = nullptr;
            if (!PartValue.IsValid() || !PartValue->TryGetObject(PartObject) || PartObject == nullptr || !(*PartObject).IsValid())
            {
                continue;
            }

            const TSharedPtr<FJsonObject>* FunctionCallObject = nullptr;
            if ((*PartObject)->TryGetObjectField(TEXT("functionCall"), FunctionCallObject) && FunctionCallObject != nullptr && (*FunctionCallObject).IsValid())
            {
                FString FunctionName;
                (*FunctionCallObject)->TryGetStringField(TEXT("name"), FunctionName);

                TSharedPtr<FJsonObject> FunctionObject = MakeShared<FJsonObject>();
                FunctionObject->SetStringField(TEXT("name"), FunctionName);

                const TSharedPtr<FJsonObject>* ArgsObject = nullptr;
                if ((*FunctionCallObject)->TryGetObjectField(TEXT("args"), ArgsObject) && ArgsObject != nullptr && (*ArgsObject).IsValid())
                {
                    FunctionObject->SetStringField(TEXT("arguments"), SerializeServiceJsonObject(*ArgsObject));
                }
                else
                {
                    FunctionObject->SetStringField(TEXT("arguments"), TEXT("{}"));
                }

                TSharedPtr<FJsonObject> ToolCallObject = MakeShared<FJsonObject>();
                ToolCallObject->SetStringField(TEXT("id"), FString::Printf(TEXT("gemini-call-%d"), ++ToolIndex));
                ToolCallObject->SetStringField(TEXT("type"), TEXT("function"));
                ToolCallObject->SetObjectField(TEXT("function"), FunctionObject);
                ToolCalls.Add(MakeShared<FJsonValueObject>(ToolCallObject));
                continue;
            }

            FString Text;
            if ((*PartObject)->TryGetStringField(TEXT("text"), Text) && !Text.IsEmpty())
            {
                bool bThought = false;
                (*PartObject)->TryGetBoolField(TEXT("thought"), bThought);
                if (bThought)
                {
                    ReasoningText.Append(Text);
                }
                else
                {
                    AssistantText.Append(Text);
                }
            }
        }

        TSharedPtr<FJsonObject> ProviderPayload = MakeShared<FJsonObject>();
        ProviderPayload->SetStringField(TEXT("provider"), TEXT("gemini"));
        ProviderPayload->SetObjectField(TEXT("native_content"), CloneJsonObject(*ContentObject));

        TSharedPtr<FJsonObject> MessageObject = MakeShared<FJsonObject>();
        MessageObject->SetStringField(TEXT("role"), TEXT("assistant"));
        MessageObject->SetStringField(TEXT("content"), AssistantText);
        if (!ReasoningText.IsEmpty())
        {
            MessageObject->SetStringField(TEXT("reasoning_content"), ReasoningText);
        }
        if (ToolCalls.Num() > 0)
        {
            MessageObject->SetArrayField(TEXT("tool_calls"), ToolCalls);
        }
        MessageObject->SetObjectField(TEXT("_ai-editor-assistant_provider_payload"), ProviderPayload);

        TSharedPtr<FJsonObject> ChoiceObject = MakeShared<FJsonObject>();
        ChoiceObject->SetObjectField(TEXT("message"), MessageObject);

        TArray<TSharedPtr<FJsonValue>> Choices;
        Choices.Add(MakeShared<FJsonValueObject>(ChoiceObject));

        TSharedPtr<FJsonObject> NormalizedObject = MakeShared<FJsonObject>();
        NormalizedObject->SetArrayField(TEXT("choices"), Choices);
        return SerializeServiceJsonObject(NormalizedObject);
    }
}

bool FAIEditorAssistantOpenAIChatService::SendStreamingChatRequest(
    const FAIEditorAssistantChatServiceSettings& Settings,
    const FAIEditorAssistantChatCompletionRequest& Request,
    FStreamChunkCallback&& OnStreamChunk,
    FRequestCompleteCallback&& OnComplete,
    const FString& OwnerId)
{
    return BeginRequest(Settings, Request, TEXT("application/json"), MoveTemp(OnStreamChunk), MoveTemp(OnComplete), OwnerId);
}

bool FAIEditorAssistantOpenAIChatService::FetchAvailableModels(
    const FAIEditorAssistantChatServiceSettings& Settings,
    FModelListCompleteCallback&& OnComplete)
{
    return BeginFetchModelsRequest(Settings, MoveTemp(OnComplete));
}

bool FAIEditorAssistantOpenAIChatService::FetchReasoningOptions(
    const FAIEditorAssistantChatServiceSettings& Settings,
    const FString& Model,
    FReasoningOptionsCompleteCallback&& OnComplete)
{
    return BeginFetchReasoningOptionsRequest(Settings, Model, MoveTemp(OnComplete));
}

void FAIEditorAssistantOpenAIChatService::CancelActiveRequests()
{
    TArray<TSharedPtr<FPendingServiceRequest>> RequestsToCancel = ActiveRequests;
    ActiveRequests.Reset();

    for (const TSharedPtr<FPendingServiceRequest>& Context : RequestsToCancel)
    {
        if (Context.IsValid() && Context->HttpRequest.IsValid())
        {
            Context->HttpRequest->CancelRequest();
        }
    }
}

void FAIEditorAssistantOpenAIChatService::CancelRequestsByOwner(const FString& OwnerId)
{
    TArray<TSharedPtr<FPendingServiceRequest>> RequestsToCancel;
    for (const TSharedPtr<FPendingServiceRequest>& Context : ActiveRequests)
    {
        if (Context.IsValid() && Context->OwnerId.Equals(OwnerId))
        {
            RequestsToCancel.Add(Context);
        }
    }

    for (const TSharedPtr<FPendingServiceRequest>& Context : RequestsToCancel)
    {
        if (Context.IsValid() && Context->HttpRequest.IsValid())
        {
            Context->HttpRequest->CancelRequest();
        }
        ActiveRequests.Remove(Context);
    }
}

bool FAIEditorAssistantOpenAIChatService::BeginFetchModelsRequest(
    const FAIEditorAssistantChatServiceSettings& Settings,
    FModelListCompleteCallback&& OnComplete)
{
    if (!OnComplete)
    {
        return false;
    }

    const FString ModelsUrl = BuildModelsUrl(Settings.BaseUrl);
    if (ModelsUrl.IsEmpty())
    {
        return false;
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(ModelsUrl);
    HttpRequest->SetVerb(TEXT("GET"));
    ApplyProviderHeaders(Settings, HttpRequest, TEXT("application/json"));
    HttpRequest->OnProcessRequestComplete().BindSP(AsShared(), &FAIEditorAssistantOpenAIChatService::HandleRequestComplete);

    TSharedPtr<FPendingServiceRequest> Context = MakeShared<FPendingServiceRequest>();
    Context->HttpRequest = HttpRequest;
    Context->OnModelListComplete = MoveTemp(OnComplete);
    Context->Provider = Settings.Provider;
    Context->bTreatProgressAsEventStream = false;
    Context->bIsModelListRequest = true;
    ActiveRequests.Add(Context);

    if (!HttpRequest->ProcessRequest())
    {
        RemoveRequestContext(HttpRequest);
        return false;
    }

    return true;
}

bool FAIEditorAssistantOpenAIChatService::BeginFetchReasoningOptionsRequest(
    const FAIEditorAssistantChatServiceSettings& Settings,
    const FString& Model,
    FReasoningOptionsCompleteCallback&& OnComplete)
{
    if (!OnComplete)
    {
        return false;
    }

    const FString TrimmedModel = Model.TrimStartAndEnd();
    if (TrimmedModel.IsEmpty())
    {
        return false;
    }

    const FString EncodedModel = FGenericPlatformHttp::UrlEncode(TrimmedModel);
    const FString Url = FString::Printf(TEXT("%s/models/%s"), *Settings.BaseUrl, *EncodedModel);
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(Url);
    HttpRequest->SetVerb(TEXT("GET"));
    ApplyProviderHeaders(Settings, HttpRequest, TEXT("application/json"));
    HttpRequest->OnProcessRequestComplete().BindSP(AsShared(), &FAIEditorAssistantOpenAIChatService::HandleRequestComplete);

    TSharedPtr<FPendingServiceRequest> Context = MakeShared<FPendingServiceRequest>();
    Context->HttpRequest = HttpRequest;
    Context->OnReasoningOptionsComplete = MoveTemp(OnComplete);
    Context->Provider = Settings.Provider;
    Context->bTreatProgressAsEventStream = false;
    Context->bIsReasoningOptionsRequest = true;
    ActiveRequests.Add(Context);

    if (!HttpRequest->ProcessRequest())
    {
        RemoveRequestContext(HttpRequest);
        return false;
    }

    return true;
}

bool FAIEditorAssistantOpenAIChatService::BeginRequest(
    const FAIEditorAssistantChatServiceSettings& Settings,
    const FAIEditorAssistantChatCompletionRequest& Request,
    const FString& AcceptHeader,
    FStreamChunkCallback&& OnStreamChunk,
    FRequestCompleteCallback&& OnComplete,
    const FString& OwnerId)
{
    FProviderRequestData RequestData;
    if (!BuildProviderRequestData(Settings, Request, RequestData))
    {
        return false;
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(RequestData.Url);
    HttpRequest->SetVerb(TEXT("POST"));
    ApplyProviderHeaders(Settings, HttpRequest, RequestData.AcceptHeader.IsEmpty() ? AcceptHeader : RequestData.AcceptHeader);
    HttpRequest->SetContentAsString(RequestData.RequestBody);
    HttpRequest->OnRequestProgress64().BindSP(AsShared(), &FAIEditorAssistantOpenAIChatService::HandleRequestProgress);
    HttpRequest->OnProcessRequestComplete().BindSP(AsShared(), &FAIEditorAssistantOpenAIChatService::HandleRequestComplete);

    TSharedPtr<FPendingServiceRequest> Context = MakeShared<FPendingServiceRequest>();
    Context->HttpRequest = HttpRequest;
    Context->OnStreamChunk = MoveTemp(OnStreamChunk);
    Context->OnComplete = MoveTemp(OnComplete);
    Context->Provider = Settings.Provider;
    Context->bTreatProgressAsEventStream = RequestData.bTreatProgressAsEventStream;
    Context->OwnerId = OwnerId;
    ActiveRequests.Add(Context);

    if (!HttpRequest->ProcessRequest())
    {
        RemoveRequestContext(HttpRequest);
        return false;
    }

    return true;
}

void FAIEditorAssistantOpenAIChatService::HandleRequestProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived)
{
    TSharedPtr<FPendingServiceRequest> Context = FindRequestContext(Request);
    if (!Context.IsValid() || !Context->OnStreamChunk || !Context->bTreatProgressAsEventStream)
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

void FAIEditorAssistantOpenAIChatService::HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    TSharedPtr<FPendingServiceRequest> Context = FindRequestContext(Request);
    if (!Context.IsValid())
    {
        return;
    }

    if (Context->bIsModelListRequest)
    {
        FAIEditorAssistantModelListResponse ModelListResponse;
        ModelListResponse.bRequestSucceeded = bWasSuccessful && Response.IsValid();

        if (Response.IsValid())
        {
            ModelListResponse.ResponseCode = Response->GetResponseCode();
            const FString ResponseBody = Response->GetContentAsString();
            if (ModelListResponse.bRequestSucceeded && ModelListResponse.ResponseCode >= 200 && ModelListResponse.ResponseCode < 300)
            {
                ModelListResponse.Models = ParseModelIdsFromListResponse(ResponseBody);
                if (ModelListResponse.Models.Num() == 0)
                {
                    ModelListResponse.bRequestSucceeded = false;
                    ModelListResponse.ErrorMessage = TEXT("The provider returned an empty model list.");
                }
            }
            else
            {
                ModelListResponse.bRequestSucceeded = false;
                ModelListResponse.ErrorMessage = ExtractServiceErrorMessage(ResponseBody);
            }
        }
        else
        {
            ModelListResponse.bRequestSucceeded = false;
        }

        if (ModelListResponse.ErrorMessage.IsEmpty() && !ModelListResponse.bRequestSucceeded)
        {
            ModelListResponse.ErrorMessage = ModelListResponse.ResponseCode > 0
                ? FString::Printf(TEXT("Failed to fetch models (HTTP %d)."), ModelListResponse.ResponseCode)
                : TEXT("Failed to fetch models from provider.");
        }

        FModelListCompleteCallback ModelListCallback = MoveTemp(Context->OnModelListComplete);
        RemoveRequestContext(Request);
        if (ModelListCallback)
        {
            ModelListCallback(ModelListResponse);
        }
        return;
    }

    if (Context->bIsReasoningOptionsRequest)
    {
        FAIEditorAssistantReasoningOptionsResponse ReasoningResponse;
        ReasoningResponse.bRequestSucceeded = bWasSuccessful && Response.IsValid();

        if (Response.IsValid())
        {
            ReasoningResponse.ResponseCode = Response->GetResponseCode();
            const FString ResponseBody = Response->GetContentAsString();
            if (ReasoningResponse.bRequestSucceeded && ReasoningResponse.ResponseCode >= 200 && ReasoningResponse.ResponseCode < 300)
            {
                ReasoningResponse.Options = ParseReasoningOptionsFromModelDetail(ResponseBody, Context->Provider);
            }
            else
            {
                ReasoningResponse.bRequestSucceeded = false;
                ReasoningResponse.ErrorMessage = ExtractServiceErrorMessage(ResponseBody);
                ReasoningResponse.Options = BuildDefaultReasoningOptions(Context->Provider);
            }
        }
        else
        {
            ReasoningResponse.bRequestSucceeded = false;
            ReasoningResponse.Options = BuildDefaultReasoningOptions(Context->Provider);
        }

        if (ReasoningResponse.ErrorMessage.IsEmpty() && !ReasoningResponse.bRequestSucceeded)
        {
            ReasoningResponse.ErrorMessage = ReasoningResponse.ResponseCode > 0
                ? FString::Printf(TEXT("Failed to fetch reasoning options (HTTP %d)."), ReasoningResponse.ResponseCode)
                : TEXT("Failed to fetch reasoning options from provider.");
        }

        FReasoningOptionsCompleteCallback ReasoningCallback = MoveTemp(Context->OnReasoningOptionsComplete);
        RemoveRequestContext(Request);
        if (ReasoningCallback)
        {
            ReasoningCallback(ReasoningResponse);
        }
        return;
    }

    if (Context->OnStreamChunk && Context->bTreatProgressAsEventStream && Response.IsValid())
    {
        const FString CurrentContent = Response->GetContentAsString();
        if (CurrentContent.Len() > Context->ProcessedLength)
        {
            Context->OnStreamChunk(CurrentContent.Mid(Context->ProcessedLength));
            Context->ProcessedLength = CurrentContent.Len();
        }
    }

    FAIEditorAssistantChatServiceResponse ServiceResponse;
    ServiceResponse.bRequestSucceeded = bWasSuccessful && Response.IsValid();
    if (Response.IsValid())
    {
        ServiceResponse.ResponseCode = Response->GetResponseCode();
        ServiceResponse.ResponseBody = Response->GetContentAsString();

        if (ServiceResponse.bRequestSucceeded)
        {
            if (Context->Provider == EAIEditorAssistantAPIProvider::Anthropic)
            {
                ServiceResponse.ResponseBody = NormalizeAnthropicResponseBody(ServiceResponse.ResponseBody);
            }
            else if (Context->Provider == EAIEditorAssistantAPIProvider::Gemini)
            {
                ServiceResponse.ResponseBody = NormalizeGeminiResponseBody(ServiceResponse.ResponseBody);
            }
        }
    }
    else
    {
        UE_LOG(LogAIEditorAssistantChatService, Warning, TEXT("AIEditorAssistant response: success=%s with no HTTP response object."), bWasSuccessful ? TEXT("true") : TEXT("false"));
    }

    FRequestCompleteCallback CompletionCallback = MoveTemp(Context->OnComplete);
    RemoveRequestContext(Request);

    if (CompletionCallback)
    {
        CompletionCallback(ServiceResponse);
    }
}

TSharedPtr<FAIEditorAssistantOpenAIChatService::FPendingServiceRequest> FAIEditorAssistantOpenAIChatService::FindRequestContext(const FHttpRequestPtr& Request) const
{
    const TSharedPtr<FPendingServiceRequest>* FoundContext = ActiveRequests.FindByPredicate([&Request](const TSharedPtr<FPendingServiceRequest>& Context)
    {
        return Context.IsValid() && Context->HttpRequest == Request;
    });

    return FoundContext != nullptr ? *FoundContext : nullptr;
}

void FAIEditorAssistantOpenAIChatService::RemoveRequestContext(const FHttpRequestPtr& Request)
{
    ActiveRequests.RemoveAll([&Request](const TSharedPtr<FPendingServiceRequest>& Context)
    {
        return Context.IsValid() && Context->HttpRequest == Request;
    });
}
