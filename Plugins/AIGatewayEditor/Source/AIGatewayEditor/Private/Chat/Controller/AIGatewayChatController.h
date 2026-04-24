#pragma once

#include "Chat/Model/AIGatewayChatTypes.h"
#include "Chat/Services/AIGatewayChatService.h"
#include "Chat/Services/AIGatewayChatSessionStore.h"
#include "Delegates/Delegate.h"

struct FAIGatewayToolResult;

class FAIGatewayChatController : public TSharedFromThis<FAIGatewayChatController>
{
public:
    explicit FAIGatewayChatController(
        TSharedRef<IAIGatewayChatSessionStore> InSessionStore,
        TSharedRef<IAIGatewayChatService> InChatService);

    void Initialize();
    void PersistActiveDraft();

    void SetModel(const FString& InModel);
    void UpdateDraft(const FString& DraftText);
    void AddPendingImagePaths(const TArray<FString>& ImagePaths);
    void RemovePendingImageAt(int32 ImageIndex);
    void ClearPendingImages();
    void SubmitPrompt();
    void CreateSession();
    void ActivateSession(const FString& SessionId);
    void CloseSession(const FString& SessionId);
    void ApprovePendingTool();
    void RejectPendingTool();

    FAIGatewayChatPanelViewState GetViewState() const;
    FSimpleMulticastDelegate& OnStateChanged();

private:
    void BroadcastStateChanged();
    bool CanSendRequest() const;
    bool CanEditSessions() const;
    bool IsAwaitingToolConfirmation() const;
    bool IsGeneratingTitle() const;
    bool ShouldDisplayConversationMessage(const FAIGatewayChatMessage& Message) const;
    int32 GetConfiguredMaxToolRounds() const;
    bool ShouldShowToolActivityInChat() const;

    FAIGatewayChatSession* GetActiveSession();
    const FAIGatewayChatSession* GetActiveSession() const;
    FAIGatewayChatSession* FindSessionById(const FString& SessionId);
    const FAIGatewayChatSession* FindSessionById(const FString& SessionId) const;

    void LoadModelFromSettings();
    bool SaveModelToSettings(FString& OutError) const;
    bool ResolveServiceSettings(FAIGatewayChatServiceSettings& OutSettings, FString& OutError) const;
    FString BuildContextSummary(const FAIGatewayChatSession& Session) const;

    void AppendMessage(const FString& Role, const FString& Text);
    void UpsertLastMessage(const FString& Role, const FString& Text);
    void AddRequestMessage(const TSharedPtr<FJsonObject>& MessageObject);
    void TouchSession(FAIGatewayChatSession& Session);
    void PersistActiveSession();
    void SaveSessionIndex() const;

    void LoadSessions();
    FAIGatewayChatSession& CreateSessionInternal(bool bMakeActive);
    void EnsureActiveSession();
    FString MakeNewSessionTitle() const;
    FString GenerateFallbackTitle(const FAIGatewayChatSession& Session) const;
    bool ShouldGenerateTitle(const FAIGatewayChatSession& Session) const;
    void MaybeGenerateTitleForActiveSession();

    void StartAssistantResponse();
    void FinalizeAssistantResponse();
    void AbortAssistantResponse();
    void ResetStreamingState();
    void BeginUserTurn(const FString& UserDisplayText, const TSharedPtr<FJsonObject>& UserMessageObject);
    bool SendChatRequest();
    void ExecuteNextPendingToolCall();
    void ExecuteCurrentPendingToolCall(bool bApproved);
    void ResumeAfterToolConfirmation(bool bApproved);
    void FinishTurnWithError(const FString& ErrorMessage, bool bKeepAssistantPlaceholder = false);

    void HandleChatResponse(const FAIGatewayChatServiceResponse& Response);
    void HandleStreamingPayloadChunk(const FString& ChunkText);
    bool HandleStreamingLine(const FString& LineText);
    bool TryAppendAssistantDelta(const TSharedPtr<FJsonObject>& ChoiceObject);
    bool TryAppendToolCallDelta(const TSharedPtr<FJsonObject>& ChoiceObject);
    bool ParseChatCompletionPayload(
        const FString& ResponseBody,
        FString& OutAssistantContent,
        TArray<FAIGatewayPendingToolCall>& OutToolCalls,
        bool& bOutHadChoices) const;
    bool TryParseToolCallsFromMessage(const TSharedPtr<FJsonObject>& MessageObject, TArray<FAIGatewayPendingToolCall>& OutToolCalls) const;

    TSharedPtr<FJsonObject> BuildUserMessageObject(const FString& UserPrompt, const TArray<FString>& ImagePaths, FString& OutError) const;
    TSharedPtr<FJsonObject> BuildAssistantMessageObject(const FString& AssistantContent, const TArray<FAIGatewayPendingToolCall>& ToolCalls) const;
    TSharedPtr<FJsonObject> BuildToolResultMessageObject(const FString& ToolCallId, const FString& Content) const;
    TArray<TSharedPtr<FJsonValue>> BuildRequestMessages() const;
    TArray<TSharedPtr<FJsonValue>> BuildToolDefinitions() const;

    FString BuildPendingAttachmentSummary(const FAIGatewayChatSession& Session) const;
    FString ExtractErrorMessage(const FString& ResponseBody) const;
    FString FormatToolArgumentsSummary(const FAIGatewayPendingToolCall& ToolCall) const;
    void AppendToolCallMessages(const TArray<FAIGatewayPendingToolCall>& ToolCalls);
    void AppendToolResultMessage(const FAIGatewayPendingToolCall& ToolCall, const FAIGatewayToolResult& Result);
    FString GetPendingToolApprovalPrompt() const;
    FString GetSendButtonText() const;

    TSharedRef<IAIGatewayChatSessionStore> SessionStore;
    TSharedRef<IAIGatewayChatService> ChatService;
    TArray<FAIGatewayChatSession> Sessions;
    FString ActiveSessionId;
    FString CurrentModel;
    FString StatusMessage;
    bool bIsSending = false;
    FSimpleMulticastDelegate StateChangedDelegate;
};
