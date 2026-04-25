#pragma once

#include "Chat/Model/AIGatewayChatTypes.h"
#include "Widgets/SCompoundWidget.h"

class SAIGatewayChatMessageCard : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAIGatewayChatMessageCard) {}
        SLATE_ARGUMENT(FAIGatewayChatMessage, Message)
        SLATE_ARGUMENT(bool, RenderMarkdown)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void Refresh(const FAIGatewayChatMessage& InMessage, bool bInRenderMarkdown);

private:
    void RebuildForMessage(const FAIGatewayChatMessage& InMessage, bool bInRenderMarkdown);

    FAIGatewayChatMessage CachedMessage;
    bool bRenderMarkdown = true;
    TSharedPtr<class SHorizontalBox> RootRow;
    TSharedPtr<class SBox> BubbleWidthBox;
    TSharedPtr<class SBorder> BubbleBorder;
    TSharedPtr<class SRichTextBlock> RoleTextBlock;
    TSharedPtr<class SAIGatewayMarkdownMessageBody> MessageBody;
    TSharedPtr<class SMultiLineEditableText> PlainTextBody;
};
