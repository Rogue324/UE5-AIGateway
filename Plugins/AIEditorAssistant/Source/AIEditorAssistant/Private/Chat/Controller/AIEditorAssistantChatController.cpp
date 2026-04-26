#include "Chat/Controller/AIEditorAssistantChatController.h"

#include "AIEditorAssistantSettings.h"
#include "Chat/Markdown/AIEditorAssistantMarkdownParser.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tools/AIEditorAssistantToolRuntime.h"

namespace
{
    const FString DefaultSessionTitle(TEXT("New Chat"));
    const FString OpenAIOfficialBaseUrl(TEXT("https://api.openai.com/v1"));
    const FString DeepSeekOfficialBaseUrl(TEXT("https://api.deepseek.com"));
    constexpr int32 MaxInlineImageLongEdge = 1568;
    constexpr int32 MaxPreferredPngBytes = 1536 * 1024;
    constexpr int32 PhotoJpegQuality = 82;
    constexpr int32 ScreenshotFallbackJpegQuality = 90;

    FString BuildAgentSystemPrompt()
    {
        return TEXT(
            "You are AI Editor Assistant, a native Unreal Engine editor agent running inside the user's editor.\n"
            "You can inspect and operate the live UE editor through the provided tools. Treat these tools as your primary source of truth for questions about the current project, level, actors, assets, Blueprints, PIE state, logs, console variables, or editor configuration.\n"
            "When the user asks about current editor or project state, do not guess from memory. Call the relevant tool first, then answer from the tool result.\n"
            "Prefer a small number of targeted tool calls. Do not repeatedly call the same tool with the same arguments.\n"
            "Python scripting is not available to this chat agent. Never say you will use Python, never generate Python scripts, and never switch to Python when a task is complex. Ignore any earlier assistant message that suggested using Python.\n"
            "Use only the provided native tools. If a task cannot be completed with the native tools, explain the limitation instead of proposing Python.\n"
            "For adding components to actors, use add-component. Do not write Python for component creation when add-component is available.\n"
            "The add-blueprint-variable tool is available in this environment. Never claim it is unavailable unless a tool call actually returned an error proving otherwise.\n"
            "The add-interface-function tool is available in this environment for Blueprint Interface assets. Never claim it is unavailable unless a tool call actually returned an error proving otherwise.\n"
            "For creating Blueprint member variables, use add-blueprint-variable. Do not use Python for Blueprint variable creation.\n"
            "For creating functions on Blueprint Interface assets (BPI_*), use add-interface-function. Do not pretend modify-interface or set-blueprint-default can create interface function definitions.\n"
            "Never use set-blueprint-default or set-asset-property to create Blueprint variable definitions. Those tools edit existing properties only; they do not create new Blueprint variables.\n"
            "Never attempt to create Blueprint variables implicitly by placing Set/Get nodes. Blueprint variables must be created explicitly with add-blueprint-variable before any variable_get or variable_set node is added.\n"
            "Prefer add-blueprint-k2-node over low-level add-graph-node for Blueprint function calls and Blueprint variable get/set nodes because it performs the correct binding and validation.\n"
            "Do not create K2Node_AddComponent through add-graph-node. That node path is unsupported here and can trigger editor assertions. Use add-component for existing level actors, and if the request is about Blueprint-owned components, explain the current limitation instead of forcing a node.\n"
            "For Blueprint EventGraph logic, build incrementally with add-blueprint-k2-node, add-blueprint-variable, query-blueprint-graph, connect-graph-pins, set-node-property, set-node-position, compile-blueprint, and save-asset. Do not use or propose Python for Blueprint graph construction.\n"
            "After you have enough information, stop calling tools and give the final answer.\n"
            "For destructive or state-changing actions, explain the intended action briefly and rely on the editor confirmation flow when approval is required.\n"
            "Reply in the user's language unless the user asks otherwise.");
    }

    TSharedPtr<FJsonObject> BuildAgentSystemMessageObject()
    {
        TSharedPtr<FJsonObject> MessageObject = MakeShared<FJsonObject>();
        MessageObject->SetStringField(TEXT("role"), TEXT("system"));
        MessageObject->SetStringField(TEXT("content"), BuildAgentSystemPrompt());
        return MessageObject;
    }

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

    FString NormalizeToolArgumentsJson(const FString& ArgumentsJson)
    {
        const FString TrimmedJson = ArgumentsJson.TrimStartAndEnd();
        if (TrimmedJson.IsEmpty())
        {
            return TEXT("{}");
        }

        TSharedPtr<FJsonObject> Object;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TrimmedJson);
        if (FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid())
        {
            return SerializeJsonObject(Object);
        }

