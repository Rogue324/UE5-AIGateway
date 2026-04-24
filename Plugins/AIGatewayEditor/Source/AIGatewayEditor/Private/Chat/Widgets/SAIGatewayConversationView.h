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

    TSharedPtr<class SScrollBox> ChatHistoryScrollBox;
    TArray<FAIGatewayChatMessage> CachedMessages;
    bool bCachedEmptyState = true;
};
