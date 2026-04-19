#pragma once

#include "Chat/Model/AIGatewayChatTypes.h"
#include "Delegates/Delegate.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_OneParam(FOnAIGatewayDraftChanged, const FString&);
DECLARE_DELEGATE(FOnAIGatewaySendRequested);

class SAIGatewayComposer : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAIGatewayComposer) {}
        SLATE_EVENT(FOnAIGatewayDraftChanged, OnDraftChanged)
        SLATE_EVENT(FOnAIGatewaySendRequested, OnSendRequested)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void Refresh(const FAIGatewayChatPanelViewState& ViewState);

private:
    void HandlePromptTextChanged(const FText& InText);
    FReply HandlePromptKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);

    FOnAIGatewayDraftChanged OnDraftChanged;
    FOnAIGatewaySendRequested OnSendRequested;
    TSharedPtr<class SMultiLineEditableTextBox> PromptTextBox;
    TSharedPtr<class SButton> SendButton;
    FString SendButtonText;
    bool bCanSend = false;
    bool bSyncingText = false;
};