        return TEXT("{}");
    }

    TSharedPtr<FJsonObject> ParseJsonObject(const FString& JsonText)
    {
        if (JsonText.IsEmpty())
        {
            return MakeShared<FJsonObject>();
        }

        TSharedPtr<FJsonObject> Object;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
        if (FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid())
        {
            return Object;
        }

        TSharedPtr<FJsonObject> FallbackObject = MakeShared<FJsonObject>();
        FallbackObject->SetStringField(TEXT("__raw_arguments"), JsonText);
        return FallbackObject;
    }

    bool TryReadFunctionArgumentsJson(const TSharedPtr<FJsonObject>& FunctionObject, FString& OutArgumentsJson)
    {
        OutArgumentsJson = TEXT("{}");
        if (!FunctionObject.IsValid())
        {
            return false;
        }

        FString ArgumentsString;
        if (FunctionObject->TryGetStringField(TEXT("arguments"), ArgumentsString))
        {
            OutArgumentsJson = NormalizeToolArgumentsJson(ArgumentsString);
            return true;
        }

        const TSharedPtr<FJsonObject>* ArgumentsObject = nullptr;
        if (FunctionObject->TryGetObjectField(TEXT("arguments"), ArgumentsObject) && ArgumentsObject != nullptr && (*ArgumentsObject).IsValid())
        {
            OutArgumentsJson = SerializeJsonObject(*ArgumentsObject);
            return true;
        }

        return false;
    }

    TSharedPtr<FJsonObject> SanitizeRequestMessageObject(const TSharedPtr<FJsonObject>& MessageObject)
    {
        if (!MessageObject.IsValid())
        {
            return nullptr;
        }

        TSharedPtr<FJsonObject> SanitizedObject = ParseJsonObject(SerializeJsonObject(MessageObject));

        FString Role;
        SanitizedObject->TryGetStringField(TEXT("role"), Role);
        if (!Role.Equals(TEXT("assistant"), ESearchCase::IgnoreCase))
        {
            return SanitizedObject;
        }

        const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
        if (!SanitizedObject->TryGetArrayField(TEXT("tool_calls"), ToolCalls))
        {
            return SanitizedObject;
        }

        for (const TSharedPtr<FJsonValue>& ToolCallValue : *ToolCalls)
        {
            const TSharedPtr<FJsonObject>* ToolCallObject = nullptr;
            if (!ToolCallValue.IsValid() || !ToolCallValue->TryGetObject(ToolCallObject) || ToolCallObject == nullptr || !(*ToolCallObject).IsValid())
            {
                continue;
            }

            const TSharedPtr<FJsonObject>* FunctionObject = nullptr;
            if ((*ToolCallObject)->TryGetObjectField(TEXT("function"), FunctionObject) && FunctionObject != nullptr && (*FunctionObject).IsValid())
            {
                FString NormalizedArgumentsJson;
                TryReadFunctionArgumentsJson(*FunctionObject, NormalizedArgumentsJson);
                (*FunctionObject)->SetStringField(TEXT("arguments"), NormalizedArgumentsJson);
            }
        }

        return SanitizedObject;
    }

    TArray<TSharedPtr<FJsonObject>> BuildConsistentRequestHistory(const TArray<TSharedPtr<FJsonObject>>& RequestMessages)
    {
        TArray<TSharedPtr<FJsonObject>> OutputMessages;

        int32 PendingAssistantIndex = INDEX_NONE;
        TSet<FString> PendingToolCallIds;
        TArray<int32> PendingToolMessageIndices;

        auto ResetPendingState =
            [&PendingAssistantIndex, &PendingToolCallIds, &PendingToolMessageIndices]()
        {
            PendingAssistantIndex = INDEX_NONE;
            PendingToolCallIds.Reset();
            PendingToolMessageIndices.Reset();
        };

        auto DropIncompletePendingBlock =
            [&OutputMessages, &PendingAssistantIndex, &PendingToolCallIds, &PendingToolMessageIndices, &ResetPendingState]()
        {
            if (PendingAssistantIndex != INDEX_NONE && PendingToolCallIds.Num() > 0)
            {
                if (OutputMessages.IsValidIndex(PendingAssistantIndex) && OutputMessages[PendingAssistantIndex].IsValid())
                {
                    // Keep assistant text, but strip dangling tool call requests that do not
                    // have complete tool responses (common after interrupted sessions/restarts).
                    OutputMessages[PendingAssistantIndex]->RemoveField(TEXT("tool_calls"));
                }

                PendingToolMessageIndices.Sort([](const int32 A, const int32 B)
                {
                    return A > B;
                });

                for (const int32 MessageIndex : PendingToolMessageIndices)
                {
                    if (OutputMessages.IsValidIndex(MessageIndex))
                    {
                        OutputMessages.RemoveAt(MessageIndex);
                    }
                }
            }

            ResetPendingState();
        };

        auto BeginPendingToolBlock =
            [&OutputMessages, &PendingAssistantIndex, &PendingToolCallIds, &PendingToolMessageIndices](const TSharedPtr<FJsonObject>& AssistantMessage)
        {
            PendingAssistantIndex = OutputMessages.Add(AssistantMessage);
            PendingToolCallIds.Reset();
            PendingToolMessageIndices.Reset();

            const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
            if (!AssistantMessage->TryGetArrayField(TEXT("tool_calls"), ToolCalls) || ToolCalls == nullptr)
            {
                return;
            }

            for (const TSharedPtr<FJsonValue>& ToolCallValue : *ToolCalls)
            {
                const TSharedPtr<FJsonObject>* ToolCallObject = nullptr;
                if (!ToolCallValue.IsValid() || !ToolCallValue->TryGetObject(ToolCallObject) || ToolCallObject == nullptr || !(*ToolCallObject).IsValid())
                {
                    continue;
                }

                FString ToolCallId;
                if ((*ToolCallObject)->TryGetStringField(TEXT("id"), ToolCallId))
                {
                    ToolCallId = ToolCallId.TrimStartAndEnd();
                    if (!ToolCallId.IsEmpty())
                    {
                        PendingToolCallIds.Add(ToolCallId);
                    }
                }
            }
        };

        for (const TSharedPtr<FJsonObject>& RequestMessage : RequestMessages)
        {
            TSharedPtr<FJsonObject> SanitizedMessage = SanitizeRequestMessageObject(RequestMessage);
            if (!SanitizedMessage.IsValid())
            {
                continue;
            }

            FString Role;
            SanitizedMessage->TryGetStringField(TEXT("role"), Role);
            const bool bIsToolMessage = Role.Equals(TEXT("tool"), ESearchCase::IgnoreCase);

            if (PendingAssistantIndex != INDEX_NONE)
            {
                if (bIsToolMessage)
                {
                    FString ToolCallId;
                    SanitizedMessage->TryGetStringField(TEXT("tool_call_id"), ToolCallId);
                    ToolCallId = ToolCallId.TrimStartAndEnd();

                    if (!ToolCallId.IsEmpty() && PendingToolCallIds.Contains(ToolCallId))
                    {
                        const int32 ToolMessageIndex = OutputMessages.Add(SanitizedMessage);
                        PendingToolMessageIndices.Add(ToolMessageIndex);
                        PendingToolCallIds.Remove(ToolCallId);
                    }

                    continue;
                }

                if (PendingToolCallIds.Num() > 0)
                {
                    DropIncompletePendingBlock();
                }
                else
                {
                    ResetPendingState();
                }
            }

            const bool bHasToolCalls = SanitizedMessage->HasTypedField<EJson::Array>(TEXT("tool_calls"));
            if (Role.Equals(TEXT("assistant"), ESearchCase::IgnoreCase) && bHasToolCalls)
            {
                BeginPendingToolBlock(SanitizedMessage);
                continue;
            }

            if (bIsToolMessage)
            {
                // Ignore orphan tool messages not attached to an active assistant tool-call block.
                continue;
            }

            OutputMessages.Add(SanitizedMessage);
        }

        if (PendingAssistantIndex != INDEX_NONE)
        {
            if (PendingToolCallIds.Num() > 0)
            {
                DropIncompletePendingBlock();
            }
            else
            {
                ResetPendingState();
            }
        }

        return OutputMessages;
    }

    FString TruncateWithEllipsis(const FString& InText, const int32 MaxLength)
    {
        FString Text = InText.TrimStartAndEnd();
        if (Text.Len() <= MaxLength)
        {
            return Text;
        }

        return Text.Left(MaxLength) + TEXT("...");
    }

    FString GetFirstConversationMessageByRole(const FAIEditorAssistantChatSession& Session, const FString& Role)
    {
        for (const FAIEditorAssistantChatMessage& Message : Session.ConversationMessages)
        {
            if (Message.Role.Equals(Role, ESearchCase::IgnoreCase) && !Message.Content.TrimStartAndEnd().IsEmpty())
            {
                return Message.Content.TrimStartAndEnd();
            }
        }

        return FString();
    }

    FString GetLastConversationMessageByRole(const FAIEditorAssistantChatSession& Session, const FString& Role)
    {
        for (int32 Index = Session.ConversationMessages.Num() - 1; Index >= 0; --Index)
        {
            const FAIEditorAssistantChatMessage& Message = Session.ConversationMessages[Index];
            if (Message.Role.Equals(Role, ESearchCase::IgnoreCase) && !Message.Content.TrimStartAndEnd().IsEmpty())
            {
                return Message.Content.TrimStartAndEnd();
            }
        }

        return FString();
    }

    FString ExtractTextFromContentParts(const TArray<TSharedPtr<FJsonValue>>& ContentParts)
    {
        FString CombinedText;
        for (const TSharedPtr<FJsonValue>& PartValue : ContentParts)
        {
            const TSharedPtr<FJsonObject>* PartObject = nullptr;
            if (!PartValue.IsValid() || !PartValue->TryGetObject(PartObject) || PartObject == nullptr || !(*PartObject).IsValid())
            {
                continue;
            }

            FString PartType;
            (*PartObject)->TryGetStringField(TEXT("type"), PartType);

            FString PartText;
            if ((*PartObject)->TryGetStringField(TEXT("text"), PartText) && !PartText.IsEmpty())
            {
                CombinedText.Append(PartText);
                continue;
            }

            if (PartType.Equals(TEXT("output_text"), ESearchCase::IgnoreCase))
            {
                const TArray<TSharedPtr<FJsonValue>>* Annotations = nullptr;
                if ((*PartObject)->TryGetArrayField(TEXT("annotations"), Annotations))
                {
                    // Intentionally ignored for now; text already captured above when present.
                }
            }
        }

        return CombinedText;
    }

    FString ExtractTextFromOutputItems(const TArray<TSharedPtr<FJsonValue>>& OutputItems)
    {
        FString CombinedText;
        for (const TSharedPtr<FJsonValue>& ItemValue : OutputItems)
        {
            const TSharedPtr<FJsonObject>* ItemObject = nullptr;
            if (!ItemValue.IsValid() || !ItemValue->TryGetObject(ItemObject) || ItemObject == nullptr || !(*ItemObject).IsValid())
            {
                continue;
            }

            FString DirectText;
            if ((*ItemObject)->TryGetStringField(TEXT("text"), DirectText) && !DirectText.IsEmpty())
            {
                CombinedText.Append(DirectText);
                continue;
            }

            const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
            if ((*ItemObject)->TryGetArrayField(TEXT("content"), ContentArray) && ContentArray != nullptr && ContentArray->Num() > 0)
            {
                CombinedText.Append(ExtractTextFromContentParts(*ContentArray));
            }
        }

        return CombinedText;
    }

    FString ExtractAssistantContentFromMessage(const TSharedPtr<FJsonObject>& MessageObject)
    {
        if (!MessageObject.IsValid())
        {
            return FString();
        }

        FString ContentText;
        if (MessageObject->TryGetStringField(TEXT("content"), ContentText) && !ContentText.IsEmpty())
        {
            return ContentText;
        }

        const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
        if (MessageObject->TryGetArrayField(TEXT("content"), ContentArray) && ContentArray != nullptr && ContentArray->Num() > 0)
        {
            return ExtractTextFromContentParts(*ContentArray);
        }

        return FString();
    }

    FString ExtractAssistantContentFromResponseObject(const TSharedPtr<FJsonObject>& ResponseObject)
    {
        if (!ResponseObject.IsValid())
        {
            return FString();
        }

        FString OutputText;
        if (ResponseObject->TryGetStringField(TEXT("output_text"), OutputText) && !OutputText.IsEmpty())
        {
            return OutputText;
        }

        const TArray<TSharedPtr<FJsonValue>>* OutputArray = nullptr;
        if (ResponseObject->TryGetArrayField(TEXT("output"), OutputArray) && OutputArray != nullptr && OutputArray->Num() > 0)
        {
            const FString CombinedOutput = ExtractTextFromOutputItems(*OutputArray);
            if (!CombinedOutput.IsEmpty())
            {
                return CombinedOutput;
            }
        }

        const TSharedPtr<FJsonObject>* NestedResponse = nullptr;
        if (ResponseObject->TryGetObjectField(TEXT("response"), NestedResponse) && NestedResponse != nullptr && (*NestedResponse).IsValid())
        {
            return ExtractAssistantContentFromResponseObject(*NestedResponse);
        }

        return FString();
    }

    FString ExtractReasoningContentFromMessage(const TSharedPtr<FJsonObject>& MessageObject)
    {
        if (!MessageObject.IsValid())
        {
            return FString();
        }

        FString ReasoningContent;
        if (MessageObject->TryGetStringField(TEXT("reasoning_content"), ReasoningContent) && !ReasoningContent.IsEmpty())
        {
            return ReasoningContent;
        }

        const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
        if (MessageObject->TryGetArrayField(TEXT("reasoning_content"), ContentArray) && ContentArray != nullptr && ContentArray->Num() > 0)
        {
            return ExtractTextFromContentParts(*ContentArray);
        }

        return FString();
    }

    bool TryExtractOutputTextDelta(const TSharedPtr<FJsonObject>& StreamObject, FString& OutDelta)
    {
        OutDelta.Empty();
        if (!StreamObject.IsValid())
        {
            return false;
        }

        FString EventType;
        StreamObject->TryGetStringField(TEXT("type"), EventType);
        if (EventType.Contains(TEXT("output_text.delta")))
        {
            return StreamObject->TryGetStringField(TEXT("delta"), OutDelta) && !OutDelta.IsEmpty();
        }

        auto TryExtractFlexibleTextFromValue =
            [](const TSharedPtr<FJsonValue>& JsonValue, FString& OutText, const auto& Self) -> bool
        {
            OutText.Empty();
            if (!JsonValue.IsValid() || JsonValue->IsNull())
            {
                return false;
            }

            if (JsonValue->Type == EJson::String)
            {
                OutText = JsonValue->AsString();
                return !OutText.IsEmpty();
            }

            if (JsonValue->Type == EJson::Array)
            {
                FString CombinedText;
                const TArray<TSharedPtr<FJsonValue>>& JsonArray = JsonValue->AsArray();
                for (const TSharedPtr<FJsonValue>& Entry : JsonArray)
                {
                    FString EntryText;
                    if (Self(Entry, EntryText, Self) && !EntryText.IsEmpty())
                    {
                        CombinedText.Append(EntryText);
                    }
                }

                if (!CombinedText.IsEmpty())
                {
                    OutText = CombinedText;
                    return true;
                }

                return false;
            }

            if (JsonValue->Type != EJson::Object)
            {
                return false;
            }

            const TSharedPtr<FJsonObject> JsonObject = JsonValue->AsObject();
            if (!JsonObject.IsValid())
            {
                return false;
            }

            static const TCHAR* PreferredKeys[] =
            {
                TEXT("text"),
                TEXT("value"),
                TEXT("content"),
                TEXT("delta")
            };

            for (const TCHAR* Key : PreferredKeys)
            {
                const TSharedPtr<FJsonValue>* NestedValue = JsonObject->Values.Find(Key);
                if (NestedValue == nullptr || !NestedValue->IsValid())
                {
                    continue;
                }

                FString NestedText;
                if (Self(*NestedValue, NestedText, Self) && !NestedText.IsEmpty())
                {
                    OutText = NestedText;
                    return true;
                }
            }

            return false;
        };

        const TSharedPtr<FJsonObject>* DeltaObject = nullptr;
        if (StreamObject->TryGetObjectField(TEXT("delta"), DeltaObject) && DeltaObject != nullptr && (*DeltaObject).IsValid())
        {
            const TSharedPtr<FJsonValue>* DeltaValue = (*DeltaObject)->Values.Find(TEXT("text"));
            if (DeltaValue != nullptr && TryExtractFlexibleTextFromValue(*DeltaValue, OutDelta, TryExtractFlexibleTextFromValue) && !OutDelta.IsEmpty())
            {
                return true;
            }

            const TSharedPtr<FJsonValue>* ContentValue = (*DeltaObject)->Values.Find(TEXT("content"));
            if (ContentValue != nullptr && TryExtractFlexibleTextFromValue(*ContentValue, OutDelta, TryExtractFlexibleTextFromValue) && !OutDelta.IsEmpty())
            {
                return true;
            }
        }

        const TSharedPtr<FJsonValue>* ContentValue = StreamObject->Values.Find(TEXT("content"));
        if (ContentValue != nullptr && TryExtractFlexibleTextFromValue(*ContentValue, OutDelta, TryExtractFlexibleTextFromValue) && !OutDelta.IsEmpty())
        {
            return true;
        }

        return false;
    }

    bool TryExtractReasoningContentDelta(const TSharedPtr<FJsonObject>& StreamObject, FString& OutDelta)
    {
        OutDelta.Empty();
        if (!StreamObject.IsValid())
        {
            return false;
        }

        FString EventType;
        StreamObject->TryGetStringField(TEXT("type"), EventType);
        if (EventType.Contains(TEXT("reasoning_content.delta")))
        {
            return StreamObject->TryGetStringField(TEXT("delta"), OutDelta) && !OutDelta.IsEmpty();
        }

        const TSharedPtr<FJsonObject>* DeltaObject = nullptr;
        if (!StreamObject->TryGetObjectField(TEXT("delta"), DeltaObject) || DeltaObject == nullptr || !(*DeltaObject).IsValid())
        {
            return false;
        }

        if ((*DeltaObject)->TryGetStringField(TEXT("reasoning_content"), OutDelta) && !OutDelta.IsEmpty())
        {
            return true;
        }

        const TSharedPtr<FJsonValue>* ReasoningValue = (*DeltaObject)->Values.Find(TEXT("reasoning_content"));
        if (ReasoningValue == nullptr || !ReasoningValue->IsValid())
        {
            return false;
        }

        if ((*ReasoningValue)->Type == EJson::String)
        {
            OutDelta = (*ReasoningValue)->AsString();
            return !OutDelta.IsEmpty();
        }

        if ((*ReasoningValue)->Type == EJson::Array)
        {
            OutDelta = ExtractTextFromContentParts((*ReasoningValue)->AsArray());
            return !OutDelta.IsEmpty();
        }

        if ((*ReasoningValue)->Type == EJson::Object)
        {
            const TSharedPtr<FJsonObject> ReasoningObject = (*ReasoningValue)->AsObject();
            if (ReasoningObject.IsValid())
            {
                if (ReasoningObject->TryGetStringField(TEXT("text"), OutDelta) && !OutDelta.IsEmpty())
                {
                    return true;
                }

                const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
                if (ReasoningObject->TryGetArrayField(TEXT("content"), ContentArray) && ContentArray != nullptr)
                {
                    OutDelta = ExtractTextFromContentParts(*ContentArray);
                    return !OutDelta.IsEmpty();
                }
            }
        }

        return false;
    }

    FString GetImageMimeType(const FString& ImagePath)
    {
        const FString Extension = FPaths::GetExtension(ImagePath, false).ToLower();
        if (Extension == TEXT("png"))
        {
            return TEXT("image/png");
        }

        if (Extension == TEXT("jpg") || Extension == TEXT("jpeg"))
        {
            return TEXT("image/jpeg");
        }

        if (Extension == TEXT("webp"))
        {
            return TEXT("image/webp");
        }

        if (Extension == TEXT("gif"))
        {
            return TEXT("image/gif");
        }

        if (Extension == TEXT("bmp"))
        {
            return TEXT("image/bmp");
        }

        return FString();
    }

    FString GetMimeTypeFromImageFormat(const EImageFormat ImageFormat)
    {
        if (ImageFormat == EImageFormat::PNG)
        {
            return TEXT("image/png");
        }

        if (ImageFormat == EImageFormat::JPEG)
        {
            return TEXT("image/jpeg");
        }

        return FString();
    }

    bool ResizeImageBilinear(
        const TArray<FColor>& SourcePixels,
        const int32 SourceWidth,
        const int32 SourceHeight,
        const int32 TargetWidth,
        const int32 TargetHeight,
        TArray<FColor>& OutPixels)
    {
        OutPixels.Reset();

        if (SourceWidth <= 0 || SourceHeight <= 0 || TargetWidth <= 0 || TargetHeight <= 0)
        {
            return false;
        }

        if (SourcePixels.Num() != SourceWidth * SourceHeight)
        {
            return false;
        }

        if (SourceWidth == TargetWidth && SourceHeight == TargetHeight)
        {
            OutPixels = SourcePixels;
            return true;
        }

        OutPixels.SetNumUninitialized(TargetWidth * TargetHeight);

        for (int32 Y = 0; Y < TargetHeight; ++Y)
        {
            const float SourceY = ((static_cast<float>(Y) + 0.5f) * static_cast<float>(SourceHeight) / static_cast<float>(TargetHeight)) - 0.5f;
            const int32 Y0 = FMath::Clamp(FMath::FloorToInt(SourceY), 0, SourceHeight - 1);
            const int32 Y1 = FMath::Clamp(Y0 + 1, 0, SourceHeight - 1);
            const float YAlpha = FMath::Clamp(SourceY - static_cast<float>(Y0), 0.0f, 1.0f);

            for (int32 X = 0; X < TargetWidth; ++X)
            {
                const float SourceX = ((static_cast<float>(X) + 0.5f) * static_cast<float>(SourceWidth) / static_cast<float>(TargetWidth)) - 0.5f;
                const int32 X0 = FMath::Clamp(FMath::FloorToInt(SourceX), 0, SourceWidth - 1);
                const int32 X1 = FMath::Clamp(X0 + 1, 0, SourceWidth - 1);
                const float XAlpha = FMath::Clamp(SourceX - static_cast<float>(X0), 0.0f, 1.0f);

                const FColor& TopLeft = SourcePixels[Y0 * SourceWidth + X0];
                const FColor& TopRight = SourcePixels[Y0 * SourceWidth + X1];
                const FColor& BottomLeft = SourcePixels[Y1 * SourceWidth + X0];
                const FColor& BottomRight = SourcePixels[Y1 * SourceWidth + X1];

                const float BlueTop = FMath::Lerp(static_cast<float>(TopLeft.B), static_cast<float>(TopRight.B), XAlpha);
                const float BlueBottom = FMath::Lerp(static_cast<float>(BottomLeft.B), static_cast<float>(BottomRight.B), XAlpha);
                const float GreenTop = FMath::Lerp(static_cast<float>(TopLeft.G), static_cast<float>(TopRight.G), XAlpha);
                const float GreenBottom = FMath::Lerp(static_cast<float>(BottomLeft.G), static_cast<float>(BottomRight.G), XAlpha);
                const float RedTop = FMath::Lerp(static_cast<float>(TopLeft.R), static_cast<float>(TopRight.R), XAlpha);
                const float RedBottom = FMath::Lerp(static_cast<float>(BottomLeft.R), static_cast<float>(BottomRight.R), XAlpha);
                const float AlphaTop = FMath::Lerp(static_cast<float>(TopLeft.A), static_cast<float>(TopRight.A), XAlpha);
                const float AlphaBottom = FMath::Lerp(static_cast<float>(BottomLeft.A), static_cast<float>(BottomRight.A), XAlpha);

                FColor& OutputPixel = OutPixels[Y * TargetWidth + X];
                OutputPixel.B = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(BlueTop, BlueBottom, YAlpha)), 0, 255));
                OutputPixel.G = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(GreenTop, GreenBottom, YAlpha)), 0, 255));
                OutputPixel.R = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(RedTop, RedBottom, YAlpha)), 0, 255));
                OutputPixel.A = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(AlphaTop, AlphaBottom, YAlpha)), 0, 255));
            }
        }

        return true;
    }

    bool CompressImageBytes(
        const TArray<FColor>& Pixels,
        const int32 Width,
        const int32 Height,
        const EImageFormat Format,
        const int32 Quality,
        TArray<uint8>& OutBytes)
    {
        OutBytes.Reset();

        if (Pixels.Num() != Width * Height || Width <= 0 || Height <= 0)
        {
            return false;
        }

        IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(Format);
        if (!ImageWrapper.IsValid())
        {
            return false;
        }

        if (!ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
        {
            return false;
        }

        const TArray64<uint8> CompressedBytes = ImageWrapper->GetCompressed(Quality);
        OutBytes.Append(CompressedBytes);
        return OutBytes.Num() > 0;
    }

    bool BuildImageDataUrl(const FString& ImagePath, FString& OutDataUrl, FString& OutError)
    {
        OutDataUrl.Empty();
        OutError.Empty();

        if (ImagePath.IsEmpty())
        {
            return true;
        }

        const FString OriginalMimeType = GetImageMimeType(ImagePath);
        if (OriginalMimeType.IsEmpty())
        {
            OutError = TEXT("Only png, jpg, jpeg, webp, gif, and bmp images are supported right now.");
            return false;
        }

        if (!FPaths::FileExists(ImagePath))
        {
            OutError = FString::Printf(TEXT("Attached image was not found: %s"), *ImagePath);
            return false;
        }

        TArray<uint8> FileBytes;
        if (!FFileHelper::LoadFileToArray(FileBytes, *ImagePath))
        {
            OutError = FString::Printf(TEXT("Failed to read attached image: %s"), *ImagePath);
            return false;
        }

        FString OutputMimeType = OriginalMimeType;
        TArray<uint8> OutputBytes = FileBytes;

        IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        const EImageFormat DetectedFormat = ImageWrapperModule.DetectImageFormat(FileBytes.GetData(), FileBytes.Num());
        const bool bCanReencode = DetectedFormat == EImageFormat::PNG || DetectedFormat == EImageFormat::JPEG || DetectedFormat == EImageFormat::BMP;
        if (bCanReencode)
        {
            const TSharedPtr<IImageWrapper> SourceWrapper = ImageWrapperModule.CreateImageWrapper(DetectedFormat);
            if (SourceWrapper.IsValid() && SourceWrapper->SetCompressed(FileBytes.GetData(), FileBytes.Num()))
            {
                TArray64<uint8> RawBytes64;
                if (SourceWrapper->GetRaw(ERGBFormat::BGRA, 8, RawBytes64))
                {
                    const int32 SourceWidth = SourceWrapper->GetWidth();
                    const int32 SourceHeight = SourceWrapper->GetHeight();
                    const int32 PixelCount = SourceWidth * SourceHeight;
                    if (PixelCount > 0 && RawBytes64.Num() == static_cast<int64>(PixelCount) * sizeof(FColor))
                    {
                        TArray<FColor> SourcePixels;
                        SourcePixels.SetNumUninitialized(PixelCount);
                        FMemory::Memcpy(SourcePixels.GetData(), RawBytes64.GetData(), RawBytes64.Num());

                        bool bHasAlpha = false;
                        for (const FColor& Pixel : SourcePixels)
                        {
                            if (Pixel.A < 255)
                            {
                                bHasAlpha = true;
                                break;
                            }
                        }

                        const int32 SourceLongEdge = FMath::Max(SourceWidth, SourceHeight);
                        const float ResizeScale = SourceLongEdge > MaxInlineImageLongEdge
                            ? static_cast<float>(MaxInlineImageLongEdge) / static_cast<float>(SourceLongEdge)
                            : 1.0f;
                        const int32 TargetWidth = FMath::Max(1, FMath::RoundToInt(static_cast<float>(SourceWidth) * ResizeScale));
                        const int32 TargetHeight = FMath::Max(1, FMath::RoundToInt(static_cast<float>(SourceHeight) * ResizeScale));

                        TArray<FColor> WorkingPixels;
                        if (!ResizeImageBilinear(SourcePixels, SourceWidth, SourceHeight, TargetWidth, TargetHeight, WorkingPixels))
                        {
                            WorkingPixels = SourcePixels;
                        }

                        const FString SourceExtension = FPaths::GetExtension(ImagePath, false).ToLower();
                        const bool bPreferPng = bHasAlpha || SourceExtension == TEXT("png");
                        EImageFormat OutputFormat = bPreferPng ? EImageFormat::PNG : EImageFormat::JPEG;
                        int32 Quality = OutputFormat == EImageFormat::JPEG ? PhotoJpegQuality : 100;

                        TArray<uint8> ReencodedBytes;
                        if (CompressImageBytes(WorkingPixels, TargetWidth, TargetHeight, OutputFormat, Quality, ReencodedBytes))
                        {
                            FString ReencodedMimeType = GetMimeTypeFromImageFormat(OutputFormat);
                            if (OutputFormat == EImageFormat::PNG && !bHasAlpha && ReencodedBytes.Num() > MaxPreferredPngBytes)
                            {
                                TArray<uint8> JpegFallbackBytes;
                                if (CompressImageBytes(WorkingPixels, TargetWidth, TargetHeight, EImageFormat::JPEG, ScreenshotFallbackJpegQuality, JpegFallbackBytes) && JpegFallbackBytes.Num() > 0 && JpegFallbackBytes.Num() < ReencodedBytes.Num())
                                {
                                    ReencodedBytes = MoveTemp(JpegFallbackBytes);
                                    ReencodedMimeType = TEXT("image/jpeg");
                                }
                            }

                            const bool bDidResize = TargetWidth != SourceWidth || TargetHeight != SourceHeight;
                            if (!( !bDidResize && ReencodedMimeType.Equals(OriginalMimeType, ESearchCase::CaseSensitive) && ReencodedBytes.Num() >= FileBytes.Num()))
                            {
                                OutputBytes = MoveTemp(ReencodedBytes);
                                OutputMimeType = ReencodedMimeType;
                            }
                        }
                    }
                }
            }
        }

        OutDataUrl = FString::Printf(TEXT("data:%s;base64,%s"), *OutputMimeType, *FBase64::Encode(OutputBytes));
        return true;
    }

    FString BuildUserDisplayText(const FString& UserPrompt, const TArray<FString>& ImagePaths)
    {
        const FString TrimmedPrompt = UserPrompt.TrimStartAndEnd();
        if (ImagePaths.Num() == 0)
        {
            return TrimmedPrompt;
        }

        TArray<FString> AttachmentNames;
        AttachmentNames.Reserve(ImagePaths.Num());
        for (const FString& ImagePath : ImagePaths)
        {
            if (!ImagePath.IsEmpty())
            {
                AttachmentNames.Add(FPaths::GetCleanFilename(ImagePath));
            }
        }

        const FString AttachmentLabel = AttachmentNames.Num() == 1
            ? FString::Printf(TEXT("[Image Attached: %s]"), *AttachmentNames[0])
            : FString::Printf(TEXT("[Images Attached: %s]"), *FString::Join(AttachmentNames, TEXT(", ")));
        if (TrimmedPrompt.IsEmpty())
        {
            return AttachmentLabel;
        }

        return FString::Printf(TEXT("%s\n%s"), *AttachmentLabel, *TrimmedPrompt);
    }

    bool IsOpenAIOfficialUrl(const FString& InBaseUrl)
    {
        FString Normalized = InBaseUrl.TrimStartAndEnd();
        while (Normalized.EndsWith(TEXT("/")))
        {
            Normalized.LeftChopInline(1, EAllowShrinking::No);
        }

        FString Official = OpenAIOfficialBaseUrl;
        while (Official.EndsWith(TEXT("/")))
        {
            Official.LeftChopInline(1, EAllowShrinking::No);
        }

        return Normalized.Equals(Official, ESearchCase::IgnoreCase);
    }
}

