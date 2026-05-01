# <img src="Plugins/AIEditorAssistant/Resources/Icon40.png" alt="AI Editor Assistant Icon" width="36" /> AI Editor Assistant for Unreal Engine 5

[中文说明 / Chinese README](README.zh-CN.md)

AI Editor Assistant is an experimental Unreal Engine 5 editor plugin that brings an OpenAI-compatible chat experience directly into the UE editor and lets the model operate native editor tooling in-process.

This project incorporates UE-side code, ideas, and tool taxonomy adapted from the [`soft-ue-cli`](https://github.com/softdaddy-o/soft-ue-cli) repository. The integrated `SoftUEBridge` and `SoftUEBridgeEditor` modules form the Unreal-side tooling foundation used to expose editor operations to the model.

- Chat with any OpenAI-compatible `/chat/completions` endpoint
- Stream assistant output live into the editor UI
- Run multiple chat sessions concurrently with independent streaming
- Persist chat sessions per project with full context history
- Let the model call native Unreal editor tools through OpenAI-style `tools` / `tool_calls`
- Approve sensitive actions inline before they run

## Installation

Copy the plugin into your Unreal project:

```text
<YourProject>/Plugins/AIEditorAssistant
```

Regenerate project files and build the editor target.

## Requirements

- Unreal Engine 5 editor
- A service exposing an OpenAI-compatible `/chat/completions` endpoint
- A valid API key

## Configuration

`Project Settings > Plugins > AI Editor Assistant`

Configure `Base URL`, `API Key`, and `Model`. The panel also exposes model and reasoning mode selectors directly in the chat UI.

## Opening the Panel

- `Window > AI Editor Assistant`
- The `AI Editor Assistant` button in the Play toolbar

## Key Features

### Multi-Session Concurrent Chat

- Tab-based session manager with create, switch, and close
- Switch sessions freely while other sessions are streaming
- Multiple sessions can run simultaneous AI conversations
- Streaming sessions show a green indicator on the tab
- Draft is preserved per session when switching tabs
- Session titles generated locally from the first user message

### Native Unreal Tool Calling

- Sends tool definitions with every request
- Parses `tool_calls` from both streaming and non-streaming responses
- Executes tool calls sequentially inside the editor
- Feeds tool results back into the model for continued reasoning
- Continues the tool loop until a final assistant response is produced

### Safety and Confirmation

Read-only tools run immediately. Destructive tools require inline confirmation:

- `delete-actor`, `batch-delete-actors`
- `start-pie`, `stop-pie`, `pie-session`

### Multi-Provider Support

Supports OpenAI, Anthropic, Gemini, DeepSeek, and custom OpenAI-compatible providers through internal format translation. All providers use a unified OpenAI-format message pipeline internally.

### Markdown Rendering

Rich message rendering with headings, bold, italic, inline code, fenced code blocks, links, and tables.

### Local Persistence

Sessions saved per project under `Saved/AIEditorAssistant/Chats/`. Both visible conversation and request-context history are persisted and restored on editor restart.

## Internal Architecture

The plugin consists of three modules:

- **`AIEditorAssistant`** — Chat UI, controller, session persistence, service integration, and tool runtime wrapper
- **`SoftUEBridge`** — Runtime bridge layer exposing editor operations
- **`SoftUEBridgeEditor`** — Editor-specific tool implementations and registry

Inside `AIEditorAssistant`, the chat system is organized as:

| Layer | Path | Purpose |
|-------|------|---------|
| Model | `Chat/Model/` | Shared types for messages, sessions, view state, tool calls |
| Services | `Chat/Services/` | Session storage and multi-provider HTTP chat service |
| Controller | `Chat/Controller/` | Session switching, streaming, tool loops, approvals, persistence |
| Widgets | `Chat/Widgets/` | Session tabs, conversation view, composer, tool confirmation, message cards |
| Markdown | `Chat/Markdown/` | Markdown parsing and rich text rendering |

### Stable Extension Points

- `IAIEditorAssistantChatSessionStore` — swap storage backends
- `IAIEditorAssistantChatService` — swap or extend AI service integration

## Current Limitations

- Experimental, fast-moving editor plugin
- Provider compatibility depends on OpenAI-style chat-completions behavior
- Best results with models supporting `tools` / `tool_calls`

## Repository Layout

```
Plugins/AIEditorAssistant/
  Source/
    AIEditorAssistant/       Chat UI, controller, services, markdown
    SoftUEBridge/            Runtime bridge layer
    SoftUEBridgeEditor/      Editor tool implementations
```
