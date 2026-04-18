#include "SAIGatewayChatPanel.h"

#include "AIGatewayEditorSettings.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SAIGatewayChatPanel"

void SAIGatewayChatPanel::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("Title", "AI Gateway Chat"))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 0)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("BaseUrlLabel", "Base URL"))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4)
        [
            SAssignNew(BaseUrlTextBox, SEditableTextBox)
            .HintText(LOCTEXT("BaseUrlHint", "https://api.openai.com/v1"))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("ApiKeyLabel", "API Key"))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4)
        [
            SAssignNew(ApiKeyTextBox, SEditableTextBox)
            .HintText(LOCTEXT("ApiKeyHint", "Enter your API key"))
            .IsPassword(true)
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("ChatEndpointLabel", "Chat Endpoint"))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4)
        [
            SAssignNew(ChatEndpointTextBox, SEditableTextBox)
            .HintText(LOCTEXT("ChatEndpointHint", "/chat/completions"))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("ModelLabel", "Model"))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4)
        [
            SAssignNew(ModelTextBox, SEditableTextBox)
            .HintText(LOCTEXT("ModelHint", "gpt-4o-mini"))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 8)
        [
            SNew(SSeparator)
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 0)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("ChatHistoryLabel", "Chat History"))
        ]

        + SVerticalBox::Slot()
        .FillHeight(0.6f)
        .Padding(8, 4)
        [
            SAssignNew(ChatHistoryTextBox, SMultiLineEditableTextBox)
            .IsReadOnly(true)
            .HintText(LOCTEXT("HistoryHint", "Messages from you and the model will appear here."))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("PromptLabel", "Prompt"))
        ]

        + SVerticalBox::Slot()
        .FillHeight(0.4f)
        .Padding(8, 4)
        [
            SAssignNew(PromptTextBox, SMultiLineEditableTextBox)
            .HintText(LOCTEXT("PromptHint", "Type the message you want to send..."))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4)
        [
            SAssignNew(StatusTextBlock, STextBlock)
            .Text(LOCTEXT("ReadyStatus", "Ready."))
            .AutoWrapText(true)
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Right)
        .Padding(8, 8)
        [
            SNew(SButton)
            .Text(this, &SAIGatewayChatPanel::GetSendButtonText)
            .IsEnabled(this, &SAIGatewayChatPanel::CanSendRequest)
            .OnClicked(this, &SAIGatewayChatPanel::HandleSendClicked)
        ]
    ];

    LoadSettingsToUI();
    SetStatusMessage(LOCTEXT("InitialStatus", "Ready."));
}

FReply SAIGatewayChatPanel::HandleSendClicked()
{
    if (bIsSending)
    {
        return FReply::Handled();
    }

    FString SaveError;
    if (!SaveSettingsFromUI(SaveError))
    {
        SetStatusMessage(FText::FromString(SaveError));
        AppendMessage(TEXT("System"), SaveError);
        return FReply::Handled();
    }

    if (!PromptTextBox.IsValid())
    {
        const FString ErrorMessage = TEXT("Prompt input is not available. Please reopen the panel.");
        SetStatusMessage(FText::FromString(ErrorMessage));
        AppendMessage(TEXT("System"), ErrorMessage);
        return FReply::Handled();
    }

    const FString UserPrompt = PromptTextBox->GetText().ToString().TrimStartAndEnd();
    if (UserPrompt.IsEmpty())
    {
        const FString ErrorMessage = TEXT("Enter a prompt before sending.");
        SetStatusMessage(FText::FromString(ErrorMessage));
        AppendMessage(TEXT("System"), ErrorMessage);
        return FReply::Handled();
    }

    const UAIGatewayEditorSettings* Settings = GetDefault<UAIGatewayEditorSettings>();

    FString NormalizedBaseUrl = Settings->BaseUrl;
    NormalizedBaseUrl.RemoveFromEnd(TEXT("/"));

    FString NormalizedEndpoint = Settings->ChatEndpoint;
    if (!NormalizedEndpoint.StartsWith(TEXT("/")))
    {
        NormalizedEndpoint = TEXT("/") + NormalizedEndpoint;
    }

    const FString RequestUrl = NormalizedBaseUrl + NormalizedEndpoint;

    AppendMessage(TEXT("You"), UserPrompt);
    SetStatusMessage(LOCTEXT("SendingStatus", "Sending request..."));

    TSharedPtr<FJsonObject> RequestBodyObject = MakeShared<FJsonObject>();
    RequestBodyObject->SetStringField(TEXT("model"), Settings->Model);

    TArray<TSharedPtr<FJsonValue>> Messages;
    TSharedPtr<FJsonObject> UserMessageObject = MakeShared<FJsonObject>();
    UserMessageObject->SetStringField(TEXT("role"), TEXT("user"));
    UserMessageObject->SetStringField(TEXT("content"), UserPrompt);
    Messages.Add(MakeShared<FJsonValueObject>(UserMessageObject));
    RequestBodyObject->SetArrayField(TEXT("messages"), Messages);

    FString RequestBody;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
    FJsonSerializer::Serialize(RequestBodyObject.ToSharedRef(), Writer);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(RequestUrl);
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Settings->ApiKey));
    HttpRequest->SetContentAsString(RequestBody);
    HttpRequest->OnProcessRequestComplete().BindSP(this, &SAIGatewayChatPanel::HandleChatResponse);

    bIsSending = true;

    if (!HttpRequest->ProcessRequest())
    {
        bIsSending = false;

        const FString ErrorMessage = TEXT("Failed to start the HTTP request.");
        SetStatusMessage(FText::FromString(ErrorMessage));
        AppendMessage(TEXT("System"), ErrorMessage);
        return FReply::Handled();
    }

    PromptTextBox->SetText(FText::GetEmpty());

    return FReply::Handled();
}

