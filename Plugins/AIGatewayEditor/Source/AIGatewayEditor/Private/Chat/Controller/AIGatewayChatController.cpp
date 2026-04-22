#include "Chat/Controller/AIGatewayChatController.h"

#include "AIGatewayEditorSettings.h"
#include "Chat/Markdown/AIGatewayMarkdownParser.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tools/AIGatewayToolRuntime.h"

namespace
{
    const FString DefaultSessionTitle(TEXT("New Chat"));

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

    FString TruncateWithEllipsis(const FString& InText, const int32 MaxLength)
    {
        FString Text = InText.TrimStartAndEnd();
        if (Text.Len() <= MaxLength)
        {
            return Text;
        }

        return Text.Left(MaxLength) + TEXT("...");
    }

    FString GetFirstConversationMessageByRole(const FAIGatewayChatSession& Session, const FString& Role)
    {
        for (const FAIGatewayChatMessage& Message : Session.ConversationMessages)
        {
            if (Message.Role.Equals(Role, ESearchCase::IgnoreCase) && !Message.Content.TrimStartAndEnd().IsEmpty())
            {
                return Message.Content.TrimStartAndEnd();
            }
        }

        return FString();
    }

    FString GetLastConversationMessageByRole(const FAIGatewayChatSession& Session, const FString& Role)
    {
        for (int32 Index = Session.ConversationMessages.Num() - 1; Index >= 0; --Index)
        {
            const FAIGatewayChatMessage& Message = Session.ConversationMessages[Index];
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
}

FAIGatewayChatController::FAIGatewayChatController(
    TSharedRef<IAIGatewayChatSessionStore> InSessionStore,
    TSharedRef<IAIGatewayChatService> InChatService)
    : SessionStore(InSessionStore)
    , ChatService(InChatService)
{
}

void FAIGatewayChatController::Initialize()
{
    LoadModelFromSettings();
    LoadSessions();
    StatusMessage = TEXT("Configure Base URL and API Key in Project Settings > Plugins > AI Gateway.");
    BroadcastStateChanged();
}

void FAIGatewayChatController::PersistActiveDraft()
{
    if (FAIGatewayChatSession* Session = GetActiveSession())
    {
        TouchSession(*Session);
        SessionStore->SaveSession(*Session);
        SaveSessionIndex();
    }
}

void FAIGatewayChatController::SetModel(const FString& InModel)
{
    CurrentModel = InModel;
}

void FAIGatewayChatController::UpdateDraft(const FString& DraftText)
{
    if (FAIGatewayChatSession* Session = GetActiveSession())
    {
        Session->DraftPrompt = DraftText;
        TouchSession(*Session);
        BroadcastStateChanged();
    }
}

void FAIGatewayChatController::SubmitPrompt()
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

    FAIGatewayChatSession* Session = GetActiveSession();
    if (Session == nullptr)
    {
        return;
    }

    const FString UserPrompt = Session->DraftPrompt.TrimStartAndEnd();
    if (UserPrompt.IsEmpty())
    {
        FinishTurnWithError(TEXT("Prompt is empty. Describe what you want the model to do."));
        return;
    }

    Session->DraftPrompt.Empty();
    BeginUserTurn(UserPrompt);

    if (!SendChatRequest())
    {
        FinishTurnWithError(TEXT("Failed to start the chat request."));
    }
}

void FAIGatewayChatController::CreateSession()
{
    if (!CanEditSessions())
    {
        return;
    }

    CreateSessionInternal(true);
    BroadcastStateChanged();
}

void FAIGatewayChatController::ActivateSession(const FString& SessionId)
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

void FAIGatewayChatController::CloseSession(const FString& SessionId)
{
    if (!CanEditSessions())
    {
        return;
    }

    const int32 SessionIndex = Sessions.IndexOfByPredicate([&SessionId](const FAIGatewayChatSession& Session)
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

void FAIGatewayChatController::ApprovePendingTool()
{
    ResumeAfterToolConfirmation(true);
}

void FAIGatewayChatController::RejectPendingTool()
{
    ResumeAfterToolConfirmation(false);
}

FAIGatewayChatPanelViewState FAIGatewayChatController::GetViewState() const
{
    FAIGatewayChatPanelViewState ViewState;
    ViewState.Model = CurrentModel;
    ViewState.StatusMessage = StatusMessage;
    ViewState.SendButtonText = GetSendButtonText();
    ViewState.bCanSend = CanSendRequest();
    ViewState.bCanEditSessions = CanEditSessions();
    ViewState.bIsSending = bIsSending;
    ViewState.bIsGeneratingTitle = IsGeneratingTitle();

    if (const FAIGatewayChatSession* Session = GetActiveSession())
    {
        ViewState.DraftPrompt = Session->DraftPrompt;
        ViewState.ToolConfirmation.bIsVisible = Session->bAwaitingToolConfirmation;
        ViewState.ToolConfirmation.Prompt = GetPendingToolApprovalPrompt();

        for (const FAIGatewayChatMessage& Message : Session->ConversationMessages)
        {
            if (ShouldDisplayConversationMessage(Message))
            {
                ViewState.VisibleMessages.Add(Message);
            }
        }
    }

    for (const FAIGatewayChatSession& Session : Sessions)
    {
        FAIGatewaySessionTabViewData& Tab = ViewState.Sessions.AddDefaulted_GetRef();
        Tab.SessionId = Session.SessionId;
        Tab.Title = Session.Title.IsEmpty() ? MakeNewSessionTitle() : Session.Title;
        Tab.bIsActive = Session.SessionId == ActiveSessionId;
    }

    return ViewState;
}

FSimpleMulticastDelegate& FAIGatewayChatController::OnStateChanged()
{
    return StateChangedDelegate;
}

void FAIGatewayChatController::BroadcastStateChanged()
{
    StateChangedDelegate.Broadcast();
}

bool FAIGatewayChatController::ShouldDisplayConversationMessage(const FAIGatewayChatMessage& Message) const
{
    return ShouldShowToolActivityInChat() ||
        (!Message.Role.Equals(TEXT("Tool"), ESearchCase::IgnoreCase) &&
            !Message.Role.Equals(TEXT("Tool Result"), ESearchCase::IgnoreCase));
}

bool FAIGatewayChatController::CanSendRequest() const
{
    return GetActiveSession() != nullptr && !bIsSending && !IsAwaitingToolConfirmation() && !IsGeneratingTitle();
}

bool FAIGatewayChatController::CanEditSessions() const
{
    return !bIsSending && !IsAwaitingToolConfirmation() && !IsGeneratingTitle();
}

bool FAIGatewayChatController::IsAwaitingToolConfirmation() const
{
    const FAIGatewayChatSession* Session = GetActiveSession();
    return Session != nullptr && Session->bAwaitingToolConfirmation;
}

bool FAIGatewayChatController::IsGeneratingTitle() const
{
    return false;
}

int32 FAIGatewayChatController::GetConfiguredMaxToolRounds() const
{
    return FMath::Clamp(GetDefault<UAIGatewayEditorSettings>()->MaxToolRounds, 1, 64);
}

bool FAIGatewayChatController::ShouldShowToolActivityInChat() const
{
    return GetDefault<UAIGatewayEditorSettings>()->bShowToolActivityInChat;
}

FAIGatewayChatSession* FAIGatewayChatController::GetActiveSession()
{
    return FindSessionById(ActiveSessionId);
}

const FAIGatewayChatSession* FAIGatewayChatController::GetActiveSession() const
{
    return FindSessionById(ActiveSessionId);
}

FAIGatewayChatSession* FAIGatewayChatController::FindSessionById(const FString& SessionId)
{
    return Sessions.FindByPredicate([&SessionId](const FAIGatewayChatSession& Session)
    {
        return Session.SessionId == SessionId;
    });
}

const FAIGatewayChatSession* FAIGatewayChatController::FindSessionById(const FString& SessionId) const
{
    return Sessions.FindByPredicate([&SessionId](const FAIGatewayChatSession& Session)
    {
        return Session.SessionId == SessionId;
    });
}

void FAIGatewayChatController::LoadModelFromSettings()
{
    CurrentModel = GetDefault<UAIGatewayEditorSettings>()->Model;
}

bool FAIGatewayChatController::SaveModelToSettings(FString& OutError) const
{
    const FString Model = CurrentModel.TrimStartAndEnd();
    if (Model.IsEmpty())
    {
        OutError = TEXT("Model is required. Example: gpt-4o-mini");
        return false;
    }

    UAIGatewayEditorSettings* MutableSettings = GetMutableDefault<UAIGatewayEditorSettings>();
    MutableSettings->Model = Model;
    MutableSettings->SaveConfig();
    return true;
}

bool FAIGatewayChatController::ResolveServiceSettings(FAIGatewayChatServiceSettings& OutSettings, FString& OutError) const
{
    const UAIGatewayEditorSettings* Settings = GetDefault<UAIGatewayEditorSettings>();
    OutSettings.BaseUrl = Settings->BaseUrl.TrimStartAndEnd();
    OutSettings.BaseUrl.RemoveFromEnd(TEXT("/"));
    OutSettings.ApiKey = Settings->ApiKey.TrimStartAndEnd();
    OutSettings.Model = CurrentModel.TrimStartAndEnd();

    if (OutSettings.BaseUrl.IsEmpty())
    {
        OutError = TEXT("Base URL is not configured. Open Project Settings > Plugins > AI Gateway.");
        return false;
    }

    if (!OutSettings.BaseUrl.StartsWith(TEXT("http://")) && !OutSettings.BaseUrl.StartsWith(TEXT("https://")))
    {
        OutError = TEXT("Base URL in Project Settings must start with http:// or https://");
        return false;
    }

    if (OutSettings.ApiKey.IsEmpty())
    {
        OutError = TEXT("API Key is not configured. Open Project Settings > Plugins > AI Gateway.");
        return false;
    }

    if (OutSettings.Model.IsEmpty())
    {
        OutError = TEXT("Model is required. Example: gpt-4o-mini");
        return false;
    }

    return true;
}

void FAIGatewayChatController::AppendMessage(const FString& Role, const FString& Text)
{
    if (FAIGatewayChatSession* Session = GetActiveSession())
    {
        Session->ConversationMessages.Add({ Role, Text });
        BroadcastStateChanged();
    }
}

void FAIGatewayChatController::UpsertLastMessage(const FString& Role, const FString& Text)
{
    if (FAIGatewayChatSession* Session = GetActiveSession())
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

void FAIGatewayChatController::AddRequestMessage(const TSharedPtr<FJsonObject>& MessageObject)
{
    if (MessageObject.IsValid())
    {
        if (FAIGatewayChatSession* Session = GetActiveSession())
        {
            Session->RequestMessages.Add(MessageObject);
        }
    }
}

void FAIGatewayChatController::TouchSession(FAIGatewayChatSession& Session)
{
    if (Session.CreatedAt == FDateTime())
    {
        Session.CreatedAt = FDateTime::UtcNow();
    }

    Session.UpdatedAt = FDateTime::UtcNow();
}

void FAIGatewayChatController::PersistActiveSession()
{
    if (FAIGatewayChatSession* Session = GetActiveSession())
    {
        TouchSession(*Session);
        SessionStore->SaveSession(*Session);
        SaveSessionIndex();
    }
}

void FAIGatewayChatController::SaveSessionIndex() const
{
    FAIGatewayChatSessionIndex SessionIndex;
    SessionIndex.ActiveSessionId = ActiveSessionId;
    for (const FAIGatewayChatSession& Session : Sessions)
    {
        SessionIndex.OpenSessionIds.Add(Session.SessionId);
    }

    SessionStore->SaveSessionIndex(SessionIndex);
}

void FAIGatewayChatController::LoadSessions()
{
    Sessions.Reset();
    ActiveSessionId.Empty();

    FAIGatewayChatSessionIndex SessionIndex;
    SessionStore->LoadSessionIndex(SessionIndex);

    for (const FString& SessionId : SessionIndex.OpenSessionIds)
    {
        FAIGatewayChatSession LoadedSession;
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

FAIGatewayChatSession& FAIGatewayChatController::CreateSessionInternal(bool bMakeActive)
{
    FAIGatewayChatSession& Session = Sessions.AddDefaulted_GetRef();
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

void FAIGatewayChatController::EnsureActiveSession()
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

FString FAIGatewayChatController::MakeNewSessionTitle() const
{
    return DefaultSessionTitle;
}

FString FAIGatewayChatController::GenerateFallbackTitle(const FAIGatewayChatSession& Session) const
{
    FString FirstPrompt = GetFirstConversationMessageByRole(Session, TEXT("You"));
    FirstPrompt = FAIGatewayMarkdownParser::NormalizeLineEndings(FirstPrompt);
    FirstPrompt.ReplaceInline(TEXT("\n"), TEXT(" "));
    FirstPrompt.ReplaceInline(TEXT("\r"), TEXT(" "));
    FirstPrompt = FirstPrompt.TrimStartAndEnd();

    return FirstPrompt.IsEmpty() ? MakeNewSessionTitle() : TruncateWithEllipsis(FirstPrompt, 20);
}

bool FAIGatewayChatController::ShouldGenerateTitle(const FAIGatewayChatSession& Session) const
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

void FAIGatewayChatController::MaybeGenerateTitleForActiveSession()
{
    FAIGatewayChatSession* Session = GetActiveSession();
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

void FAIGatewayChatController::StartAssistantResponse()
{
    if (FAIGatewayChatSession* Session = GetActiveSession())
    {
        Session->bAssistantMessageOpen = true;
        Session->StreamedResponseCache.Empty();
        Session->PendingResponseBuffer.Empty();
        Session->StreamingToolCalls.Reset();
        AppendMessage(TEXT("AI"), TEXT(""));
    }
}

void FAIGatewayChatController::FinalizeAssistantResponse()
{
    if (FAIGatewayChatSession* Session = GetActiveSession())
    {
        if (!Session->bAssistantMessageOpen)
        {
            return;
        }

        Session->bAssistantMessageOpen = false;
        UpsertLastMessage(TEXT("AI"), Session->StreamedResponseCache.IsEmpty() ? TEXT("(No content returned)") : Session->StreamedResponseCache);
    }
}

void FAIGatewayChatController::AbortAssistantResponse()
{
    if (FAIGatewayChatSession* Session = GetActiveSession())
    {
        if (!Session->bAssistantMessageOpen)
        {
            return;
        }

        Session->bAssistantMessageOpen = false;
        Session->StreamedResponseCache.Empty();
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

void FAIGatewayChatController::ResetStreamingState()
{
    if (FAIGatewayChatSession* Session = GetActiveSession())
    {
        Session->PendingResponseBuffer.Empty();
        Session->StreamedResponseCache.Empty();
        Session->StreamingToolCalls.Reset();
    }
}

void FAIGatewayChatController::BeginUserTurn(const FString& UserPrompt)
{
    if (FAIGatewayChatSession* Session = GetActiveSession())
    {
        Session->CurrentToolRound = 0;
        Session->PendingToolCalls.Reset();
        Session->PendingToolCallIndex = INDEX_NONE;
        Session->bAwaitingToolConfirmation = false;
        Session->PendingUserPrompt = UserPrompt;
        Session->DraftPrompt.Empty();

        AppendMessage(TEXT("You"), UserPrompt);
        AddRequestMessage(BuildUserMessageObject(UserPrompt));
        PersistActiveSession();
    }
}

bool FAIGatewayChatController::SendChatRequest()
{
    FAIGatewayChatSession* Session = GetActiveSession();
    if (Session == nullptr)
    {
        return false;
    }

    FString SettingsError;
    FAIGatewayChatServiceSettings Settings;
    if (!ResolveServiceSettings(Settings, SettingsError))
    {
        FinishTurnWithError(SettingsError);
        return false;
    }

    FAIGatewayChatCompletionRequest Request;
    Request.bStream = true;
    Request.Messages = BuildRequestMessages();
    Request.Tools = BuildToolDefinitions();

    ResetStreamingState();
    bIsSending = true;
    StartAssistantResponse();
    StatusMessage = Session->CurrentToolRound == 0
        ? TEXT("Sending request with native UE tools enabled...")
        : TEXT("Continuing the tool loop...");
    BroadcastStateChanged();

    const TWeakPtr<FAIGatewayChatController> WeakController = AsShared();
    if (!ChatService->SendStreamingChatRequest(
            Settings,
            Request,
            [WeakController](const FString& ChunkText)
            {
                if (const TSharedPtr<FAIGatewayChatController> Pinned = WeakController.Pin())
                {
                    Pinned->HandleStreamingPayloadChunk(ChunkText);
                }
            },
            [WeakController](const FAIGatewayChatServiceResponse& Response)
            {
                if (const TSharedPtr<FAIGatewayChatController> Pinned = WeakController.Pin())
                {
                    Pinned->HandleChatResponse(Response);
                }
            }))
    {
        bIsSending = false;
        AbortAssistantResponse();
        BroadcastStateChanged();
        return false;
    }

    return true;
}

void FAIGatewayChatController::ExecuteNextPendingToolCall()
{
    FAIGatewayChatSession* Session = GetActiveSession();
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

    const FAIGatewayPendingToolCall& ToolCall = Session->PendingToolCalls[Session->PendingToolCallIndex];
    const FAIGatewayToolDefinition* Definition = FAIGatewayToolRuntime::Get().FindDefinition(ToolCall.Name);
    if (Definition == nullptr)
    {
        const FAIGatewayToolResult Result = FAIGatewayToolResult::Error(FString::Printf(TEXT("Unknown tool '%s'."), *ToolCall.Name));
        AddRequestMessage(BuildToolResultMessageObject(ToolCall.Id, Result.ToMessageContent()));
        AppendToolResultMessage(ToolCall, Result);
        PersistActiveSession();
        ++Session->PendingToolCallIndex;
        ExecuteNextPendingToolCall();
        return;
    }

    if (Definition->ConfirmationPolicy == EAIGatewayToolConfirmationPolicy::ExplicitApproval)
    {
        Session->bAwaitingToolConfirmation = true;
        StatusMessage = TEXT("Waiting for your approval before executing the requested tool.");
        PersistActiveSession();
        BroadcastStateChanged();
        return;
    }

    ExecuteCurrentPendingToolCall(true);
}

void FAIGatewayChatController::ExecuteCurrentPendingToolCall(bool bApproved)
{
    FAIGatewayChatSession* Session = GetActiveSession();
    if (Session == nullptr || !Session->PendingToolCalls.IsValidIndex(Session->PendingToolCallIndex))
    {
        return;
    }

    const FAIGatewayPendingToolCall ToolCall = Session->PendingToolCalls[Session->PendingToolCallIndex];
    const FAIGatewayToolResult Result = bApproved
        ? FAIGatewayToolRuntime::Get().ExecuteTool(ToolCall.Name, ToolCall.Arguments.IsValid() ? ToolCall.Arguments : MakeShared<FJsonObject>())
        : FAIGatewayToolResult::Rejected(TEXT("The user rejected this tool call."));

    AddRequestMessage(BuildToolResultMessageObject(ToolCall.Id, Result.ToMessageContent()));
    AppendToolResultMessage(ToolCall, Result);
    StatusMessage = Result.Summary;
    PersistActiveSession();
    BroadcastStateChanged();

    ++Session->PendingToolCallIndex;
    ExecuteNextPendingToolCall();
}

void FAIGatewayChatController::ResumeAfterToolConfirmation(bool bApproved)
{
    if (FAIGatewayChatSession* Session = GetActiveSession())
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

void FAIGatewayChatController::FinishTurnWithError(const FString& ErrorMessage, bool bKeepAssistantPlaceholder)
{
    bIsSending = false;

    if (FAIGatewayChatSession* Session = GetActiveSession())
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

void FAIGatewayChatController::HandleChatResponse(const FAIGatewayChatServiceResponse& Response)
{
    bIsSending = false;

    FAIGatewayChatSession* Session = GetActiveSession();
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
    TArray<FAIGatewayPendingToolCall> ParsedToolCalls;
    bool bHadChoices = false;
    ParseChatCompletionPayload(Response.ResponseBody, ParsedAssistantContent, ParsedToolCalls, bHadChoices);

    if (ParsedAssistantContent.IsEmpty())
    {
        ParsedAssistantContent = Session->StreamedResponseCache;
    }
    else
    {
        Session->StreamedResponseCache = ParsedAssistantContent;
    }

    if (ParsedToolCalls.Num() == 0 && Session->StreamingToolCalls.Num() > 0)
    {
        for (const FAIGatewayStreamingToolCall& StreamingToolCall : Session->StreamingToolCalls)
        {
            FAIGatewayPendingToolCall ToolCall;
            ToolCall.Id = StreamingToolCall.Id;
            ToolCall.Name = StreamingToolCall.Name;
            ToolCall.ArgumentsJson = StreamingToolCall.ArgumentsJson;
            ToolCall.Arguments = ParseJsonObject(StreamingToolCall.ArgumentsJson);
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

        AddRequestMessage(BuildAssistantMessageObject(ParsedAssistantContent, ParsedToolCalls));
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
        AddRequestMessage(BuildAssistantMessageObject(ParsedAssistantContent, {}));
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
            ? TEXT("The gateway returned a completion with no assistant text and no tool calls. Make sure your gateway supports OpenAI-style tool calling.")
            : TEXT("The response did not contain a usable assistant message or tool call payload."));
}

void FAIGatewayChatController::HandleStreamingPayloadChunk(const FString& ChunkText)
{
    FAIGatewayChatSession* Session = GetActiveSession();
    if (Session == nullptr)
    {
        return;
    }

    Session->PendingResponseBuffer.Append(ChunkText);

    FString NormalizedBuffer = FAIGatewayMarkdownParser::NormalizeLineEndings(Session->PendingResponseBuffer);
    TArray<FString> Lines;
    NormalizedBuffer.ParseIntoArray(Lines, TEXT("\n"), false);

    const bool bEndsWithNewline = NormalizedBuffer.EndsWith(TEXT("\n"));
    Session->PendingResponseBuffer = bEndsWithNewline ? FString() : Lines.Pop(EAllowShrinking::No);

    for (const FString& Line : Lines)
    {
        HandleStreamingLine(Line.TrimStartAndEnd());
    }
}

bool FAIGatewayChatController::HandleStreamingLine(const FString& LineText)
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

bool FAIGatewayChatController::TryAppendAssistantDelta(const TSharedPtr<FJsonObject>& ChoiceObject)
{
    FAIGatewayChatSession* Session = GetActiveSession();
    if (Session == nullptr)
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* DeltaObject = nullptr;
    if (!ChoiceObject->TryGetObjectField(TEXT("delta"), DeltaObject) || DeltaObject == nullptr || !(*DeltaObject).IsValid())
    {
        return false;
    }

    FString DeltaContent;
    if (!(*DeltaObject)->TryGetStringField(TEXT("content"), DeltaContent) || DeltaContent.IsEmpty())
    {
        return false;
    }

    Session->StreamedResponseCache.Append(DeltaContent);
    UpsertLastMessage(TEXT("AI"), Session->StreamedResponseCache);
    StatusMessage = TEXT("Receiving streamed response...");
    BroadcastStateChanged();
    return true;
}

bool FAIGatewayChatController::TryAppendToolCallDelta(const TSharedPtr<FJsonObject>& ChoiceObject)
{
    FAIGatewayChatSession* Session = GetActiveSession();
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

        FAIGatewayStreamingToolCall& StreamingToolCall = Session->StreamingToolCalls[ToolCallIndex];
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

bool FAIGatewayChatController::ParseChatCompletionPayload(
    const FString& ResponseBody,
    FString& OutAssistantContent,
    TArray<FAIGatewayPendingToolCall>& OutToolCalls,
    bool& bOutHadChoices) const
{
    OutAssistantContent.Empty();
    OutToolCalls.Reset();
    bOutHadChoices = false;

    TSharedPtr<FJsonObject> ResponseObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
    if (!FJsonSerializer::Deserialize(Reader, ResponseObject) || !ResponseObject.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
    if (!ResponseObject->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0)
    {
        return false;
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

    OutAssistantContent = ExtractAssistantContentFromMessage(*MessageObject);
    if (OutAssistantContent.IsEmpty())
    {
        // Compatibility fallback for some OpenAI-like gateways that still return
        // text on the top-level choice object instead of message.content.
        (*FirstChoiceObject)->TryGetStringField(TEXT("text"), OutAssistantContent);
    }

    TryParseToolCallsFromMessage(*MessageObject, OutToolCalls);
    return true;
}

bool FAIGatewayChatController::TryParseToolCallsFromMessage(const TSharedPtr<FJsonObject>& MessageObject, TArray<FAIGatewayPendingToolCall>& OutToolCalls) const
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

        FAIGatewayPendingToolCall ParsedToolCall;
        (*ToolCallObject)->TryGetStringField(TEXT("id"), ParsedToolCall.Id);

        const TSharedPtr<FJsonObject>* FunctionObject = nullptr;
        if ((*ToolCallObject)->TryGetObjectField(TEXT("function"), FunctionObject) && FunctionObject != nullptr && (*FunctionObject).IsValid())
        {
            (*FunctionObject)->TryGetStringField(TEXT("name"), ParsedToolCall.Name);
            (*FunctionObject)->TryGetStringField(TEXT("arguments"), ParsedToolCall.ArgumentsJson);
        }

        ParsedToolCall.Arguments = ParseJsonObject(ParsedToolCall.ArgumentsJson);
        OutToolCalls.Add(ParsedToolCall);
    }

    return OutToolCalls.Num() > 0;
}

TSharedPtr<FJsonObject> FAIGatewayChatController::BuildUserMessageObject(const FString& UserPrompt) const
{
    TSharedPtr<FJsonObject> MessageObject = MakeShared<FJsonObject>();
    MessageObject->SetStringField(TEXT("role"), TEXT("user"));
    MessageObject->SetStringField(TEXT("content"), UserPrompt);
    return MessageObject;
}

TSharedPtr<FJsonObject> FAIGatewayChatController::BuildAssistantMessageObject(const FString& AssistantContent, const TArray<FAIGatewayPendingToolCall>& ToolCalls) const
{
    TSharedPtr<FJsonObject> MessageObject = MakeShared<FJsonObject>();
    MessageObject->SetStringField(TEXT("role"), TEXT("assistant"));
    MessageObject->SetStringField(TEXT("content"), AssistantContent);

    if (ToolCalls.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> ToolCallArray;
        for (const FAIGatewayPendingToolCall& ToolCall : ToolCalls)
        {
            TSharedPtr<FJsonObject> FunctionObject = MakeShared<FJsonObject>();
            FunctionObject->SetStringField(TEXT("name"), ToolCall.Name);
            FunctionObject->SetStringField(TEXT("arguments"), ToolCall.ArgumentsJson);

            TSharedPtr<FJsonObject> ToolCallObject = MakeShared<FJsonObject>();
            ToolCallObject->SetStringField(TEXT("id"), ToolCall.Id);
            ToolCallObject->SetStringField(TEXT("type"), TEXT("function"));
            ToolCallObject->SetObjectField(TEXT("function"), FunctionObject);
            ToolCallArray.Add(MakeShared<FJsonValueObject>(ToolCallObject));
        }

        MessageObject->SetArrayField(TEXT("tool_calls"), ToolCallArray);
    }

    return MessageObject;
}

TSharedPtr<FJsonObject> FAIGatewayChatController::BuildToolResultMessageObject(const FString& ToolCallId, const FString& Content) const
{
    TSharedPtr<FJsonObject> MessageObject = MakeShared<FJsonObject>();
    MessageObject->SetStringField(TEXT("role"), TEXT("tool"));
    MessageObject->SetStringField(TEXT("tool_call_id"), ToolCallId);
    MessageObject->SetStringField(TEXT("content"), Content);
    return MessageObject;
}

TArray<TSharedPtr<FJsonValue>> FAIGatewayChatController::BuildRequestMessages() const
{
    TArray<TSharedPtr<FJsonValue>> Messages;
    if (const FAIGatewayChatSession* Session = GetActiveSession())
    {
        for (const TSharedPtr<FJsonObject>& Message : Session->RequestMessages)
        {
            if (Message.IsValid())
            {
                Messages.Add(MakeShared<FJsonValueObject>(Message));
            }
        }
    }
    return Messages;
}

TArray<TSharedPtr<FJsonValue>> FAIGatewayChatController::BuildToolDefinitions() const
{
    TArray<TSharedPtr<FJsonValue>> Tools;

    for (const FAIGatewayToolDefinition& Definition : FAIGatewayToolRuntime::Get().GetToolDefinitions())
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

FString FAIGatewayChatController::ExtractErrorMessage(const FString& ResponseBody) const
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

FString FAIGatewayChatController::FormatToolArgumentsSummary(const FAIGatewayPendingToolCall& ToolCall) const
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

void FAIGatewayChatController::AppendToolCallMessages(const TArray<FAIGatewayPendingToolCall>& ToolCalls)
{
    if (!ShouldShowToolActivityInChat())
    {
        return;
    }

    for (const FAIGatewayPendingToolCall& ToolCall : ToolCalls)
    {
        AppendMessage(
            TEXT("Tool"),
            FString::Printf(TEXT("Requested `%s` with arguments:\n```json\n%s\n```"), *ToolCall.Name, *FormatToolArgumentsSummary(ToolCall)));
    }
}

void FAIGatewayChatController::AppendToolResultMessage(const FAIGatewayPendingToolCall& ToolCall, const FAIGatewayToolResult& Result)
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

FString FAIGatewayChatController::GetPendingToolApprovalPrompt() const
{
    const FAIGatewayChatSession* Session = GetActiveSession();
    if (Session == nullptr || !Session->PendingToolCalls.IsValidIndex(Session->PendingToolCallIndex))
    {
        return TEXT("No tool call is waiting for approval.");
    }

    const FAIGatewayPendingToolCall& ToolCall = Session->PendingToolCalls[Session->PendingToolCallIndex];
    return FString::Printf(TEXT("Approve tool '%s' with arguments %s?"), *ToolCall.Name, *FormatToolArgumentsSummary(ToolCall));
}

FString FAIGatewayChatController::GetSendButtonText() const
{
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
