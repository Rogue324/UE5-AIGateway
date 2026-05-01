# <img src="Plugins/AIEditorAssistant/Resources/Icon40.png" alt="AI Editor Assistant Icon" width="36" /> AI Editor Assistant for Unreal Engine 5

[English README](README.md)

AI Editor Assistant 是一个实验性的 Unreal Engine 5 编辑器插件，将 OpenAI 兼容的聊天体验直接带入 UE 编辑器，并让大模型在进程内调用原生 Unreal 编辑器工具。

本项目整合了来自 [`soft-ue-cli`](https://github.com/softdaddy-o/soft-ue-cli) 的 UE 侧代码与设计。插件中的 `SoftUEBridge` 和 `SoftUEBridgeEditor` 模块构成了向模型暴露编辑器操作能力的核心基础。

- 连接任意 OpenAI 兼容的 `/chat/completions` 端点
- 流式显示模型输出
- 多个会话并发聊天，各自独立的流式响应
- 按项目本地持久化会话及完整上下文
- OpenAI 风格的 `tools` / `tool_calls` 原生工具调用
- 敏感操作内联审批

## 安装

将插件复制到 Unreal 项目目录：

```text
<YourProject>/Plugins/AIEditorAssistant
```

重新生成工程文件并编译编辑器目标。

## 依赖

- Unreal Engine 5 编辑器
- 提供 OpenAI 兼容 `/chat/completions` 接口的服务
- 有效的 API Key

## 配置

`Project Settings > Plugins > AI Editor Assistant`

设置 `Base URL`、`API Key`、`Model`。聊天界面内也提供模型和推理模式选择器。

## 打开面板

- `Window > AI Editor Assistant`
- Play 工具栏中的 `AI Editor Assistant` 按钮

## 核心功能

### 多会话并发聊天

- 标签式会话管理，支持新建、切换、关闭
- 流式输出期间可自由切换会话
- 多个会话可同时进行 AI 对话
- 流式传输中的会话标签显示绿色指示器
- 切换时保留每个会话的草稿
- 会话标题由首条用户消息本地生成

### 原生 Unreal 工具调用

- 每次请求附带工具定义
- 支持从流式和非流式响应中解析 `tool_calls`
- 工具在编辑器内顺序执行
- 结果回灌给模型以继续推理
- 持续执行工具循环直到获得最终回复

### 安全与审批

只读工具直接执行。高风险工具需要内联确认：

- `delete-actor`、`batch-delete-actors`
- `start-pie`、`stop-pie`、`pie-session`

### 多提供商支持

支持 OpenAI、Anthropic、Gemini、DeepSeek 及自定义 OpenAI 兼容提供商。内部统一使用 OpenAI 格式的消息管道进行格式转换。

### Markdown 渲染

支持标题、粗体、斜体、行内代码、代码块、链接、表格等富文本渲染。

### 本地持久化

会话按项目保存至 `Saved/AIEditorAssistant/Chats/`。UI 可见的聊天记录和请求上下文历史均会持久化，编辑器重启后自动恢复。

## 内部架构

插件包含三个模块：

- **`AIEditorAssistant`** — 聊天 UI、控制器、会话持久化、服务集成、工具运行时
- **`SoftUEBridge`** — 运行时桥接层，暴露编辑器操作
- **`SoftUEBridgeEditor`** — 编辑器专用工具实现与注册

`AIEditorAssistant` 内部聊天系统分层：

| 层 | 路径 | 职责 |
|-----|------|------|
| Model | `Chat/Model/` | 消息、会话、视图状态、工具调用的共享类型 |
| Services | `Chat/Services/` | 会话存储与多提供商 HTTP 聊天服务 |
| Controller | `Chat/Controller/` | 会话切换、流式响应、工具循环、审批、持久化 |
| Widgets | `Chat/Widgets/` | 会话标签栏、对话视图、输入框、工具确认、消息卡片 |
| Markdown | `Chat/Markdown/` | Markdown 解析与富文本渲染 |

### 稳定扩展点

- `IAIEditorAssistantChatSessionStore` — 更换存储后端
- `IAIEditorAssistantChatService` — 更换或扩展 AI 服务集成

## 当前限制

- 仍处于实验阶段，快速迭代中
- 服务兼容性依赖 OpenAI 风格的 chat-completions 行为
- 最佳效果需搭配支持 `tools` / `tool_calls` 的模型

## 仓库结构

```
Plugins/AIEditorAssistant/
  Source/
    AIEditorAssistant/       聊天 UI、控制器、服务、Markdown
    SoftUEBridge/            运行时桥接层
    SoftUEBridgeEditor/      编辑器工具实现
```
