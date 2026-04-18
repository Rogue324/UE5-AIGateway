#include "AIGatewayEditorSettings.h"

UAIGatewayEditorSettings::UAIGatewayEditorSettings()
{
    BaseUrl = TEXT("https://api.openai.com/v1");
    ApiKey = TEXT("");
    ChatEndpoint = TEXT("/chat/completions");
    Model = TEXT("gpt-4o-mini");
}

FName UAIGatewayEditorSettings::GetCategoryName() const
{
    return FName(TEXT("Plugins"));
}
