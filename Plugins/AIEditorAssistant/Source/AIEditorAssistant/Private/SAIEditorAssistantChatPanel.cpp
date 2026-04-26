#include "SAIEditorAssistantChatPanel.h"

#include "Chat/Controller/AIEditorAssistantChatController.h"
#include "Chat/Services/AIEditorAssistantChatService.h"
#include "Chat/Services/AIEditorAssistantChatSessionStore.h"
#include "Chat/Widgets/SAIEditorAssistantComposer.h"
#include "Chat/Widgets/SAIEditorAssistantConversationView.h"
#include "Chat/Widgets/SAIEditorAssistantSessionTabBar.h"
#include "Chat/Widgets/SAIEditorAssistantToolConfirmationBar.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

SAIEditorAssistantChatPanel::~SAIEditorAssistantChatPanel()
{
    if (ChatController.IsValid())
    {
        ChatController->PersistActiveDraft();
    }
}

void SAIEditorAssistantChatPanel::Construct(const FArguments& InArgs)
{
    const TSharedRef<FAIEditorAssistantFileChatSessionStore> SessionStore = MakeShared<FAIEditorAssistantFileChatSessionStore>();
    const TSharedRef<FAIEditorAssistantOpenAIChatService> ChatService = MakeShared<FAIEditorAssistantOpenAIChatService>();

    ChatController = MakeShared<FAIEditorAssistantChatController>(
        StaticCastSharedRef<IAIEditorAssistantChatSessionStore>(SessionStore),
        StaticCastSharedRef<IAIEditorAssistantChatService>(ChatService));

    ChildSlot
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("AI Editor Assistant Chat")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f, 0.0f, 8.0f, 4.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("This panel keeps conversation context and exposes native UE editor tools to the model.")))
            .AutoWrapText(true)
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f, 4.0f, 8.0f, 4.0f)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SVerticalBox)

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Model")))
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                    [
                        SAssignNew(ModelComboBox, SComboBox<TSharedPtr<FString>>)
                        .OptionsSource(&ModelOptions)
                        .OnSelectionChanged(this, &SAIEditorAssistantChatPanel::HandleModelSelectionChanged)
                        .OnGenerateWidget(this, &SAIEditorAssistantChatPanel::GenerateModelOptionWidget)
                        .Content()
                        [
                            SNew(STextBlock)
                            .Text(this, &SAIEditorAssistantChatPanel::GetSelectedModelText)
                        ]
                    ]

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SButton)
                        .OnClicked_Lambda([this]()
                        {
                            if (ChatController.IsValid())
                            {
                                ChatController->RefreshModelOptions();
                            }
                            return FReply::Handled();
                        })
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("Refresh")))
                        ]
                    ]
                ]
            ]

            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(SVerticalBox)

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Reasoning Mode")))
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                    [
                        SAssignNew(ReasoningComboBox, SComboBox<TSharedPtr<FString>>)
                        .OptionsSource(&ReasoningOptions)
                        .OnSelectionChanged(this, &SAIEditorAssistantChatPanel::HandleReasoningSelectionChanged)
                        .OnGenerateWidget(this, &SAIEditorAssistantChatPanel::GenerateReasoningOptionWidget)
                        .Content()
                        [
                            SNew(STextBlock)
                            .Text(this, &SAIEditorAssistantChatPanel::GetSelectedReasoningText)
                        ]
                    ]

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SButton)
                        .OnClicked_Lambda([this]()
                        {
                            if (ChatController.IsValid())
                            {
                                ChatController->RefreshReasoningOptions();
                            }
                            return FReply::Handled();
                        })
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("Refresh")))
                        ]
                    ]
                ]
            ]
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f, 0.0f, 8.0f, 4.0f)
        [
            SAssignNew(ModelManualTextBox, SEditableTextBox)
            .HintText(FText::FromString(TEXT("Type model name when list is unavailable")))
            .Visibility(EVisibility::Collapsed)
            .OnTextChanged_Lambda([this](const FText& InText)
            {
                if (ChatController.IsValid())
                {
                    ChatController->SetModel(InText.ToString());
                }
            })
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f, 0.0f, 8.0f, 4.0f)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SAssignNew(ModelListStatusTextBlock, STextBlock)
                .AutoWrapText(true)
            ]

            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SAssignNew(ReasoningStatusTextBlock, STextBlock)
                .AutoWrapText(true)
            ]
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f, 8.0f)
        [
            SNew(SSeparator)
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f, 0.0f, 8.0f, 4.0f)
        [
            SAssignNew(ContextTextBlock, STextBlock)
            .AutoWrapText(true)
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Chat History")))
        ]

        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        .Padding(8.0f, 4.0f, 8.0f, 4.0f)
        [
            SNew(SSplitter)
            .Orientation(Orient_Vertical)

            + SSplitter::Slot()
            .Value(0.62f)
            .MinSize(180.0f)
            [
                SNew(SBorder)
                .Padding(8.0f)
                [
                    SNew(SVerticalBox)

                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                    [
                        SAssignNew(SessionTabBar, SAIEditorAssistantSessionTabBar)
                        .OnNewSessionRequested(FOnAIEditorAssistantNewSessionRequested::CreateLambda([this]()
                        {
                            if (ChatController.IsValid())
                            {
                                ChatController->CreateSession();
                            }
                        }))
                        .OnSessionSelected(FOnAIEditorAssistantSessionSelected::CreateLambda([this](const FString& SessionId)
                        {
                            if (ChatController.IsValid())
                            {
                                ChatController->ActivateSession(SessionId);
                            }
                        }))
                        .OnSessionClosed(FOnAIEditorAssistantSessionClosed::CreateLambda([this](const FString& SessionId)
                        {
                            if (ChatController.IsValid())
                            {
                                ChatController->CloseSession(SessionId);
                            }
                        }))
                    ]

                    + SVerticalBox::Slot()
                    .FillHeight(1.0f)
                    [
                        SAssignNew(ConversationView, SAIEditorAssistantConversationView)
                    ]
                ]
            ]

            + SSplitter::Slot()
            .Value(0.38f)
            .MinSize(140.0f)
            [
                SAssignNew(Composer, SAIEditorAssistantComposer)
                .OnDraftChanged(FOnAIEditorAssistantDraftChanged::CreateLambda([this](const FString& DraftText)
                {
                    if (ChatController.IsValid())
                    {
                        ChatController->UpdateDraft(DraftText);
                    }
                }))
                .OnSendRequested(FOnAIEditorAssistantSendRequested::CreateLambda([this]()
                {
                    if (ChatController.IsValid())
                    {
                        ChatController->SubmitPrompt();
                    }
                }))
                .OnCancelRequested(FOnAIEditorAssistantCancelRequested::CreateLambda([this]()
                {
                    if (ChatController.IsValid())
                    {
                        ChatController->CancelCurrentWork();
                    }
                }))
                .OnImageAttachRequested(FOnAIEditorAssistantImageAttachRequested::CreateLambda([this]()
                {
                    if (!ChatController.IsValid())
                    {
                        return;
                    }

                    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
                    if (DesktopPlatform == nullptr)
                    {
                        return;
                    }

                    TArray<FString> SelectedFiles;
                    const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
                    const bool bDidChooseFile = DesktopPlatform->OpenFileDialog(
                        const_cast<void*>(ParentWindowHandle),
                        TEXT("Choose Images"),
                        FPaths::ProjectDir(),
                        TEXT(""),
                        TEXT("Image Files|*.png;*.jpg;*.jpeg;*.webp;*.gif;*.bmp"),
                        EFileDialogFlags::Multiple,
                        SelectedFiles);

                    if (bDidChooseFile && SelectedFiles.Num() > 0)
                    {
                        ChatController->AddPendingImagePaths(SelectedFiles);
                    }
                }))
                .OnImageClearRequested(FOnAIEditorAssistantImageClearRequested::CreateLambda([this]()
                {
                    if (ChatController.IsValid())
                    {
                        ChatController->ClearPendingImages();
                    }
                }))
                .OnImageRemoveRequested(FOnAIEditorAssistantImageRemoveRequested::CreateLambda([this](const int32 ImageIndex)
                {
                    if (ChatController.IsValid())
                    {
                        ChatController->RemovePendingImageAt(ImageIndex);
                    }
                }))
            ]
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f, 4.0f)
        [
            SAssignNew(StatusTextBlock, STextBlock)
            .AutoWrapText(true)
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f, 4.0f)
        [
            SAssignNew(ToolConfirmationBar, SAIEditorAssistantToolConfirmationBar)
            .OnApproved(FOnAIEditorAssistantToolApproved::CreateLambda([this]()
            {
                if (ChatController.IsValid())
                {
                    ChatController->ApprovePendingTool();
                }
            }))
            .OnRejected(FOnAIEditorAssistantToolRejected::CreateLambda([this]()
            {
                if (ChatController.IsValid())
                {
                    ChatController->RejectPendingTool();
                }
            }))
        ]
    ];

    ChatController->OnStateChanged().AddSP(this, &SAIEditorAssistantChatPanel::RefreshFromController);
    ChatController->Initialize();
    RefreshFromController();
}

