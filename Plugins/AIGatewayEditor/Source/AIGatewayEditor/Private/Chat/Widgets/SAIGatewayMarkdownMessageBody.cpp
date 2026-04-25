#include "Chat/Widgets/SAIGatewayMarkdownMessageBody.h"

#include "Chat/Markdown/AIGatewayMarkdownParser.h"
#include "Chat/Markdown/AIGatewayMarkdownRichTextRenderer.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "Framework/Text/TextDecorators.h"
#include "HAL/PlatformProcess.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SMultiLineEditableText.h"

namespace
{
    void HandleBrowserLinkClicked(const FSlateHyperlinkRun::FMetadata& Metadata)
    {
        const FString* Url = Metadata.Find(TEXT("href"));
        if (Url != nullptr && !Url->IsEmpty())
        {
            FPlatformProcess::LaunchURL(**Url, nullptr, nullptr);
        }
    }

    FString ReplaceLegacyTagRange(
        const FString& Source,
        const FString& TagName,
        const FString& Prefix,
        const FString& Suffix)
    {
        const FString OpenToken = FString::Printf(TEXT("<%s>"), *TagName);
        const FString NamedCloseToken = FString::Printf(TEXT("</%s>"), *TagName);

        FString Result;
        int32 Cursor = 0;
        while (Cursor < Source.Len())
        {
            const int32 OpenIndex = Source.Find(OpenToken, ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor);
            if (OpenIndex == INDEX_NONE)
            {
                Result.Append(Source.Mid(Cursor));
                break;
            }

            Result.Append(Source.Mid(Cursor, OpenIndex - Cursor));

            const int32 ContentStart = OpenIndex + OpenToken.Len();
            const int32 ShortCloseIndex = Source.Find(TEXT("</>"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ContentStart);
            const int32 NamedCloseIndex = Source.Find(NamedCloseToken, ESearchCase::CaseSensitive, ESearchDir::FromStart, ContentStart);

            int32 CloseIndex = INDEX_NONE;
            int32 CloseLength = 0;
            if (ShortCloseIndex != INDEX_NONE && (NamedCloseIndex == INDEX_NONE || ShortCloseIndex < NamedCloseIndex))
            {
                CloseIndex = ShortCloseIndex;
                CloseLength = 3;
            }
            else if (NamedCloseIndex != INDEX_NONE)
            {
                CloseIndex = NamedCloseIndex;
                CloseLength = NamedCloseToken.Len();
            }

            if (CloseIndex == INDEX_NONE)
            {
                Result.Append(Source.Mid(OpenIndex));
                break;
            }

            Result.Append(Prefix);
            Result.Append(Source.Mid(ContentStart, CloseIndex - ContentStart));
            Result.Append(Suffix);
            Cursor = CloseIndex + CloseLength;
        }

        return Result;
    }

    FString ReplaceLegacyBrowserTags(const FString& Source)
    {
        FString Result;
        int32 Cursor = 0;
        while (Cursor < Source.Len())
        {
            const int32 OpenIndex = Source.Find(TEXT("<browser"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor);
            if (OpenIndex == INDEX_NONE)
            {
                Result.Append(Source.Mid(Cursor));
                break;
            }

            Result.Append(Source.Mid(Cursor, OpenIndex - Cursor));

            const int32 TagEndIndex = Source.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, OpenIndex);
            if (TagEndIndex == INDEX_NONE)
            {
                Result.Append(Source.Mid(OpenIndex));
                break;
            }

            FString Url;
            const int32 HrefIndex = Source.Find(TEXT("href=\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, OpenIndex);
            if (HrefIndex != INDEX_NONE && HrefIndex < TagEndIndex)
            {
                const int32 UrlStart = HrefIndex + 6;
                const int32 UrlEnd = Source.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, UrlStart);
                if (UrlEnd != INDEX_NONE && UrlEnd <= TagEndIndex)
                {
                    Url = Source.Mid(UrlStart, UrlEnd - UrlStart);
                }
            }

            const int32 ContentStart = TagEndIndex + 1;
            const int32 ShortCloseIndex = Source.Find(TEXT("</>"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ContentStart);
            const int32 NamedCloseIndex = Source.Find(TEXT("</browser>"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ContentStart);

            int32 CloseIndex = INDEX_NONE;
            int32 CloseLength = 0;
            if (ShortCloseIndex != INDEX_NONE && (NamedCloseIndex == INDEX_NONE || ShortCloseIndex < NamedCloseIndex))
            {
                CloseIndex = ShortCloseIndex;
                CloseLength = 3;
            }
            else if (NamedCloseIndex != INDEX_NONE)
            {
                CloseIndex = NamedCloseIndex;
                CloseLength = 10;
            }

            if (CloseIndex == INDEX_NONE)
            {
                Result.Append(Source.Mid(OpenIndex));
                break;
            }

            const FString Label = Source.Mid(ContentStart, CloseIndex - ContentStart);
            if (Url.IsEmpty())
            {
                Result.Append(Label);
            }
            else
            {
                Result.Append(FString::Printf(TEXT("[%s](%s)"), *Label, *Url));
            }

            Cursor = CloseIndex + CloseLength;
        }

        return Result;
    }

    FString ConvertLegacyCodeBlockTagsToMarkdown(const FString& Source)
    {
        FString Result;
        int32 Cursor = 0;
        while (Cursor < Source.Len())
        {
            const int32 OpenIndex = Source.Find(TEXT("<MarkdownCodeBlock>"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor);
            if (OpenIndex == INDEX_NONE)
            {
                Result.Append(Source.Mid(Cursor));
                break;
            }

            Result.Append(Source.Mid(Cursor, OpenIndex - Cursor));

            const int32 ContentStart = OpenIndex + 19;
            const int32 ShortCloseIndex = Source.Find(TEXT("</>"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ContentStart);
            const int32 NamedCloseIndex = Source.Find(TEXT("</MarkdownCodeBlock>"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ContentStart);

            int32 CloseIndex = INDEX_NONE;
            int32 CloseLength = 0;
            if (ShortCloseIndex != INDEX_NONE && (NamedCloseIndex == INDEX_NONE || ShortCloseIndex < NamedCloseIndex))
            {
                CloseIndex = ShortCloseIndex;
                CloseLength = 3;
            }
            else if (NamedCloseIndex != INDEX_NONE)
            {
                CloseIndex = NamedCloseIndex;
                CloseLength = 20;
            }

            if (CloseIndex == INDEX_NONE)
            {
                Result.Append(Source.Mid(OpenIndex));
                break;
            }

            const FString CodeContent = Source.Mid(ContentStart, CloseIndex - ContentStart).TrimStartAndEnd();
            Result.Append(TEXT("\n```\n"));
            Result.Append(CodeContent);
            Result.Append(TEXT("\n```\n"));
            Cursor = CloseIndex + CloseLength;
        }

        return Result;
    }

    FString StripResidualLegacyTags(const FString& Source)
    {
        FString Output = Source;

        static const TCHAR* LegacyTokens[] =
        {
            TEXT("<MarkdownHeading>"),
            TEXT("</MarkdownHeading>"),
            TEXT("<MarkdownBold>"),
            TEXT("</MarkdownBold>"),
            TEXT("<MarkdownItalic>"),
            TEXT("</MarkdownItalic>"),
            TEXT("<MarkdownInlineCode>"),
            TEXT("</MarkdownInlineCode>"),
            TEXT("<MarkdownCodeBlock>"),
            TEXT("</MarkdownCodeBlock>"),
            TEXT("</>"),
        };

        for (const TCHAR* Token : LegacyTokens)
        {
            Output.ReplaceInline(Token, TEXT(" "));
        }

        return Output;
    }

    FString PreprocessMarkdown(const FString& InMarkdownText)
    {
        FString Output = FAIGatewayMarkdownParser::NormalizeLineEndings(InMarkdownText);

        Output.ReplaceInline(TEXT("<RoleAI>"), TEXT(""));
        Output.ReplaceInline(TEXT("</RoleAI>"), TEXT(""));
        Output.ReplaceInline(TEXT("<RoleYou>"), TEXT(""));
        Output.ReplaceInline(TEXT("</RoleYou>"), TEXT(""));
        Output.ReplaceInline(TEXT("<RoleSystem>"), TEXT(""));
        Output.ReplaceInline(TEXT("</RoleSystem>"), TEXT(""));
        Output.ReplaceInline(TEXT("<RoleTool>"), TEXT(""));
        Output.ReplaceInline(TEXT("</RoleTool>"), TEXT(""));
        Output.ReplaceInline(TEXT("<RoleToolResult>"), TEXT(""));
        Output.ReplaceInline(TEXT("</RoleToolResult>"), TEXT(""));

        Output = ConvertLegacyCodeBlockTagsToMarkdown(Output);
        Output = ReplaceLegacyBrowserTags(Output);
        Output = ReplaceLegacyTagRange(Output, TEXT("MarkdownHeading"), TEXT("## "), TEXT(""));
        Output = ReplaceLegacyTagRange(Output, TEXT("MarkdownBold"), TEXT("**"), TEXT("**"));
        Output = ReplaceLegacyTagRange(Output, TEXT("MarkdownItalic"), TEXT("*"), TEXT("*"));
        Output = ReplaceLegacyTagRange(Output, TEXT("MarkdownInlineCode"), TEXT("`"), TEXT("`"));
        Output = StripResidualLegacyTags(Output);

        return Output;
    }

    FString RenderCodeBlockRichText(const FString& CodeText)
    {
        TArray<FString> Lines;
        FAIGatewayMarkdownParser::NormalizeLineEndings(CodeText).ParseIntoArray(Lines, TEXT("\n"), false);

        FString Output;
        for (int32 Index = 0; Index < Lines.Num(); ++Index)
        {
            if (Index > 0)
            {
                Output.Append(TEXT("\n"));
            }

            const FString NormalizedLine = FAIGatewayMarkdownRichTextRenderer::NormalizeForDisplay(Lines[Index]);
            const FString EscapedLine = FAIGatewayMarkdownRichTextRenderer::EscapeRichText(NormalizedLine.IsEmpty() ? TEXT(" ") : NormalizedLine);
            Output.Append(FString::Printf(TEXT("<MarkdownCodeBlock>%s</>"), *EscapedLine));
        }

        return Output.IsEmpty() ? TEXT("<MarkdownCodeBlock> </>") : Output;
    }

}

void SAIGatewayMarkdownMessageBody::Construct(const FArguments& InArgs)
{
    TSharedRef<FRichTextLayoutMarshaller> Marshaller = FRichTextLayoutMarshaller::Create(TArray<TSharedRef<ITextDecorator>>(), &FAIGatewayMarkdownRichTextRenderer::GetStyle());
    Marshaller->AppendInlineDecorator(FHyperlinkDecorator::Create(TEXT("browser"), FSlateHyperlinkRun::FOnClick::CreateStatic(&HandleBrowserLinkClicked)));
    RichTextMarshaller = Marshaller;

    ChildSlot
    [
        SAssignNew(BodyContainer, SVerticalBox)
    ];

    RebuildContent(InArgs._MarkdownText);
}

void SAIGatewayMarkdownMessageBody::RefreshMarkdown(const FString& InMarkdownText)
{
    RebuildContent(InMarkdownText);
}

void SAIGatewayMarkdownMessageBody::RebuildContent(const FString& InMarkdownText)
{
    if (!BodyContainer.IsValid())
    {
        return;
    }

    BodyContainer->ClearChildren();

    const FString PreprocessedMarkdown = PreprocessMarkdown(InMarkdownText);
    const TArray<FAIGatewayMarkdownBlock> Blocks = FAIGatewayMarkdownParser::ParseBlocks(PreprocessedMarkdown);
    if (Blocks.Num() == 0)
    {
        BodyContainer->AddSlot()
        .AutoHeight()
        [
            BuildRichTextBlockWidget(TEXT(" "), TEXT("MarkdownBody"), FMargin(0.0f))
        ];
        return;
    }

    for (int32 Index = 0; Index < Blocks.Num(); ++Index)
    {
        BodyContainer->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, Index + 1 < Blocks.Num() ? 10.0f : 0.0f)
        [
            BuildBlockWidget(Blocks[Index])
        ];
    }
}

FString SAIGatewayMarkdownMessageBody::BuildRenderableRichText(const FString& InMarkdownText) const
{
    const FString PreprocessedMarkdown = PreprocessMarkdown(InMarkdownText);
    const TArray<FAIGatewayMarkdownBlock> Blocks = FAIGatewayMarkdownParser::ParseBlocks(PreprocessedMarkdown);

    if (Blocks.Num() == 0)
    {
        return TEXT(" ");
    }

    FString Output;
    for (int32 Index = 0; Index < Blocks.Num(); ++Index)
    {
        if (Index > 0)
        {
            Output.Append(TEXT("\n\n"));
        }

        const FAIGatewayMarkdownBlock& Block = Blocks[Index];
        switch (Block.Type)
        {
        case EAIGatewayMarkdownBlockType::CodeBlock:
            Output.Append(RenderCodeBlockRichText(Block.Text));
            break;

        case EAIGatewayMarkdownBlockType::Table:
            Output.Append(TEXT("[table]"));
            break;

        case EAIGatewayMarkdownBlockType::Paragraph:
        default:
            Output.Append(FAIGatewayMarkdownRichTextRenderer::RenderMarkdownToRichText(Block.Text, true));
            break;
        }
    }

    return Output.IsEmpty() ? TEXT(" ") : Output;
}

TSharedRef<SWidget> SAIGatewayMarkdownMessageBody::BuildBlockWidget(const FAIGatewayMarkdownBlock& Block) const
{
    switch (Block.Type)
    {
    case EAIGatewayMarkdownBlockType::CodeBlock:
        return BuildRichTextBlockWidget(RenderCodeBlockRichText(Block.Text), TEXT("MarkdownBody"), FMargin(0.0f));

    case EAIGatewayMarkdownBlockType::Table:
        return BuildTableWidget(Block.TableRows);

    case EAIGatewayMarkdownBlockType::Paragraph:
    default:
        return BuildRichTextBlockWidget(
            FAIGatewayMarkdownRichTextRenderer::RenderMarkdownToRichText(Block.Text, true),
            TEXT("MarkdownBody"),
            FMargin(0.0f));
    }
}

TSharedRef<SWidget> SAIGatewayMarkdownMessageBody::BuildRichTextBlockWidget(const FString& RichText, const FName& TextStyleName, const FMargin& Margin) const
{
    return SNew(SMultiLineEditableText)
        .Marshaller(RichTextMarshaller.ToSharedRef())
        .Text(FText::FromString(RichText.IsEmpty() ? TEXT(" ") : RichText))
        .TextStyle(&FAIGatewayMarkdownRichTextRenderer::GetStyle().GetWidgetStyle<FTextBlockStyle>(TextStyleName))
        .IsReadOnly(true)
        .AllowContextMenu(true)
        .AutoWrapText(true)
        .Margin(Margin)
        .ClearTextSelectionOnFocusLoss(false)
        .SelectWordOnMouseDoubleClick(true);
}

TSharedRef<SWidget> SAIGatewayMarkdownMessageBody::BuildTableWidget(const TArray<TArray<FString>>& Rows) const
{
    TSharedRef<SGridPanel> Grid = SNew(SGridPanel);

    int32 MaxColumns = 0;
    for (const TArray<FString>& Row : Rows)
    {
        MaxColumns = FMath::Max(MaxColumns, Row.Num());
    }

    for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
    {
        const bool bIsHeader = (RowIndex == 0);
        for (int32 ColumnIndex = 0; ColumnIndex < MaxColumns; ++ColumnIndex)
        {
            const FString CellSource = Rows[RowIndex].IsValidIndex(ColumnIndex) ? Rows[RowIndex][ColumnIndex] : FString();
            const FString CellRichText = FAIGatewayMarkdownRichTextRenderer::RenderInlineMarkdown(CellSource, true);
            const FName BrushName = bIsHeader ? TEXT("TableHeaderBubble") : TEXT("TableCellBubble");

            Grid->AddSlot(ColumnIndex, RowIndex)
            .Padding(2.0f)
            [
                SNew(SBorder)
                .BorderImage(FAIGatewayMarkdownRichTextRenderer::GetStyle().GetBrush(BrushName))
                .Padding(FMargin(8.0f, 6.0f))
                [
                    BuildRichTextBlockWidget(CellRichText, TEXT("MarkdownBody"), FMargin(0.0f))
                ]
            ];
        }
    }

    return Grid;
}
