#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AIEditorAssistantSettings.generated.h"

UENUM()
enum class EAIEditorAssistantAPIProvider : uint8
{
    // Legacy values (preserved numeric values for migration compatibility).
    OpenAICompatible = 0 UMETA(Hidden),
    DeepSeek = 1 UMETA(DisplayName = "DeepSeek"),
    Anthropic = 2 UMETA(Hidden),
    Gemini = 3 UMETA(Hidden),

    // New business-facing values.
    OpenAI = 4 UMETA(DisplayName = "OpenAI"),
    Custom = 5 UMETA(DisplayName = "Custom (Base URL + API Key)"),
};

UENUM()
enum class EAIEditorAssistantReasoningIntensity : uint8
{
    ProviderDefault UMETA(DisplayName = "Provider Default"),
    Disabled UMETA(DisplayName = "Disabled"),
    Minimal UMETA(DisplayName = "Minimal"),
    Low UMETA(DisplayName = "Low"),
    Medium UMETA(DisplayName = "Medium"),
    High UMETA(DisplayName = "High"),
    Maximum UMETA(DisplayName = "Maximum"),
};

UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig, meta = (DisplayName = "AI Editor Assistant"))
class UAIEditorAssistantSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UAIEditorAssistantSettings();

    virtual FName GetContainerName() const override;
    virtual FName GetCategoryName() const override;

    UPROPERTY(Config, EditAnywhere, Category = "Connection", meta = (DisplayName = "API Provider", ToolTip = "OpenAI and DeepSeek use built-in official endpoints and only require API Key. Use Custom for OpenAI-compatible gateways where you need to specify Base URL manually."))
    EAIEditorAssistantAPIProvider Provider;

    UPROPERTY(Config, EditAnywhere, Category = "Connection", meta = (DisplayName = "API Key"))
    FString ApiKey;

    UPROPERTY(
        Config,
        EditAnywhere,
        Category = "Connection",
        meta = (
            DisplayName = "Base URL",
            EditCondition = "Provider == EAIEditorAssistantAPIProvider::Custom",
            EditConditionHides))
    FString BaseUrl;

    // Managed from the chat panel, persisted in config.
    UPROPERTY(Config)
    FString Model;

    // Managed from the chat panel, persisted in config.
    UPROPERTY(Config)
    EAIEditorAssistantReasoningIntensity ReasoningIntensity;

    UPROPERTY(Config, EditAnywhere, Category = "Chat", meta = (DisplayName = "Max Tool Rounds", ClampMin = "1", UIMin = "1"))
    int32 MaxToolRounds;

    UPROPERTY(Config, EditAnywhere, Category = "Chat", meta = (DisplayName = "Show Tool Activity In Chat"))
    bool bShowToolActivityInChat;
};
