#pragma once

#include "Chat/Model/AIGatewayChatTypes.h"
#include "Delegates/Delegate.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_OneParam(FOnAIGatewayDraftChanged, const FString&);
DECLARE_DELEGATE(FOnAIGatewaySendRequested);
DECLARE_DELEGATE(FOnAIGatewayImageAttachRequested);
DECLARE_DELEGATE(FOnAIGatewayImageClearRequested);
DECLARE_DELEGATE_OneParam(FOnAIGatewayImageRemoveRequested, int32);

class SAIGatewayComposer : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAIGatewayComposer) {}
        SLATE_EVENT(FOnAIGatewayDraftChanged, OnDraftChanged)
        SLATE_EVENT(FOnAIGatewaySendRequested, OnSendRequested)
        SLATE_EVENT(FOnAIGatewayImageAttachRequested, OnImageAttachRequested)
        SLATE_EVENT(FOnAIGatewayImageClearRequested, OnImageClearRequested)
        SLATE_EVENT(FOnAIGatewayImageRemoveRequested, OnImageRemoveRequested)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void Refresh(const FAIGatewayChatPanelViewState& ViewState);
    virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
    void HandlePromptTextChanged(const FText& InText);
    FReply HandlePromptKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);
    void RebuildAttachmentThumbnails();
    void RefreshAttachmentPreviewWidgets();

    FOnAIGatewayDraftChanged OnDraftChanged;
    FOnAIGatewaySendRequested OnSendRequested;
    FOnAIGatewayImageAttachRequested OnImageAttachRequested;
    FOnAIGatewayImageClearRequested OnImageClearRequested;
    FOnAIGatewayImageRemoveRequested OnImageRemoveRequested;
    TSharedPtr<class SMultiLineEditableTextBox> PromptTextBox;
    TSharedPtr<class SButton> SendButton;
    TSharedPtr<class SBox> AttachmentPreviewAreaBox;
    TSharedPtr<class SVerticalBox> AttachmentPreviewContainer;
    TArray<TSharedPtr<struct FSlateDynamicImageBrush>> AttachmentThumbnailBrushes;
    TArray<FVector2D> AttachmentThumbnailDisplaySizes;
    TArray<int32> AttachmentThumbnailSourceIndices;
    FString PendingAttachmentSummary;
    TArray<FString> PendingAttachmentPaths;
    FString SendButtonText;
    float CachedAttachmentLayoutWidth = 0.0f;
    bool bCanSend = false;
    bool bSyncingText = false;
};
