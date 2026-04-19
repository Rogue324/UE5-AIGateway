#pragma once

#include "Chat/Model/AIGatewayChatTypes.h"
#include "Widgets/SCompoundWidget.h"

class SAIGatewayChatMessageCard : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAIGatewayChatMessageCard) {}
        SLATE_ARGUMENT(FAIGatewayChatMessage, Message)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
};
