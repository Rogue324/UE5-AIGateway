#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SAIGatewayChatPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAIGatewayChatPanel) {}
    SLATE_END_ARGS()

    virtual ~SAIGatewayChatPanel() override;

    void Construct(const FArguments& InArgs);

private:
    void RefreshFromController();

    TSharedPtr<class FAIGatewayChatController> ChatController;
    TSharedPtr<class SEditableTextBox> ModelTextBox;
    TSharedPtr<class STextBlock> ContextTextBlock;
    TSharedPtr<class STextBlock> StatusTextBlock;
    TSharedPtr<class SAIGatewaySessionTabBar> SessionTabBar;
    TSharedPtr<class SAIGatewayConversationView> ConversationView;
    TSharedPtr<class SAIGatewayToolConfirmationBar> ToolConfirmationBar;
    TSharedPtr<class SAIGatewayComposer> Composer;
};