void SAIEditorAssistantChatPanel::RefreshFromController()
{
    if (!ChatController.IsValid())
    {
        return;
    }

    const FAIEditorAssistantChatPanelViewState ViewState = ChatController->GetViewState();

    ModelOptions.Reset();
    ModelOptions.Reserve(ViewState.ModelOptions.Num());
    SelectedModelOption.Reset();
    for (const FString& ModelOption : ViewState.ModelOptions)
    {
        TSharedPtr<FString> Item = MakeShared<FString>(ModelOption);
        if (ModelOption.Equals(ViewState.Model, ESearchCase::CaseSensitive))
        {
            SelectedModelOption = Item;
        }
        ModelOptions.Add(Item);
    }

    if (!SelectedModelOption.IsValid() && !ViewState.Model.IsEmpty())
    {
        SelectedModelOption = MakeShared<FString>(ViewState.Model);
        ModelOptions.AddUnique(SelectedModelOption);
    }

    if (ModelComboBox.IsValid())
    {
        ModelComboBox->SetEnabled(!ViewState.bIsModelListLoading && ModelOptions.Num() > 0);
        ModelComboBox->RefreshOptions();
        if (SelectedModelOption.IsValid())
        {
            const TSharedPtr<FString> CurrentSelected = ModelComboBox->GetSelectedItem();
            const bool bNeedsSelectionUpdate =
                !CurrentSelected.IsValid() ||
                !CurrentSelected->Equals(*SelectedModelOption, ESearchCase::CaseSensitive);
            if (bNeedsSelectionUpdate)
            {
                ModelComboBox->SetSelectedItem(SelectedModelOption);
            }
        }
        else
        {
            ModelComboBox->ClearSelection();
        }
    }

    if (ModelManualTextBox.IsValid())
    {
        ModelManualTextBox->SetVisibility(ViewState.bAllowManualModelInput ? EVisibility::Visible : EVisibility::Collapsed);
        ModelManualTextBox->SetEnabled(!ViewState.bIsModelListLoading);

        const FString CurrentText = ModelManualTextBox->GetText().ToString();
        if (!CurrentText.Equals(ViewState.Model, ESearchCase::CaseSensitive))
        {
            ModelManualTextBox->SetText(FText::FromString(ViewState.Model));
        }
    }

    if (ModelListStatusTextBlock.IsValid())
    {
        ModelListStatusTextBlock->SetText(FText::FromString(ViewState.ModelListStatus));
    }

    ReasoningOptions.Reset();
    ReasoningOptions.Reserve(ViewState.ReasoningModeOptions.Num());
    SelectedReasoningOption.Reset();
    for (const FString& Option : ViewState.ReasoningModeOptions)
    {
        TSharedPtr<FString> Item = MakeShared<FString>(Option);
        if (Option.Equals(ViewState.ReasoningMode, ESearchCase::CaseSensitive))
        {
            SelectedReasoningOption = Item;
        }
        ReasoningOptions.Add(Item);
    }

    if (!SelectedReasoningOption.IsValid() && !ViewState.ReasoningMode.IsEmpty())
    {
        SelectedReasoningOption = MakeShared<FString>(ViewState.ReasoningMode);
        ReasoningOptions.AddUnique(SelectedReasoningOption);
    }

    if (ReasoningComboBox.IsValid())
    {
        ReasoningComboBox->SetEnabled(!ViewState.bIsReasoningOptionsLoading && ReasoningOptions.Num() > 0);
        ReasoningComboBox->RefreshOptions();
        if (SelectedReasoningOption.IsValid())
        {
            const TSharedPtr<FString> CurrentSelected = ReasoningComboBox->GetSelectedItem();
            const bool bNeedsSelectionUpdate =
                !CurrentSelected.IsValid() ||
                !CurrentSelected->Equals(*SelectedReasoningOption, ESearchCase::CaseSensitive);
            if (bNeedsSelectionUpdate)
            {
                ReasoningComboBox->SetSelectedItem(SelectedReasoningOption);
            }
        }
        else
        {
            ReasoningComboBox->ClearSelection();
        }
    }

    if (ReasoningStatusTextBlock.IsValid())
    {
        ReasoningStatusTextBlock->SetText(FText::FromString(ViewState.ReasoningOptionsStatus));
    }

    if (StatusTextBlock.IsValid())
    {
        StatusTextBlock->SetText(FText::FromString(ViewState.StatusMessage));
    }

    if (ContextTextBlock.IsValid())
    {
        ContextTextBlock->SetText(FText::FromString(ViewState.ContextSummary));
    }

    if (SessionTabBar.IsValid())
    {
        SessionTabBar->Refresh(ViewState);
    }

    if (ConversationView.IsValid())
    {
        ConversationView->Refresh(ViewState);
    }

    if (Composer.IsValid())
    {
        Composer->Refresh(ViewState);
    }

    if (ToolConfirmationBar.IsValid())
    {
        ToolConfirmationBar->Refresh(ViewState);
    }
}

