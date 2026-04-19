#include "Chat/Markdown/AIGatewayMarkdownRichTextRenderer.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Chat/Markdown/AIGatewayMarkdownParser.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleRegistry.h"

namespace
{
    const FName ChatRichTextStyleSetName(TEXT("AIGatewayChatRichText"));

    FTextBlockStyle MakeChatTextStyle(const FTextBlockStyle& BaseStyle, const FSlateFontInfo& Font, const FSlateColor& Color)
    {
        FTextBlockStyle Style = BaseStyle;
        Style.SetFont(Font);
        Style.SetColorAndOpacity(Color);
        return Style;
    }

    FHyperlinkStyle MakeHyperlinkStyle(const FHyperlinkStyle& BaseStyle, const FTextBlockStyle& TextStyle)
    {
        FHyperlinkStyle Style = BaseStyle;
        Style.SetTextStyle(TextStyle);
        Style.UnderlineStyle.SetNormal(FSlateNoResource());
        Style.UnderlineStyle.SetHovered(FSlateNoResource());
        Style.UnderlineStyle.SetPressed(FSlateNoResource());
        return Style;
    }
}

const ISlateStyle& FAIGatewayMarkdownRichTextRenderer::GetStyle()
{
    static TSharedPtr<FSlateStyleSet> StyleSet;
    if (!StyleSet.IsValid())
    {
        StyleSet = MakeShared<FSlateStyleSet>(ChatRichTextStyleSetName);

        const FTextBlockStyle BaseStyle = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
        const FHyperlinkStyle BaseHyperlinkStyle = FCoreStyle::Get().GetWidgetStyle<FHyperlinkStyle>("Hyperlink");
        const FSlateFontInfo BodyFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
        const FSlateFontInfo BoldFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
        const FSlateFontInfo HeadingFont = FCoreStyle::GetDefaultFontStyle("Bold", 12);
        const FSlateFontInfo MonoFont = FCoreStyle::GetDefaultFontStyle("Mono", 10);

        const FTextBlockStyle BodyStyle = MakeChatTextStyle(BaseStyle, BodyFont, FSlateColor(FLinearColor(0.90f, 0.92f, 0.95f)));
        StyleSet->Set("MarkdownBody", BodyStyle);
        StyleSet->Set("MarkdownBold", MakeChatTextStyle(BaseStyle, BoldFont, FSlateColor(FLinearColor(0.97f, 0.97f, 0.97f))));
        StyleSet->Set("MarkdownItalic", MakeChatTextStyle(BaseStyle, BodyFont, FSlateColor(FLinearColor(0.90f, 0.92f, 0.95f))));
        StyleSet->Set("MarkdownHeading", MakeChatTextStyle(BaseStyle, HeadingFont, FSlateColor(FLinearColor(0.96f, 0.96f, 0.98f))));
        StyleSet->Set("MarkdownInlineCode", MakeChatTextStyle(BaseStyle, MonoFont, FSlateColor(FLinearColor(0.98f, 0.84f, 0.62f))));
        StyleSet->Set("MarkdownCodeBlock", MakeChatTextStyle(BaseStyle, MonoFont, FSlateColor(FLinearColor(0.84f, 0.90f, 1.0f))));
        StyleSet->Set("RoleYou", MakeChatTextStyle(BaseStyle, BoldFont, FSlateColor(FLinearColor(0.55f, 0.82f, 1.0f))));
        StyleSet->Set("RoleAI", MakeChatTextStyle(BaseStyle, BoldFont, FSlateColor(FLinearColor(0.73f, 0.94f, 0.66f))));
        StyleSet->Set("RoleSystem", MakeChatTextStyle(BaseStyle, BoldFont, FSlateColor(FLinearColor(1.0f, 0.84f, 0.50f))));
        StyleSet->Set("RoleTool", MakeChatTextStyle(BaseStyle, BoldFont, FSlateColor(FLinearColor(0.94f, 0.72f, 1.0f))));
        StyleSet->Set("RoleToolResult", MakeChatTextStyle(BaseStyle, BoldFont, FSlateColor(FLinearColor(0.90f, 0.74f, 0.58f))));
        StyleSet->Set("browser", MakeHyperlinkStyle(BaseHyperlinkStyle, MakeChatTextStyle(BaseStyle, BodyFont, FSlateColor(FLinearColor(0.45f, 0.76f, 1.0f)))));
        StyleSet->Set("BubbleYou", new FSlateRoundedBoxBrush(FLinearColor(0.15f, 0.28f, 0.45f, 0.96f), 14.0f, FLinearColor(0.27f, 0.48f, 0.72f, 0.95f), 1.0f));
        StyleSet->Set("BubbleAI", new FSlateRoundedBoxBrush(FLinearColor(0.17f, 0.22f, 0.19f, 0.96f), 14.0f, FLinearColor(0.30f, 0.40f, 0.33f, 0.95f), 1.0f));
        StyleSet->Set("BubbleSystem", new FSlateRoundedBoxBrush(FLinearColor(0.20f, 0.18f, 0.12f, 0.96f), 14.0f, FLinearColor(0.55f, 0.43f, 0.18f, 0.95f), 1.0f));
        StyleSet->Set("BubbleTool", new FSlateRoundedBoxBrush(FLinearColor(0.26f, 0.19f, 0.31f, 0.96f), 14.0f, FLinearColor(0.50f, 0.34f, 0.58f, 0.95f), 1.0f));
        StyleSet->Set("BubbleToolResult", new FSlateRoundedBoxBrush(FLinearColor(0.28f, 0.23f, 0.17f, 0.96f), 14.0f, FLinearColor(0.56f, 0.43f, 0.26f, 0.95f), 1.0f));
        StyleSet->Set("CodeBlockBubble", new FSlateRoundedBoxBrush(FLinearColor(0.08f, 0.10f, 0.12f, 0.98f), 10.0f, FLinearColor(0.23f, 0.28f, 0.35f, 0.98f), 1.0f));
        StyleSet->Set("TableHeaderBubble", new FSlateRoundedBoxBrush(FLinearColor(0.20f, 0.24f, 0.29f, 0.95f), 8.0f, FLinearColor(0.34f, 0.41f, 0.50f, 0.95f), 1.0f));
        StyleSet->Set("TableCellBubble", new FSlateRoundedBoxBrush(FLinearColor(0.12f, 0.14f, 0.17f, 0.95f), 8.0f, FLinearColor(0.25f, 0.29f, 0.34f, 0.95f), 1.0f));
        FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
    }

    return *StyleSet;
}

