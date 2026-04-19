#include "Chat/Widgets/SAIGatewayConversationView.h"

#include "Chat/Widgets/SAIGatewayChatMessageCard.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

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

    ChatHistoryScrollBox->ClearChildren();

    if (ViewState.VisibleMessages.Num() == 0)
    {
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
