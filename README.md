# UE5-AIGateway

An experimental UE5 editor plugin project for connecting to LLM gateway services.

## Current Status

The editor now supports configuring and sending chat requests directly from Unreal Editor:

- Open the panel from `Window -> AI Gateway`.
- Configure and save `Base URL`, `API Key`, and `Model`.
- Click **Save Configuration and Send** to send a `POST {BaseURL}/chat/completions` request.
- The request includes `Authorization: Bearer <API Key>`.
- The panel parses OpenAI-compatible responses from `choices[0].message.content` and appends them to chat history.

## Plugin Location

- `Plugins/AIGatewayEditor`
