#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AIGatewayEditorSettings.generated.h"

UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig, meta = (DisplayName = "AI Gateway"))
class UAIGatewayEditorSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UAIGatewayEditorSettings();

    virtual FName GetCategoryName() const override;

    UPROPERTY(Config, EditAnywhere, Category = "Connection", meta = (DisplayName = "Base URL"))
    FString BaseUrl;

    UPROPERTY(Config, EditAnywhere, Category = "Connection", meta = (DisplayName = "API Key"))
    FString ApiKey;

    UPROPERTY(Config, EditAnywhere, Category = "Request", meta = (DisplayName = "Chat Endpoint", ToolTip = "会拼接到 Base URL 之后，例如 /chat/completions"))
    FString ChatEndpoint;

    UPROPERTY(Config, EditAnywhere, Category = "Request", meta = (DisplayName = "Model"))
    FString Model;
};
