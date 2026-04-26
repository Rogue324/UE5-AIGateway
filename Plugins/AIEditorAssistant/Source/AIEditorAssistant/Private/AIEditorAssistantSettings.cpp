#include "AIEditorAssistantSettings.h"

UAIEditorAssistantSettings::UAIEditorAssistantSettings()
{
    BaseUrl = TEXT("https://api.openai.com/v1");
    ApiKey = TEXT("");
    Provider = EAIEditorAssistantAPIProvider::OpenAI;
    Model = TEXT("gpt-4o-mini");
    ReasoningIntensity = EAIEditorAssistantReasoningIntensity::Medium;
    MaxToolRounds = 500;
    bShowToolActivityInChat = false;
}

FName UAIEditorAssistantSettings::GetContainerName() const
{
    return FName(TEXT("Project"));
}

FName UAIEditorAssistantSettings::GetCategoryName() const
{
    return FName(TEXT("Plugins"));
}
