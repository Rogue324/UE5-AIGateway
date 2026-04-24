#include "Chat/Widgets/SAIGatewayChatMessageCard.h"

#include "Chat/Markdown/AIGatewayMarkdownRichTextRenderer.h"
#include "Chat/Widgets/SAIGatewayMarkdownMessageBody.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SRichTextBlock.h"

void SAIGatewayChatMessageCard::Construct(const FArguments& InArgs)
{
    const FAIGatewayChatMessage Message = InArgs._Message;

    FString RoleStyle = TEXT("RoleSystem");
    const FSlateBrush* BubbleBrush = FAIGatewayMarkdownRichTextRenderer::GetStyle().GetBrush("BubbleSystem");
    EHorizontalAlignment BubbleAlignment = HAlign_Left;

    if (Message.Role.Equals(TEXT("You"), ESearchCase::IgnoreCase))
    {
        RoleStyle = TEXT("RoleYou");
        BubbleBrush = FAIGatewayMarkdownRichTextRenderer::GetStyle().GetBrush("BubbleYou");
        BubbleAlignment = HAlign_Right;
    }
    else if (Message.Role.Equals(TEXT("AI"), ESearchCase::IgnoreCase))
    {
        RoleStyle = TEXT("RoleAI");
        BubbleBrush = FAIGatewayMarkdownRichTextRenderer::GetStyle().GetBrush("BubbleAI");
    }
    else if (Message.Role.Equals(TEXT("Tool"), ESearchCase::IgnoreCase))
    {
        RoleStyle = TEXT("RoleTool");
        BubbleBrush = FAIGatewayMarkdownRichTextRenderer::GetStyle().GetBrush("BubbleTool");
    }
    else if (Message.Role.Equals(TEXT("Tool Result"), ESearchCase::IgnoreCase))
    {
        RoleStyle = TEXT("RoleToolResult");
        BubbleBrush = FAIGatewayMarkdownRichTextRenderer::GetStyle().GetBrush("BubbleToolResult");
    }

    ChildSlot
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .HAlign(BubbleAlignment)
        [
            SNew(SBox)
            .MaxDesiredWidth(860.0f)
            [
                SNew(SBorder)
                .BorderImage(BubbleBrush)
                .Padding(FMargin(12.0f, 10.0f))
                .OnMouseButtonUp_Lambda([MessageText = Message.Content](const FGeometry&, const FPointerEvent& MouseEvent)
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
                        SNew(SRichTextBlock)
                        .TextStyle(&FAIGatewayMarkdownRichTextRenderer::GetStyle().GetWidgetStyle<FTextBlockStyle>(*RoleStyle))
                        .DecoratorStyleSet(&FAIGatewayMarkdownRichTextRenderer::GetStyle())
                        .Text(FText::FromString(FString::Printf(TEXT("<%s>%s</>"), *RoleStyle, *FAIGatewayMarkdownRichTextRenderer::EscapeRichText(Message.Role))))
                    ]

                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(SAIGatewayMarkdownMessageBody)
                        .MarkdownText(Message.Content)
                    ]
                ]
            ]
        ]
    ];
}
