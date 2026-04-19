#pragma once

#include "CoreMinimal.h"

enum class EAIGatewayMarkdownBlockType : uint8
{
    Paragraph,
    CodeBlock,
    Table
};

struct FAIGatewayMarkdownBlock
{
    EAIGatewayMarkdownBlockType Type = EAIGatewayMarkdownBlockType::Paragraph;
    FString Text;
    TArray<TArray<FString>> TableRows;
};

class FAIGatewayMarkdownParser
{
public:
    static FString NormalizeLineEndings(const FString& InText);
    static TArray<FAIGatewayMarkdownBlock> ParseBlocks(const FString& MarkdownText);
};