FAIEditorAssistantChatController::FAIEditorAssistantChatController(
    TSharedRef<IAIEditorAssistantChatSessionStore> InSessionStore,
    TSharedRef<IAIEditorAssistantChatService> InChatService)
    : SessionStore(InSessionStore)
    , ChatService(InChatService)
{
}

void FAIEditorAssistantChatController::Initialize()
{
    LoadModelFromSettings();
    LoadReasoningFromSettings();
    LoadSessions();
    StatusMessage = TEXT("Select provider and configure API Key in Project Settings > Plugins > AI Editor Assistant. Base URL is only required for Custom provider.");
    RefreshModelOptionsInternal(false);
    RefreshReasoningOptionsInternal(false);
    BroadcastStateChanged();
}

void FAIEditorAssistantChatController::PersistActiveDraft()
{
    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        TouchSession(*Session);
        SessionStore->SaveSession(*Session);
        SaveSessionIndex();
    }
}

void FAIEditorAssistantChatController::SetModel(const FString& InModel)
{
    if (CurrentModel.Equals(InModel, ESearchCase::CaseSensitive))
    {
        return;
    }

    CurrentModel = InModel;
    RefreshReasoningOptionsInternal(false);
    BroadcastStateChanged();
}

void FAIEditorAssistantChatController::RefreshModelOptions()
{
    RefreshModelOptionsInternal(true);
}

void FAIEditorAssistantChatController::SetReasoningMode(const FString& InReasoningMode)
{
    EAIEditorAssistantReasoningIntensity ParsedIntensity = EAIEditorAssistantReasoningIntensity::ProviderDefault;
    if (!TryParseReasoningModeLabel(InReasoningMode, ParsedIntensity))
    {
        return;
    }

    if (CurrentReasoningIntensity == ParsedIntensity)
    {
        return;
    }

    CurrentReasoningIntensity = ParsedIntensity;
    BroadcastStateChanged();
}

void FAIEditorAssistantChatController::RefreshReasoningOptions()
{
    RefreshReasoningOptionsInternal(true);
}

void FAIEditorAssistantChatController::UpdateDraft(const FString& DraftText)
{
    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        Session->DraftPrompt = DraftText;
        TouchSession(*Session);
        BroadcastStateChanged();
    }
}

void FAIEditorAssistantChatController::AddPendingImagePaths(const TArray<FString>& ImagePaths)
{
    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        bool bDidChange = false;
        for (const FString& ImagePath : ImagePaths)
        {
            const FString TrimmedPath = ImagePath.TrimStartAndEnd();
            if (TrimmedPath.IsEmpty() || Session->PendingImagePaths.Contains(TrimmedPath))
            {
                continue;
            }

            Session->PendingImagePaths.Add(TrimmedPath);
            bDidChange = true;
        }

        if (!bDidChange)
        {
            return;
        }

        PersistActiveSession();
        BroadcastStateChanged();
    }
}

void FAIEditorAssistantChatController::RemovePendingImageAt(const int32 ImageIndex)
{
    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        if (!Session->PendingImagePaths.IsValidIndex(ImageIndex))
        {
            return;
        }

        Session->PendingImagePaths.RemoveAt(ImageIndex);
        PersistActiveSession();
        BroadcastStateChanged();
    }
}

void FAIEditorAssistantChatController::ClearPendingImages()
{
    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        if (Session->PendingImagePaths.Num() == 0)
        {
            return;
        }

        Session->PendingImagePaths.Reset();
        PersistActiveSession();
        BroadcastStateChanged();
    }
}

