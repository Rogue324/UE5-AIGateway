#include "Chat/Widgets/SAIGatewayToolConfirmationBar.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SAIGatewayToolConfirmationBar::Construct(const FArguments& InArgs)
{
    OnApproved = InArgs._OnApproved;
    OnRejected = InArgs._OnRejected;

    ChildSlot
    [
        SNew(SHorizontalBox)
        .Visibility_Lambda([this]() { return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed; })

        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .VAlign(VAlign_Center)
        .Padding(0.0f, 0.0f, 8.0f, 0.0f)
        [
            SAssignNew(PromptTextBlock, STextBlock)
            .AutoWrapText(true)
        ]

        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(0.0f, 0.0f, 8.0f, 0.0f)
        [
            SNew(SButton)
            .Text(FText::FromString(TEXT("Approve")))
            .OnClicked_Lambda([this]()
            {
                if (OnApproved.IsBound())
                {
                    OnApproved.Execute();
                }
                return FReply::Handled();
            })
        ]

        + SHorizontalBox::Slot()
        .AutoWidth()
        [
            SNew(SButton)
            .Text(FText::FromString(TEXT("Reject")))
            .OnClicked_Lambda([this]()
            {
                if (OnRejected.IsBound())
                {
                    OnRejected.Execute();
                }
                return FReply::Handled();
            })
        ]
    ];
}

void SAIGatewayToolConfirmationBar::Refresh(const FAIGatewayChatPanelViewState& ViewState)
{
    bIsVisible = ViewState.ToolConfirmation.bIsVisible;

    if (PromptTextBlock.IsValid())
    {
        PromptTextBlock->SetText(FText::FromString(ViewState.ToolConfirmation.Prompt));
    }
}
