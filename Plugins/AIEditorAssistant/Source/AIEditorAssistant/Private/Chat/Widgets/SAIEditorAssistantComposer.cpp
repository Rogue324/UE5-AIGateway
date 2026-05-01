#include "Chat/Widgets/SAIEditorAssistantComposer.h"

#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SAIEditorAssistantComposer::Construct(const FArguments& InArgs)
{
    OnDraftChanged = InArgs._OnDraftChanged;
    OnSendRequested = InArgs._OnSendRequested;
    OnCancelRequested = InArgs._OnCancelRequested;

    ChildSlot
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        [
            SAssignNew(PromptTextBox, SMultiLineEditableTextBox)
            .OnTextChanged(this, &SAIEditorAssistantComposer::HandlePromptTextChanged)
            .OnKeyDownHandler(this, &SAIEditorAssistantComposer::HandlePromptKeyDown)
            .HintText(FText::GetEmpty())
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Right)
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            SAssignNew(SendButton, SButton)
            .IsEnabled_Lambda([this]() { return bCanSend || bCanCancel; })
            .OnClicked_Lambda([this]()
            {
                if (bCanCancel)
                {
                    if (OnCancelRequested.IsBound())
                    {
                        OnCancelRequested.Execute();
                    }
                }
                else if (OnSendRequested.IsBound())
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

void SAIEditorAssistantComposer::Refresh(const FAIEditorAssistantChatPanelViewState& ViewState)
{
    bCanSend = ViewState.bCanSend;
    bCanCancel = ViewState.bCanCancel;
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

void SAIEditorAssistantComposer::HandlePromptTextChanged(const FText& InText)
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

FReply SAIEditorAssistantComposer::HandlePromptKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
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
