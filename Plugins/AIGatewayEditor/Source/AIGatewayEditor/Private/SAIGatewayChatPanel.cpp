#include "SAIGatewayChatPanel.h"

#include "Chat/Controller/AIGatewayChatController.h"
#include "Chat/Services/AIGatewayChatService.h"
#include "Chat/Services/AIGatewayChatSessionStore.h"
#include "Chat/Widgets/SAIGatewayComposer.h"
#include "Chat/Widgets/SAIGatewayConversationView.h"
#include "Chat/Widgets/SAIGatewaySessionTabBar.h"
#include "Chat/Widgets/SAIGatewayToolConfirmationBar.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

SAIGatewayChatPanel::~SAIGatewayChatPanel()
{
    if (ChatController.IsValid())
    {
        ChatController->PersistActiveDraft();
    }
}

void SAIGatewayChatPanel::Construct(const FArguments& InArgs)
{
    const TSharedRef<FAIGatewayFileChatSessionStore> SessionStore = MakeShared<FAIGatewayFileChatSessionStore>();
    const TSharedRef<FAIGatewayOpenAIChatService> ChatService = MakeShared<FAIGatewayOpenAIChatService>();

    ChatController = MakeShared<FAIGatewayChatController>(
        StaticCastSharedRef<IAIGatewayChatSessionStore>(SessionStore),
        StaticCastSharedRef<IAIGatewayChatService>(ChatService));

    ChildSlot
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("AI Gateway Chat")))
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
        .Padding(8.0f, 4.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Model")))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f, 4.0f)
        [
            SAssignNew(ModelTextBox, SEditableTextBox)
            .HintText(FText::FromString(TEXT("gpt-4o-mini")))
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
                        SAssignNew(SessionTabBar, SAIGatewaySessionTabBar)
                        .OnNewSessionRequested(FOnAIGatewayNewSessionRequested::CreateLambda([this]()
                        {
                            if (ChatController.IsValid())
                            {
                                ChatController->CreateSession();
                            }
                        }))
                        .OnSessionSelected(FOnAIGatewaySessionSelected::CreateLambda([this](const FString& SessionId)
                        {
                            if (ChatController.IsValid())
                            {
                                ChatController->ActivateSession(SessionId);
                            }
                        }))
                        .OnSessionClosed(FOnAIGatewaySessionClosed::CreateLambda([this](const FString& SessionId)
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
                        SAssignNew(ConversationView, SAIGatewayConversationView)
                    ]
                ]
            ]

            + SSplitter::Slot()
            .Value(0.38f)
            .MinSize(140.0f)
            [
                SAssignNew(Composer, SAIGatewayComposer)
                .OnDraftChanged(FOnAIGatewayDraftChanged::CreateLambda([this](const FString& DraftText)
                {
                    if (ChatController.IsValid())
                    {
                        ChatController->UpdateDraft(DraftText);
                    }
                }))
                .OnSendRequested(FOnAIGatewaySendRequested::CreateLambda([this]()
                {
                    if (ChatController.IsValid())
                    {
                        ChatController->SubmitPrompt();
                    }
                }))
                .OnImageAttachRequested(FOnAIGatewayImageAttachRequested::CreateLambda([this]()
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
                .OnImageClearRequested(FOnAIGatewayImageClearRequested::CreateLambda([this]()
                {
                    if (ChatController.IsValid())
                    {
                        ChatController->ClearPendingImages();
                    }
                }))
                .OnImageRemoveRequested(FOnAIGatewayImageRemoveRequested::CreateLambda([this](const int32 ImageIndex)
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
            SAssignNew(ToolConfirmationBar, SAIGatewayToolConfirmationBar)
            .OnApproved(FOnAIGatewayToolApproved::CreateLambda([this]()
            {
                if (ChatController.IsValid())
                {
                    ChatController->ApprovePendingTool();
                }
            }))
            .OnRejected(FOnAIGatewayToolRejected::CreateLambda([this]()
            {
                if (ChatController.IsValid())
                {
                    ChatController->RejectPendingTool();
                }
            }))
        ]
    ];

    ChatController->OnStateChanged().AddSP(this, &SAIGatewayChatPanel::RefreshFromController);
    ChatController->Initialize();
    RefreshFromController();
}

void SAIGatewayChatPanel::RefreshFromController()
{
    if (!ChatController.IsValid())
    {
        return;
    }

    const FAIGatewayChatPanelViewState ViewState = ChatController->GetViewState();

    if (ModelTextBox.IsValid())
    {
        const FString CurrentText = ModelTextBox->GetText().ToString();
        if (!CurrentText.Equals(ViewState.Model, ESearchCase::CaseSensitive))
        {
            ModelTextBox->SetText(FText::FromString(ViewState.Model));
        }
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
