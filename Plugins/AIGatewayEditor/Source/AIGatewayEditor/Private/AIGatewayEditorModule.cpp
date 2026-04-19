#include "AIGatewayEditorModule.h"

#include "LevelEditor.h"
#include "SAIGatewayChatPanel.h"
#include "Tools/AIGatewayToolRuntime.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FAIGatewayEditorModule"

const FName FAIGatewayEditorModule::AIGatewayTabName(TEXT("AIGatewayChatTab"));

void FAIGatewayEditorModule::StartupModule()
{
    FAIGatewayToolRuntime::Get().Startup();

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
    FAIGatewayToolRuntime::Get().Shutdown();

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
        LOCTEXT("OpenAIGatewayTabTooltip", "Open the AI Gateway chat panel."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([]
        {
            FGlobalTabmanager::Get()->TryInvokeTab(FAIGatewayEditorModule::AIGatewayTabName);
        })));

    UToolMenu* PlayToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
    FToolMenuSection& PlayToolbarSection = PlayToolbarMenu->FindOrAddSection("PluginTools");

    PlayToolbarSection.AddEntry(FToolMenuEntry::InitToolBarButton(
        "OpenAIGatewayToolbarButton",
        FUIAction(FExecuteAction::CreateLambda([]
        {
            FGlobalTabmanager::Get()->TryInvokeTab(FAIGatewayEditorModule::AIGatewayTabName);
        })),
        LOCTEXT("OpenAIGatewayToolbarLabel", "AI Gateway"),
        LOCTEXT("OpenAIGatewayToolbarTooltip", "Open the AI Gateway chat panel."),
        FSlateIcon()));
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
