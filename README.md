# UE5-AIGateway

一个用于 UE5 编辑器的大模型网关插件实验项目。

## 当前进度

已支持在编辑器内直接配置并发送聊天请求：

- 在菜单 `Window -> AI Gateway` 打开聊天面板。
- 可输入并保存 `Base URL`、`API Key`、`Model` 配置。
- 点击“保存配置并发送”后，会发起 `POST {BaseURL}/chat/completions` 请求。
- 请求头包含 `Authorization: Bearer <API Key>`。
- 对 OpenAI 兼容响应的 `choices[0].message.content` 做解析并显示到对话记录。

## 插件位置

- `Plugins/AIGatewayEditor`
