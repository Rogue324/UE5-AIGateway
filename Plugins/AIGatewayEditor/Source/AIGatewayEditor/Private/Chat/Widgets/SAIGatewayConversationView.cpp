#include "Chat/Widgets/SAIGatewayConversationView.h"

#include "Chat/Widgets/SAIGatewayChatMessageCard.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

bool SAIGatewayConversationView::AreMessagesUnchanged(const TArray<FAIGatewayChatMessage>& InMessages) const
{
    if (CachedMessages.Num() != InMessages.Num())
    {
        return false;
    }

    for (int32 Index = 0; Index < InMessages.Num(); ++Index)
    {
        const FAIGatewayChatMessage& CachedMessage = CachedMessages[Index];
        const FAIGatewayChatMessage& IncomingMessage = InMessages[Index];
        if (!CachedMessage.Role.Equals(IncomingMessage.Role, ESearchCase::CaseSensitive)
            || !CachedMessage.Content.Equals(IncomingMessage.Content, ESearchCase::CaseSensitive))
        {
            return false;
        }
    }

    return true;
}

void SAIGatewayConversationView::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SAssignNew(ChatHistoryScrollBox, SScrollBox)
    ];
}

bool SAIGatewayConversationView::ShouldRenderMarkdownForMessage(const FAIGatewayChatPanelViewState& ViewState, int32 MessageIndex, const FAIGatewayChatMessage& Message) const
{
    (void)ViewState;
    (void)MessageIndex;
    (void)Message;
    return true;
}

void SAIGatewayConversationView::RebuildMessageList(const TArray<FAIGatewayChatMessage>& InMessages)
{
    if (!ChatHistoryScrollBox.IsValid())
    {
        return;
    }

    ChatHistoryScrollBox->ClearChildren();
    MessageCards.Reset();
    CachedMessages = InMessages;

    for (const FAIGatewayChatMessage& Message : InMessages)
    {
        TSharedPtr<SAIGatewayChatMessageCard> MessageCard;
        ChatHistoryScrollBox->AddSlot()
        .Padding(0.0f, 0.0f, 0.0f, 10.0f)
        [
            SAssignNew(MessageCard, SAIGatewayChatMessageCard)
            .Message(Message)
            .RenderMarkdown(true)
        ];
        MessageCards.Add(MessageCard);
    }
}

void SAIGatewayConversationView::Refresh(const FAIGatewayChatPanelViewState& ViewState)
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
        ChatHistoryScrollBox->AddSlot()
        .Padding(0.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Messages from you and the model will appear here.")))
            .AutoWrapText(true)
            .ColorAndOpacity(FLinearColor(0.52f, 0.56f, 0.62f))
        ];
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
            const FAIGatewayChatMessage& Message = ViewState.VisibleMessages[Index];
            TSharedPtr<SAIGatewayChatMessageCard> MessageCard;
            ChatHistoryScrollBox->AddSlot()
            .Padding(0.0f, 0.0f, 0.0f, 10.0f)
            [
                SAssignNew(MessageCard, SAIGatewayChatMessageCard)
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
        const FAIGatewayChatMessage& IncomingMessage = ViewState.VisibleMessages[Index];
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
