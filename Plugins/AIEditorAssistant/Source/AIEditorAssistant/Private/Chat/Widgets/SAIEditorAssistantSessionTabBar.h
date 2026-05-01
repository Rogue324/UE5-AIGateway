#pragma once

#include "Chat/Model/AIEditorAssistantChatTypes.h"
#include "Delegates/Delegate.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE(FOnAIEditorAssistantNewSessionRequested);
DECLARE_DELEGATE_OneParam(FOnAIEditorAssistantSessionSelected, const FString&);
DECLARE_DELEGATE_OneParam(FOnAIEditorAssistantSessionClosed, const FString&);

class SAIEditorAssistantSessionTabBar : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAIEditorAssistantSessionTabBar) {}
        SLATE_EVENT(FOnAIEditorAssistantNewSessionRequested, OnNewSessionRequested)
        SLATE_EVENT(FOnAIEditorAssistantSessionSelected, OnSessionSelected)
        SLATE_EVENT(FOnAIEditorAssistantSessionClosed, OnSessionClosed)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void Refresh(const FAIEditorAssistantChatPanelViewState& ViewState);

private:
    TSharedRef<SWidget> BuildSessionTabWidget(const FAIEditorAssistantSessionTabViewData& SessionData);
    static FString TruncateWithEllipsis(const FString& InText, int32 MaxLength);

    FOnAIEditorAssistantNewSessionRequested OnNewSessionRequested;
    FOnAIEditorAssistantSessionSelected OnSessionSelected;
    FOnAIEditorAssistantSessionClosed OnSessionClosed;
    TSharedPtr<class SHorizontalBox> SessionTabsBox;
};