void FAIEditorAssistantChatController::SubmitPrompt()
{
    if (!CanSendRequest())
    {
        return;
    }

    FString SettingsError;
    if (!SaveModelToSettings(SettingsError))
    {
        FinishTurnWithError(SettingsError);
        return;
    }
    if (!SaveReasoningToSettings(SettingsError))
    {
        FinishTurnWithError(SettingsError);
        return;
    }

    FAIEditorAssistantChatSession* Session = GetActiveSession();
    if (Session == nullptr)
    {
        return;
    }

    const FString UserPrompt = Session->DraftPrompt.TrimStartAndEnd();
    TArray<FString> ImagePaths;
    for (const FString& PendingPath : Session->PendingImagePaths)
    {
        const FString TrimmedPath = PendingPath.TrimStartAndEnd();
        if (!TrimmedPath.IsEmpty())
        {
            ImagePaths.Add(TrimmedPath);
        }
    }

    if (UserPrompt.IsEmpty() && ImagePaths.Num() == 0)
    {
        FinishTurnWithError(TEXT("Prompt is empty. Describe what you want the model to do, or attach one or more images."));
        return;
    }

    FString BuildError;
    const TSharedPtr<FJsonObject> UserMessageObject = BuildUserMessageObject(UserPrompt, ImagePaths, BuildError);
    if (!UserMessageObject.IsValid())
    {
        FinishTurnWithError(BuildError.IsEmpty() ? TEXT("Failed to build the user message.") : BuildError);
        return;
    }

    Session->DraftPrompt.Empty();
    BeginUserTurn(BuildUserDisplayText(UserPrompt, ImagePaths), UserMessageObject);

    if (!SendChatRequest())
    {
        FinishTurnWithError(TEXT("Failed to start the chat request."));
    }
}

void FAIEditorAssistantChatController::CancelCurrentWork()
{
    FAIEditorAssistantChatSession* Session = GetActiveSession();
    if (Session == nullptr || !CanCancelWork())
    {
        return;
    }

    if (bIsSending)
    {
        ChatService->CancelActiveRequests();
    }

    ActiveRequestSerial = 0;
    bIsSending = false;
    Session->bAwaitingToolConfirmation = false;
    Session->PendingToolCalls.Reset();
    Session->PendingToolCallIndex = INDEX_NONE;
    Session->StreamingToolCalls.Reset();
    Session->CurrentToolRound = 0;
    Session->PendingResponseBuffer.Empty();
    Session->PendingUserPrompt.Empty();

    if (Session->bAssistantMessageOpen && !Session->StreamedResponseCache.IsEmpty())
    {
        FinalizeAssistantResponse();
    }
    else
    {
        AbortAssistantResponse();
    }

    StatusMessage = TEXT("Canceled the current agent run.");
    AppendMessage(TEXT("System"), TEXT("Canceled the current agent run."));
    PersistActiveSession();
    BroadcastStateChanged();
}

void FAIEditorAssistantChatController::CreateSession()
{
    if (!CanEditSessions())
    {
        return;
    }

    CreateSessionInternal(true);
    BroadcastStateChanged();
}

void FAIEditorAssistantChatController::ActivateSession(const FString& SessionId)
{
    if (!CanEditSessions() || SessionId == ActiveSessionId || FindSessionById(SessionId) == nullptr)
    {
        return;
    }

    PersistActiveDraft();
    ActiveSessionId = SessionId;
    SaveSessionIndex();
    BroadcastStateChanged();
}

void FAIEditorAssistantChatController::CloseSession(const FString& SessionId)
{
    if (!CanEditSessions())
    {
        return;
    }

    const int32 SessionIndex = Sessions.IndexOfByPredicate([&SessionId](const FAIEditorAssistantChatSession& Session)
    {
        return Session.SessionId == SessionId;
    });
    if (!Sessions.IsValidIndex(SessionIndex))
    {
        return;
    }

    SessionStore->DeleteSession(SessionId);
    Sessions.RemoveAt(SessionIndex);

    if (ActiveSessionId == SessionId)
    {
        if (Sessions.Num() == 0)
        {
            CreateSessionInternal(true);
        }
        else
        {
            const int32 NextIndex = FMath::Clamp(SessionIndex, 0, Sessions.Num() - 1);
            ActiveSessionId = Sessions[NextIndex].SessionId;
        }
    }

    SaveSessionIndex();
    BroadcastStateChanged();
}

void FAIEditorAssistantChatController::ApprovePendingTool()
{
    ResumeAfterToolConfirmation(true);
}

void FAIEditorAssistantChatController::RejectPendingTool()
{
    ResumeAfterToolConfirmation(false);
}

FAIEditorAssistantChatPanelViewState FAIEditorAssistantChatController::GetViewState() const
{
    FAIEditorAssistantChatPanelViewState ViewState;
    ViewState.Model = CurrentModel;
    ViewState.ModelOptions = CachedModelOptions;
    ViewState.ModelListStatus = ModelListStatus;
    ViewState.ReasoningMode = ToReasoningModeLabel(CurrentReasoningIntensity);
    ViewState.ReasoningOptionsStatus = ReasoningOptionsStatus;
    for (const EAIEditorAssistantReasoningIntensity ModeOption : CachedReasoningModeOptions)
    {
        ViewState.ReasoningModeOptions.Add(ToReasoningModeLabel(ModeOption));
    }
    ViewState.StatusMessage = StatusMessage;
    ViewState.SendButtonText = GetSendButtonText();
    ViewState.bCanSend = CanSendRequest();
    ViewState.bCanCancel = CanCancelWork();
    ViewState.bCanEditSessions = CanEditSessions();
    ViewState.bIsSending = bIsSending;
    ViewState.bIsGeneratingTitle = IsGeneratingTitle();
    ViewState.bIsModelListLoading = bIsModelListLoading;
    ViewState.bAllowManualModelInput = bAllowManualModelInput || CachedModelOptions.Num() == 0;
    ViewState.bIsReasoningOptionsLoading = bIsReasoningOptionsLoading;

    if (const FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        ViewState.ContextSummary = BuildContextSummary(*Session);
        ViewState.PendingAttachmentSummary = BuildPendingAttachmentSummary(*Session);
        ViewState.PendingAttachmentPaths = Session->PendingImagePaths;
        ViewState.DraftPrompt = Session->DraftPrompt;
        ViewState.ToolConfirmation.bIsVisible = Session->bAwaitingToolConfirmation;
        ViewState.ToolConfirmation.Prompt = GetPendingToolApprovalPrompt();

        for (const FAIEditorAssistantChatMessage& Message : Session->ConversationMessages)
        {
            if (ShouldDisplayConversationMessage(Message))
            {
                ViewState.VisibleMessages.Add(Message);
            }
        }
    }

    for (const FAIEditorAssistantChatSession& Session : Sessions)
    {
        FAIEditorAssistantSessionTabViewData& Tab = ViewState.Sessions.AddDefaulted_GetRef();
        Tab.SessionId = Session.SessionId;
        Tab.Title = Session.Title.IsEmpty() ? MakeNewSessionTitle() : Session.Title;
        Tab.bIsActive = Session.SessionId == ActiveSessionId;
    }

    return ViewState;
}

FSimpleMulticastDelegate& FAIEditorAssistantChatController::OnStateChanged()
{
    return StateChangedDelegate;
}

void FAIEditorAssistantChatController::BroadcastStateChanged()
{
    StateChangedDelegate.Broadcast();
}

bool FAIEditorAssistantChatController::ShouldDisplayConversationMessage(const FAIEditorAssistantChatMessage& Message) const
{
    return !Message.Role.Equals(TEXT("Tool"), ESearchCase::IgnoreCase) &&
        !Message.Role.Equals(TEXT("Tool Result"), ESearchCase::IgnoreCase);
}

bool FAIEditorAssistantChatController::CanSendRequest() const
{
    return GetActiveSession() != nullptr && !bIsSending && !IsAwaitingToolConfirmation() && !IsGeneratingTitle();
}

bool FAIEditorAssistantChatController::CanCancelWork() const
{
    const FAIEditorAssistantChatSession* Session = GetActiveSession();
    if (Session == nullptr || IsGeneratingTitle())
    {
        return false;
    }

    return bIsSending ||
        Session->bAwaitingToolConfirmation ||
        Session->PendingToolCalls.Num() > 0 ||
        Session->bAssistantMessageOpen;
}

bool FAIEditorAssistantChatController::CanEditSessions() const
{
    return !bIsSending && !IsAwaitingToolConfirmation() && !IsGeneratingTitle();
}

bool FAIEditorAssistantChatController::IsAwaitingToolConfirmation() const
{
    const FAIEditorAssistantChatSession* Session = GetActiveSession();
    return Session != nullptr && Session->bAwaitingToolConfirmation;
}

bool FAIEditorAssistantChatController::IsGeneratingTitle() const
{
    return false;
}

int32 FAIEditorAssistantChatController::GetConfiguredMaxToolRounds() const
{
    return FMath::Max(GetDefault<UAIEditorAssistantSettings>()->MaxToolRounds, 1);
}

bool FAIEditorAssistantChatController::ShouldShowToolActivityInChat() const
{
    return GetDefault<UAIEditorAssistantSettings>()->bShowToolActivityInChat;
}

FAIEditorAssistantChatSession* FAIEditorAssistantChatController::GetActiveSession()
{
    return FindSessionById(ActiveSessionId);
}

const FAIEditorAssistantChatSession* FAIEditorAssistantChatController::GetActiveSession() const
{
    return FindSessionById(ActiveSessionId);
}

FAIEditorAssistantChatSession* FAIEditorAssistantChatController::FindSessionById(const FString& SessionId)
{
    return Sessions.FindByPredicate([&SessionId](const FAIEditorAssistantChatSession& Session)
    {
        return Session.SessionId == SessionId;
    });
}

const FAIEditorAssistantChatSession* FAIEditorAssistantChatController::FindSessionById(const FString& SessionId) const
{
    return Sessions.FindByPredicate([&SessionId](const FAIEditorAssistantChatSession& Session)
    {
        return Session.SessionId == SessionId;
    });
}

void FAIEditorAssistantChatController::LoadModelFromSettings()
{
    CurrentModel = GetDefault<UAIEditorAssistantSettings>()->Model.TrimStartAndEnd();
}

void FAIEditorAssistantChatController::LoadReasoningFromSettings()
{
    CurrentReasoningIntensity = GetDefault<UAIEditorAssistantSettings>()->ReasoningIntensity;
}

void FAIEditorAssistantChatController::RefreshModelOptionsInternal(const bool bUserInitiated)
{
    FString ResolveError;
    FAIEditorAssistantChatServiceSettings Settings;
    if (!ResolveModelListSettings(Settings, ResolveError))
    {
        bIsModelListLoading = false;
        bAllowManualModelInput = true;
        CachedModelOptions.Reset();
        ModelListStatus = ResolveError;
        BroadcastStateChanged();
        return;
    }

    bIsModelListLoading = true;
    bAllowManualModelInput = false;
    ModelListStatus = bUserInitiated ? TEXT("Refreshing available models...") : TEXT("Loading available models...");
    const int32 RequestSerial = ++NextModelListRequestSerial;
    ActiveModelListRequestSerial = RequestSerial;
    BroadcastStateChanged();

    const TWeakPtr<FAIEditorAssistantChatController> WeakController = AsShared();
    if (!ChatService->FetchAvailableModels(
            Settings,
            [WeakController, RequestSerial](const FAIEditorAssistantModelListResponse& Response)
            {
                const TSharedPtr<FAIEditorAssistantChatController> Pinned = WeakController.Pin();
                if (!Pinned.IsValid() || Pinned->ActiveModelListRequestSerial != RequestSerial)
                {
                    return;
                }

                Pinned->ActiveModelListRequestSerial = 0;
                Pinned->bIsModelListLoading = false;
                if (Response.bRequestSucceeded && Response.Models.Num() > 0)
                {
                    Pinned->CachedModelOptions = Response.Models;
                    Pinned->bAllowManualModelInput = false;
                    Pinned->ModelListStatus = FString::Printf(TEXT("Loaded %d model(s) from provider."), Response.Models.Num());

                    const bool bCurrentModelInList = Pinned->CachedModelOptions.ContainsByPredicate([&Pinned](const FString& Value)
                    {
                        return Value.Equals(Pinned->CurrentModel, ESearchCase::CaseSensitive);
                    });
                    if (!bCurrentModelInList)
                    {
                        Pinned->CurrentModel = Pinned->CachedModelOptions[0];
                        Pinned->RefreshReasoningOptionsInternal(false);
                    }
                }
                else
                {
                    Pinned->CachedModelOptions.Reset();
                    Pinned->bAllowManualModelInput = true;
                    Pinned->ModelListStatus = Response.ErrorMessage.IsEmpty()
                        ? TEXT("Failed to load models. You can still type a model name manually.")
                        : FString::Printf(TEXT("%s You can still type a model name manually."), *Response.ErrorMessage);
                }

                Pinned->BroadcastStateChanged();
            }))
    {
        ActiveModelListRequestSerial = 0;
        bIsModelListLoading = false;
        bAllowManualModelInput = true;
        CachedModelOptions.Reset();
        ModelListStatus = TEXT("Failed to start model list request. You can still type a model name manually.");
        BroadcastStateChanged();
    }
}

