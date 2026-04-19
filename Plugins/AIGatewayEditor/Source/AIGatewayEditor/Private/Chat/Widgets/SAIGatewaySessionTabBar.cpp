#include "Chat/Widgets/SAIGatewaySessionTabBar.h"

#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SAIGatewaySessionTabBar::Construct(const FArguments& InArgs)
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
        .Padding(8.0f, 0.0f, 0.0f, 0.0f)
        [
            SNew(SButton)
            .IsEnabled_Lambda([this]() { return bCanEditSessions; })
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
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("+")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("New Chat")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ]
            ]
        ]
    ];
}

void SAIGatewaySessionTabBar::Refresh(const FAIGatewayChatPanelViewState& ViewState)
{
    bCanEditSessions = ViewState.bCanEditSessions;

    if (!SessionTabsBox.IsValid())
    {
        return;
    }

    SessionTabsBox->ClearChildren();
    for (const FAIGatewaySessionTabViewData& SessionData : ViewState.Sessions)
    {
        SessionTabsBox->AddSlot()
        .AutoWidth()
        .Padding(0.0f, 0.0f, 6.0f, 0.0f)
        [
            BuildSessionTabWidget(SessionData, ViewState.bCanEditSessions)
        ];
    }
}

TSharedRef<SWidget> SAIGatewaySessionTabBar::BuildSessionTabWidget(const FAIGatewaySessionTabViewData& SessionData, bool bSessionsEditable)
{
    const FButtonStyle& ButtonStyle = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button");

    return SNew(SBorder)
        .BorderImage(&ButtonStyle.Normal)
        .BorderBackgroundColor(SessionData.bIsActive ? FLinearColor(0.56f, 0.56f, 0.58f, 1.0f) : FLinearColor(0.42f, 0.42f, 0.44f, 1.0f))
        .Padding(FMargin(2.0f, 1.0f))
        [
            SNew(SHorizontalBox)
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
                .IsEnabled(bSessionsEditable)
                .ContentPadding(FMargin(8.0f, 2.0f, 6.0f, 2.0f))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TruncateWithEllipsis(SessionData.Title, 22)))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
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
                .IsEnabled(bSessionsEditable)
                .ContentPadding(FMargin(5.0f, 2.0f, 6.0f, 2.0f))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("x")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ]
            ]
        ];
}

FString SAIGatewaySessionTabBar::TruncateWithEllipsis(const FString& InText, int32 MaxLength)
{
    FString Text = InText.TrimStartAndEnd();
    if (Text.Len() <= MaxLength)
    {
        return Text;
    }

    return Text.Left(MaxLength) + TEXT("...");
}
