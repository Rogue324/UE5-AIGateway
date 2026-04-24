#include "AIGatewayEditorSettings.h"

UAIGatewayEditorSettings::UAIGatewayEditorSettings()
{
    BaseUrl = TEXT("https://api.openai.com/v1");
    ApiKey = TEXT("");
    Provider = EAIGatewayAPIProvider::OpenAICompatible;
    Model = TEXT("gpt-4o-mini");
    ReasoningIntensity = EAIGatewayReasoningIntensity::Medium;
    MaxToolRounds = 8;
    bShowToolActivityInChat = false;
}

FName UAIGatewayEditorSettings::GetContainerName() const
{
    return FName(TEXT("Project"));
}

FName UAIGatewayEditorSettings::GetCategoryName() const
{
    return FName(TEXT("Plugins"));
}