void FAIEditorAssistantChatController::RefreshReasoningOptionsInternal(const bool bUserInitiated)
{
    FString ResolveError;
    FAIEditorAssistantChatServiceSettings Settings;
    if (!ResolveModelListSettings(Settings, ResolveError))
    {
        bIsReasoningOptionsLoading = false;
        CachedReasoningModeOptions = {
            EAIEditorAssistantReasoningIntensity::ProviderDefault,
            EAIEditorAssistantReasoningIntensity::Disabled,
            EAIEditorAssistantReasoningIntensity::Medium
        };
        ReasoningOptionsStatus = ResolveError;
        BroadcastStateChanged();
        return;
    }

    const FString Model = CurrentModel.TrimStartAndEnd();
    if (Model.IsEmpty())
    {
        bIsReasoningOptionsLoading = false;
        CachedReasoningModeOptions = { EAIEditorAssistantReasoningIntensity::ProviderDefault };
        ReasoningOptionsStatus = TEXT("Select a model first to load reasoning options.");
        BroadcastStateChanged();
        return;
    }

    bIsReasoningOptionsLoading = true;
    ReasoningOptionsStatus = bUserInitiated ? TEXT("Refreshing reasoning options...") : TEXT("Loading reasoning options...");
    const int32 RequestSerial = ++NextReasoningOptionsRequestSerial;
    ActiveReasoningOptionsRequestSerial = RequestSerial;
    BroadcastStateChanged();

    const TWeakPtr<FAIEditorAssistantChatController> WeakController = AsShared();
    if (!ChatService->FetchReasoningOptions(
            Settings,
            Model,
            [WeakController, RequestSerial](const FAIEditorAssistantReasoningOptionsResponse& Response)
            {
                const TSharedPtr<FAIEditorAssistantChatController> Pinned = WeakController.Pin();
                if (!Pinned.IsValid() || Pinned->ActiveReasoningOptionsRequestSerial != RequestSerial)
                {
                    return;
                }

                Pinned->ActiveReasoningOptionsRequestSerial = 0;
                Pinned->bIsReasoningOptionsLoading = false;
                Pinned->CachedReasoningModeOptions = Response.Options;
                if (Pinned->CachedReasoningModeOptions.Num() == 0)
                {
                    Pinned->CachedReasoningModeOptions = { EAIEditorAssistantReasoningIntensity::ProviderDefault };
                }

                const bool bContainsCurrent = Pinned->CachedReasoningModeOptions.Contains(Pinned->CurrentReasoningIntensity);
                if (!bContainsCurrent)
                {
                    Pinned->CurrentReasoningIntensity = Pinned->CachedReasoningModeOptions[0];
                }

                Pinned->ReasoningOptionsStatus = Response.bRequestSucceeded
                    ? TEXT("Reasoning options loaded from provider.")
                    : (Response.ErrorMessage.IsEmpty()
                        ? TEXT("Using fallback reasoning options for this provider/model.")
                        : FString::Printf(TEXT("%s Using fallback reasoning options."), *Response.ErrorMessage));
                Pinned->BroadcastStateChanged();
            }))
    {
        ActiveReasoningOptionsRequestSerial = 0;
        bIsReasoningOptionsLoading = false;
        CachedReasoningModeOptions = {
            EAIEditorAssistantReasoningIntensity::ProviderDefault,
            EAIEditorAssistantReasoningIntensity::Disabled,
            EAIEditorAssistantReasoningIntensity::Medium
        };
        ReasoningOptionsStatus = TEXT("Failed to start reasoning options request. Using fallback options.");
        BroadcastStateChanged();
    }
}

bool FAIEditorAssistantChatController::ResolveModelListSettings(FAIEditorAssistantChatServiceSettings& OutSettings, FString& OutError) const
{
    const UAIEditorAssistantSettings* Settings = GetDefault<UAIEditorAssistantSettings>();
    FString BaseUrl;
    OutSettings.Provider = ResolveEffectiveProvider(*Settings, BaseUrl);
    OutSettings.BaseUrl = BaseUrl;
    OutSettings.ApiKey = Settings->ApiKey.TrimStartAndEnd();
    OutSettings.ReasoningIntensity = Settings->ReasoningIntensity;

    if (OutSettings.BaseUrl.IsEmpty())
    {
        OutError = OutSettings.Provider == EAIEditorAssistantAPIProvider::Custom
            ? TEXT("Base URL is required for Custom provider.")
            : TEXT("Provider base URL is empty.");
        return false;
    }

    if (!OutSettings.BaseUrl.StartsWith(TEXT("http://")) && !OutSettings.BaseUrl.StartsWith(TEXT("https://")))
    {
        OutError = TEXT("Provider base URL must start with http:// or https://");
        return false;
    }

    if (OutSettings.ApiKey.IsEmpty())
    {
        OutError = TEXT("API Key is not configured. Open Project Settings > Plugins > AI Editor Assistant.");
        return false;
    }

    return true;
}

bool FAIEditorAssistantChatController::SaveModelToSettings(FString& OutError) const
{
    const FString Model = CurrentModel.TrimStartAndEnd();
    if (Model.IsEmpty())
    {
        OutError = TEXT("Model is required. Example: gpt-4o-mini");
        return false;
    }

    UAIEditorAssistantSettings* MutableSettings = GetMutableDefault<UAIEditorAssistantSettings>();
    MutableSettings->Model = Model;
    MutableSettings->SaveConfig();
    return true;
}

bool FAIEditorAssistantChatController::SaveReasoningToSettings(FString& OutError) const
{
    UAIEditorAssistantSettings* MutableSettings = GetMutableDefault<UAIEditorAssistantSettings>();
    MutableSettings->ReasoningIntensity = CurrentReasoningIntensity;
    MutableSettings->SaveConfig();
    return true;
}

EAIEditorAssistantAPIProvider FAIEditorAssistantChatController::ResolveEffectiveProvider(const UAIEditorAssistantSettings& Settings, FString& OutBaseUrl) const
{
    OutBaseUrl = Settings.BaseUrl.TrimStartAndEnd();
    while (OutBaseUrl.EndsWith(TEXT("/")))
    {
        OutBaseUrl.LeftChopInline(1, EAllowShrinking::No);
    }

    switch (Settings.Provider)
    {
    case EAIEditorAssistantAPIProvider::OpenAI:
        OutBaseUrl = OpenAIOfficialBaseUrl;
        return EAIEditorAssistantAPIProvider::OpenAI;

    case EAIEditorAssistantAPIProvider::DeepSeek:
        OutBaseUrl = DeepSeekOfficialBaseUrl;
        return EAIEditorAssistantAPIProvider::DeepSeek;

    case EAIEditorAssistantAPIProvider::Custom:
        return EAIEditorAssistantAPIProvider::Custom;

    case EAIEditorAssistantAPIProvider::OpenAICompatible:
        if (OutBaseUrl.IsEmpty() || IsOpenAIOfficialUrl(OutBaseUrl))
        {
            OutBaseUrl = OpenAIOfficialBaseUrl;
            return EAIEditorAssistantAPIProvider::OpenAI;
        }
        return EAIEditorAssistantAPIProvider::Custom;

    case EAIEditorAssistantAPIProvider::Anthropic:
    case EAIEditorAssistantAPIProvider::Gemini:
    default:
        return EAIEditorAssistantAPIProvider::Custom;
    }
}

bool FAIEditorAssistantChatController::ResolveServiceSettings(FAIEditorAssistantChatServiceSettings& OutSettings, FString& OutError) const
{
    const UAIEditorAssistantSettings* Settings = GetDefault<UAIEditorAssistantSettings>();
    FString BaseUrl;
    OutSettings.Provider = ResolveEffectiveProvider(*Settings, BaseUrl);
    OutSettings.BaseUrl = BaseUrl;
    OutSettings.ApiKey = Settings->ApiKey.TrimStartAndEnd();
    OutSettings.Model = CurrentModel.TrimStartAndEnd();
    OutSettings.ReasoningIntensity = CurrentReasoningIntensity;

    if (OutSettings.BaseUrl.IsEmpty())
    {
        OutError = OutSettings.Provider == EAIEditorAssistantAPIProvider::Custom
            ? TEXT("Base URL is not configured for Custom provider. Open Project Settings > Plugins > AI Editor Assistant.")
            : TEXT("Provider base URL is empty.");
        return false;
    }

    if (!OutSettings.BaseUrl.StartsWith(TEXT("http://")) && !OutSettings.BaseUrl.StartsWith(TEXT("https://")))
    {
        OutError = TEXT("Base URL in Project Settings must start with http:// or https://");
        return false;
    }

    if (OutSettings.ApiKey.IsEmpty())
    {
        OutError = TEXT("API Key is not configured. Open Project Settings > Plugins > AI Editor Assistant.");
        return false;
    }

    if (OutSettings.Model.IsEmpty())
    {
        OutError = TEXT("Model is required. Example: gpt-4o-mini");
        return false;
    }

    return true;
}

FString FAIEditorAssistantChatController::ToReasoningModeLabel(const EAIEditorAssistantReasoningIntensity Intensity) const
{
    switch (Intensity)
    {
    case EAIEditorAssistantReasoningIntensity::ProviderDefault:
        return TEXT("Provider Default");
    case EAIEditorAssistantReasoningIntensity::Disabled:
        return TEXT("Disabled");
    case EAIEditorAssistantReasoningIntensity::Minimal:
        return TEXT("Minimal");
    case EAIEditorAssistantReasoningIntensity::Low:
        return TEXT("Low");
    case EAIEditorAssistantReasoningIntensity::Medium:
        return TEXT("Medium");
    case EAIEditorAssistantReasoningIntensity::High:
        return TEXT("High");
    case EAIEditorAssistantReasoningIntensity::Maximum:
        return TEXT("Maximum");
    default:
        return TEXT("Provider Default");
    }
}

bool FAIEditorAssistantChatController::TryParseReasoningModeLabel(const FString& Label, EAIEditorAssistantReasoningIntensity& OutIntensity) const
{
    const FString Normalized = Label.TrimStartAndEnd().ToLower();
    if (Normalized == TEXT("provider default") || Normalized == TEXT("providerdefault") || Normalized == TEXT("default"))
    {
        OutIntensity = EAIEditorAssistantReasoningIntensity::ProviderDefault;
        return true;
    }
    if (Normalized == TEXT("disabled") || Normalized == TEXT("none"))
    {
        OutIntensity = EAIEditorAssistantReasoningIntensity::Disabled;
        return true;
    }
    if (Normalized == TEXT("minimal"))
    {
        OutIntensity = EAIEditorAssistantReasoningIntensity::Minimal;
        return true;
    }
    if (Normalized == TEXT("low"))
    {
        OutIntensity = EAIEditorAssistantReasoningIntensity::Low;
        return true;
    }
    if (Normalized == TEXT("medium") || Normalized == TEXT("enabled"))
    {
        OutIntensity = EAIEditorAssistantReasoningIntensity::Medium;
        return true;
    }
    if (Normalized == TEXT("high"))
    {
        OutIntensity = EAIEditorAssistantReasoningIntensity::High;
        return true;
    }
    if (Normalized == TEXT("maximum") || Normalized == TEXT("xhigh") || Normalized == TEXT("max"))
    {
        OutIntensity = EAIEditorAssistantReasoningIntensity::Maximum;
        return true;
    }
    return false;
}

FString FAIEditorAssistantChatController::BuildContextSummary(const FAIEditorAssistantChatSession& Session) const
{
    int32 SerializedCharacterCount = 0;
    for (const TSharedPtr<FJsonObject>& MessageObject : Session.RequestMessages)
    {
        SerializedCharacterCount += SerializeJsonObject(MessageObject).Len();
    }

    const int32 DraftCharacterCount = Session.DraftPrompt.Len();
    const int32 ApproxTokenCount = FMath::CeilToInt(static_cast<float>(SerializedCharacterCount) / 4.0f);
    const FString DraftSuffix = DraftCharacterCount > 0
        ? FString::Printf(TEXT(" | Draft: %d chars"), DraftCharacterCount)
        : FString();

    return FString::Printf(
            TEXT("Context: %d request message(s) | %d chars | ~%d token(s)%s"),
        Session.RequestMessages.Num(),
        SerializedCharacterCount,
        ApproxTokenCount,
        *DraftSuffix);
}

FString FAIEditorAssistantChatController::BuildPendingAttachmentSummary(const FAIEditorAssistantChatSession& Session) const
{
    if (Session.PendingImagePaths.Num() == 0)
    {
        return TEXT("Attachment: none");
    }

    if (Session.PendingImagePaths.Num() == 1)
    {
        return FString::Printf(TEXT("Attachment: %s"), *FPaths::GetCleanFilename(Session.PendingImagePaths[0]));
    }

    return FString::Printf(TEXT("Attachments: %d image(s)"), Session.PendingImagePaths.Num());
}

void FAIEditorAssistantChatController::AppendMessage(const FString& Role, const FString& Text)
{
    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        Session->ConversationMessages.Add({ Role, Text });
        BroadcastStateChanged();
    }
}

void FAIEditorAssistantChatController::UpsertLastMessage(const FString& Role, const FString& Text)
{
    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        if (Session->ConversationMessages.Num() > 0 && Session->ConversationMessages.Last().Role.Equals(Role, ESearchCase::IgnoreCase))
        {
            Session->ConversationMessages.Last().Content = Text;
        }
        else
        {
            Session->ConversationMessages.Add({ Role, Text });
        }

        BroadcastStateChanged();
    }
}