bool SAIGatewayChatPanel::CanSendRequest() const
{
    return !bIsSending;
}

void SAIGatewayChatPanel::AppendMessage(const FString& Role, const FString& Text)
{
    if (!ChatHistoryTextBox.IsValid())
    {
        return;
    }

    FString ExistingText = ChatHistoryTextBox->GetText().ToString();
    if (!ExistingText.IsEmpty())
    {
        ExistingText.Append(TEXT("\n\n"));
    }

    ExistingText.Append(FString::Printf(TEXT("[%s] %s"), *Role, *Text));
    ChatHistoryTextBox->SetText(FText::FromString(ExistingText));
}

void SAIGatewayChatPanel::LoadSettingsToUI()
{
    const UAIGatewayEditorSettings* Settings = GetDefault<UAIGatewayEditorSettings>();

    if (BaseUrlTextBox.IsValid())
    {
        BaseUrlTextBox->SetText(FText::FromString(Settings->BaseUrl));
    }

    if (ApiKeyTextBox.IsValid())
    {
        ApiKeyTextBox->SetText(FText::FromString(Settings->ApiKey));
    }

    if (ChatEndpointTextBox.IsValid())
    {
        ChatEndpointTextBox->SetText(FText::FromString(Settings->ChatEndpoint));
    }

    if (ModelTextBox.IsValid())
    {
        ModelTextBox->SetText(FText::FromString(Settings->Model));
    }
}

bool SAIGatewayChatPanel::SaveSettingsFromUI(FString& OutError)
{
    if (!BaseUrlTextBox.IsValid() || !ApiKeyTextBox.IsValid() || !ChatEndpointTextBox.IsValid() || !ModelTextBox.IsValid())
    {
        OutError = TEXT("Configuration controls are unavailable. Please reopen the panel.");
        return false;
    }

    const FString BaseUrl = BaseUrlTextBox->GetText().ToString().TrimStartAndEnd();
    const FString ApiKey = ApiKeyTextBox->GetText().ToString().TrimStartAndEnd();
    const FString ChatEndpoint = ChatEndpointTextBox->GetText().ToString().TrimStartAndEnd();
    const FString Model = ModelTextBox->GetText().ToString().TrimStartAndEnd();

    if (BaseUrl.IsEmpty())
    {
        OutError = TEXT("Base URL is required. Example: https://api.openai.com/v1");
        return false;
    }

    if (!BaseUrl.StartsWith(TEXT("http://")) && !BaseUrl.StartsWith(TEXT("https://")))
    {
        OutError = TEXT("Base URL must start with http:// or https://");
        return false;
    }

    if (ApiKey.IsEmpty())
    {
        OutError = TEXT("API Key is required.");
        return false;
    }

    if (ChatEndpoint.IsEmpty())
    {
        OutError = TEXT("Chat Endpoint is required. Example: /chat/completions");
        return false;
    }

    if (Model.IsEmpty())
    {
        OutError = TEXT("Model is required. Example: gpt-4o-mini");
        return false;
    }

    UAIGatewayEditorSettings* MutableSettings = GetMutableDefault<UAIGatewayEditorSettings>();
    MutableSettings->BaseUrl = BaseUrl;
    MutableSettings->ApiKey = ApiKey;
    MutableSettings->ChatEndpoint = ChatEndpoint;
    MutableSettings->Model = Model;
    MutableSettings->SaveConfig();

    return true;
}

