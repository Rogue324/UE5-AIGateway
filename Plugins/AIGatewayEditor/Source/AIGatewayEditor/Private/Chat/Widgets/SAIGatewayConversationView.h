#pragma once

#include "Chat/Model/AIGatewayChatTypes.h"
#include "Widgets/SCompoundWidget.h"

class SAIGatewayConversationView : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAIGatewayConversationView) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void Refresh(const FAIGatewayChatPanelViewState& ViewState);

private:
    bool AreMessagesUnchanged(const TArray<FAIGatewayChatMessage>& InMessages) const;
    bool ShouldRenderMarkdownForMessage(const FAIGatewayChatPanelViewState& ViewState, int32 MessageIndex, const FAIGatewayChatMessage& Message) const;
    void RebuildMessageList(const TArray<FAIGatewayChatMessage>& InMessages);

    TSharedPtr<class SScrollBox> ChatHistoryScrollBox;
    TArray<FAIGatewayChatMessage> CachedMessages;
    TArray<TSharedPtr<class SAIGatewayChatMessageCard>> MessageCards;
    bool bCachedEmptyState = true;
};
