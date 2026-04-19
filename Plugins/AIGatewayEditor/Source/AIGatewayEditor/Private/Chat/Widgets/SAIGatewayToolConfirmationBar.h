#pragma once

#include "Chat/Model/AIGatewayChatTypes.h"
#include "Delegates/Delegate.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE(FOnAIGatewayToolApproved);
DECLARE_DELEGATE(FOnAIGatewayToolRejected);

class SAIGatewayToolConfirmationBar : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAIGatewayToolConfirmationBar) {}
        SLATE_EVENT(FOnAIGatewayToolApproved, OnApproved)
        SLATE_EVENT(FOnAIGatewayToolRejected, OnRejected)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void Refresh(const FAIGatewayChatPanelViewState& ViewState);

private:
    FOnAIGatewayToolApproved OnApproved;
    FOnAIGatewayToolRejected OnRejected;
    TSharedPtr<class STextBlock> PromptTextBlock;
    bool bIsVisible = false;
};
