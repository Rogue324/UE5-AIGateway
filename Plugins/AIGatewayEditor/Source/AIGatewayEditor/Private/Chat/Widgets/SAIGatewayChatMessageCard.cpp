#include "Chat/Widgets/SAIGatewayChatMessageCard.h"

#include "Chat/Markdown/AIGatewayMarkdownRichTextRenderer.h"
#include "Chat/Widgets/SAIGatewayMarkdownMessageBody.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/SRichTextBlock.h"

namespace
{
    void ResolveMessageStyle(
        const FAIGatewayChatMessage& Message,
        FString& OutRoleStyle,
        const FSlateBrush*& OutBubbleBrush,
        EHorizontalAlignment& OutBubbleAlignment)
    {
        OutRoleStyle = TEXT("RoleSystem");
        OutBubbleBrush = FAIGatewayMarkdownRichTextRenderer::GetStyle().GetBrush("BubbleSystem");
        OutBubbleAlignment = HAlign_Left;

        if (Message.Role.Equals(TEXT("You"), ESearchCase::IgnoreCase))
        {
            OutRoleStyle = TEXT("RoleYou");
            OutBubbleBrush = FAIGatewayMarkdownRichTextRenderer::GetStyle().GetBrush("BubbleYou");
            OutBubbleAlignment = HAlign_Right;
        }
        else if (Message.Role.Equals(TEXT("AI"), ESearchCase::IgnoreCase))
        {
            OutRoleStyle = TEXT("RoleAI");
            OutBubbleBrush = FAIGatewayMarkdownRichTextRenderer::GetStyle().GetBrush("BubbleAI");
        }
        else if (Message.Role.Equals(TEXT("Tool"), ESearchCase::IgnoreCase))
        {
            OutRoleStyle = TEXT("RoleTool");
            OutBubbleBrush = FAIGatewayMarkdownRichTextRenderer::GetStyle().GetBrush("BubbleTool");
        }
        else if (Message.Role.Equals(TEXT("Tool Result"), ESearchCase::IgnoreCase))
        {
            OutRoleStyle = TEXT("RoleToolResult");
            OutBubbleBrush = FAIGatewayMarkdownRichTextRenderer::GetStyle().GetBrush("BubbleToolResult");
        }
    }
}

void SAIGatewayChatMessageCard::Construct(const FArguments& InArgs)
{
    CachedMessage = InArgs._Message;
    bRenderMarkdown = InArgs._RenderMarkdown;

    ChildSlot
    [
        SAssignNew(RootRow, SHorizontalBox)
    ];

    RebuildForMessage(CachedMessage, bRenderMarkdown);
}

void SAIGatewayChatMessageCard::Refresh(const FAIGatewayChatMessage& InMessage, bool bInRenderMarkdown)
{
    const bool bRoleChanged = !CachedMessage.Role.Equals(InMessage.Role, ESearchCase::CaseSensitive);
    const bool bContentChanged = !CachedMessage.Content.Equals(InMessage.Content, ESearchCase::CaseSensitive);
    const bool bRenderModeChanged = (bRenderMarkdown != bInRenderMarkdown);
    if (!bRoleChanged && !bContentChanged && !bRenderModeChanged)
    {
        return;
    }

    CachedMessage = InMessage;
    bRenderMarkdown = bInRenderMarkdown;

    if (bRoleChanged || !RoleTextBlock.IsValid() || !BubbleBorder.IsValid() || !RootRow.IsValid())
    {
        RebuildForMessage(CachedMessage, bRenderMarkdown);
        return;
    }

    if (bRenderModeChanged)
    {
        RebuildForMessage(CachedMessage, bRenderMarkdown);
        return;
    }

    if (bRenderMarkdown && MessageBody.IsValid())
    {
        MessageBody->RefreshMarkdown(CachedMessage.Content);
    }
    else if (!bRenderMarkdown && PlainTextBody.IsValid())
    {
        PlainTextBody->SetText(FText::FromString(CachedMessage.Content));
    }
}

void SAIGatewayChatMessageCard::RebuildForMessage(const FAIGatewayChatMessage& InMessage, bool bInRenderMarkdown)
{
    if (!RootRow.IsValid())
    {
        return;
    }

    FString RoleStyle;
    const FSlateBrush* BubbleBrush = nullptr;
    EHorizontalAlignment BubbleAlignment = HAlign_Left;
    ResolveMessageStyle(InMessage, RoleStyle, BubbleBrush, BubbleAlignment);
    const bool bUseStreamingPlainTextWidth =
        !bRenderMarkdown && InMessage.Role.Equals(TEXT("AI"), ESearchCase::IgnoreCase);
    bRenderMarkdown = bInRenderMarkdown;
    MessageBody.Reset();
    PlainTextBody.Reset();

    RootRow->ClearChildren();
    RootRow->AddSlot()
    .FillWidth(1.0f)
    .HAlign(BubbleAlignment)
    [
        SAssignNew(BubbleWidthBox, SBox)
        .MinDesiredWidth(bUseStreamingPlainTextWidth ? 420.0f : 0.0f)
        .MaxDesiredWidth(860.0f)
        [
            SAssignNew(BubbleBorder, SBorder)
            .BorderImage(BubbleBrush)
            .Padding(FMargin(12.0f, 10.0f))
            .OnMouseButtonUp_Lambda([MessageText = InMessage.Content](const FGeometry&, const FPointerEvent& MouseEvent)
            {
                if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
                {
                    FPlatformApplicationMisc::ClipboardCopy(*MessageText);
                    return FReply::Handled();
                }

                return FReply::Unhandled();
            })
            [
                SNew(SVerticalBox)

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                [
                    SAssignNew(RoleTextBlock, SRichTextBlock)
                    .TextStyle(&FAIGatewayMarkdownRichTextRenderer::GetStyle().GetWidgetStyle<FTextBlockStyle>(*RoleStyle))
                    .DecoratorStyleSet(&FAIGatewayMarkdownRichTextRenderer::GetStyle())
                    .Text(FText::FromString(FString::Printf(TEXT("<%s>%s</>"), *RoleStyle, *FAIGatewayMarkdownRichTextRenderer::EscapeRichText(InMessage.Role))))
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    bRenderMarkdown
                        ? StaticCastSharedRef<SWidget>(
                            SAssignNew(MessageBody, SAIGatewayMarkdownMessageBody)
                            .MarkdownText(InMessage.Content))
                        : StaticCastSharedRef<SWidget>(
                            SAssignNew(PlainTextBody, SMultiLineEditableText)
                            .Text(FText::FromString(InMessage.Content))
                            .TextStyle(&FAIGatewayMarkdownRichTextRenderer::GetStyle().GetWidgetStyle<FTextBlockStyle>("MarkdownBody"))
                            .IsReadOnly(true)
                            .AllowContextMenu(true)
                            .AutoWrapText(true)
                            .Margin(FMargin(0.0f))
                            .ClearTextSelectionOnFocusLoss(false)
                            .SelectWordOnMouseDoubleClick(true))
                ]
            ]
        ]
    ];
}
