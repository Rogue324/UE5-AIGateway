#include "Chat/Widgets/SAIGatewayComposer.h"

#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SAIGatewayComposer::Construct(const FArguments& InArgs)
{
    OnDraftChanged = InArgs._OnDraftChanged;
    OnSendRequested = InArgs._OnSendRequested;

    ChildSlot
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        [
            SAssignNew(PromptTextBox, SMultiLineEditableTextBox)
            .OnTextChanged(this, &SAIGatewayComposer::HandlePromptTextChanged)
            .OnKeyDownHandler(this, &SAIGatewayComposer::HandlePromptKeyDown)
            .HintText(FText::FromString(TEXT("Describe what you want the model to do in the editor...")))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Right)
        .Padding(0.0f, 8.0f, 0.0f, 0.0f)
        [
            SAssignNew(SendButton, SButton)
            .IsEnabled_Lambda([this]() { return bCanSend; })
            .OnClicked_Lambda([this]()
            {
                if (OnSendRequested.IsBound())
                {
                    OnSendRequested.Execute();
                }
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    return FText::FromString(SendButtonText.IsEmpty() ? TEXT("Send") : SendButtonText);
                })
            ]
        ]
    ];
}

void SAIGatewayComposer::Refresh(const FAIGatewayChatPanelViewState& ViewState)
{
    bCanSend = ViewState.bCanSend;
    SendButtonText = ViewState.SendButtonText;

    if (PromptTextBox.IsValid())
    {
        const FString CurrentText = PromptTextBox->GetText().ToString();
        if (!CurrentText.Equals(ViewState.DraftPrompt, ESearchCase::CaseSensitive))
        {
            TGuardValue<bool> Guard(bSyncingText, true);
            PromptTextBox->SetText(FText::FromString(ViewState.DraftPrompt));
        }
    }
}

void SAIGatewayComposer::HandlePromptTextChanged(const FText& InText)
{
    if (bSyncingText)
    {
        return;
    }

    if (OnDraftChanged.IsBound())
    {
        OnDraftChanged.Execute(InText.ToString());
    }
}

FReply SAIGatewayComposer::HandlePromptKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Enter && !InKeyEvent.IsShiftDown())
    {
        if (bCanSend && OnSendRequested.IsBound())
        {
            OnSendRequested.Execute();
            return FReply::Handled();
        }
    }

    return FReply::Unhandled();
}
