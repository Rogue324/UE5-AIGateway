# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 仓库概述

这是一个仅包含 UE5 编辑器插件的仓库（无 .uproject 文件）。插件名称为 **AI Editor Assistant** (v0.1.0 beta)，将其放入现有 UE5 项目的 `Plugins/` 目录即可构建和运行。

## 构建

此仓库没有独立的构建脚本或 CI，构建依赖 Unreal Build Tool (UBT)。

```
# 将插件复制到现有 UE5 项目后，通过 UBT 编译：
<UE5Root>/Engine/Build/BatchFiles/RunUAT.bat BuildEditor -project=<YourProject>.uproject -platform=Win64 -configuration=Development

# 或直接在 VS 中打开项目的 .sln 编译对应配置
```

生成 VS 项目文件后，在 IDE 中编译 `AIEditorAssistant`、`SoftUEBridge`、`SoftUEBridgeEditor` 三个模块即可。

## 模块架构

插件包含三个模块，加载阶段均为 `Default`，类型均为 `Editor`：

```
AIEditorAssistant (顶层：聊天 UI + 控制器)
    └── 依赖 SoftUEBridge
            ├── 依赖 Core, Engine, HTTPServer, AIModule, ImageWrapper 等
            └── 被 SoftUEBridgeEditor 依赖
                    └── 依赖 Kismet, BlueprintGraph, StateTree, PythonScriptPlugin,
                        RewindDebugger, AssetTools, UMGEditor 等编辑器子系统
```

- **`SoftUEBridge`**：运行时桥层。包含 JSON-RPC HTTP 服务器（`FBridgeServer`）、工具注册表（`FBridgeToolRegistry`）、工具基类（`UBridgeToolBase`）以及约 14 个核心工具。通过 `PublicIncludePaths.Add(Private)` 将 Private 目录暴露给外部模块。
- **`SoftUEBridgeEditor`**：编辑器工具层。包含约 50+ 个编辑器专用工具（蓝图操作、资产管理、PIE 控制、StateTree、Rewind 调试器、材质编辑、Python 脚本等）。模块启动时通过 `REGISTER_BRIDGE_TOOL` 宏将工具注册到 `FBridgeToolRegistry`。
- **`AIEditorAssistant`**：聊天 UI 和控制器层。包含 Slate 面板、聊天控制器、多提供商 HTTP 聊天服务、会话持久化、Markdown 渲染和工具运行时封装。启动时通过 `FModuleManager::LoadModuleChecked` 显式加载 `SoftUEBridgeEditor`。

**关键点**：`AIEditorAssistant` 不直接依赖 `SoftUEBridgeEditor`，而是在运行时动态加载它以触发工具注册。

## 核心架构模式

### 聊天系统：M-V-C 架构

聊天面板 (`SAIEditorAssistantChatPanel`) 是 View，`FAIEditorAssistantChatController` 是 Controller，`Chat/Model/` 中的类型是 Model。

- Controller 通过 `OnStateChanged` 事件广播状态变更，View 通过 `RefreshFromController()` 拉取不可变的 `FAIEditorAssistantChatPanelViewState` 快照。
- 状态管理采用**不可变快照 + 事件驱动**模式，避免 Slate 控件直接操作可变状态。

### 聊天数据流

```
用户输入 → Controller.SubmitPrompt()
  → 构建 OpenAI 格式请求消息（含工具定义）
  → ChatService.SendStreamingChatRequest() 通过 HTTP 发送
  → 流式 SSE 响应 → Controller.HandleStreamingLine()
  → 若 LLM 返回 tool_calls → Controller.ExecuteNextPendingToolCall()
    → ToolRuntime.ExecuteTool() → BridgeToolRegistry.ExecuteTool()
    → 结果注入请求上下文 → 继续发送回 LLM
  → 最终 assistant 回复 → 更新 ViewState → 刷新 UI
```

### 多提供商适配

`FAIEditorAssistantOpenAIChatService`（`Chat/Services/`）是 `IAIEditorAssistantChatService` 的唯一实现。它通过**内部格式转换**支持多个 AI 提供商：

- **内部统一使用 OpenAI `/chat/completions` 格式**（请求和响应结构）
- **OpenAI / DeepSeek / Custom**：直接使用 OpenAI 格式
- **Anthropic**：发送前转为 `/messages` 格式，接收后转回 OpenAI 格式
- **Gemini**：发送前转为 `:generateContent` 格式，接收后转回 OpenAI 格式

### 工具注册与执行

```
工具定义（C++ UCLASS）--[REGISTER_BRIDGE_TOOL 宏]--> FBridgeToolRegistry（单例）
                                                          │
              AIEditorAssistantToolRuntime::Startup() ────┘
                │
                ├── 从 Registry 获取所有工具定义
                ├── 添加聊天特定工具（get-selected-actors, delete-actor 等）
                └── 构建 OpenAI 工具定义 JSON，随请求发送
```

- 工具基类：`UBridgeToolBase`（UObject 抽象类），实现 `GetToolName()`、`GetToolDescription()`、`GetInputSchema()`、`Execute()`
- `REGISTER_BRIDGE_TOOL(ToolClass)` 宏：在全局静态初始化器中调用 `FBridgeToolRegistry::RegisterToolClass<T>()`，确保模块加载时工具自动注册
- 安全确认：`delete-actor`、`batch-delete-actors`、`start-pie`、`stop-pie`、`pie-session` 等工具需要用户在 UI 中批准后才会执行

### Chat/ 子目录分层

| 目录 | 职责 |
|------|------|
| `Chat/Model/` | 数据类型：消息、会话、视图状态、工具状态、代理角色 |
| `Chat/Services/` | `IAIEditorAssistantChatSessionStore`（文件持久化）和 `IAIEditorAssistantChatService`（HTTP API）接口及实现 |
| `Chat/Controller/` | 聊天运行时协调器：会话切换、流式处理、工具循环、审批、持久化 |
| `Chat/Widgets/` | Slate UI 控件：会话标签栏、对话视图、消息卡片、编辑器、工具确认栏 |
| `Chat/Markdown/` | Markdown 解析器和富文本渲染器 |

### 持久化

会话存储实现 (`FAIEditorAssistantFileChatSessionStore`) 将数据保存在 `{Project}/Saved/AIEditorAssistant/Chats/` 目录：
- `index.json`：会话索引
- `{SessionId}.json`：单个会话的完整数据（含请求上下文中的隐藏工具调用消息）

### 稳定扩展点

两个内部接口被视为长期扩展点：
- `IAIEditorAssistantChatSessionStore`：替换存储后端（当前仅文件实现）
- `IAIEditorAssistantChatService`：替换或扩展 AI 服务集成（当前支持 OpenAI/Anthropic/Gemini）

## 必需的引擎插件

`AIEditorAssistant.uplugin` 声明了以下必需插件：
- `EditorScriptingUtilities`
- `EnhancedInput`
- `PythonScriptPlugin`
- `StateTree`

## 配置文件

- 插件设置通过 `UAIEditorAssistantSettings`（继承 `UDeveloperSettings`）配置，存储在 `EditorPerProjectUserSettings` 中
- 项目设置路径：`Project Settings > Plugins > AI Editor Assistant`
- 默认值：Provider=`OpenAI`，Model=`gpt-4o-mini`，BaseUrl=`https://api.openai.com/v1`，MaxToolRounds=`500`
