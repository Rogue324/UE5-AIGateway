#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ITextLayoutMarshaller;
class SMultiLineEditableText;
class SVerticalBox;

class SAIGatewayMarkdownMessageBody : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAIGatewayMarkdownMessageBody) {}
        SLATE_ARGUMENT(FString, MarkdownText)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void RefreshMarkdown(const FString& InMarkdownText);

private:
    void RebuildContent(const FString& InMarkdownText);
    FString BuildRenderableRichText(const FString& InMarkdownText) const;
    TSharedRef<class SWidget> BuildBlockWidget(const struct FAIGatewayMarkdownBlock& Block) const;
    TSharedRef<class SWidget> BuildRichTextBlockWidget(const FString& RichText, const FName& TextStyleName, const FMargin& Margin) const;
    TSharedRef<class SWidget> BuildTableWidget(const TArray<TArray<FString>>& Rows) const;

    TSharedPtr<SVerticalBox> BodyContainer;
    TSharedPtr<ITextLayoutMarshaller> RichTextMarshaller;
};
