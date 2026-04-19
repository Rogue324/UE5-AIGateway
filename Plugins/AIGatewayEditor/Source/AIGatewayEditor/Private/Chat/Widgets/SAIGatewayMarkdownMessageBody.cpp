#include "Chat/Widgets/SAIGatewayMarkdownMessageBody.h"

#include "Chat/Markdown/AIGatewayMarkdownParser.h"
#include "Chat/Markdown/AIGatewayMarkdownRichTextRenderer.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "HAL/PlatformProcess.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/SRichTextBlock.h"

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

    FString StripMarkdownInlineSyntax(const FString& InText)
    {
        FString Result = InText;
        Result.ReplaceInline(TEXT("**"), TEXT(""));
        Result.ReplaceInline(TEXT("__"), TEXT(""));
        Result.ReplaceInline(TEXT("`"), TEXT(""));
        Result.ReplaceInline(TEXT("</>"), TEXT(""));
        Result.ReplaceInline(TEXT("<MarkdownBold>"), TEXT(""));
        Result.ReplaceInline(TEXT("<MarkdownHeading>"), TEXT(""));

        for (int32 Index = 0; Index < Result.Len();)
        {
            if (Result[Index] == TEXT('['))
            {
                const int32 CloseBracketIndex = Result.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1);
                const int32 OpenParenIndex = CloseBracketIndex != INDEX_NONE ? Result.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, CloseBracketIndex + 1) : INDEX_NONE;
                const int32 CloseParenIndex = OpenParenIndex != INDEX_NONE ? Result.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, OpenParenIndex + 1) : INDEX_NONE;
                if (CloseBracketIndex != INDEX_NONE && OpenParenIndex == CloseBracketIndex + 1 && CloseParenIndex != INDEX_NONE)
                {
                    const FString LinkText = Result.Mid(Index + 1, CloseBracketIndex - Index - 1);
                    Result = Result.Left(Index) + LinkText + Result.Mid(CloseParenIndex + 1);
                    Index += LinkText.Len();
                    continue;
                }
            }

            ++Index;
        }

        return Result;
    }

    FString BuildPlainTextSelectionContent(const TArray<FAIGatewayMarkdownBlock>& Blocks)
    {
        FString PlainText;

        for (int32 Index = 0; Index < Blocks.Num(); ++Index)
        {
            const FAIGatewayMarkdownBlock& Block = Blocks[Index];
            if (!PlainText.IsEmpty())
            {
                PlainText.Append(TEXT("\n\n"));
            }

            if (Block.Type == EAIGatewayMarkdownBlockType::CodeBlock)
            {
                PlainText.Append(Block.Text);
                continue;
            }

            if (Block.Type == EAIGatewayMarkdownBlockType::Table)
            {
                for (int32 RowIndex = 0; RowIndex < Block.TableRows.Num(); ++RowIndex)
                {
                    if (RowIndex > 0)
                    {
                        PlainText.Append(TEXT("\n"));
                    }

                    const TArray<FString>& Row = Block.TableRows[RowIndex];
                    for (int32 ColumnIndex = 0; ColumnIndex < Row.Num(); ++ColumnIndex)
                    {
                        if (ColumnIndex > 0)
                        {
                            PlainText.Append(TEXT("\t"));
                        }
                        PlainText.Append(StripMarkdownInlineSyntax(Row[ColumnIndex]));
                    }
                }
                continue;
            }

            FString ParagraphText = Block.Text;
            const FString Trimmed = ParagraphText.TrimStartAndEnd();
            if (Trimmed.StartsWith(TEXT("### ")))
            {
                ParagraphText = Trimmed.RightChop(4);
            }
            else if (Trimmed.StartsWith(TEXT("## ")))
            {
                ParagraphText = Trimmed.RightChop(3);
            }
            else if (Trimmed.StartsWith(TEXT("# ")))
            {
                ParagraphText = Trimmed.RightChop(2);
            }

            PlainText.Append(StripMarkdownInlineSyntax(ParagraphText));
        }

        return PlainText;
    }

    TSharedRef<SRichTextBlock> CreateDisplayRichTextBlock(
        const FString& RichText,
        const FName& TextStyleName)
    {
        return SNew(SRichTextBlock)
            .TextStyle(&FAIGatewayMarkdownRichTextRenderer::GetStyle().GetWidgetStyle<FTextBlockStyle>(TextStyleName))
            .DecoratorStyleSet(&FAIGatewayMarkdownRichTextRenderer::GetStyle())
            .Text(FText::FromString(RichText))
            .AutoWrapText(true)
            + SRichTextBlock::HyperlinkDecorator(TEXT("browser"), FSlateHyperlinkRun::FOnClick::CreateStatic(&HandleBrowserLinkClicked));
    }

    const FTextBlockStyle& GetTransparentSelectionTextStyle()
    {
        static FTextBlockStyle Style = []()
        {
            FTextBlockStyle NewStyle = FAIGatewayMarkdownRichTextRenderer::GetStyle().GetWidgetStyle<FTextBlockStyle>("MarkdownBody");
            NewStyle.SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.02f)));
            return NewStyle;
        }();

        return Style;
    }
}

