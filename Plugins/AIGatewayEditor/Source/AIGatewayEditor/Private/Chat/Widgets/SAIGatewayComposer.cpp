#include "Chat/Widgets/SAIGatewayComposer.h"

#include "Brushes/SlateDynamicImageBrush.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Visibility.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    constexpr float AttachmentPreviewMaxWidth = 96.0f;
    constexpr float AttachmentPreviewMaxHeight = 72.0f;
    constexpr float AttachmentPreviewAreaMaxHeight = 164.0f;
    constexpr float PromptMinHeight = 140.0f;
    constexpr float AttachmentPreviewHorizontalSpacing = 8.0f;
    constexpr float AttachmentPreviewVerticalSpacing = 8.0f;
    constexpr float AttachmentPreviewCardMinWidth = 72.0f;
    constexpr float AttachmentPreviewScrollBarAllowance = 18.0f;
}

void SAIGatewayComposer::Construct(const FArguments& InArgs)
{
    OnDraftChanged = InArgs._OnDraftChanged;
    OnSendRequested = InArgs._OnSendRequested;
    OnImageAttachRequested = InArgs._OnImageAttachRequested;
    OnImageClearRequested = InArgs._OnImageClearRequested;
    OnImageRemoveRequested = InArgs._OnImageRemoveRequested;

    ChildSlot
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 8.0f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    return FText::FromString(PendingAttachmentSummary.IsEmpty() ? TEXT("Attachment: none") : PendingAttachmentSummary);
                })
                .AutoWrapText(true)
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
            .HAlign(HAlign_Fill)
            [
                SAssignNew(AttachmentPreviewAreaBox, SBox)
                .MaxDesiredHeight(AttachmentPreviewAreaMaxHeight)
                .Visibility_Lambda([this]()
                {
                    return AttachmentThumbnailBrushes.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
                })
                [
                    SNew(SScrollBox)
                    .Orientation(Orient_Vertical)

                    + SScrollBox::Slot()
                    [
                        SAssignNew(AttachmentPreviewContainer, SVerticalBox)
                    ]
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    SNew(SButton)
                    .OnClicked_Lambda([this]()
                    {
                        if (OnImageAttachRequested.IsBound())
                        {
                            OnImageAttachRequested.Execute();
                        }
                        return FReply::Handled();
                    })
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Add Images")))
                    ]
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .IsEnabled_Lambda([this]()
                    {
                        return PendingAttachmentPaths.Num() > 0;
                    })
                    .OnClicked_Lambda([this]()
                    {
                        if (OnImageClearRequested.IsBound())
                        {
                            OnImageClearRequested.Execute();
                        }
                        return FReply::Handled();
                    })
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Clear Images")))
                    ]
                ]
            ]
        ]

        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        [
            SNew(SBox)
            .MinDesiredHeight(PromptMinHeight)
            [
                SAssignNew(PromptTextBox, SMultiLineEditableTextBox)
                .OnTextChanged(this, &SAIGatewayComposer::HandlePromptTextChanged)
                .OnKeyDownHandler(this, &SAIGatewayComposer::HandlePromptKeyDown)
                .HintText(FText::FromString(TEXT("Describe what you want the model to do in the editor...")))
            ]
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Right)
        .Padding(0.0f, 8.0f, 0.0f, 0.0f)
        [
            SAssignNew(SendButton, SButton)
            .IsEnabled_Lambda([this]() { return bCanSend; })
            .OnClicked_Lambda([this]()
            {
                if (OnSendRequested.IsBound())
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

void SAIGatewayComposer::Refresh(const FAIGatewayChatPanelViewState& ViewState)
{
    bCanSend = ViewState.bCanSend;
    PendingAttachmentSummary = ViewState.PendingAttachmentSummary;
    if (PendingAttachmentPaths != ViewState.PendingAttachmentPaths)
    {
        PendingAttachmentPaths = ViewState.PendingAttachmentPaths;
        RebuildAttachmentThumbnails();
    }
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

void SAIGatewayComposer::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

    if (!AttachmentPreviewAreaBox.IsValid() || AttachmentThumbnailBrushes.Num() == 0)
    {
        return;
    }

    const float CurrentLayoutWidth = AttachmentPreviewAreaBox->GetCachedGeometry().GetLocalSize().X;
    if (CurrentLayoutWidth <= 0.0f)
    {
        return;
    }

    if (!FMath::IsNearlyEqual(CurrentLayoutWidth, CachedAttachmentLayoutWidth, 1.0f))
    {
        CachedAttachmentLayoutWidth = CurrentLayoutWidth;
        RefreshAttachmentPreviewWidgets();
    }
}

void SAIGatewayComposer::HandlePromptTextChanged(const FText& InText)
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

FReply SAIGatewayComposer::HandlePromptKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
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

void SAIGatewayComposer::RebuildAttachmentThumbnails()
{
    AttachmentThumbnailBrushes.Reset();
    AttachmentThumbnailDisplaySizes.Reset();
    AttachmentThumbnailSourceIndices.Reset();

    for (int32 AttachmentIndex = 0; AttachmentIndex < PendingAttachmentPaths.Num(); ++AttachmentIndex)
    {
        const FString& PendingAttachmentPath = PendingAttachmentPaths[AttachmentIndex];
        if (PendingAttachmentPath.IsEmpty() || !FPaths::FileExists(PendingAttachmentPath))
        {
            continue;
        }

        FVector2D ThumbnailSize(AttachmentPreviewMaxWidth, AttachmentPreviewMaxHeight);

        TArray<uint8> FileBytes;
        if (FFileHelper::LoadFileToArray(FileBytes, *PendingAttachmentPath))
        {
            IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
            const EImageFormat DetectedFormat = ImageWrapperModule.DetectImageFormat(FileBytes.GetData(), FileBytes.Num());
            if (DetectedFormat != EImageFormat::Invalid)
            {
                const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(DetectedFormat);
                if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(FileBytes.GetData(), FileBytes.Num()))
                {
                    const int32 SourceWidth = ImageWrapper->GetWidth();
                    const int32 SourceHeight = ImageWrapper->GetHeight();
                    if (SourceWidth > 0 && SourceHeight > 0)
                    {
                        const float Scale = FMath::Min(
                            AttachmentPreviewMaxWidth / static_cast<float>(SourceWidth),
                            AttachmentPreviewMaxHeight / static_cast<float>(SourceHeight));
                        const float ClampedScale = FMath::Min(Scale, 1.0f);
                        ThumbnailSize.X = FMath::Max(1.0f, static_cast<float>(SourceWidth) * ClampedScale);
                        ThumbnailSize.Y = FMath::Max(1.0f, static_cast<float>(SourceHeight) * ClampedScale);
                    }
                }
            }
        }

        AttachmentThumbnailDisplaySizes.Add(ThumbnailSize);
        AttachmentThumbnailBrushes.Add(MakeShared<FSlateDynamicImageBrush>(FName(*PendingAttachmentPath), ThumbnailSize));
        AttachmentThumbnailSourceIndices.Add(AttachmentIndex);
    }

    RefreshAttachmentPreviewWidgets();
}

void SAIGatewayComposer::RefreshAttachmentPreviewWidgets()
{
    if (!AttachmentPreviewContainer.IsValid())
    {
        return;
    }

    AttachmentPreviewContainer->ClearChildren();

    const int32 PreviewCount = FMath::Min3(AttachmentThumbnailBrushes.Num(), AttachmentThumbnailDisplaySizes.Num(), AttachmentThumbnailSourceIndices.Num());
    const float AvailableWidth = CachedAttachmentLayoutWidth > 1.0f
        ? FMath::Max(1.0f, CachedAttachmentLayoutWidth - AttachmentPreviewScrollBarAllowance)
        : AttachmentPreviewMaxWidth;

    TSharedPtr<SHorizontalBox> CurrentRow;
    float CurrentRowWidth = 0.0f;
    int32 ItemsInCurrentRow = 0;
    for (int32 PreviewIndex = 0; PreviewIndex < PreviewCount; ++PreviewIndex)
    {
        const float ThumbnailWidth = AttachmentThumbnailDisplaySizes[PreviewIndex].X;
        const float CardWidth = FMath::Max(ThumbnailWidth, AttachmentPreviewCardMinWidth);
        const float RequiredRowWidth = ItemsInCurrentRow == 0
            ? CardWidth
            : CurrentRowWidth + AttachmentPreviewHorizontalSpacing + CardWidth;

        if (!CurrentRow.IsValid() || RequiredRowWidth > AvailableWidth)
        {
            AttachmentPreviewContainer->AddSlot()
            .AutoHeight()
            .Padding(0.0f, PreviewIndex == 0 ? 0.0f : AttachmentPreviewVerticalSpacing, 0.0f, 0.0f)
            [
                SAssignNew(CurrentRow, SHorizontalBox)
            ];

            CurrentRowWidth = 0.0f;
            ItemsInCurrentRow = 0;
        }

        if (!CurrentRow.IsValid())
        {
            continue;
        }

        const int32 SourceIndex = AttachmentThumbnailSourceIndices[PreviewIndex];
        CurrentRow->AddSlot()
        .AutoWidth()
        .Padding(ItemsInCurrentRow == 0 ? 0.0f : AttachmentPreviewHorizontalSpacing, 0.0f, 0.0f, 0.0f)
        [
            SNew(SBox)
            .WidthOverride(CardWidth)
            [
                SNew(SVerticalBox)

                + SVerticalBox::Slot()
                .AutoHeight()
                .HAlign(HAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(AttachmentThumbnailDisplaySizes[PreviewIndex].X)
                    .HeightOverride(AttachmentThumbnailDisplaySizes[PreviewIndex].Y)
                    [
                        SNew(SImage)
                        .Image(AttachmentThumbnailBrushes[PreviewIndex].Get())
                    ]
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .HAlign(HAlign_Fill)
                .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                [
                    SNew(SBox)
                    .WidthOverride(CardWidth)
                    [
                        SNew(SButton)
                        .OnClicked_Lambda([this, SourceIndex]()
                        {
                            if (OnImageRemoveRequested.IsBound())
                            {
                                OnImageRemoveRequested.Execute(SourceIndex);
                            }
                            return FReply::Handled();
                        })
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("Remove")))
                        ]
                    ]
                ]
            ]
        ];

        CurrentRowWidth = ItemsInCurrentRow == 0
            ? CardWidth
            : CurrentRowWidth + AttachmentPreviewHorizontalSpacing + CardWidth;
        ++ItemsInCurrentRow;
    }
}
