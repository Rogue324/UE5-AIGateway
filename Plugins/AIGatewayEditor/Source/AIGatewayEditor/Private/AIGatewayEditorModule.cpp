#include "AIGatewayEditorModule.h"

#include "LevelEditor.h"
#include "SAIGatewayChatPanel.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FAIGatewayEditorModule"

const FName FAIGatewayEditorModule::AIGatewayTabName(TEXT("AIGatewayChatTab"));

void FAIGatewayEditorModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        AIGatewayTabName,
        FOnSpawnTab::CreateRaw(this, &FAIGatewayEditorModule::SpawnAIGatewayTab))
        .SetDisplayName(LOCTEXT("TabTitle", "AI Gateway"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAIGatewayEditorModule::RegisterMenus));
}

void FAIGatewayEditorModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);

    if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
    {
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AIGatewayTabName);
    }
}

void FAIGatewayEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
    FToolMenuSection& Section = WindowMenu->FindOrAddSection("WindowLayout");

    Section.AddMenuEntry(
        "OpenAIGatewayTab",
        LOCTEXT("OpenAIGatewayTabLabel", "AI Gateway"),
        LOCTEXT("OpenAIGatewayTabTooltip", "打开 AI Gateway 聊天窗口"),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([]
        {
            FGlobalTabmanager::Get()->TryInvokeTab(FAIGatewayEditorModule::AIGatewayTabName);
        })));
}

TSharedRef<SDockTab> FAIGatewayEditorModule::SpawnAIGatewayTab(const FSpawnTabArgs& SpawnTabArgs)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SAIGatewayChatPanel)
        ];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAIGatewayEditorModule, AIGatewayEditor)