void SAIGatewayMarkdownMessageBody::Construct(const FArguments& InArgs)
{
    const TArray<FAIGatewayMarkdownBlock> Blocks = FAIGatewayMarkdownParser::ParseBlocks(InArgs._MarkdownText);
    TSharedRef<SVerticalBox> ContentBox = SNew(SVerticalBox);
    FString CombinedRichText;
    const FString PlainTextContent = BuildPlainTextSelectionContent(Blocks);

    auto FlushCombinedRichText = [&](const bool bAddBottomPadding)
    {
        if (CombinedRichText.IsEmpty())
        {
            return;
        }

        ContentBox->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, bAddBottomPadding ? 6.0f : 0.0f)
        [
            CreateDisplayRichTextBlock(CombinedRichText, TEXT("MarkdownBody"))
        ];

        CombinedRichText.Empty();
    };

    if (Blocks.Num() == 0)
    {
        ContentBox->AddSlot()
        .AutoHeight()
        [
            CreateDisplayRichTextBlock(TEXT("<MarkdownBody> </>"), TEXT("MarkdownBody"))
        ];

        ChildSlot
        [
            SNew(SOverlay)
            + SOverlay::Slot()
            [
                ContentBox
            ]
            + SOverlay::Slot()
            [
                SNew(SMultiLineEditableText)
                .Text(FText::FromString(TEXT(" ")))
                .TextStyle(&GetTransparentSelectionTextStyle())
                .IsReadOnly(true)
                .AllowContextMenu(true)
                .AutoWrapText(true)
                .Margin(FMargin(0.0f))
                .ClearTextSelectionOnFocusLoss(false)
                .SelectWordOnMouseDoubleClick(true)
            ]
        ];
        return;
    }

    for (int32 Index = 0; Index < Blocks.Num(); ++Index)
    {
        const FAIGatewayMarkdownBlock& Block = Blocks[Index];
        const bool bAddBottomPadding = Index < Blocks.Num() - 1;
        FString HeadingContent;

        if (Block.Type == EAIGatewayMarkdownBlockType::CodeBlock)
        {
            FlushCombinedRichText(true);

            ContentBox->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, bAddBottomPadding ? 8.0f : 0.0f)
            [
                SNew(SBorder)
                .BorderImage(FAIGatewayMarkdownRichTextRenderer::GetStyle().GetBrush("CodeBlockBubble"))
                .Padding(FMargin(10.0f, 8.0f))
                [
                    SNew(SMultiLineEditableText)
                    .Text(FText::FromString(Block.Text))
                    .TextStyle(&FAIGatewayMarkdownRichTextRenderer::GetStyle().GetWidgetStyle<FTextBlockStyle>("MarkdownCodeBlock"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
                    .IsReadOnly(true)
                    .AllowContextMenu(true)
                    .AutoWrapText(true)
                    .Margin(FMargin(0.0f))
                ]
            ];
            continue;
        }

        if (Block.Type == EAIGatewayMarkdownBlockType::Table)
        {
            FlushCombinedRichText(true);

            TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
            for (int32 RowIndex = 0; RowIndex < Block.TableRows.Num(); ++RowIndex)
            {
                const TArray<FString>& Row = Block.TableRows[RowIndex];
                for (int32 ColumnIndex = 0; ColumnIndex < Row.Num(); ++ColumnIndex)
                {
                    const bool bIsHeader = RowIndex == 0;
                    Grid->AddSlot(ColumnIndex, RowIndex)
                    .Padding(1.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(FAIGatewayMarkdownRichTextRenderer::GetStyle().GetBrush(bIsHeader ? "TableHeaderBubble" : "TableCellBubble"))
                        .Padding(FMargin(8.0f, 6.0f))
                        [
                            CreateDisplayRichTextBlock(
                                FAIGatewayMarkdownRichTextRenderer::RenderMarkdownToRichText(Row[ColumnIndex], true),
                                bIsHeader ? TEXT("MarkdownBold") : TEXT("MarkdownBody"))
                        ]
                    ];
                }
            }

            ContentBox->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, bAddBottomPadding ? 8.0f : 0.0f)
            [
                Grid
            ];
            continue;
        }

        if (FAIGatewayMarkdownRichTextRenderer::TryExtractHeadingContent(Block.Text, HeadingContent))
        {
            if (!CombinedRichText.IsEmpty())
            {
                CombinedRichText.Append(TEXT("\n\n"));
            }
            CombinedRichText.Append(FString::Printf(TEXT("<MarkdownHeading>%s</>"), *HeadingContent));
            continue;
        }

        if (!CombinedRichText.IsEmpty())
        {
            CombinedRichText.Append(TEXT("\n\n"));
        }
        CombinedRichText.Append(FAIGatewayMarkdownRichTextRenderer::RenderMarkdownToRichText(Block.Text, true));
    }

    FlushCombinedRichText(false);

    ChildSlot
    [
        SNew(SOverlay)
        + SOverlay::Slot()
        [
            SNew(SBox)
            .Visibility(EVisibility::HitTestInvisible)
            [
                ContentBox
            ]
        ]
        + SOverlay::Slot()
        [
            SNew(SMultiLineEditableText)
            .Text(FText::FromString(PlainTextContent.IsEmpty() ? TEXT(" ") : PlainTextContent))
            .TextStyle(&GetTransparentSelectionTextStyle())
            .IsReadOnly(true)
            .AllowContextMenu(true)
            .AutoWrapText(true)
            .Margin(FMargin(0.0f))
            .ClearTextSelectionOnFocusLoss(false)
            .SelectWordOnMouseDoubleClick(true)
        ]
    ];
}
