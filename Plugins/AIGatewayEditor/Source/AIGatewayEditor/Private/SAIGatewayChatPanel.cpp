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
            .Text(LOCTEXT("Title", "AI Gateway 聊天面板"))
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
            .HintText(LOCTEXT("BaseUrlHint", "例如: https://api.openai.com/v1"))
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
            .HintText(LOCTEXT("ApiKeyHint", "输入你的 API Key"))
            .IsPassword(true)
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
            .HintText(LOCTEXT("ModelHint", "例如: gpt-4o-mini"))
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
            .Text(LOCTEXT("ChatHistoryLabel", "对话记录"))
        ]

        + SVerticalBox::Slot()
        .FillHeight(0.65f)
        .Padding(8, 4)
        [
            SAssignNew(ChatHistoryTextBox, SMultiLineEditableTextBox)
            .IsReadOnly(true)
            .HintText(LOCTEXT("HistoryHint", "这里会显示你和大模型的对话。"))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("PromptLabel", "输入消息"))
        ]

        + SVerticalBox::Slot()
        .FillHeight(0.35f)
        .Padding(8, 4)
        [
            SAssignNew(PromptTextBox, SMultiLineEditableTextBox)
            .HintText(LOCTEXT("PromptHint", "请输入要发送给大模型的内容..."))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Right)
        .Padding(8, 8)
        [
            SNew(SButton)
            .Text(LOCTEXT("SendButton", "保存配置并发送"))
            .OnClicked(this, &SAIGatewayChatPanel::HandleSendClicked)
        ]
    ];

    LoadSettingsToUI();
}

FReply SAIGatewayChatPanel::HandleSendClicked()
{
    FString SaveError;
    if (!SaveSettingsFromUI(SaveError))
    {
        AppendMessage(TEXT("System"), SaveError);
        return FReply::Handled();
    }

    if (!PromptTextBox.IsValid())
    {
        return FReply::Handled();
    }

    const FString UserPrompt = PromptTextBox->GetText().ToString().TrimStartAndEnd();
    if (UserPrompt.IsEmpty())
    {
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
    AppendMessage(TEXT("System"), TEXT("正在发送请求..."));

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

    if (!HttpRequest->ProcessRequest())
    {
        AppendMessage(TEXT("System"), TEXT("请求发送失败：无法启动 HTTP 请求。"));
    }

    PromptTextBox->SetText(FText::GetEmpty());

    return FReply::Handled();
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

    if (ModelTextBox.IsValid())
    {
        ModelTextBox->SetText(FText::FromString(Settings->Model));
    }
}

bool SAIGatewayChatPanel::SaveSettingsFromUI(FString& OutError)
{
    if (!BaseUrlTextBox.IsValid() || !ApiKeyTextBox.IsValid() || !ModelTextBox.IsValid())
    {
        OutError = TEXT("配置控件初始化失败。请关闭并重开窗口。");
        return false;
    }

    FString BaseUrl = BaseUrlTextBox->GetText().ToString().TrimStartAndEnd();
    FString ApiKey = ApiKeyTextBox->GetText().ToString().TrimStartAndEnd();
    FString Model = ModelTextBox->GetText().ToString().TrimStartAndEnd();

    if (BaseUrl.IsEmpty())
    {
        OutError = TEXT("Base URL 不能为空。\n示例：https://api.openai.com/v1");
        return false;
    }

    if (ApiKey.IsEmpty())
    {
        OutError = TEXT("API Key 不能为空。\n请填写后再发送。");
        return false;
    }

    if (Model.IsEmpty())
    {
        OutError = TEXT("Model 不能为空。\n示例：gpt-4o-mini");
        return false;
    }

    UAIGatewayEditorSettings* MutableSettings = GetMutableDefault<UAIGatewayEditorSettings>();
    MutableSettings->BaseUrl = BaseUrl;
    MutableSettings->ApiKey = ApiKey;
    MutableSettings->Model = Model;
    MutableSettings->SaveConfig();

    return true;
}

void SAIGatewayChatPanel::HandleChatResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        AppendMessage(TEXT("System"), TEXT("请求失败：未收到有效响应。"));
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    const FString ResponseBody = Response->GetContentAsString();
    if (ResponseCode < 200 || ResponseCode >= 300)
    {
        AppendMessage(TEXT("System"), FString::Printf(TEXT("请求失败（HTTP %d）：%s"), ResponseCode, *ResponseBody));
        return;
    }

    TSharedPtr<FJsonObject> ResponseObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
    if (!FJsonSerializer::Deserialize(Reader, ResponseObject) || !ResponseObject.IsValid())
    {
        AppendMessage(TEXT("System"), FString::Printf(TEXT("响应解析失败：%s"), *ResponseBody));
        return;
    }

    const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
    if (!ResponseObject->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0)
    {
        AppendMessage(TEXT("System"), FString::Printf(TEXT("响应缺少 choices 字段：%s"), *ResponseBody));
        return;
    }

    const TSharedPtr<FJsonObject>* FirstChoiceObject = nullptr;
    if (!(*Choices)[0]->TryGetObject(FirstChoiceObject) || !FirstChoiceObject || !(*FirstChoiceObject).IsValid())
    {
        AppendMessage(TEXT("System"), FString::Printf(TEXT("响应 choices[0] 格式不正确：%s"), *ResponseBody));
        return;
    }

    const TSharedPtr<FJsonObject>* MessageObject = nullptr;
    if (!(*FirstChoiceObject)->TryGetObjectField(TEXT("message"), MessageObject) || !MessageObject || !(*MessageObject).IsValid())
    {
        AppendMessage(TEXT("System"), FString::Printf(TEXT("响应缺少 message 字段：%s"), *ResponseBody));
        return;
    }

    FString AssistantReply;
    if (!(*MessageObject)->TryGetStringField(TEXT("content"), AssistantReply) || AssistantReply.IsEmpty())
    {
        AppendMessage(TEXT("System"), FString::Printf(TEXT("响应缺少 content 字段：%s"), *ResponseBody));
        return;
    }

    AppendMessage(TEXT("AI"), AssistantReply);
}

#undef LOCTEXT_NAMESPACE
