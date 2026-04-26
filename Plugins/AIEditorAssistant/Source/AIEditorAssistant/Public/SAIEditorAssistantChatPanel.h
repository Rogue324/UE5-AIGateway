#pragma once

#include "CoreMinimal.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SCompoundWidget.h"

class SAIEditorAssistantChatPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAIEditorAssistantChatPanel) {}
    SLATE_END_ARGS()

    virtual ~SAIEditorAssistantChatPanel() override;

    void Construct(const FArguments& InArgs);

private:
    void RefreshFromController();
    void HandleModelSelectionChanged(TSharedPtr<FString> InSelectedItem, ESelectInfo::Type SelectInfo);
    TSharedRef<SWidget> GenerateModelOptionWidget(TSharedPtr<FString> InItem) const;
    FText GetSelectedModelText() const;
    void HandleReasoningSelectionChanged(TSharedPtr<FString> InSelectedItem, ESelectInfo::Type SelectInfo);
    TSharedRef<SWidget> GenerateReasoningOptionWidget(TSharedPtr<FString> InItem) const;
    FText GetSelectedReasoningText() const;

    TSharedPtr<class FAIEditorAssistantChatController> ChatController;
    TArray<TSharedPtr<FString>> ModelOptions;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> ModelComboBox;
    TSharedPtr<FString> SelectedModelOption;
    TSharedPtr<class SEditableTextBox> ModelManualTextBox;
    TSharedPtr<class STextBlock> ModelListStatusTextBlock;
    TArray<TSharedPtr<FString>> ReasoningOptions;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> ReasoningComboBox;
    TSharedPtr<FString> SelectedReasoningOption;
    TSharedPtr<class STextBlock> ReasoningStatusTextBlock;
    TSharedPtr<class STextBlock> ContextTextBlock;
    TSharedPtr<class STextBlock> StatusTextBlock;
    TSharedPtr<class SAIEditorAssistantSessionTabBar> SessionTabBar;
    TSharedPtr<class SAIEditorAssistantConversationView> ConversationView;
    TSharedPtr<class SAIEditorAssistantToolConfirmationBar> ToolConfirmationBar;
    TSharedPtr<class SAIEditorAssistantComposer> Composer;
};
