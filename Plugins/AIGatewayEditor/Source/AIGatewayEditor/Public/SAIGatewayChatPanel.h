#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Widgets/SCompoundWidget.h"

class SAIGatewayChatPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAIGatewayChatPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    FReply HandleSendClicked();
    void AppendMessage(const FString& Role, const FString& Text);
    void LoadSettingsToUI();
    bool SaveSettingsFromUI(FString& OutError);
    void HandleChatResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

    TSharedPtr<class SEditableTextBox> BaseUrlTextBox;
    TSharedPtr<class SEditableTextBox> ApiKeyTextBox;
    TSharedPtr<class SEditableTextBox> ModelTextBox;
    TSharedPtr<class SMultiLineEditableTextBox> ChatHistoryTextBox;
    TSharedPtr<class SMultiLineEditableTextBox> PromptTextBox;
};
