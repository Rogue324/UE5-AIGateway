#include "Chat/Widgets/SAIGatewayMarkdownMessageBody.h"

#include "Chat/Markdown/AIGatewayMarkdownParser.h"
#include "Chat/Markdown/AIGatewayMarkdownRichTextRenderer.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "Framework/Text/TextDecorators.h"
#include "HAL/PlatformProcess.h"
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

    FString RenderTableRichText(const TArray<TArray<FString>>& Rows)
    {
        if (Rows.Num() == 0)
        {
            return TEXT("");
        }

        int32 MaxColumns = 0;
        for (const TArray<FString>& Row : Rows)
        {
            MaxColumns = FMath::Max(MaxColumns, Row.Num());
        }

        TArray<int32> ColumnWidths;
        ColumnWidths.Init(0, MaxColumns);

        auto GetCellText = [](const FString& CellText)
        {
            return FAIGatewayMarkdownRichTextRenderer::NormalizeForDisplay(
                FAIGatewayMarkdownRichTextRenderer::RenderInlineMarkdown(CellText, false));
        };

        for (const TArray<FString>& Row : Rows)
        {
            for (int32 ColumnIndex = 0; ColumnIndex < MaxColumns; ++ColumnIndex)
            {
                const FString CellText = Row.IsValidIndex(ColumnIndex) ? GetCellText(Row[ColumnIndex]) : TEXT("");
                ColumnWidths[ColumnIndex] = FMath::Max(ColumnWidths[ColumnIndex], CellText.Len());
            }
        }

        TArray<FString> RenderedLines;

        auto PadRight = [](const FString& Value, const int32 Width)
        {
            if (Value.Len() >= Width)
            {
                return Value;
            }

            return Value + FString::ChrN(Width - Value.Len(), TEXT(' '));
        };

        auto AppendRow = [&](const TArray<FString>& Row, const bool bIsHeader)
        {
            FString Line = TEXT("| ");
            for (int32 ColumnIndex = 0; ColumnIndex < MaxColumns; ++ColumnIndex)
            {
                FString CellText = Row.IsValidIndex(ColumnIndex) ? GetCellText(Row[ColumnIndex]) : TEXT("");
                CellText = PadRight(CellText, ColumnWidths[ColumnIndex]);
                Line.Append(CellText);
                Line.Append(ColumnIndex + 1 < MaxColumns ? TEXT(" | ") : TEXT(" |"));
            }

            RenderedLines.Add(Line);

            if (bIsHeader)
            {
                FString Separator = TEXT("| ");
                for (int32 ColumnIndex = 0; ColumnIndex < MaxColumns; ++ColumnIndex)
                {
                    Separator.Append(FString::ChrN(FMath::Max(3, ColumnWidths[ColumnIndex]), TEXT('-')));
                    Separator.Append(ColumnIndex + 1 < MaxColumns ? TEXT(" | ") : TEXT(" |"));
                }
                RenderedLines.Add(Separator);
            }
        };

        AppendRow(Rows[0], true);
        for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
        {
            AppendRow(Rows[RowIndex], false);
        }

        return RenderCodeBlockRichText(FString::Join(RenderedLines, TEXT("\n")));
    }
}

void SAIGatewayMarkdownMessageBody::Construct(const FArguments& InArgs)
{
    TSharedRef<FRichTextLayoutMarshaller> Marshaller = FRichTextLayoutMarshaller::Create(TArray<TSharedRef<ITextDecorator>>(), &FAIGatewayMarkdownRichTextRenderer::GetStyle());
    Marshaller->AppendInlineDecorator(FHyperlinkDecorator::Create(TEXT("browser"), FSlateHyperlinkRun::FOnClick::CreateStatic(&HandleBrowserLinkClicked)));
    RichTextMarshaller = Marshaller;

    ChildSlot
    [
        SAssignNew(RichTextWidget, SMultiLineEditableText)
        .Marshaller(Marshaller)
        .Text(FText::GetEmpty())
        .TextStyle(&FAIGatewayMarkdownRichTextRenderer::GetStyle().GetWidgetStyle<FTextBlockStyle>("MarkdownBody"))
        .IsReadOnly(true)
        .AllowContextMenu(true)
        .AutoWrapText(true)
        .Margin(FMargin(0.0f))
        .ClearTextSelectionOnFocusLoss(false)
        .SelectWordOnMouseDoubleClick(true)
    ];

    RebuildContent(InArgs._MarkdownText);
}

void SAIGatewayMarkdownMessageBody::RebuildContent(const FString& InMarkdownText)
{
    if (RichTextWidget.IsValid())
    {
        RichTextWidget->SetText(FText::FromString(BuildRenderableRichText(InMarkdownText)));
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
            Output.Append(RenderTableRichText(Block.TableRows));
            break;

        case EAIGatewayMarkdownBlockType::Paragraph:
        default:
            Output.Append(FAIGatewayMarkdownRichTextRenderer::RenderMarkdownToRichText(Block.Text, true));
            break;
        }
    }

    return Output.IsEmpty() ? TEXT(" ") : Output;
}
