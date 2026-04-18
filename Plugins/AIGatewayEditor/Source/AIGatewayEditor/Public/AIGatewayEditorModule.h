#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FAIGatewayEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();
    TSharedRef<class SDockTab> SpawnAIGatewayTab(const class FSpawnTabArgs& SpawnTabArgs);

    static const FName AIGatewayTabName;
};
