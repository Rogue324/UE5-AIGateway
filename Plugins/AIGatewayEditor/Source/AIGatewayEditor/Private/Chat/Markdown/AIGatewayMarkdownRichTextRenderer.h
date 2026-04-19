#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

class FAIGatewayMarkdownRichTextRenderer
{
public:
    static const ISlateStyle& GetStyle();
    static FString RenderMarkdownToRichText(const FString& MarkdownText, bool bTreatAsParagraph = false);
    static FString RenderInlineMarkdown(const FString& Text, bool bEnableStyleTags = true);
    static FString EscapeRichText(const FString& Text);
    static bool TryExtractHeadingContent(const FString& MarkdownText, FString& OutHeadingContent);
};
