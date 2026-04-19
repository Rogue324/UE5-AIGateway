#pragma once

#include "Chat/Model/AIGatewayChatTypes.h"
#include "Delegates/Delegate.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE(FOnAIGatewayNewSessionRequested);
DECLARE_DELEGATE_OneParam(FOnAIGatewaySessionSelected, const FString&);
DECLARE_DELEGATE_OneParam(FOnAIGatewaySessionClosed, const FString&);

class SAIGatewaySessionTabBar : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAIGatewaySessionTabBar) {}
        SLATE_EVENT(FOnAIGatewayNewSessionRequested, OnNewSessionRequested)
        SLATE_EVENT(FOnAIGatewaySessionSelected, OnSessionSelected)
        SLATE_EVENT(FOnAIGatewaySessionClosed, OnSessionClosed)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void Refresh(const FAIGatewayChatPanelViewState& ViewState);

private:
    TSharedRef<SWidget> BuildSessionTabWidget(const FAIGatewaySessionTabViewData& SessionData, bool bSessionsEditable);
    static FString TruncateWithEllipsis(const FString& InText, int32 MaxLength);

    FOnAIGatewayNewSessionRequested OnNewSessionRequested;
    FOnAIGatewaySessionSelected OnSessionSelected;
    FOnAIGatewaySessionClosed OnSessionClosed;
    TSharedPtr<class SHorizontalBox> SessionTabsBox;
    bool bCanEditSessions = true;
};
