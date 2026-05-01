#include "Chat/Widgets/SAIEditorAssistantSessionTabBar.h"

#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SAIEditorAssistantSessionTabBar::Construct(const FArguments& InArgs)
{
    OnNewSessionRequested = InArgs._OnNewSessionRequested;
    OnSessionSelected = InArgs._OnSessionSelected;
    OnSessionClosed = InArgs._OnSessionClosed;

    ChildSlot
    [
        SNew(SHorizontalBox)

        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        [
            SNew(SScrollBox)
            .Orientation(Orient_Horizontal)

            + SScrollBox::Slot()
            [
                SAssignNew(SessionTabsBox, SHorizontalBox)
            ]
        ]

        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(12.0f, 0.0f, 0.0f, 0.0f)
        [
            SNew(SButton)
            .OnClicked_Lambda([this]()
            {
                if (OnNewSessionRequested.IsBound())
                {
                    OnNewSessionRequested.Execute();
                }
                return FReply::Handled();
            })
            .ContentPadding(FMargin(8.0f, 3.0f))
            .ButtonColorAndOpacity(FLinearColor(0.56f, 0.56f, 0.58f, 1.0f))
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("+ New Chat")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            ]
        ]
    ];
}

void SAIEditorAssistantSessionTabBar::Refresh(const FAIEditorAssistantChatPanelViewState& ViewState)
{
    if (!SessionTabsBox.IsValid())
    {
        return;
    }

    SessionTabsBox->ClearChildren();
    for (const FAIEditorAssistantSessionTabViewData& SessionData : ViewState.Sessions)
    {
        SessionTabsBox->AddSlot()
        .AutoWidth()
        .Padding(0.0f, 0.0f, 4.0f, 0.0f)
        [
            BuildSessionTabWidget(SessionData)
        ];
    }
}

TSharedRef<SWidget> SAIEditorAssistantSessionTabBar::BuildSessionTabWidget(const FAIEditorAssistantSessionTabViewData& SessionData)
{
    return SNew(SHorizontalBox)

        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
            SNew(SButton)
            .ButtonStyle(FAppStyle::Get(), "NoBorder")
            .OnClicked_Lambda([this, SessionId = SessionData.SessionId]()
            {
                if (OnSessionSelected.IsBound())
                {
                    OnSessionSelected.Execute(SessionId);
                }
                return FReply::Handled();
            })
            .ContentPadding(FMargin(10.0f, 4.0f, 4.0f, 4.0f))
            [
                SNew(STextBlock)
                .Text(FText::FromString(
                    (SessionData.bIsStreaming ? TEXT("● ") : TEXT("")) +
                    TruncateWithEllipsis(SessionData.Title, 22)))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                .ColorAndOpacity(SessionData.bIsActive
                    ? FSlateColor(FLinearColor(1.0f, 0.54f, 0.08f))
                    : SessionData.bIsStreaming
                        ? FSlateColor(FLinearColor(0.35f, 0.82f, 0.45f))
                        : FSlateColor(FLinearColor(0.42f, 0.42f, 0.46f)))
            ]
        ]

        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
            SNew(SButton)
            .ButtonStyle(FAppStyle::Get(), "NoBorder")
            .OnClicked_Lambda([this, SessionId = SessionData.SessionId]()
            {
                if (OnSessionClosed.IsBound())
                {
                    OnSessionClosed.Execute(SessionId);
                }
                return FReply::Handled();
            })
            .ContentPadding(FMargin(4.0f, 4.0f, 8.0f, 4.0f))
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("\u00D7")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                .ColorAndOpacity(SessionData.bIsActive
                    ? FSlateColor(FLinearColor(0.60f, 0.60f, 0.64f))
                    : FSlateColor(FLinearColor(0.36f, 0.36f, 0.40f)))
            ]
        ];
}

FString SAIEditorAssistantSessionTabBar::TruncateWithEllipsis(const FString& InText, int32 MaxLength)
{
    FString Text = InText.TrimStartAndEnd();
    if (Text.Len() <= MaxLength)
    {
        return Text;
    }

    return Text.Left(MaxLength) + TEXT("...");
}
