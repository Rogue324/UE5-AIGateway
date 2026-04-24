#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ITextLayoutMarshaller;
class SMultiLineEditableText;

class SAIGatewayMarkdownMessageBody : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAIGatewayMarkdownMessageBody) {}
        SLATE_ARGUMENT(FString, MarkdownText)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    void RebuildContent(const FString& InMarkdownText);
    FString BuildRenderableRichText(const FString& InMarkdownText) const;

    TSharedPtr<SMultiLineEditableText> RichTextWidget;
    TSharedPtr<ITextLayoutMarshaller> RichTextMarshaller;
};