void SAIEditorAssistantChatPanel::HandleModelSelectionChanged(TSharedPtr<FString> InSelectedItem, ESelectInfo::Type SelectInfo)
{
    if (SelectInfo == ESelectInfo::Direct)
    {
        return;
    }

    if (!ChatController.IsValid() || !InSelectedItem.IsValid())
    {
        return;
    }

    SelectedModelOption = InSelectedItem;
    ChatController->SetModel(*InSelectedItem);
}

TSharedRef<SWidget> SAIEditorAssistantChatPanel::GenerateModelOptionWidget(TSharedPtr<FString> InItem) const
{
    return SNew(STextBlock)
        .Text(FText::FromString(InItem.IsValid() ? *InItem : FString()));
}

FText SAIEditorAssistantChatPanel::GetSelectedModelText() const
{
    return FText::FromString(SelectedModelOption.IsValid() ? *SelectedModelOption : TEXT("Select model"));
}

void SAIEditorAssistantChatPanel::HandleReasoningSelectionChanged(TSharedPtr<FString> InSelectedItem, ESelectInfo::Type SelectInfo)
{
    if (SelectInfo == ESelectInfo::Direct)
    {
        return;
    }

    if (!ChatController.IsValid() || !InSelectedItem.IsValid())
    {
        return;
    }

    SelectedReasoningOption = InSelectedItem;
    ChatController->SetReasoningMode(*InSelectedItem);
}

TSharedRef<SWidget> SAIEditorAssistantChatPanel::GenerateReasoningOptionWidget(TSharedPtr<FString> InItem) const
{
    return SNew(STextBlock)
        .Text(FText::FromString(InItem.IsValid() ? *InItem : FString()));
}

FText SAIEditorAssistantChatPanel::GetSelectedReasoningText() const
{
    return FText::FromString(SelectedReasoningOption.IsValid() ? *SelectedReasoningOption : TEXT("Select reasoning mode"));
}
