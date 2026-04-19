# AI Gateway Editor for Unreal Engine 5

AI Gateway Editor is an experimental Unreal Engine 5 editor plugin that brings an OpenAI-compatible chat experience directly into the UE editor and lets the model operate native editor tooling in-process.

Instead of acting as a simple chat window, the plugin turns the editor into an AI-assisted workspace:

- Chat with any OpenAI-compatible `/chat/completions` gateway
- Keep conversation context across turns
- Stream assistant output live into the editor UI
- Persist multiple local chat sessions per project
- Let the model call native Unreal editor tools through OpenAI-style `tools` / `tool_calls`
- Approve sensitive actions inline before they run
- Use the same panel to inspect, query, and modify UE editor state

## What This Plugin Does

At a high level, the plugin combines three things:

1. An editor-embedded chat UI
2. An OpenAI-compatible chat client with streaming and tool-calling support
3. A native Unreal tool runtime that exposes editor operations to the model

This means you can open a panel inside Unreal Editor, ask the model to inspect the current level, query assets, work with Blueprints, control PIE, or perform other editor tasks, and the plugin will execute supported operations directly inside the editor process.

There is no required external MCP sidecar in the current in-editor workflow. The tool runtime is hosted inside the plugin and bridges to Unreal-native functionality.

## Key Features

### In-Editor Chat Panel

- Dockable `AI Gateway` tab inside Unreal Editor
- Menu entry under `Window`
- Extra toolbar entry in the Play toolbar area
- Enter-to-send workflow
- ChatGPT-like message cards instead of one large text box
- Rounded message bubbles for user and assistant replies
- Selectable message text
- Right-click copy support on message cards

### Multi-Session Chat

- Multiple chat sessions in a browser-style tab bar
- Create, switch, and close sessions from the panel
- Session titles generated locally from the first user message
- Current session draft is preserved when switching tabs
- Busy-state protection to prevent switching/closing during active requests or tool approval

### Local Persistence

- Sessions are saved per Unreal project
- Storage location:
  - `Saved/AIGatewayEditor/Chats/index.json`
  - `Saved/AIGatewayEditor/Chats/<SessionId>.json`
- Sessions restore automatically after the editor restarts
- Both UI-visible conversation history and request-context history are persisted

### OpenAI-Compatible Chat Integration

- Uses `POST {BaseUrl}/chat/completions`
- Sends bearer-token authorization with the configured API key
- Supports standard assistant messages
- Supports streaming responses
- Supports OpenAI-style `tools` and `tool_calls`
- Surfaces clear errors when the gateway response is malformed or tool-calling is unsupported

### Native Unreal Tool Calling

- Sends tool definitions with every request
- Parses `tool_calls` from both normal and streaming responses
- Executes tool calls sequentially inside the editor
- Feeds tool results back into the model as `role=tool` messages
- Continues the tool loop until a final assistant response is produced
- Keeps tool context in the request history without polluting the visible transcript

### Safety and Confirmation

Read-only tools run immediately.

Sensitive tools require explicit inline confirmation in the chat panel, including operations such as:

- `delete-actor`
- `batch-delete-actors`
- `start-pie`
- `stop-pie`
- `pie-session`

If a tool is rejected, the rejection is returned cleanly into the tool loop so the model can continue from that outcome.

### Markdown-Oriented Chat Rendering

The chat transcript is designed for model-style responses rather than plain text only. The panel includes support for rich message rendering such as:

- Headings
- Bold and inline emphasis
- Inline code
- Fenced code blocks
- Links
- Tables
- Wrapped text without forcing horizontal scrolling for normal prose

## Supported Workflow

The intended workflow is:

1. Open the `AI Gateway` tab
2. Configure the gateway in Project Settings
3. Ask the model something about the current Unreal project or editor state
4. Let the model decide whether it needs native tools
5. Approve sensitive actions when prompted
6. Continue the conversation with full context preserved

Typical examples:

- "List the first 10 actors in the current level."
- "Inspect the selected actor and tell me its important properties."
- "Find this Blueprint and explain its structure."
- "Start PIE after checking the current map setup."
- "Modify a Blueprint default and compile it."

## Tooling Coverage

The plugin has moved beyond a minimal proof of concept and now routes a broad Unreal editor tool surface through the in-process runtime.

The exact available tool list is built from the internal bridge registry at startup, then extended with a few plugin-specific convenience tools such as:

- `get-selected-actors`
- `delete-actor`
- `set-actor-transform`
- `set-blueprint-default`
- `start-pie`
- `stop-pie`

In practice, the integrated Unreal-side tool surface covers a wide set of editor operations, including categories such as:

