#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FAIGatewayChatMessage
{
    FString Role;
    FString Content;
};

struct FAIGatewayPendingToolCall
{
    FString Id;
    FString Name;
    FString ArgumentsJson;
    TSharedPtr<FJsonObject> Arguments;
};

struct FAIGatewayStreamingToolCall
{
    FString Id;
    FString Name;
    FString ArgumentsJson;
};

struct FAIGatewayChatSession
{
    FString SessionId;
    FString Title;
    bool bHasGeneratedTitle = false;
    FDateTime CreatedAt;
    FDateTime UpdatedAt;
    FString DraftPrompt;
    TArray<FString> PendingImagePaths;
    TArray<FAIGatewayChatMessage> ConversationMessages;
    TArray<TSharedPtr<FJsonObject>> RequestMessages;

    TArray<FAIGatewayPendingToolCall> PendingToolCalls;
    TArray<FAIGatewayStreamingToolCall> StreamingToolCalls;
    FString PendingResponseBuffer;
    FString StreamedResponseCache;
    FString PendingUserPrompt;
    int32 CurrentToolRound = 0;
    int32 PendingToolCallIndex = INDEX_NONE;
    bool bAssistantMessageOpen = false;
    bool bAwaitingToolConfirmation = false;
    bool bIsGeneratingTitle = false;
};

struct FAIGatewayChatSessionIndex
{
    TArray<FString> OpenSessionIds;
    FString ActiveSessionId;
};

struct FAIGatewaySessionTabViewData
{
    FString SessionId;
    FString Title;
    bool bIsActive = false;
};

struct FAIGatewayToolConfirmationViewData
{
    bool bIsVisible = false;
    FString Prompt;
};

struct FAIGatewayChatPanelViewState
{
    TArray<FAIGatewaySessionTabViewData> Sessions;
    TArray<FAIGatewayChatMessage> VisibleMessages;
    FAIGatewayToolConfirmationViewData ToolConfirmation;
    FString ContextSummary;
    FString PendingAttachmentSummary;
    TArray<FString> PendingAttachmentPaths;
    FString DraftPrompt;
    FString Model;
    FString StatusMessage;
    FString SendButtonText;
    bool bCanSend = false;
    bool bCanEditSessions = true;
    bool bIsSending = false;
    bool bIsGeneratingTitle = false;
};