void SAIGatewayChatPanel::HandleChatResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    bIsSending = false;

    if (!bWasSuccessful || !Response.IsValid())
    {
        const FString ErrorMessage = TEXT("Request failed before a valid response was received. Check the URL, key, and network connection.");
        SetStatusMessage(FText::FromString(ErrorMessage));
        AppendMessage(TEXT("System"), ErrorMessage);
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    const FString ResponseBody = Response->GetContentAsString();
    if (ResponseCode < 200 || ResponseCode >= 300)
    {
        FString ErrorMessage = FString::Printf(TEXT("Request failed with HTTP %d."), ResponseCode);
        const FString ServiceMessage = ExtractErrorMessage(ResponseBody);
        if (!ServiceMessage.IsEmpty())
        {
            ErrorMessage = FString::Printf(TEXT("%s %s"), *ErrorMessage, *ServiceMessage);
        }

        SetStatusMessage(FText::FromString(ErrorMessage));
        AppendMessage(TEXT("System"), ErrorMessage);
        return;
    }

    TSharedPtr<FJsonObject> ResponseObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
    if (!FJsonSerializer::Deserialize(Reader, ResponseObject) || !ResponseObject.IsValid())
    {
        const FString ErrorMessage = TEXT("The response body was not valid JSON.");
        SetStatusMessage(FText::FromString(ErrorMessage));
        AppendMessage(TEXT("System"), ErrorMessage);
        return;
    }

    const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
    if (!ResponseObject->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0)
    {
        const FString ErrorMessage = TEXT("The response did not contain choices[0].");
        SetStatusMessage(FText::FromString(ErrorMessage));
        AppendMessage(TEXT("System"), ErrorMessage);
        return;
    }

    const TSharedPtr<FJsonObject>* FirstChoiceObject = nullptr;
    if (!(*Choices)[0]->TryGetObject(FirstChoiceObject) || !FirstChoiceObject || !(*FirstChoiceObject).IsValid())
    {
        const FString ErrorMessage = TEXT("choices[0] was not an object.");
        SetStatusMessage(FText::FromString(ErrorMessage));
        AppendMessage(TEXT("System"), ErrorMessage);
        return;
    }

    const TSharedPtr<FJsonObject>* MessageObject = nullptr;
    if (!(*FirstChoiceObject)->TryGetObjectField(TEXT("message"), MessageObject) || !MessageObject || !(*MessageObject).IsValid())
    {
        const FString ErrorMessage = TEXT("The response did not contain message.content.");
        SetStatusMessage(FText::FromString(ErrorMessage));
        AppendMessage(TEXT("System"), ErrorMessage);
        return;
    }

    FString AssistantReply;
    if (!(*MessageObject)->TryGetStringField(TEXT("content"), AssistantReply) || AssistantReply.IsEmpty())
    {
        const FString ErrorMessage = TEXT("message.content was empty.");
        SetStatusMessage(FText::FromString(ErrorMessage));
        AppendMessage(TEXT("System"), ErrorMessage);
        return;
    }

    SetStatusMessage(LOCTEXT("SuccessStatus", "Response received."));
    AppendMessage(TEXT("AI"), AssistantReply);
}

FString SAIGatewayChatPanel::ExtractErrorMessage(const FString& ResponseBody) const
{
    TSharedPtr<FJsonObject> ResponseObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
    if (!FJsonSerializer::Deserialize(Reader, ResponseObject) || !ResponseObject.IsValid())
    {
        return FString();
    }

    const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
    if (ResponseObject->TryGetObjectField(TEXT("error"), ErrorObject) && ErrorObject && (*ErrorObject).IsValid())
    {
        FString ErrorMessage;
        if ((*ErrorObject)->TryGetStringField(TEXT("message"), ErrorMessage))
        {
            return ErrorMessage;
        }
    }

    FString Message;
    if (ResponseObject->TryGetStringField(TEXT("message"), Message))
    {
        return Message;
    }

    return FString();
}

void SAIGatewayChatPanel::SetStatusMessage(const FText& InMessage)
{
    if (StatusTextBlock.IsValid())
    {
        StatusTextBlock->SetText(InMessage);
    }
}

FText SAIGatewayChatPanel::GetSendButtonText() const
{
    return bIsSending
        ? LOCTEXT("SendButtonSending", "Sending...")
        : LOCTEXT("SendButtonReady", "Save Configuration and Send");
}

#undef LOCTEXT_NAMESPACE
