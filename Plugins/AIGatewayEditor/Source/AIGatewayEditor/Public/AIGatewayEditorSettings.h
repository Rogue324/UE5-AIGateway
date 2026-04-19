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

    virtual FName GetContainerName() const override;
    virtual FName GetCategoryName() const override;

    UPROPERTY(Config, EditAnywhere, Category = "Connection", meta = (DisplayName = "Base URL"))
    FString BaseUrl;

    UPROPERTY(Config, EditAnywhere, Category = "Connection", meta = (DisplayName = "API Key"))
    FString ApiKey;

    UPROPERTY(Config, EditAnywhere, Category = "Request", meta = (DisplayName = "Model"))
    FString Model;

    UPROPERTY(Config, EditAnywhere, Category = "Chat", meta = (DisplayName = "Max Tool Rounds", ClampMin = "1", ClampMax = "64", UIMin = "1", UIMax = "64"))
    int32 MaxToolRounds;

    UPROPERTY(Config, EditAnywhere, Category = "Chat", meta = (DisplayName = "Show Tool Activity In Chat"))
    bool bShowToolActivityInChat;
};