FString FAIGatewayMarkdownRichTextRenderer::RenderMarkdownToRichText(const FString& MarkdownText, bool bTreatAsParagraph)
{
    (void)bTreatAsParagraph;

    FString Trimmed = MarkdownText.TrimStartAndEnd();
    if (Trimmed.IsEmpty())
    {
        return TEXT(" ");
    }

    if (Trimmed.StartsWith(TEXT("### ")))
    {
        return FString::Printf(TEXT("<MarkdownHeading>%s</>"), *RenderInlineMarkdown(Trimmed.RightChop(4)));
    }

    if (Trimmed.StartsWith(TEXT("## ")))
    {
        return FString::Printf(TEXT("<MarkdownHeading>%s</>"), *RenderInlineMarkdown(Trimmed.RightChop(3)));
    }

    if (Trimmed.StartsWith(TEXT("# ")))
    {
        return FString::Printf(TEXT("<MarkdownHeading>%s</>"), *RenderInlineMarkdown(Trimmed.RightChop(2)));
    }

    TArray<FString> Lines;
    FAIGatewayMarkdownParser::NormalizeLineEndings(MarkdownText).ParseIntoArray(Lines, TEXT("\n"), false);

    FString Output;
    for (int32 Index = 0; Index < Lines.Num(); ++Index)
    {
        const FString& Line = Lines[Index];
        const FString TrimmedLine = Line.TrimStartAndEnd();

        if (Index > 0)
        {
            Output.Append(TEXT("\n"));
        }

        if (TrimmedLine.StartsWith(TEXT("- ")) || TrimmedLine.StartsWith(TEXT("* ")))
        {
            Output.Append(FString::Printf(TEXT("- %s"), *RenderInlineMarkdown(TrimmedLine.RightChop(2))));
        }
        else if (TrimmedLine.Len() > 2 && FChar::IsDigit(TrimmedLine[0]) && TrimmedLine[1] == TEXT('.'))
        {
            Output.Append(RenderInlineMarkdown(TrimmedLine));
        }
        else
        {
            Output.Append(RenderInlineMarkdown(Line));
        }
    }

    return Output;
}

