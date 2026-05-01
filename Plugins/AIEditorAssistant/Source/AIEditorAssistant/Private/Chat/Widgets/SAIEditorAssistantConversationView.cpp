#include "Chat/Widgets/SAIEditorAssistantConversationView.h"

#include "Chat/Widgets/SAIEditorAssistantChatMessageCard.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

bool SAIEditorAssistantConversationView::AreMessagesUnchanged(const TArray<FAIEditorAssistantChatMessage>& InMessages) const
{
    if (CachedMessages.Num() != InMessages.Num())
    {
        return false;
    }

    for (int32 Index = 0; Index < InMessages.Num(); ++Index)
    {
        const FAIEditorAssistantChatMessage& CachedMessage = CachedMessages[Index];
        const FAIEditorAssistantChatMessage& IncomingMessage = InMessages[Index];
        if (!CachedMessage.Role.Equals(IncomingMessage.Role, ESearchCase::CaseSensitive)
            || !CachedMessage.Content.Equals(IncomingMessage.Content, ESearchCase::CaseSensitive))
        {
            return false;
        }
    }

    return true;
}

void SAIEditorAssistantConversationView::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SAssignNew(ChatHistoryScrollBox, SScrollBox)
    ];
}

bool SAIEditorAssistantConversationView::ShouldRenderMarkdownForMessage(const FAIEditorAssistantChatPanelViewState& ViewState, int32 MessageIndex, const FAIEditorAssistantChatMessage& Message) const
{
    (void)ViewState;
    (void)MessageIndex;
    (void)Message;
    return true;
}

void SAIEditorAssistantConversationView::RebuildMessageList(const TArray<FAIEditorAssistantChatMessage>& InMessages)
{
    if (!ChatHistoryScrollBox.IsValid())
    {
        return;
    }

    ChatHistoryScrollBox->ClearChildren();
    MessageCards.Reset();
    CachedMessages = InMessages;

    for (const FAIEditorAssistantChatMessage& Message : InMessages)
    {
        TSharedPtr<SAIEditorAssistantChatMessageCard> MessageCard;
        ChatHistoryScrollBox->AddSlot()
        .Padding(0.0f, 0.0f, 0.0f, 10.0f)
        [
            SAssignNew(MessageCard, SAIEditorAssistantChatMessageCard)
            .Message(Message)
            .RenderMarkdown(true)
        ];
        MessageCards.Add(MessageCard);
    }
}

void SAIEditorAssistantConversationView::Refresh(const FAIEditorAssistantChatPanelViewState& ViewState)
{
    if (!ChatHistoryScrollBox.IsValid())
    {
        return;
    }

    const bool bIsEmpty = ViewState.VisibleMessages.Num() == 0;
    if (bIsEmpty == bCachedEmptyState && (bIsEmpty || AreMessagesUnchanged(ViewState.VisibleMessages)))
    {
        return;
    }

    bCachedEmptyState = bIsEmpty;

    if (bIsEmpty)
    {
        ChatHistoryScrollBox->ClearChildren();
        CachedMessages.Reset();
        MessageCards.Reset();
        return;
    }

    const bool bShouldRebuild = CachedMessages.Num() != ViewState.VisibleMessages.Num()
        || MessageCards.Num() != ViewState.VisibleMessages.Num();

    if (bShouldRebuild)
    {
        ChatHistoryScrollBox->ClearChildren();
        MessageCards.Reset();
        CachedMessages = ViewState.VisibleMessages;

        for (int32 Index = 0; Index < ViewState.VisibleMessages.Num(); ++Index)
        {
            const FAIEditorAssistantChatMessage& Message = ViewState.VisibleMessages[Index];
            TSharedPtr<SAIEditorAssistantChatMessageCard> MessageCard;
            ChatHistoryScrollBox->AddSlot()
            .Padding(0.0f, 0.0f, 0.0f, 10.0f)
            [
                SAssignNew(MessageCard, SAIEditorAssistantChatMessageCard)
                .Message(Message)
                .RenderMarkdown(ShouldRenderMarkdownForMessage(ViewState, Index, Message))
            ];
            MessageCards.Add(MessageCard);
        }

        ChatHistoryScrollBox->ScrollToEnd();
        return;
    }

    bool bLastMessageChanged = false;
    for (int32 Index = 0; Index < ViewState.VisibleMessages.Num(); ++Index)
    {
        const FAIEditorAssistantChatMessage& IncomingMessage = ViewState.VisibleMessages[Index];
        if (!CachedMessages[Index].Role.Equals(IncomingMessage.Role, ESearchCase::CaseSensitive)
            || !CachedMessages[Index].Content.Equals(IncomingMessage.Content, ESearchCase::CaseSensitive))
        {
            if (MessageCards.IsValidIndex(Index) && MessageCards[Index].IsValid())
            {
                MessageCards[Index]->Refresh(IncomingMessage, ShouldRenderMarkdownForMessage(ViewState, Index, IncomingMessage));
            }

            CachedMessages[Index] = IncomingMessage;
            bLastMessageChanged = (Index == ViewState.VisibleMessages.Num() - 1);
        }
    }

    if (bLastMessageChanged)
    {
        ChatHistoryScrollBox->ScrollToEnd();
    }
}