void FAIEditorAssistantChatController::AddRequestMessage(const TSharedPtr<FJsonObject>& MessageObject)
{
    if (MessageObject.IsValid())
    {
        if (FAIEditorAssistantChatSession* Session = GetActiveSession())
        {
            Session->RequestMessages.Add(MessageObject);
        }
    }
}

void FAIEditorAssistantChatController::TouchSession(FAIEditorAssistantChatSession& Session)
{
    if (Session.CreatedAt == FDateTime())
    {
        Session.CreatedAt = FDateTime::UtcNow();
    }

    Session.UpdatedAt = FDateTime::UtcNow();
}

void FAIEditorAssistantChatController::PersistActiveSession()
{
    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        TouchSession(*Session);
        SessionStore->SaveSession(*Session);
        SaveSessionIndex();
    }
}

void FAIEditorAssistantChatController::SaveSessionIndex() const
{
    FAIEditorAssistantChatSessionIndex SessionIndex;
    SessionIndex.ActiveSessionId = ActiveSessionId;
    for (const FAIEditorAssistantChatSession& Session : Sessions)
    {
        SessionIndex.OpenSessionIds.Add(Session.SessionId);
    }

    SessionStore->SaveSessionIndex(SessionIndex);
}

void FAIEditorAssistantChatController::LoadSessions()
{
    Sessions.Reset();
    ActiveSessionId.Empty();

    FAIEditorAssistantChatSessionIndex SessionIndex;
    SessionStore->LoadSessionIndex(SessionIndex);

    for (const FString& SessionId : SessionIndex.OpenSessionIds)
    {
        FAIEditorAssistantChatSession LoadedSession;
        if (SessionStore->LoadSession(SessionId, LoadedSession))
        {
            if (LoadedSession.Title.IsEmpty())
            {
                LoadedSession.Title = MakeNewSessionTitle();
            }
            Sessions.Add(MoveTemp(LoadedSession));
        }
    }

    if (Sessions.Num() == 0)
    {
        CreateSessionInternal(true);
        return;
    }

    ActiveSessionId = SessionIndex.ActiveSessionId;
    EnsureActiveSession();
}

FAIEditorAssistantChatSession& FAIEditorAssistantChatController::CreateSessionInternal(bool bMakeActive)
{
    FAIEditorAssistantChatSession& Session = Sessions.AddDefaulted_GetRef();
    Session.SessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    Session.Title = MakeNewSessionTitle();
    Session.CreatedAt = FDateTime::UtcNow();
    Session.UpdatedAt = Session.CreatedAt;
    Session.bHasGeneratedTitle = false;

    if (bMakeActive)
    {
        ActiveSessionId = Session.SessionId;
        Session.DraftPrompt.Empty();
    }

    SessionStore->SaveSession(Session);
    SaveSessionIndex();
    return Session;
}

void FAIEditorAssistantChatController::EnsureActiveSession()
{
    if (GetActiveSession() != nullptr)
    {
        return;
    }

    if (Sessions.Num() == 0)
    {
        CreateSessionInternal(true);
        return;
    }

    ActiveSessionId = Sessions[0].SessionId;
}

FString FAIEditorAssistantChatController::MakeNewSessionTitle() const
{
    return DefaultSessionTitle;
}

FString FAIEditorAssistantChatController::GenerateFallbackTitle(const FAIEditorAssistantChatSession& Session) const
{
    FString FirstPrompt = GetFirstConversationMessageByRole(Session, TEXT("You"));
    FirstPrompt = FAIEditorAssistantMarkdownParser::NormalizeLineEndings(FirstPrompt);
    FirstPrompt.ReplaceInline(TEXT("\n"), TEXT(" "));
    FirstPrompt.ReplaceInline(TEXT("\r"), TEXT(" "));
    FirstPrompt = FirstPrompt.TrimStartAndEnd();

    return FirstPrompt.IsEmpty() ? MakeNewSessionTitle() : TruncateWithEllipsis(FirstPrompt, 20);
}

bool FAIEditorAssistantChatController::ShouldGenerateTitle(const FAIEditorAssistantChatSession& Session) const
{
    if (Session.bHasGeneratedTitle)
    {
        return false;
    }

    const FString CurrentTitle = Session.Title.TrimStartAndEnd();
    if (!CurrentTitle.IsEmpty() && !CurrentTitle.Equals(MakeNewSessionTitle(), ESearchCase::CaseSensitive))
    {
        return false;
    }

    return !GetFirstConversationMessageByRole(Session, TEXT("You")).IsEmpty() &&
        !GetLastConversationMessageByRole(Session, TEXT("AI")).IsEmpty();
}

void FAIEditorAssistantChatController::MaybeGenerateTitleForActiveSession()
{
    FAIEditorAssistantChatSession* Session = GetActiveSession();
    if (Session == nullptr || !ShouldGenerateTitle(*Session))
    {
        return;
    }

    Session->Title = GenerateFallbackTitle(*Session);
    Session->bHasGeneratedTitle = true;
    TouchSession(*Session);
    SessionStore->SaveSession(*Session);
    SaveSessionIndex();
    StatusMessage = TEXT("Response received. Session title was generated locally.");
    BroadcastStateChanged();
}

void FAIEditorAssistantChatController::StartAssistantResponse()
{
    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        Session->bAssistantMessageOpen = true;
        Session->StreamedResponseCache.Empty();
        Session->StreamedReasoningCache.Empty();
        Session->PendingResponseBuffer.Empty();
        Session->StreamingToolCalls.Reset();
        AppendMessage(TEXT("AI"), TEXT(""));
    }
}

void FAIEditorAssistantChatController::FinalizeAssistantResponse()
{
    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        if (!Session->bAssistantMessageOpen)
        {
            return;
        }

        Session->bAssistantMessageOpen = false;
        UpsertLastMessage(TEXT("AI"), Session->StreamedResponseCache.IsEmpty() ? TEXT("(No content returned)") : Session->StreamedResponseCache);
    }
}

void FAIEditorAssistantChatController::AbortAssistantResponse()
{
    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        if (!Session->bAssistantMessageOpen)
        {
            return;
        }

        Session->bAssistantMessageOpen = false;
        Session->StreamedResponseCache.Empty();
        Session->StreamedReasoningCache.Empty();
        Session->PendingResponseBuffer.Empty();

        if (Session->ConversationMessages.Num() > 0 &&
            Session->ConversationMessages.Last().Role.Equals(TEXT("AI"), ESearchCase::IgnoreCase) &&
            Session->ConversationMessages.Last().Content.IsEmpty())
        {
            Session->ConversationMessages.Pop();
            BroadcastStateChanged();
        }
    }
}

void FAIEditorAssistantChatController::ResetStreamingState()
{
    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        Session->PendingResponseBuffer.Empty();
        Session->StreamedResponseCache.Empty();
        Session->StreamedReasoningCache.Empty();
        Session->StreamingToolCalls.Reset();
    }
}

void FAIEditorAssistantChatController::BeginUserTurn(const FString& UserDisplayText, const TSharedPtr<FJsonObject>& UserMessageObject)
{
    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        Session->CurrentToolRound = 0;
        Session->PendingToolCalls.Reset();
        Session->PendingToolCallIndex = INDEX_NONE;
        Session->bAwaitingToolConfirmation = false;
        Session->PendingUserPrompt = UserDisplayText;
        Session->DraftPrompt.Empty();
        Session->PendingImagePaths.Reset();

        AppendMessage(TEXT("You"), UserDisplayText);
        AddRequestMessage(UserMessageObject);
        PersistActiveSession();
    }
}

bool FAIEditorAssistantChatController::SendChatRequest()
{
    FAIEditorAssistantChatSession* Session = GetActiveSession();
    if (Session == nullptr)
    {
        return false;
    }

    FString SettingsError;
    FAIEditorAssistantChatServiceSettings Settings;
    if (!ResolveServiceSettings(Settings, SettingsError))
    {
        FinishTurnWithError(SettingsError);
        return false;
    }

    FAIEditorAssistantChatCompletionRequest Request;
    Request.bStream = true;
    Request.Messages = BuildRequestMessages();
    Request.Tools = BuildToolDefinitions();
    Request.ToolChoice = Request.Tools.Num() > 0 ? TEXT("auto") : FString();

    ResetStreamingState();
    bIsSending = true;
    const int32 RequestSerial = ++NextRequestSerial;
    ActiveRequestSerial = RequestSerial;
    StartAssistantResponse();
    StatusMessage = Session->CurrentToolRound == 0
        ? TEXT("Sending request with native UE tools enabled...")
        : TEXT("Continuing the tool loop...");
    BroadcastStateChanged();

    const TWeakPtr<FAIEditorAssistantChatController> WeakController = AsShared();
    if (!ChatService->SendStreamingChatRequest(
            Settings,
            Request,
            [WeakController, RequestSerial](const FString& ChunkText)
            {
                if (const TSharedPtr<FAIEditorAssistantChatController> Pinned = WeakController.Pin())
                {
                    Pinned->HandleStreamingPayloadChunk(RequestSerial, ChunkText);
                }
            },
            [WeakController, RequestSerial](const FAIEditorAssistantChatServiceResponse& Response)
            {
                if (const TSharedPtr<FAIEditorAssistantChatController> Pinned = WeakController.Pin())
                {
                    Pinned->HandleChatResponse(RequestSerial, Response);
                }
            }))
    {
        ActiveRequestSerial = 0;
        bIsSending = false;
        AbortAssistantResponse();
        BroadcastStateChanged();
        return false;
    }

    return true;
}

void FAIEditorAssistantChatController::ExecuteNextPendingToolCall()
{
    FAIEditorAssistantChatSession* Session = GetActiveSession();
    if (Session == nullptr)
    {
        return;
    }

    if (!Session->PendingToolCalls.IsValidIndex(Session->PendingToolCallIndex))
    {
        Session->PendingToolCalls.Reset();
        Session->PendingToolCallIndex = INDEX_NONE;
        Session->bAwaitingToolConfirmation = false;

        const int32 ConfiguredMaxToolRounds = GetConfiguredMaxToolRounds();
        if (Session->CurrentToolRound + 1 >= ConfiguredMaxToolRounds)
        {
            FinishTurnWithError(
                FString::Printf(
                    TEXT("Stopped after the maximum number of tool rounds. The model did not finish within %d tool loops."),
                    ConfiguredMaxToolRounds),
                true);
            return;
        }

        ++Session->CurrentToolRound;
        if (!SendChatRequest())
        {
            FinishTurnWithError(TEXT("Failed to continue the tool loop."));
        }
        return;
    }

    const FAIEditorAssistantPendingToolCall& ToolCall = Session->PendingToolCalls[Session->PendingToolCallIndex];
    const FAIEditorAssistantToolDefinition* Definition = FAIEditorAssistantToolRuntime::Get().FindDefinition(ToolCall.Name);
    if (Definition == nullptr)
    {
        const FAIEditorAssistantToolResult Result = FAIEditorAssistantToolResult::Error(FString::Printf(TEXT("Unknown tool '%s'."), *ToolCall.Name));
        AddRequestMessage(BuildToolResultMessageObject(ToolCall.Id, ToolCall.Name, Result.ToMessageContent()));
        AppendToolResultMessage(ToolCall, Result);
        PersistActiveSession();
        ++Session->PendingToolCallIndex;
        ExecuteNextPendingToolCall();
        return;
    }

    if (Definition->ConfirmationPolicy == EAIEditorAssistantToolConfirmationPolicy::ExplicitApproval)
    {
        Session->bAwaitingToolConfirmation = true;
        StatusMessage = TEXT("Waiting for your approval before executing the requested tool.");
        PersistActiveSession();
        BroadcastStateChanged();
        return;
    }

    ExecuteCurrentPendingToolCall(true);
}

void FAIEditorAssistantChatController::ExecuteCurrentPendingToolCall(bool bApproved)
{
    FAIEditorAssistantChatSession* Session = GetActiveSession();
    if (Session == nullptr || !Session->PendingToolCalls.IsValidIndex(Session->PendingToolCallIndex))
    {
        return;
    }

    const FAIEditorAssistantPendingToolCall ToolCall = Session->PendingToolCalls[Session->PendingToolCallIndex];
    const FAIEditorAssistantToolResult Result = bApproved
        ? FAIEditorAssistantToolRuntime::Get().ExecuteTool(ToolCall.Name, ToolCall.Arguments.IsValid() ? ToolCall.Arguments : MakeShared<FJsonObject>())
        : FAIEditorAssistantToolResult::Rejected(TEXT("The user rejected this tool call."));

    AddRequestMessage(BuildToolResultMessageObject(ToolCall.Id, ToolCall.Name, Result.ToMessageContent()));
    AppendToolResultMessage(ToolCall, Result);
    StatusMessage = Result.Summary;
    PersistActiveSession();
    BroadcastStateChanged();

    ++Session->PendingToolCallIndex;
    ExecuteNextPendingToolCall();
}

void FAIEditorAssistantChatController::ResumeAfterToolConfirmation(bool bApproved)
{
    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        if (!Session->PendingToolCalls.IsValidIndex(Session->PendingToolCallIndex))
        {
            Session->bAwaitingToolConfirmation = false;
            BroadcastStateChanged();
            return;
        }

        Session->bAwaitingToolConfirmation = false;
        BroadcastStateChanged();
        ExecuteCurrentPendingToolCall(bApproved);
    }
}

