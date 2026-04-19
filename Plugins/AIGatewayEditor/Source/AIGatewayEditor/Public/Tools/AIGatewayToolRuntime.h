#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

enum class EAIGatewayToolConfirmationPolicy : uint8
{
    None,
    ExplicitApproval
};

struct FAIGatewayToolDefinition
{
    FString Name;
    FString Description;
    TSharedPtr<FJsonObject> Parameters;
    EAIGatewayToolConfirmationPolicy ConfirmationPolicy = EAIGatewayToolConfirmationPolicy::None;
};

struct FAIGatewayToolResult
{
    bool bSuccess = false;
    bool bWasRejected = false;
    FString Summary;
    TSharedPtr<FJsonObject> Payload;

    static FAIGatewayToolResult Success(const FString& InSummary, const TSharedPtr<FJsonObject>& InPayload = nullptr);
    static FAIGatewayToolResult Error(const FString& InSummary, const TSharedPtr<FJsonObject>& InPayload = nullptr);
    static FAIGatewayToolResult Rejected(const FString& InSummary);

    FString ToMessageContent() const;
};

class FAIGatewayToolRuntime
{
public:
    static FAIGatewayToolRuntime& Get();

    void Startup();
    void Shutdown();

    const TArray<FAIGatewayToolDefinition>& GetToolDefinitions() const;
    const FAIGatewayToolDefinition* FindDefinition(const FString& ToolName) const;
    FAIGatewayToolResult ExecuteTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments) const;

private:
    void BuildDefinitions();

    TArray<FAIGatewayToolDefinition> Definitions;
    TMap<FString, int32> DefinitionIndexByName;
    bool bStarted = false;
};