- Level and actor inspection
- Actor spawning, deletion, transform changes, and property updates
- Selected actor queries
- Blueprint inspection, editing-related operations, compilation, and save flows
- Asset search and asset-oriented editor operations
- PIE control
- Screenshot and viewport capture
- Console variable and config-related operations
- Logging and editor diagnostics
- Additional editor-facing operations exposed through the integrated bridge modules

The model sees these tools through the OpenAI function-calling format, while the editor executes them natively in C++.

## Installation

### Option 1: Use As a Project Plugin

Copy the plugin into your Unreal project:

```text
<YourProject>/Plugins/AIGatewayEditor
```

Then regenerate project files if needed and build the editor target.

### Option 2: Work From This Repository

This repository already contains the plugin under:

```text
Plugins/AIGatewayEditor
```

You can package or copy that plugin into another UE5 project for testing.

## Requirements

- Unreal Engine 5 editor environment
- A gateway that exposes an OpenAI-compatible `/chat/completions` endpoint
- A valid API key for that gateway

The plugin is editor-focused and currently intended for in-editor usage rather than packaged runtime builds.

## Configuration

Open:

`Project Settings > Plugins > AI Gateway`

Configure:

- `Base URL`
- `API Key`
- `Model`

These settings are stored in project config and are intentionally kept out of the chat composer UI.

## Opening the Panel

You can open the panel from either of these entry points:

- `Window > AI Gateway`
- The `AI Gateway` button in the Play toolbar area

## Chat Session Behavior

Each session keeps two layers of history:

- A visible conversation transcript for the UI
- A request-context history used for model continuity, including hidden tool-call messages

This design allows the plugin to:

- Keep tool-call context across restarts
- Hide raw tool chatter from the visible chat log
- Resume practical conversations instead of starting over each time

Session titles are generated locally from the first user message and are not produced by a second AI call.

## Tool Call Loop Behavior

When the model returns `tool_calls`, the plugin:

1. Parses the requested tool calls
2. Executes them one by one
3. Requests approval first if a tool is sensitive
4. Appends each tool result back into the request context as `role=tool`
5. Sends the updated conversation back to the gateway
6. Repeats until the gateway returns a final assistant message

This keeps the model in control of the reasoning loop while keeping execution inside Unreal Editor.

## Internal Architecture

The plugin has been refactored so the main panel is no longer a single monolithic class.

### Main Editor Modules

- `AIGatewayEditor`
  - The chat UI, chat controller, session persistence, gateway integration, and tool runtime wrapper
- `SoftUEBridge`
  - Unreal-facing bridge functionality used to expose editor operations
- `SoftUEBridgeEditor`
  - Editor-specific tool implementations and registry integration

### Chat System Structure

Inside `AIGatewayEditor`, the chat implementation is split into dedicated layers:

- `Chat/Model`
  - Shared types for messages, sessions, view state, and tool-call state
- `Chat/Services`
  - Session storage and OpenAI-compatible HTTP chat service
- `Chat/Controller`
  - The runtime coordinator for session switching, streaming, tool loops, approvals, and persistence
- `Chat/Widgets`
  - Session tabs, conversation view, composer, tool confirmation bar, and message cards
- `Chat/Markdown`
  - Internal markdown parsing and rendering helpers for message display

### Stable Internal Extension Points

Two internal interfaces are treated as the main long-term extension points:

- `IAIGatewayChatSessionStore`
- `IAIGatewayChatService`

This makes it easier to change storage or gateway backends later without rewriting the panel UI.

## Current Limitations

- The plugin is still experimental
- It is primarily intended for editor workflows
- Gateway compatibility depends on OpenAI-style chat-completions behavior
- Best results require a model that actually supports `tools` / `tool_calls`
- UI and markdown rendering are still evolving

## Repository Layout

Key paths:

- `Plugins/AIGatewayEditor`
- `Plugins/AIGatewayEditor/Source/AIGatewayEditor`
- `Plugins/AIGatewayEditor/Source/SoftUEBridge`
- `Plugins/AIGatewayEditor/Source/SoftUEBridgeEditor`

## Why This Plugin Exists

The goal of AI Gateway Editor is to make Unreal Editor directly operable by a model from inside the editor itself, instead of limiting the experience to external chat windows or requiring a separate orchestration process for the common workflow.

In short:

- The editor stays the primary workspace
- The model gets structured native tools
- The user keeps control over sensitive actions
- Conversations remain persistent and project-local

## Status

This project is actively evolving. The current implementation already supports a practical in-editor AI workflow, but it should still be treated as a fast-moving editor plugin rather than a finished product.