FString FAIGatewayMarkdownRichTextRenderer::RenderInlineMarkdown(const FString& Text, bool bEnableStyleTags)
{
    FString Output;
    bool bInBold = false;
    bool bInItalic = false;
    bool bInInlineCode = false;

    for (int32 Index = 0; Index < Text.Len();)
    {
        const TCHAR CurrentChar = Text[Index];

        if (!bInInlineCode && Index + 1 < Text.Len() && Text[Index] == TEXT('*') && Text[Index + 1] == TEXT('*'))
        {
            if (bEnableStyleTags)
            {
                Output.Append(bInBold ? TEXT("</>") : TEXT("<MarkdownBold>"));
            }
            bInBold = !bInBold;
            Index += 2;
            continue;
        }

        if (!bInInlineCode && CurrentChar == TEXT('*'))
        {
            if (bEnableStyleTags)
            {
                Output.Append(bInItalic ? TEXT("</>") : TEXT("<MarkdownItalic>"));
            }
            bInItalic = !bInItalic;
            ++Index;
            continue;
        }

        if (CurrentChar == TEXT('`'))
        {
            if (bEnableStyleTags)
            {
                Output.Append(bInInlineCode ? TEXT("</>") : TEXT("<MarkdownInlineCode>"));
            }
            bInInlineCode = !bInInlineCode;
            ++Index;
            continue;
        }

        if (!bInInlineCode && (Text.Mid(Index).StartsWith(TEXT("http://")) || Text.Mid(Index).StartsWith(TEXT("https://"))))
        {
            int32 EndIndex = Index;
            while (EndIndex < Text.Len() && !FChar::IsWhitespace(Text[EndIndex]) && Text[EndIndex] != TEXT(')'))
            {
                ++EndIndex;
            }

            const FString Url = Text.Mid(Index, EndIndex - Index);
            Output.Append(FString::Printf(TEXT("<browser href=\"%s\">%s</>"), *EscapeRichText(Url), *EscapeRichText(Url)));
            Index = EndIndex;
            continue;
        }

        if (!bInInlineCode && CurrentChar == TEXT('['))
        {
            const int32 CloseBracketIndex = Text.Find(TEXT("]"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Index + 1);
            const int32 OpenParenIndex = CloseBracketIndex == INDEX_NONE ? INDEX_NONE : Text.Find(TEXT("("), ESearchCase::IgnoreCase, ESearchDir::FromStart, CloseBracketIndex + 1);
            const int32 CloseParenIndex = OpenParenIndex == INDEX_NONE ? INDEX_NONE : Text.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromStart, OpenParenIndex + 1);
            if (CloseBracketIndex != INDEX_NONE && OpenParenIndex == CloseBracketIndex + 1 && CloseParenIndex != INDEX_NONE)
            {
                const FString Label = Text.Mid(Index + 1, CloseBracketIndex - Index - 1);
                const FString Url = Text.Mid(OpenParenIndex + 1, CloseParenIndex - OpenParenIndex - 1);
                Output.Append(FString::Printf(TEXT("<browser href=\"%s\">%s</>"), *EscapeRichText(Url), *EscapeRichText(Label)));
                Index = CloseParenIndex + 1;
                continue;
            }
        }

        Output.Append(EscapeRichText(FString(1, &CurrentChar)));
        ++Index;
    }

    while (bInInlineCode)
    {
        if (bEnableStyleTags)
        {
            Output.Append(TEXT("</>"));
        }
        bInInlineCode = false;
    }
    while (bInBold)
    {
        if (bEnableStyleTags)
        {
            Output.Append(TEXT("</>"));
        }
        bInBold = false;
    }
    while (bInItalic)
    {
        if (bEnableStyleTags)
        {
            Output.Append(TEXT("</>"));
        }
        bInItalic = false;
    }

    return Output;
}

bool FAIGatewayMarkdownRichTextRenderer::TryExtractHeadingContent(const FString& MarkdownText, FString& OutHeadingContent)
{
    const FString Trimmed = MarkdownText.TrimStartAndEnd();

    if (Trimmed.StartsWith(TEXT("### ")))
    {
        OutHeadingContent = RenderInlineMarkdown(Trimmed.RightChop(4), false);
        return true;
    }

    if (Trimmed.StartsWith(TEXT("## ")))
    {
        OutHeadingContent = RenderInlineMarkdown(Trimmed.RightChop(3), false);
        return true;
    }

    if (Trimmed.StartsWith(TEXT("# ")))
    {
        OutHeadingContent = RenderInlineMarkdown(Trimmed.RightChop(2), false);
        return true;
    }

    OutHeadingContent.Empty();
    return false;
}

FString FAIGatewayMarkdownRichTextRenderer::EscapeRichText(const FString& Text)
{
    FString Escaped = Text;
    Escaped.ReplaceInline(TEXT("&"), TEXT("&amp;"));
    Escaped.ReplaceInline(TEXT("<"), TEXT("&lt;"));
    Escaped.ReplaceInline(TEXT(">"), TEXT("&gt;"));
    return Escaped;
}
