#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FAIEditorAssistantChatMessage
{
    FString Role;
    FString Content;
};

struct FAIEditorAssistantPendingToolCall
{
    FString Id;
    FString Name;
    FString ArgumentsJson;
    TSharedPtr<FJsonObject> Arguments;
};

struct FAIEditorAssistantStreamingToolCall
{
    FString Id;
    FString Name;
    FString ArgumentsJson;
};

struct FAIEditorAssistantChatSession
{
    FString SessionId;
    FString Title;
    bool bHasGeneratedTitle = false;
    FDateTime CreatedAt;
    FDateTime UpdatedAt;
    FString DraftPrompt;
    TArray<FString> PendingImagePaths;
    TArray<FAIEditorAssistantChatMessage> ConversationMessages;
    TArray<TSharedPtr<FJsonObject>> RequestMessages;

    TArray<FAIEditorAssistantPendingToolCall> PendingToolCalls;
    TArray<FAIEditorAssistantStreamingToolCall> StreamingToolCalls;
    FString PendingResponseBuffer;
    FString StreamedResponseCache;
    FString StreamedReasoningCache;
    FString PendingUserPrompt;
    int32 CurrentToolRound = 0;
    int32 PendingToolCallIndex = INDEX_NONE;
    bool bAssistantMessageOpen = false;
    bool bAwaitingToolConfirmation = false;
    bool bIsGeneratingTitle = false;
};

struct FAIEditorAssistantChatSessionIndex
{
    TArray<FString> OpenSessionIds;
    FString ActiveSessionId;
};

struct FAIEditorAssistantSessionTabViewData
{
    FString SessionId;
    FString Title;
    bool bIsActive = false;
};

struct FAIEditorAssistantToolConfirmationViewData
{
    bool bIsVisible = false;
    FString Prompt;
};

struct FAIEditorAssistantChatPanelViewState
{
    TArray<FAIEditorAssistantSessionTabViewData> Sessions;
    TArray<FAIEditorAssistantChatMessage> VisibleMessages;
    FAIEditorAssistantToolConfirmationViewData ToolConfirmation;
    FString ContextSummary;
    FString PendingAttachmentSummary;
    TArray<FString> PendingAttachmentPaths;
    FString DraftPrompt;
    FString Model;
    TArray<FString> ModelOptions;
    FString ModelListStatus;
    FString ReasoningMode;
    TArray<FString> ReasoningModeOptions;
    FString ReasoningOptionsStatus;
    FString StatusMessage;
    FString SendButtonText;
    bool bCanSend = false;
    bool bCanCancel = false;
    bool bCanEditSessions = true;
    bool bIsSending = false;
    bool bIsGeneratingTitle = false;
    bool bIsModelListLoading = false;
    bool bAllowManualModelInput = false;
    bool bIsReasoningOptionsLoading = false;
};
