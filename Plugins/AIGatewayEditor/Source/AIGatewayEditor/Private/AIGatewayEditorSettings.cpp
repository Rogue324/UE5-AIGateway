#include "AIGatewayEditorSettings.h"

UAIGatewayEditorSettings::UAIGatewayEditorSettings()
{
    BaseUrl = TEXT("https://api.openai.com/v1");
    ApiKey = TEXT("");
    Model = TEXT("gpt-4o-mini");
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
