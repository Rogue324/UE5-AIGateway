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

    ChatHistoryScrollBox->ClearChildren();
    bCachedEmptyState = bIsEmpty;

    if (bIsEmpty)
    {
        CachedMessages.Reset();
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

    CachedMessages = ViewState.VisibleMessages;

    for (const FAIGatewayChatMessage& Message : ViewState.VisibleMessages)
    {
        ChatHistoryScrollBox->AddSlot()
        .Padding(0.0f, 0.0f, 0.0f, 10.0f)
        [
            SNew(SAIGatewayChatMessageCard)
            .Message(Message)
        ];
    }

    ChatHistoryScrollBox->ScrollToEnd();
}