void FAIEditorAssistantChatController::FinishTurnWithError(const FString& ErrorMessage, bool bKeepAssistantPlaceholder)
{
    bIsSending = false;

    if (FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        Session->bAwaitingToolConfirmation = false;
        Session->PendingToolCalls.Reset();
        Session->PendingToolCallIndex = INDEX_NONE;
        Session->StreamingToolCalls.Reset();
        Session->CurrentToolRound = 0;

        if (!bKeepAssistantPlaceholder)
        {
            AbortAssistantResponse();
        }
    }

    StatusMessage = ErrorMessage;
    AppendMessage(TEXT("System"), ErrorMessage);
    PersistActiveSession();
    BroadcastStateChanged();
}

void FAIEditorAssistantChatController::HandleChatResponse(int32 RequestSerial, const FAIEditorAssistantChatServiceResponse& Response)
{
    if (RequestSerial != ActiveRequestSerial)
    {
        return;
    }

    ActiveRequestSerial = 0;
    bIsSending = false;

    FAIEditorAssistantChatSession* Session = GetActiveSession();
    if (Session == nullptr)
    {
        return;
    }

    if (!Response.bRequestSucceeded)
    {
        FinishTurnWithError(TEXT("Request failed before a valid response was received. Check the URL, key, and network connection."));
        return;
    }

    if (Response.ResponseCode < 200 || Response.ResponseCode >= 300)
    {
        FString ErrorMessage = FString::Printf(TEXT("Request failed with HTTP %d."), Response.ResponseCode);
        const FString ServiceMessage = ExtractErrorMessage(Response.ResponseBody);
        if (!ServiceMessage.IsEmpty())
        {
            ErrorMessage = FString::Printf(TEXT("%s %s"), *ErrorMessage, *ServiceMessage);
        }

        FinishTurnWithError(ErrorMessage);
        return;
    }

    FString ParsedAssistantContent;
    FString ParsedReasoningContent;
    TArray<FAIEditorAssistantPendingToolCall> ParsedToolCalls;
    TSharedPtr<FJsonObject> ParsedProviderPayload;
    bool bHadChoices = false;
    ParseChatCompletionPayload(Response.ResponseBody, ParsedAssistantContent, ParsedReasoningContent, ParsedToolCalls, ParsedProviderPayload, bHadChoices);

    if (ParsedAssistantContent.IsEmpty())
    {
        ParsedAssistantContent = Session->StreamedResponseCache;
    }
    else
    {
        Session->StreamedResponseCache = ParsedAssistantContent;
    }

    if (ParsedReasoningContent.IsEmpty())
    {
        ParsedReasoningContent = Session->StreamedReasoningCache;
    }
    else
    {
        Session->StreamedReasoningCache = ParsedReasoningContent;
    }

    if (ParsedToolCalls.Num() == 0 && Session->StreamingToolCalls.Num() > 0)
    {
        for (const FAIEditorAssistantStreamingToolCall& StreamingToolCall : Session->StreamingToolCalls)
        {
            FAIEditorAssistantPendingToolCall ToolCall;
            ToolCall.Id = StreamingToolCall.Id;
            ToolCall.Name = StreamingToolCall.Name;
            ToolCall.ArgumentsJson = NormalizeToolArgumentsJson(StreamingToolCall.ArgumentsJson);
            ToolCall.Arguments = ParseJsonObject(ToolCall.ArgumentsJson);
            ParsedToolCalls.Add(ToolCall);
        }
    }

    if (ParsedToolCalls.Num() > 0)
    {
        if (!ParsedAssistantContent.IsEmpty())
        {
            FinalizeAssistantResponse();
        }
        else
        {
            AbortAssistantResponse();
        }

        AddRequestMessage(BuildAssistantMessageObject(ParsedAssistantContent, ParsedReasoningContent, ParsedToolCalls, ParsedProviderPayload));
        AppendToolCallMessages(ParsedToolCalls);
        Session->PendingToolCalls = ParsedToolCalls;
        Session->PendingToolCallIndex = 0;
        PersistActiveSession();
        BroadcastStateChanged();
        ExecuteNextPendingToolCall();
        return;
    }

    if (!ParsedAssistantContent.IsEmpty())
    {
        FinalizeAssistantResponse();
        AddRequestMessage(BuildAssistantMessageObject(ParsedAssistantContent, ParsedReasoningContent, {}, ParsedProviderPayload));
        PersistActiveSession();
        MaybeGenerateTitleForActiveSession();

        if (!IsGeneratingTitle())
        {
            StatusMessage = TEXT("Response received. Context and tool history are ready for the next turn.");
            BroadcastStateChanged();
        }
        return;
    }

    FinishTurnWithError(
        bHadChoices
            ? TEXT("The provider returned a completion with no assistant text and no tool calls. Make sure your provider supports OpenAI-style tool calling.")
            : TEXT("The response did not contain a usable assistant message or tool call payload."));
}

void FAIEditorAssistantChatController::HandleStreamingPayloadChunk(int32 RequestSerial, const FString& ChunkText)
{
    if (RequestSerial != ActiveRequestSerial)
    {
        return;
    }

    FAIEditorAssistantChatSession* Session = GetActiveSession();
    if (Session == nullptr)
    {
        return;
    }

    Session->PendingResponseBuffer.Append(ChunkText);

    FString NormalizedBuffer = FAIEditorAssistantMarkdownParser::NormalizeLineEndings(Session->PendingResponseBuffer);
    TArray<FString> Lines;
    NormalizedBuffer.ParseIntoArray(Lines, TEXT("\n"), false);

    const bool bEndsWithNewline = NormalizedBuffer.EndsWith(TEXT("\n"));
    Session->PendingResponseBuffer = bEndsWithNewline ? FString() : Lines.Pop(EAllowShrinking::No);

    for (const FString& Line : Lines)
    {
        HandleStreamingLine(Line.TrimStartAndEnd());
    }
}

bool FAIEditorAssistantChatController::HandleStreamingLine(const FString& LineText)
{
    if (!LineText.StartsWith(TEXT("data:")))
    {
        return false;
    }

    const FString Payload = LineText.RightChop(5).TrimStartAndEnd();
    if (Payload.IsEmpty() || Payload.Equals(TEXT("[DONE]"), ESearchCase::CaseSensitive))
    {
        return true;
    }

    TSharedPtr<FJsonObject> ResponseObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Payload);
    if (!FJsonSerializer::Deserialize(Reader, ResponseObject) || !ResponseObject.IsValid())
    {
        return false;
    }

    FString ReasoningContentDelta;
    if (TryExtractReasoningContentDelta(ResponseObject, ReasoningContentDelta))
    {
        if (FAIEditorAssistantChatSession* Session = GetActiveSession())
        {
            Session->StreamedReasoningCache.Append(ReasoningContentDelta);
        }
    }

    FString OutputTextDelta;
    if (TryExtractOutputTextDelta(ResponseObject, OutputTextDelta))
    {
        if (FAIEditorAssistantChatSession* Session = GetActiveSession())
        {
            Session->StreamedResponseCache.Append(OutputTextDelta);
            UpsertLastMessage(TEXT("AI"), Session->StreamedResponseCache);
            StatusMessage = TEXT("Receiving streamed response...");
            BroadcastStateChanged();
        }
        return true;
    }

    const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
    if (!ResponseObject->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0)
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* ChoiceObject = nullptr;
    if (!(*Choices)[0]->TryGetObject(ChoiceObject) || ChoiceObject == nullptr || !(*ChoiceObject).IsValid())
    {
        return false;
    }

    const bool bHandledAssistantDelta = TryAppendAssistantDelta(*ChoiceObject);
    const bool bHandledToolCallDelta = TryAppendToolCallDelta(*ChoiceObject);
    return bHandledAssistantDelta || bHandledToolCallDelta;
}

bool FAIEditorAssistantChatController::TryAppendAssistantDelta(const TSharedPtr<FJsonObject>& ChoiceObject)
{
    FAIEditorAssistantChatSession* Session = GetActiveSession();
    if (Session == nullptr)
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* DeltaObject = nullptr;
    if (!ChoiceObject->TryGetObjectField(TEXT("delta"), DeltaObject) || DeltaObject == nullptr || !(*DeltaObject).IsValid())
    {
        return false;
    }

    FString ReasoningDelta;
    if ((*DeltaObject)->TryGetStringField(TEXT("reasoning_content"), ReasoningDelta) && !ReasoningDelta.IsEmpty())
    {
        Session->StreamedReasoningCache.Append(ReasoningDelta);
    }

    FString DeltaContent;
    const TSharedPtr<FJsonValue>* ContentValue = (*DeltaObject)->Values.Find(TEXT("content"));
    if (ContentValue != nullptr)
    {
        if (ContentValue->IsValid())
        {
            if ((*ContentValue)->Type == EJson::String)
            {
                DeltaContent = (*ContentValue)->AsString();
            }
            else if ((*ContentValue)->Type == EJson::Array)
            {
                DeltaContent = ExtractTextFromContentParts((*ContentValue)->AsArray());
            }
            else if ((*ContentValue)->Type == EJson::Object)
            {
                const TSharedPtr<FJsonObject> ContentObject = (*ContentValue)->AsObject();
                if (ContentObject.IsValid())
                {
                    ContentObject->TryGetStringField(TEXT("text"), DeltaContent);
                    if (DeltaContent.IsEmpty())
                    {
                        const TArray<TSharedPtr<FJsonValue>>* ContentParts = nullptr;
                        if (ContentObject->TryGetArrayField(TEXT("content"), ContentParts) && ContentParts != nullptr)
                        {
                            DeltaContent = ExtractTextFromContentParts(*ContentParts);
                        }
                    }
                }
            }
        }
    }

    if (DeltaContent.IsEmpty() && !(*DeltaObject)->TryGetStringField(TEXT("content"), DeltaContent))
    {
        (*DeltaObject)->TryGetStringField(TEXT("text"), DeltaContent);
    }

    if (DeltaContent.IsEmpty())
    {
        return false;
    }

    Session->StreamedResponseCache.Append(DeltaContent);
    UpsertLastMessage(TEXT("AI"), Session->StreamedResponseCache);
    StatusMessage = TEXT("Receiving streamed response...");
    BroadcastStateChanged();
    return true;
}

bool FAIEditorAssistantChatController::TryAppendToolCallDelta(const TSharedPtr<FJsonObject>& ChoiceObject)
{
    FAIEditorAssistantChatSession* Session = GetActiveSession();
    if (Session == nullptr)
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* DeltaObject = nullptr;
    if (!ChoiceObject->TryGetObjectField(TEXT("delta"), DeltaObject) || DeltaObject == nullptr || !(*DeltaObject).IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* ToolCallArray = nullptr;
    if (!(*DeltaObject)->TryGetArrayField(TEXT("tool_calls"), ToolCallArray) || ToolCallArray->Num() == 0)
    {
        return false;
    }

    bool bHandled = false;
    for (int32 ArrayIndex = 0; ArrayIndex < ToolCallArray->Num(); ++ArrayIndex)
    {
        const TSharedPtr<FJsonObject>* ToolCallObject = nullptr;
        if (!(*ToolCallArray)[ArrayIndex]->TryGetObject(ToolCallObject) || ToolCallObject == nullptr || !(*ToolCallObject).IsValid())
        {
            continue;
        }

        int32 ToolCallIndex = ArrayIndex;
        (*ToolCallObject)->TryGetNumberField(TEXT("index"), ToolCallIndex);

        while (Session->StreamingToolCalls.Num() <= ToolCallIndex)
        {
            Session->StreamingToolCalls.AddDefaulted();
        }

        FAIEditorAssistantStreamingToolCall& StreamingToolCall = Session->StreamingToolCalls[ToolCallIndex];
        (*ToolCallObject)->TryGetStringField(TEXT("id"), StreamingToolCall.Id);

        const TSharedPtr<FJsonObject>* FunctionObject = nullptr;
        if ((*ToolCallObject)->TryGetObjectField(TEXT("function"), FunctionObject) && FunctionObject != nullptr && (*FunctionObject).IsValid())
        {
            FString NameFragment;
            if ((*FunctionObject)->TryGetStringField(TEXT("name"), NameFragment) && !NameFragment.IsEmpty())
            {
                StreamingToolCall.Name = NameFragment;
            }

            FString ArgumentsFragment;
            if ((*FunctionObject)->TryGetStringField(TEXT("arguments"), ArgumentsFragment) && !ArgumentsFragment.IsEmpty())
            {
                StreamingToolCall.ArgumentsJson.Append(ArgumentsFragment);
            }
        }

        bHandled = true;
    }

    if (bHandled)
    {
        StatusMessage = TEXT("Receiving tool call request...");
        BroadcastStateChanged();
    }

    return bHandled;
}

bool FAIEditorAssistantChatController::ParseChatCompletionPayload(
    const FString& ResponseBody,
    FString& OutAssistantContent,
    FString& OutReasoningContent,
    TArray<FAIEditorAssistantPendingToolCall>& OutToolCalls,
    TSharedPtr<FJsonObject>& OutProviderPayload,
    bool& bOutHadChoices) const
{
    OutAssistantContent.Empty();
    OutReasoningContent.Empty();
    OutToolCalls.Reset();
    OutProviderPayload.Reset();
    bOutHadChoices = false;

    TSharedPtr<FJsonObject> ResponseObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
    if (!FJsonSerializer::Deserialize(Reader, ResponseObject) || !ResponseObject.IsValid())
    {
        return false;
    }

    OutAssistantContent = ExtractAssistantContentFromResponseObject(ResponseObject);

    const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
    if (!ResponseObject->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0)
    {
        return !OutAssistantContent.IsEmpty();
    }

    bOutHadChoices = true;

    const TSharedPtr<FJsonObject>* FirstChoiceObject = nullptr;
    if (!(*Choices)[0]->TryGetObject(FirstChoiceObject) || FirstChoiceObject == nullptr || !(*FirstChoiceObject).IsValid())
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* MessageObject = nullptr;
    if (!(*FirstChoiceObject)->TryGetObjectField(TEXT("message"), MessageObject) || MessageObject == nullptr || !(*MessageObject).IsValid())
    {
        return false;
    }

    const FString MessageAssistantContent = ExtractAssistantContentFromMessage(*MessageObject);
    OutReasoningContent = ExtractReasoningContentFromMessage(*MessageObject);
    if (!MessageAssistantContent.IsEmpty())
    {
        OutAssistantContent = MessageAssistantContent;
    }
    if (OutAssistantContent.IsEmpty())
    {
        // Compatibility fallback for some OpenAI-like providers that still return
        // text on the top-level choice object instead of message.content.
        (*FirstChoiceObject)->TryGetStringField(TEXT("text"), OutAssistantContent);
    }

    const TSharedPtr<FJsonObject>* ProviderPayloadObject = nullptr;
    if ((*MessageObject)->TryGetObjectField(TEXT("_ai-editor-assistant_provider_payload"), ProviderPayloadObject) && ProviderPayloadObject != nullptr && (*ProviderPayloadObject).IsValid())
    {
        OutProviderPayload = ParseJsonObject(SerializeJsonObject(*ProviderPayloadObject));
    }

    TryParseToolCallsFromMessage(*MessageObject, OutToolCalls);
    return true;
}

bool FAIEditorAssistantChatController::TryParseToolCallsFromMessage(const TSharedPtr<FJsonObject>& MessageObject, TArray<FAIEditorAssistantPendingToolCall>& OutToolCalls) const
{
    const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
    if (!MessageObject->TryGetArrayField(TEXT("tool_calls"), ToolCalls) || ToolCalls->Num() == 0)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& ToolCallValue : *ToolCalls)
    {
        const TSharedPtr<FJsonObject>* ToolCallObject = nullptr;
        if (!ToolCallValue->TryGetObject(ToolCallObject) || ToolCallObject == nullptr || !(*ToolCallObject).IsValid())
        {
            continue;
        }

        FAIEditorAssistantPendingToolCall ParsedToolCall;
        (*ToolCallObject)->TryGetStringField(TEXT("id"), ParsedToolCall.Id);

        const TSharedPtr<FJsonObject>* FunctionObject = nullptr;
        if ((*ToolCallObject)->TryGetObjectField(TEXT("function"), FunctionObject) && FunctionObject != nullptr && (*FunctionObject).IsValid())
        {
            (*FunctionObject)->TryGetStringField(TEXT("name"), ParsedToolCall.Name);
            TryReadFunctionArgumentsJson(*FunctionObject, ParsedToolCall.ArgumentsJson);
        }

        ParsedToolCall.Arguments = ParseJsonObject(ParsedToolCall.ArgumentsJson);
        OutToolCalls.Add(ParsedToolCall);
    }

    return OutToolCalls.Num() > 0;
}

