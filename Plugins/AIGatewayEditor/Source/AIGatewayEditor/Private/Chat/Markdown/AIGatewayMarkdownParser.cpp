#include "Chat/Markdown/AIGatewayMarkdownParser.h"

namespace
{
    bool IsMarkdownHeadingLine(const FString& TrimmedLine)
    {
        return TrimmedLine.StartsWith(TEXT("# ")) ||
            TrimmedLine.StartsWith(TEXT("## ")) ||
            TrimmedLine.StartsWith(TEXT("### "));
    }

    bool IsMarkdownTableLine(const FString& TrimmedLine)
    {
        return TrimmedLine.StartsWith(TEXT("|")) && TrimmedLine.EndsWith(TEXT("|")) && TrimmedLine.Contains(TEXT("|"));
    }

    bool IsMarkdownTableSeparatorLine(const FString& TrimmedLine)
    {
        if (!IsMarkdownTableLine(TrimmedLine))
        {
            return false;
        }

        for (int32 Index = 0; Index < TrimmedLine.Len(); ++Index)
        {
            const TCHAR Character = TrimmedLine[Index];
            if (Character != TEXT('|') && Character != TEXT('-') && Character != TEXT(':') && Character != TEXT(' '))
            {
                return false;
            }
        }

        return true;
    }

    TArray<FString> SplitMarkdownTableRow(const FString& TrimmedLine)
    {
        FString WorkingLine = TrimmedLine;
        WorkingLine.RemoveFromStart(TEXT("|"));
        WorkingLine.RemoveFromEnd(TEXT("|"));

        TArray<FString> Cells;
        WorkingLine.ParseIntoArray(Cells, TEXT("|"), false);
        for (FString& Cell : Cells)
        {
            Cell = Cell.TrimStartAndEnd();
        }
        return Cells;
    }
}

FString FAIGatewayMarkdownParser::NormalizeLineEndings(const FString& InText)
{
    FString Normalized = InText;
    Normalized.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
    Normalized.ReplaceInline(TEXT("\r"), TEXT("\n"));
    return Normalized;
}

TArray<FAIGatewayMarkdownBlock> FAIGatewayMarkdownParser::ParseBlocks(const FString& MarkdownText)
{
    const FString Normalized = NormalizeLineEndings(MarkdownText);
    TArray<FString> Lines;
    Normalized.ParseIntoArray(Lines, TEXT("\n"), false);

    TArray<FAIGatewayMarkdownBlock> Blocks;
    FString ParagraphBuffer;
    bool bInCodeBlock = false;
    FString CodeBlockBuffer;

    auto FlushParagraph = [&]()
    {
        if (ParagraphBuffer.IsEmpty())
        {
            return;
        }

        FAIGatewayMarkdownBlock& Block = Blocks.AddDefaulted_GetRef();
        Block.Type = EAIGatewayMarkdownBlockType::Paragraph;
        Block.Text = ParagraphBuffer;
        ParagraphBuffer.Empty();
    };

    auto FlushCodeBlock = [&]()
    {
        FAIGatewayMarkdownBlock& Block = Blocks.AddDefaulted_GetRef();
        Block.Type = EAIGatewayMarkdownBlockType::CodeBlock;
        Block.Text = CodeBlockBuffer;
        CodeBlockBuffer.Empty();
    };

    for (int32 Index = 0; Index < Lines.Num(); ++Index)
    {
        const FString& Line = Lines[Index];
        const FString TrimmedLine = Line.TrimStartAndEnd();

        if (TrimmedLine.StartsWith(TEXT("```")))
        {
            FlushParagraph();
            if (bInCodeBlock)
            {
                FlushCodeBlock();
            }

            bInCodeBlock = !bInCodeBlock;
            continue;
        }

        if (bInCodeBlock)
        {
            if (!CodeBlockBuffer.IsEmpty())
            {
                CodeBlockBuffer.Append(TEXT("\n"));
            }
            CodeBlockBuffer.Append(Line);
            continue;
        }

        if (IsMarkdownHeadingLine(TrimmedLine))
        {
            FlushParagraph();

            FAIGatewayMarkdownBlock& Block = Blocks.AddDefaulted_GetRef();
            Block.Type = EAIGatewayMarkdownBlockType::Paragraph;
            Block.Text = Line;
            continue;
        }

        if (IsMarkdownTableLine(TrimmedLine))
        {
            const bool bHasSeparator = (Index + 1) < Lines.Num() && IsMarkdownTableSeparatorLine(Lines[Index + 1].TrimStartAndEnd());
            if (bHasSeparator)
            {
                FlushParagraph();

                FAIGatewayMarkdownBlock& Block = Blocks.AddDefaulted_GetRef();
                Block.Type = EAIGatewayMarkdownBlockType::Table;
                Block.TableRows.Add(SplitMarkdownTableRow(TrimmedLine));

                Index += 2;
                while (Index < Lines.Num() && IsMarkdownTableLine(Lines[Index].TrimStartAndEnd()))
                {
                    Block.TableRows.Add(SplitMarkdownTableRow(Lines[Index].TrimStartAndEnd()));
                    ++Index;
                }

                --Index;
                continue;
            }
        }

        if (TrimmedLine.IsEmpty())
        {
            FlushParagraph();
            continue;
        }

        if (!ParagraphBuffer.IsEmpty())
        {
            ParagraphBuffer.Append(TEXT("\n"));
        }
        ParagraphBuffer.Append(Line);
    }

    if (bInCodeBlock)
    {
        FlushCodeBlock();
    }
    else
    {
        FlushParagraph();
    }

    return Blocks;
}
