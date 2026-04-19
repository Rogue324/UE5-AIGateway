#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SAIGatewayMarkdownMessageBody : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAIGatewayMarkdownMessageBody) {}
        SLATE_ARGUMENT(FString, MarkdownText)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
};