TSharedPtr<FJsonObject> FAIEditorAssistantChatController::BuildUserMessageObject(const FString& UserPrompt, const TArray<FString>& ImagePaths, FString& OutError) const
{
    OutError.Empty();

    TSharedPtr<FJsonObject> MessageObject = MakeShared<FJsonObject>();
    MessageObject->SetStringField(TEXT("role"), TEXT("user"));

    const FString TrimmedPrompt = UserPrompt.TrimStartAndEnd();
    if (ImagePaths.Num() == 0)
    {
        MessageObject->SetStringField(TEXT("content"), TrimmedPrompt);
        return MessageObject;
    }

    TArray<TSharedPtr<FJsonValue>> ContentParts;
    TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
    TextPart->SetStringField(TEXT("type"), TEXT("text"));
    TextPart->SetStringField(TEXT("text"), TrimmedPrompt.IsEmpty() ? TEXT("Please analyze the attached images.") : TrimmedPrompt);
    ContentParts.Add(MakeShared<FJsonValueObject>(TextPart));

    for (const FString& ImagePath : ImagePaths)
    {
        FString ImageDataUrl;
        if (!BuildImageDataUrl(ImagePath, ImageDataUrl, OutError))
        {
            return nullptr;
        }

        TSharedPtr<FJsonObject> ImageUrlObject = MakeShared<FJsonObject>();
        ImageUrlObject->SetStringField(TEXT("url"), ImageDataUrl);
        ImageUrlObject->SetStringField(TEXT("detail"), TEXT("auto"));

        TSharedPtr<FJsonObject> ImagePart = MakeShared<FJsonObject>();
        ImagePart->SetStringField(TEXT("type"), TEXT("image_url"));
        ImagePart->SetObjectField(TEXT("image_url"), ImageUrlObject);
        ContentParts.Add(MakeShared<FJsonValueObject>(ImagePart));
    }

    MessageObject->SetArrayField(TEXT("content"), ContentParts);
    return MessageObject;
}

TSharedPtr<FJsonObject> FAIEditorAssistantChatController::BuildAssistantMessageObject(const FString& AssistantContent, const FString& ReasoningContent, const TArray<FAIEditorAssistantPendingToolCall>& ToolCalls, const TSharedPtr<FJsonObject>& ProviderPayload) const
{
    TSharedPtr<FJsonObject> MessageObject = MakeShared<FJsonObject>();
    MessageObject->SetStringField(TEXT("role"), TEXT("assistant"));
    MessageObject->SetStringField(TEXT("content"), AssistantContent);

    if (!ReasoningContent.IsEmpty())
    {
        const FAIEditorAssistantChatSession* Session = GetActiveSession();
        const bool bShouldPreserveReasoning = ToolCalls.Num() > 0 || (Session != nullptr && Session->CurrentToolRound > 0);
        if (bShouldPreserveReasoning)
        {
            MessageObject->SetStringField(TEXT("reasoning_content"), ReasoningContent);
        }
    }

    if (ToolCalls.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> ToolCallArray;
        for (const FAIEditorAssistantPendingToolCall& ToolCall : ToolCalls)
        {
            TSharedPtr<FJsonObject> FunctionObject = MakeShared<FJsonObject>();
            FunctionObject->SetStringField(TEXT("name"), ToolCall.Name);
            FunctionObject->SetStringField(TEXT("arguments"), NormalizeToolArgumentsJson(ToolCall.ArgumentsJson));

            TSharedPtr<FJsonObject> ToolCallObject = MakeShared<FJsonObject>();
            ToolCallObject->SetStringField(TEXT("id"), ToolCall.Id);
            ToolCallObject->SetStringField(TEXT("type"), TEXT("function"));
            ToolCallObject->SetObjectField(TEXT("function"), FunctionObject);
            ToolCallArray.Add(MakeShared<FJsonValueObject>(ToolCallObject));
        }

        MessageObject->SetArrayField(TEXT("tool_calls"), ToolCallArray);
    }

    if (ProviderPayload.IsValid())
    {
        MessageObject->SetObjectField(TEXT("_ai-editor-assistant_provider_payload"), ParseJsonObject(SerializeJsonObject(ProviderPayload)));
    }

    return MessageObject;
}

TSharedPtr<FJsonObject> FAIEditorAssistantChatController::BuildToolResultMessageObject(const FString& ToolCallId, const FString& ToolName, const FString& Content) const
{
    TSharedPtr<FJsonObject> MessageObject = MakeShared<FJsonObject>();
    MessageObject->SetStringField(TEXT("role"), TEXT("tool"));
    MessageObject->SetStringField(TEXT("tool_call_id"), ToolCallId);
    if (!ToolName.IsEmpty())
    {
        MessageObject->SetStringField(TEXT("name"), ToolName);
    }
    MessageObject->SetStringField(TEXT("content"), Content);
    return MessageObject;
}

TArray<TSharedPtr<FJsonValue>> FAIEditorAssistantChatController::BuildRequestMessages() const
{
    TArray<TSharedPtr<FJsonValue>> Messages;
    Messages.Add(MakeShared<FJsonValueObject>(BuildAgentSystemMessageObject()));

    if (const FAIEditorAssistantChatSession* Session = GetActiveSession())
    {
        const TArray<TSharedPtr<FJsonObject>> ConsistentHistory = BuildConsistentRequestHistory(Session->RequestMessages);
        for (const TSharedPtr<FJsonObject>& Message : ConsistentHistory)
        {
            Messages.Add(MakeShared<FJsonValueObject>(Message));
        }
    }
    return Messages;
}

TArray<TSharedPtr<FJsonValue>> FAIEditorAssistantChatController::BuildToolDefinitions() const
{
    TArray<TSharedPtr<FJsonValue>> Tools;

    for (const FAIEditorAssistantToolDefinition& Definition : FAIEditorAssistantToolRuntime::Get().GetToolDefinitions())
    {
        TSharedPtr<FJsonObject> FunctionObject = MakeShared<FJsonObject>();
        FunctionObject->SetStringField(TEXT("name"), Definition.Name);
        FunctionObject->SetStringField(TEXT("description"), Definition.Description);
        FunctionObject->SetObjectField(TEXT("parameters"), Definition.Parameters.IsValid() ? Definition.Parameters : MakeShared<FJsonObject>());

        TSharedPtr<FJsonObject> ToolObject = MakeShared<FJsonObject>();
        ToolObject->SetStringField(TEXT("type"), TEXT("function"));
        ToolObject->SetObjectField(TEXT("function"), FunctionObject);
        Tools.Add(MakeShared<FJsonValueObject>(ToolObject));
    }

    return Tools;
}

FString FAIEditorAssistantChatController::ExtractErrorMessage(const FString& ResponseBody) const
{
    TSharedPtr<FJsonObject> ResponseObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
    if (!FJsonSerializer::Deserialize(Reader, ResponseObject) || !ResponseObject.IsValid())
    {
        return FString();
    }

    const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
    if (ResponseObject->TryGetObjectField(TEXT("error"), ErrorObject) && ErrorObject != nullptr && (*ErrorObject).IsValid())
    {
        FString ErrorMessage;
        if ((*ErrorObject)->TryGetStringField(TEXT("message"), ErrorMessage))
        {
            return ErrorMessage;
        }
    }

    FString Message;
    if (ResponseObject->TryGetStringField(TEXT("message"), Message))
    {
        return Message;
    }

    return FString();
}

FString FAIEditorAssistantChatController::FormatToolArgumentsSummary(const FAIEditorAssistantPendingToolCall& ToolCall) const
{
    FString Summary = ToolCall.ArgumentsJson.IsEmpty()
        ? SerializeJsonObject(ToolCall.Arguments)
        : ToolCall.ArgumentsJson;

    Summary.ReplaceInline(TEXT("\n"), TEXT(" "));
    Summary.ReplaceInline(TEXT("\r"), TEXT(" "));

    constexpr int32 MaxSummaryLength = 180;
    if (Summary.Len() > MaxSummaryLength)
    {
        Summary = Summary.Left(MaxSummaryLength) + TEXT("...");
    }

    return Summary;
}

void FAIEditorAssistantChatController::AppendToolCallMessages(const TArray<FAIEditorAssistantPendingToolCall>& ToolCalls)
{
    if (!ShouldShowToolActivityInChat())
    {
        return;
    }

    for (const FAIEditorAssistantPendingToolCall& ToolCall : ToolCalls)
    {
        AppendMessage(
            TEXT("Tool"),
            FString::Printf(TEXT("Requested `%s` with arguments:\n```json\n%s\n```"), *ToolCall.Name, *FormatToolArgumentsSummary(ToolCall)));
    }
}

void FAIEditorAssistantChatController::AppendToolResultMessage(const FAIEditorAssistantPendingToolCall& ToolCall, const FAIEditorAssistantToolResult& Result)
{
    if (!ShouldShowToolActivityInChat())
    {
        return;
    }

    const FString StatusText = Result.bSuccess ? TEXT("Success") : (Result.bWasRejected ? TEXT("Rejected") : TEXT("Error"));
    AppendMessage(
        TEXT("Tool Result"),
        FString::Printf(TEXT("`%s` finished with status: %s\n\n%s"), *ToolCall.Name, *StatusText, *Result.Summary));
}

FString FAIEditorAssistantChatController::GetPendingToolApprovalPrompt() const
{
    const FAIEditorAssistantChatSession* Session = GetActiveSession();
    if (Session == nullptr || !Session->PendingToolCalls.IsValidIndex(Session->PendingToolCallIndex))
    {
        return TEXT("No tool call is waiting for approval.");
    }

    const FAIEditorAssistantPendingToolCall& ToolCall = Session->PendingToolCalls[Session->PendingToolCallIndex];
    return FString::Printf(TEXT("Approve tool '%s' with arguments %s?"), *ToolCall.Name, *FormatToolArgumentsSummary(ToolCall));
}

FString FAIEditorAssistantChatController::GetSendButtonText() const
{
    if (CanCancelWork())
    {
        return TEXT("Cancel");
    }

    if (IsAwaitingToolConfirmation())
    {
        return TEXT("Waiting for Approval");
    }

    if (IsGeneratingTitle())
    {
        return TEXT("Generating Title...");
    }

    return bIsSending ? TEXT("Streaming...") : TEXT("Send");
}
