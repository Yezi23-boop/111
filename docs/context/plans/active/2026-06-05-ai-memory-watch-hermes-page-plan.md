---
id: 2026-06-05-ai-memory-watch-hermes-page-plan
tags: plan, active, ai-memory-watch, hermes, voice, ui, official-chat, owner
summary: 将 AI Memory Watch 作为独立 Hermes 功能页面推进，固定 V1 触摸录音、Ogg Opus 一次性 HTTP 上传、服务器侧 ASR/Hermes 处理和当前 owner 边界。
created: 2026-06-05
last_updated: 2026-06-08
last_reviewed: 2026-06-05
status: active
memory_type: project_plan
scope: repo
owners: docs/context/plans/active/2026-06-05-ai-memory-watch-hermes-page-plan.md, docs/context/knowledge/project/ai-memory-watch-product-positioning.md
triggers: AI Memory Watch, Hermes, memory_watch_service, voice_client_service, Ogg Opus, voice-command, 独立页面
evidence_level: design
---

# AI Memory Watch / Hermes 独立页面计划

## Purpose / Big Picture

- 任务目标：把 `AI Memory Watch` 做成独立 `Hermes` 功能页面，而不是复用或改造现有“小智 / official_chat”聊天页。
- 为什么现在做：产品定位已经确定为“连接个人 AI 大脑的随身记忆手表”，需要把 V1 功能、通信协议、UI 状态和 service owner 边界固定住。
- 完成后用户会看到什么变化：主菜单里有独立 `Hermes` 入口；进入后按住触摸按钮说话，松开发送，页面显示 ASR 文本和 Hermes 短回复。

## Scope / Non-Goals

本轮明确要做：

- 新增独立 hand-written LVGL 页面，不占用现有 `ai_ui_controller.c` 的“小智”页面语义。
- 新增独立主菜单入口 `Hermes`，保留现有 AI/小智入口不动。
- 新增窄 service，定名 `memory_watch_service`。
- 页面只表达用户意图：按住录音、松开发送、滑出取消、取消等待、取消当前追问。
- 复用当前项目 owner 合同：`audio_codec` 仍是麦克风 session owner，`network_service` 仍是网络 ready owner，`power_policy` 仍发布录音/上传预算。
- 把 `components/official_chat` 当作参考：学习它的 audio session、WebSocket 音频流、事件回调、snapshot 和 shutdown quiet period 设计。

本轮明确不做：

- 不把 Hermes 接入现有 `official_chat_service`。
- 不改 `components/official_chat` 的协议字段、OTA、MCP、NVS key 或默认 endpoint。
- 不把 AI Memory Watch 做成 `official_chat` 的另一个前台模式。
- 不在 ESP32-S3 端实现长期记忆、完整 ASR/TTS 或复杂 agent 编排。
- 不让 UI 线程直接上传音频、等待 Hermes 或管理音频 session。
- 不做 WebSocket/MQTT 常驻连接、Hermes 主动推送、离线缓存、TTS 播放、音频提示或震动反馈。
- 不做手表本地长期历史；只在 RAM 中保留当前 interaction 和最近一次结果。

## Proposed Runtime Shape

```text
Hermes 页面 / UI
  -> 投递用户意图：press_start / release_send / slide_cancel / cancel_wait / cancel_clarification

memory_watch_service
  -> FreeRTOS owner task
  -> command queue 接收 UI 命令
  -> snapshot 发布 ready / recording / encoding / uploading / thinking / needs_clarification / done / timeout / error / canceled
  -> 等 network_service ready
  -> 进入页面时做一次 /v1/watch/health
  -> 按 power_policy budget 决定是否允许录音/上传
  -> 申请 AUDIO_CODEC_OWNER_HERMES input session，申请不到则提示“麦克风忙，请稍后”

audio_codec
  -> input session owner；V1 不申请 output session

Hermes voice endpoint
  -> 与 Hermes 同服务器部署
  -> 接收 Ogg Opus、做 ASR、把文本交给 Hermes、返回短文本 JSON
```

V1 不做独立 watch bridge；只在 Hermes 服务器旁边加一个轻量 voice endpoint，职责仅限：

```text
Ogg Opus -> ASR -> Hermes 文本请求 -> 短文本响应
```

## Current Hermes Docker Status

2026-06-08 验证结论：

- Docker 容器 `hermes` 已运行，镜像为 `nousresearch/hermes-agent:latest`。
- 数据目录挂载到 `D:\Docker_data\hermes\data`。
- Hermes Dashboard 可通过宿主机本地 `http://127.0.0.1:9119` 访问；Dashboard 可用不等于 API Server 可用，该 Dashboard 也不是 ESP32-S3 的设备接口。
- Hermes 当前模型为 `mimo-v2.5`，provider 为 `Xiaomi MiMo`。
- `D:\Docker_data\hermes\data\.env` 当前已有 MiMo 相关配置，并已启用 `API_SERVER_ENABLED=true`、`API_SERVER_HOST=0.0.0.0`、`API_SERVER_PORT=8642` 与强随机 `API_SERVER_KEY`；该密钥不得写入仓库或 ESP32-S3 固件。
- Docker 已映射宿主机 `8642`，容器内和宿主机均已验证 API Server 有服务监听。
- 宿主机 `GET http://127.0.0.1:8642/health` 返回 `status=ok`。
- 带 `Authorization: Bearer <api_server_key>` 的 `GET /v1/models` 返回 `hermes-agent`。
- 带同一鉴权的 `POST /v1/responses`，输入 `手表用户说：记一下明天看电池日志`、`conversation=watch-001-ai-memory-watch`，已同步返回 Hermes + MiMo 最终文本。
- `server/watch_voice_endpoint` 已构建为 Docker 镜像 `ai-memory-watch-voice-endpoint:dev`，并以常驻容器 `ai-memory-watch-voice-endpoint` 运行在宿主机本地 `127.0.0.1:8787`。
- 常驻容器读取 `D:\Docker_data\hermes\watch_voice_endpoint.env`，该文件保存 `HERMES_API_KEY` 与 `WATCH_DEVICE_TOKENS`，不得提交进仓库。
- 常驻入口 `GET /v1/watch/health?device_id=watch-001` 已验证返回 `hermes_status=online`。
- 常驻入口 `POST /v1/watch/voice-command` 已切到 `WATCH_ASR_PROVIDER=mimo`，用本机合成中文 Ogg Opus 样本验证可返回手表 V1 固定 7 字段 JSON。
- `server/watch_voice_endpoint/smoke_test.ps1` 支持默认 mock 快速健康检查，也支持 `-UseRealAsr -AudioPath <sample.ogg>` 真实 ASR 检查。
- `smoke_test.ps1` 已支持 `-IncludeCancel`，可在同一次 smoke 中额外验证 `POST /v1/watch/request/{request_id}/cancel` 返回固定 7 字段 `status=canceled/action=no_action`。
- `smoke_test.ps1` 现在对 voice 与可选 cancel 响应执行“恰好 7 字段”校验，缺字段或多字段都会失败，防止 ESP32-S3 固定解析契约漂移。
- `smoke_test.ps1` 已支持 `-IncludeAuthFailure`，用无效 device token 验证 `/v1/watch/health` 返回 HTTP 403，不输出真实 token。
- `server/watch_voice_endpoint/runtime_status.ps1` 可输出无密钥 JSON 状态总览，覆盖 env key presence、Docker 容器状态、watch endpoint `/health`、`/v1/watch/health`、Hermes `/health` 与 `/v1/models`。
- `server/watch_voice_endpoint/acceptance_test.ps1` 可一键串起 runtime status、mock voice、cancel、无效 token 403、中文 Ogg Opus 生成和真实 MiMo ASR smoke，作为服务器侧版本迭代前验收门槛。
- `server/watch_voice_endpoint/watch_contract.v1.json` 已作为 ESP32-S3 设备侧机器可读契约，固定 endpoint、鉴权、request 字段、response 7 字段、枚举、超时、幂等和安全边界；`tests/test_contract.py` 会把契约与 `app.py` 的 FastAPI 模型/限制做一致性检查。
- 私有 `/health` 与 `runtime_status.ps1` 已增加非敏感请求指标：event/status/error 计数与最近一次请求摘要；最近请求只包含 `device_id/request_id/status/action/error_code/duration_ms/audio_bytes/asr_provider/completed_at`，不包含 ASR 文本、回复文本、音频内容或任何 key/token。
- `smoke_test.ps1` 已支持 `-BaseUrl <https://watch.example.com>` 指向公网域名，并可用 `-SkipServiceHealth` 跳过私有 `/health`；这样 Caddy 只公开 `/v1/watch/*` 时仍能验证设备协议入口。
- `runtime_status.ps1` 与 `acceptance_test.ps1` 也已支持 `-SkipServiceHealth`；公网只代理 `/v1/watch/*` 时可同时跳过 Docker、Hermes API 和私有 `/health` 检查。
- `server/watch_voice_endpoint/make_tts_sample.ps1` 可在 Windows 本机用中文 System.Speech voice + ffmpeg 生成可复现 Ogg Opus 测试样本；`smoke_test.ps1 -UseRealAsr` 现在要求显式传入 `AudioPath`，避免把 dummy Ogg 当真实语音测试。
- voice endpoint 已增加 `WATCH_REQUEST_TIMEOUT_SECONDS` 端到端请求预算，当前常驻容器配置为 `115` 秒，小于 ESP32-S3 侧 120 秒等待窗口；ASR + Hermes 处理超过总预算时服务器先返回手表 V1 `status=timeout/error_code=server_timeout`。
- voice endpoint 已补强 ESP32-S3 上传输入校验：`request_id` 只接受 1-96 位 ASCII 字母、数字、`.`、`_`、`:`、`-`，空音频和超长音频会在进入 ASR/Hermes 前返回手表 V1 固定 7 字段 JSON 错误，避免真实录音/重试异常掉进 ffmpeg 或 ASR 后才失败。
- MiMo ASR adapter 已用本机合成中文语音样本验证：WAV 经 ffmpeg 转 Ogg Opus 上传，endpoint 再转 16 kHz mono WAV 调 `mimo-v2.5-asr`，ASR 文本为 `记一下，明天看电池日志。`，随后 Hermes 返回 `status=done/action=memory_saved`。
- voice endpoint 已实现 `device_id + request_id` 最小幂等：已完成请求返回缓存结果，处理中重复请求等待同一任务，已取消请求返回 `canceled/no_action`，完成后再 cancel 返回完成结果。
- 本地 `/health` 会返回 `asr_provider`、`inflight_requests`、`completed_requests` 与 `canceled_requests`，用于联调时判断服务是否卡住；不返回 token 或 API key。
- Docker 常驻容器已验证幂等：同一个 `request_id` 连续提交两次，第二次返回第一次缓存结果，`asr_text` 未被第二次 mock 文本覆盖，`/health` 显示 `inflight_requests=0`。
- `server/watch_voice_endpoint/compose.local.yml` 已作为本地可重复部署入口，`docker compose -f compose.local.yml up -d --build` 可构建并启动 `ai-memory-watch-voice-endpoint`，healthcheck 当前为 `healthy`。
- 安全注意：不要把 `docker compose config` 输出贴到日志或 issue，因为 Compose 会展开 `env_file` 中的 API key 和 device token。
- 轮换过 Hermes `API_SERVER_KEY` 与 `watch-001` device token 后，已重新验证 Hermes `/health`、`/v1/models`、`/v1/responses` 和 watch endpoint 真实 ASR smoke 均可用。
- 已新增 `server/watch_voice_endpoint/deploy/Caddyfile.example`，公网域名第一版只反向代理 `/v1/watch/*` 到 `127.0.0.1:8787`；Hermes API Server `8642` 与 Dashboard `9119` 继续保持私有。
- Webhook platform 当前未启用；V1 不优先走 webhook，因为手表侧需要同步等待最终文本结果。

因此本计划的服务器链路已从纯 mock 推进为：

```text
Hermes OpenAI-compatible API Server /v1/responses 已验证
  -> watch voice endpoint 原型接收 Ogg Opus multipart
  -> mock ASR 或 MiMo ASR adapter -> Hermes API -> 手表 V1 7 字段 JSON
```

## V1 Communication Contract

### Interaction Flow

```text
触摸按钮按住
  -> memory_watch_service 开始录音

手指仍在按钮内松开
  -> 停止录音
  -> 封装 Ogg Opus
  -> HTTP POST /v1/watch/voice-command
  -> 上传中 -> 思考中
  -> 最多等待 120 秒
  -> 显示 done / timeout / error / canceled

手指滑出按钮区域后松开
  -> 取消录音并丢弃本次音频，不上传
```

### Audio

- V1 音频上传格式：`Ogg Opus`。
- 录音触发：只支持屏幕触摸按钮按住说话，暂不接物理按键。
- 单次录音最长 30 秒；少于 0.5 秒丢弃并提示说得太短；超过 30 秒自动停止并发送。
- ASR 在服务器 / Hermes 侧完成，ESP32-S3 不做 ASR。

### HTTP Endpoints

```http
GET  /v1/watch/health
POST /v1/watch/voice-command
POST /v1/watch/request/{request_id}/cancel
```

- 三个 endpoint 都使用 `Authorization: Bearer <device_token>`。
- V1 使用 `device_id + device_token allowlist`；开发期 token 写入 NVS 或本地配置，不硬编码进公开源码。
- `/health` 只在进入 Hermes 页面时请求一次，不做持续轮询；失败显示 `Hermes · 不可用`。
- `voice-command` 使用 `multipart/form-data`。

### Request Fields

`POST /v1/watch/voice-command` 字段：

```text
request_id=<device_id>-<boot_id>-<seq>
device_id=watch-001
audio=command.ogg
clarification_id=<active_clarification_id or empty>
battery_percent=76
charging=false
rssi=-62
firmware_version=...
locale=zh-CN
timezone=Asia/Shanghai
source=watch_hermes_page
ui_state=ready|recording|encoding|uploading|thinking|needs_clarification|done|timeout|error
```

- `conversation_id` 不由 ESP32 上传；服务器根据 `device_id` 固定选择 Hermes conversation，例如 `watch-001 -> ai-memory-watch-default`。
- `request_id` 由 ESP32 生成，格式为 `device_id + boot_id + seq`，示例 `watch-001-a3f91c20-0001`。
- `boot_id` 开机生成 32-bit 随机 hex；`seq` 为本次开机内递增计数。
- ESP32 不持久化 `request_id`，重启后当前请求视为中断。
- `locale` 固定 `zh-CN`，`timezone` 固定 `Asia/Shanghai`。
- V1 不上传本地时间，服务器用自己的时间解释提醒。

### Response Fields

服务器固定返回：

```json
{
  "request_id": "watch-001-a3f91c20-0001",
  "status": "done",
  "action": "memory_saved",
  "asr_text": "记一下明天看电池日志",
  "reply_text": "已记录：明天看电池日志。",
  "clarification_id": null,
  "error_code": null
}
```

`status`：

```text
done
error
timeout
canceled
```

`action`：

```text
memory_saved
reminder_created
question_answered
clarification_needed
no_action
error
```

- 取消时使用 `status=canceled`、`action=no_action`。
- `reply_text` 由服务器限制为适合小屏的短文本，目标不超过 80 个中文字；ESP32 端仍必须做安全截断。
- 页面不显示 `request_id` 或底层工具名，相关信息只写日志。
- 工具执行结果由 `reply_text` 概括。

### Clarification

- V1 允许多次追问/补充。
- `clarification_id` 由服务器 / Hermes 生成，ESP32 只保存当前 active id 并在下一次上传时带回。
- `needs_clarification` 状态下按住说话表示回答追问。
- 页面显示当前澄清链路的多条气泡，但只保存在 RAM；新的独立请求开始后清空，长期历史仍归 Hermes。
- `needs_clarification` 状态显示一个小的“取消”按钮；V1 可先本地清空 `active_clarification_id` 并回到 ready。

### Cancel

- `thinking` 状态提供“取消”按钮。
- 取消语义：取消手表侧等待，并尽力通知服务器取消 Hermes run；不承诺撤销已执行外部工具副作用。
- 点击取消后，ESP32 断开当前等待请求，并调用 `POST /v1/watch/request/{request_id}/cancel`。
- cancel 请求失败时，手表仍回 ready，显示“已取消等待”，日志记录 `cancel_request_failed`。
- 服务器需对 `device_id + request_id` 做最小幂等保护：正在处理返回同一任务，已完成返回旧结果，已取消返回 canceled。

### Server Behavior

- 开发期服务器可保存上传音频用于调试；正式环境默认不保存音频。
- ASR 落在 watch voice endpoint 内部 adapter，不放进 ESP32-S3，也不假设 Hermes `/v1/responses` 自带音频转写能力。
- 开发期默认 `WATCH_ASR_PROVIDER=mock`；后续可切换 `WATCH_ASR_PROVIDER=mimo`，用 `mimo-v2.5-asr` 调 OpenAI-compatible `/chat/completions`。
- MiMo ASR 当前不接受 `audio/ogg`，只接受 `audio/wav`、`audio/mp3` 或 `audio/mpeg`；因此 voice endpoint 对手表上传的 Ogg Opus 先用 ffmpeg 转成 16 kHz mono WAV，再作为 base64 `input_audio` data URI 上传。
- V1 原型允许 Hermes 执行外部工具；ESP32 不做工具白名单判断。
- 外部工具失败时，手表只显示简短失败，详细错误写服务器日志。
- Hermes watch-specific instructions 必须固定：输入来自手表短语音；优先识别记忆、提醒、问答或工具动作；回复适合小屏；需要追问时一次只问一个问题。

### Hermes API Server Route

V1 推荐让 voice endpoint 调用 Hermes 的 OpenAI-compatible API Server，而不是让 ESP32-S3 直接调用 Hermes：

```http
POST /v1/responses
Authorization: Bearer <api_server_key>
```

推荐使用 `conversation` 固定手表会话，例如：

```json
{
  "model": "hermes-agent",
  "input": "手表用户说：记一下明天看电池日志",
  "conversation": "watch-001-ai-memory-watch"
}
```

边界：

- `API_SERVER_KEY` 只放在服务器 / voice endpoint 侧，不写入 ESP32-S3 固件。
- ESP32-S3 只持有可轮换的 `device_token`，向 voice endpoint 鉴权。
- voice endpoint 负责把 Hermes 的 OpenAI-style 响应整理成手表 V1 的 7 字段 JSON。
- 若后续公开到域名，公网入口应优先暴露 voice endpoint；Hermes API Server 可只在 Docker network / 本机内网可见。

## Relation To official_chat

`components/official_chat` 可以复用的经验：

- `official_chat_service` 证明了“UI 投命令、service task 管生命周期、snapshot 给 UI 读”的模式适合本仓库。
- `LocalAudioCodecAdapter` 已经把麦克风/喇叭接入 `audio_codec` input/output session，后续新 service 必须沿用同样的资源所有权口径。
- `WebsocketProtocol` 的 hello、二进制音频帧、`stt/tts` 文本事件和 downlink audio 设计可以作为 Hermes voice endpoint 协议参考。
- shutdown quiet period 说明音频/网络链路销毁前需要收敛窗口，不能在 LVGL 页面线程同步销毁。

不直接复用 `official_chat` 的原因：

- `official_chat` 是完整前台聊天 runtime，包含 Application、OTA、MCP server、WebSocket/MQTT、wake word 和激活/升级状态机。
- AI Memory Watch 的产品语义是“记录、整理、提醒、回顾”，不是“小智前台聊天会话”。
- Hermes 第一版更适合通过服务器 voice endpoint 接入，避免 ESP32 端兼容完整 third-party runtime 协议。

## Page Boundary

建议新增：

```text
main/ui/custom/memory_watch_controller.[ch]
main/ui/custom/memory_watch_view.[ch]
main/services/memory_watch_service.[ch]
```

主菜单入口：

```text
Hermes
```

页面 V1 状态：

- `Hermes · 在线`：`network_service` ready 且 `/v1/watch/health` 成功。
- `Hermes · 未联网`：`network_service` 未 ready。
- `Hermes · 不可用`：`/v1/watch/health` 失败。
- `准备好了`：可以按住说话。
- `聆听中`：正在录音。
- `编码中`：正在封装 Ogg Opus。
- `上传中`：音频发送到服务器。
- `思考中`：Hermes 正在处理。
- `需要补充`：Hermes 正在追问，下一次录音作为补充回答。
- `已完成`：Hermes 已确认保存、创建提醒、回答问题或完成工具动作。
- `超时`：ESP32 等待 120 秒后仍未得到最终结果。
- `失败`：展示可重试错误。
- `已取消`：用户取消等待或取消追问。

页面视觉：

- V1 页面要像聊天页，但不复用 `ai_chat_view`。
- 新建 `memory_watch_view`，可参考 `ai_chat_view` 的气泡和滚动区。
- 顶部显示 Hermes 在线状态，不提供“进入配网”按钮。
- 中部显示当前 interaction 的气泡：用户气泡显示 `asr_text`，Hermes 气泡显示 `reply_text`。
- ASR 文本只显示，不允许编辑确认。
- 底部是触摸按住说话按钮。
- 按钮文案：
  - 默认：`按住说话`
  - 录音中且手指在按钮内：`松开发送`
  - 录音中且手指滑出按钮：`松手取消`

错误文案：

```text
network_error      网络异常，请稍后重试
server_timeout     Hermes 处理超时
asr_or_agent_error 没有处理成功，请再说一次
mic_busy           麦克风忙，请稍后
```

## FreeRTOS Teaching Point

这个功能应作为 FreeRTOS 学习样例：

- `task`：长期 service owner，承载录音/上传/状态推进。
- `queue`：UI 向 service 投递带参数命令，避免裸 `volatile` flag。
- `event group`：组合网络 ready、服务器 ready、录音许可等 readiness。
- `mutex/critical section`：保护小型 snapshot，UI 只复制状态，不推进业务。
- `timeout wait`：录音最长时长、HTTP 120 秒等待、cancel 收敛窗口。

同一时间只允许一个 active request：

```text
ready / needs_clarification
  -> 可以开始录音

recording / encoding / uploading / thinking
  -> 不接受新的录音请求

done / timeout / error / canceled
  -> 用户可开始下一次独立请求；新请求清空旧 interaction 气泡
```

## Progress

- `[x]` 已确定产品定位：AI Memory Watch。
- `[x]` 已确定不复用现有小智 AI 聊天页，改为独立功能页面。
- `[x]` 设计 V1 页面 wireframe 与主菜单入口。
- `[x]` 设计 `memory_watch_service` 的 command/snapshot/API。
- `[x]` 确定 Hermes voice endpoint 的最小协议。
- `[x]` 已确认 Docker Hermes 部署存在，MiMo 模型已配置。
- `[x]` 启用并验证 Hermes API Server `/v1/responses` 文本链路。
- `[x]` 落地 watch voice endpoint 的最小 server mock / adapter。
- `[x]` 以 Docker Desktop 常驻容器方式提供本机联调入口 `127.0.0.1:8787`。
- `[x]` 落地可配置 MiMo ASR adapter，默认保持 mock ASR，并已用本机合成中文 Ogg Opus 样本验证真实 ASR -> Hermes 链路。
- `[x]` 将常驻本机联调容器切到 `WATCH_ASR_PROVIDER=mimo`，保留 smoke 脚本的 mock override 快速检查。
- `[x]` 实现 voice endpoint `device_id + request_id` 最小幂等与本地健康统计，降低 ESP32 Wi-Fi 重试导致重复记忆/提醒的风险。
- `[x]` 增加 Docker Compose 本地可重复部署入口，并验证容器 healthcheck 为 healthy。
- `[x]` 增加公网域名反向代理示例，固定只暴露 watch endpoint，不暴露 Hermes Dashboard 或 Hermes API Server。
- `[x]` 增加可复现中文 Ogg Opus 样本生成脚本，稳定真实 ASR smoke test 输入。
- `[x]` 增加 ESP32-S3 设备侧 V1 机器可读契约，并用 source test 锁定 response 字段、枚举、request 限制和超时预算。
- `[x]` 落地 `memory_watch_service` owner skeleton 和 `AUDIO_CODEC_OWNER_HERMES`，先固定 FreeRTOS command queue、只读 snapshot、网络 ready 读取和不复用 `official_chat` 的边界。
- `[x]` 落地独立 `memory_watch_ogg_opus_muxer`，将裸 Opus packet 封装为 Ogg Opus 容器；写回调失败后进入 fail-closed，避免半页写出后继续复用损坏流。
- `[x]` 落地 `memory_watch_recorder` 窄模块，封装 Hermes 麦克风 owner session、后台 Safety Monitor 暂停、硬件 PCM 主麦通道选取、24 kHz -> 16 kHz mono 线性重采样、Opus 编码和 Ogg muxer 输出；尚未接 HTTP multipart 和实机手表语音样本。
- `[ ]` 落地独立 Hermes 页面 skeleton。

## Decision Log

- 2026-06-05：
  - 决策：AI Memory Watch 使用独立功能页面。
  - 原因：现有 `official_chat_service` 绑定“小智前台聊天”语义；Hermes 线是“记忆/提醒/回顾”产品闭环，直接混用会污染 owner 和 UX。
- 2026-06-05：
  - 决策：V1 使用 HTTP multipart 一次性上传 Ogg Opus，服务器 / Hermes 侧 ASR，ESP32 最长等待 120 秒最终文本结果。
  - 原因：复用当前已验证的 Opus 音频方向，同时避免第一版引入 WebSocket 流式、TTS 播放或常驻连接。
- 2026-06-05：
  - 决策：V1 原型允许 Hermes 执行外部工具，但手表不显示底层 tool 名，也不承诺 cancel 能撤销已执行副作用。
  - 原因：用户希望原型阶段先放开 Hermes 能力；安全分级后续作为消费版或公网版本再收敛。
- 2026-06-08：
  - 决策：已部署 Docker Hermes 后，V1 服务器链路优先启用 Hermes API Server `/v1/responses`，由 watch voice endpoint 调用；不让 ESP32-S3 直接调用 Dashboard API，也不优先用 webhook。
  - 原因：`/v1/responses` 支持同步返回最终文本并可用 `conversation` 维护手表会话，更贴合 ESP32-S3 侧 120 秒等待和小屏 JSON 返回；Dashboard 是本地管理 UI，webhook 更适合外部事件触发，不适合作为第一版同步语音命令返回路径。
- 2026-06-08：
  - 决策：服务器端新增 `server/watch_voice_endpoint` 原型，使用 FastAPI 暴露 `/v1/watch/health`、`/v1/watch/voice-command` 和 `/v1/watch/request/{request_id}/cancel`；开发期 ASR 先固定为 mock 文本，再调用 Hermes `/v1/responses`。
  - 原因：先验证 `ESP32-S3 -> voice endpoint -> Hermes API Server -> 手表 V1 JSON` 的同步闭环，避免在固件侧提前引入 ASR、Hermes API key 或 Dashboard 依赖；真实 ASR 后续只替换 endpoint 内部 adapter。
- 2026-06-08：
  - 决策：本机联调阶段将 watch voice endpoint 作为独立 Docker Desktop 常驻容器运行，只绑定 `127.0.0.1:8787`，通过 `host.docker.internal:8642` 访问 Hermes API Server。
  - 原因：先形成服务器侧稳定联调入口，公网域名、反向代理和 TLS 暴露后置；ESP32-S3 后续只需面向 voice endpoint 协议，不依赖 Hermes Dashboard 或 Hermes API key。
- 2026-06-08：
  - 决策：真实 ASR 不塞进 Hermes `/v1/responses` 调用，也不放在 ESP32-S3；watch voice endpoint 增加 `WATCH_ASR_PROVIDER=mimo` adapter，按 MiMo 官方 ASR 文档调用 `mimo-v2.5-asr` 的 OpenAI-compatible `/chat/completions`。
  - 原因：MiMo ASR 的官方入口是 `input_audio` data URI -> `/chat/completions`，而 Hermes API Server 当前本机未暴露可确认的 OpenAPI 或 audio transcription endpoint；把 ASR 留在 endpoint 内部可保持手表 V1 JSON 和 Hermes agent 调用不变。
- 2026-06-08：
  - 决策：即使 ESP32-S3 继续上传 Ogg Opus，voice endpoint 给 MiMo ASR 前必须转码为 WAV；当前 Docker 镜像安装 ffmpeg，并在 `WATCH_ASR_PROVIDER=mimo` 路径执行 Ogg/未知 MIME -> 16 kHz mono WAV。
  - 原因：MiMo ASR 对 `audio/ogg` 返回 `400 Param Incorrect`，错误体明确要求 `audio/wav`、`audio/mp3` 或 `audio/mpeg`；转码放在服务器侧不会改变 ESP32 V1 协议，也比让手表改音频格式更符合当前“复用已能正常通信的 Opus 代码”约束。
- 2026-06-08：
  - 决策：voice endpoint 在 V1 使用内存态 request registry 实现 `device_id + request_id` 幂等，不引入数据库；完成结果按 `WATCH_REQUEST_CACHE_LIMIT` 缓存，取消集合按 `WATCH_CANCELED_REQUEST_LIMIT` 限制。
  - 原因：ESP32-S3 后续可能因 Wi-Fi 抖动或 120 秒等待重试同一个 `request_id`；服务器必须避免重复 ASR/Hermes/tool 动作，同时保持第一版可回退、可观察、无需额外存储服务。
- 2026-06-08：
  - 决策：本地服务器侧联调用 `compose.local.yml` 承载 `ai-memory-watch-voice-endpoint` 的可重复 build/up/healthcheck；仍只绑定 `127.0.0.1:8787`。
  - 原因：把手动 `docker run` 收敛为可重复操作，便于 Docker Desktop 长时间联调和重启恢复；公网域名/TLS 暴露仍后置，不把 Hermes API Server 直接暴露给 ESP32。
- 2026-06-08：
  - 决策：公网域名第一版用反向代理只暴露 `/v1/watch/*` 到 voice endpoint；Hermes API Server `8642`、Dashboard `9119` 和全部 provider/API key 只留在服务器侧。
  - 原因：ESP32-S3 只需要 device endpoint 与 device token；让设备直连 Hermes Dashboard 或持有 Hermes API key 会破坏端云边界，也不利于后续公网安全收敛。

## Validation and Acceptance

计划运行的验证命令：

```powershell
uv run python scripts/context/validate_context.py --level standard --q "AI Memory Watch Hermes V1 通讯 独立页面 official_chat owner" --brief
```

后续改源码时再补：

```powershell
uv run python -m pytest tests/test_*memory* tests/test_official_chat_service_source.py tests/test_audio_codec_port_source.py tests/test_network_service_wifi_management_source.py
idf.py build
```

服务器链路先验收：

```powershell
curl http://127.0.0.1:8642/health
curl http://127.0.0.1:8642/v1/responses `
  -H "Authorization: Bearer <api_server_key>" `
  -H "Content-Type: application/json" `
  -d "{\"model\":\"hermes-agent\",\"input\":\"手表用户说：记一下明天看电池日志\",\"conversation\":\"watch-001-ai-memory-watch\"}"
```

2026-06-08 已通过的服务器验证：

```powershell
docker ps --filter "name=hermes"
Invoke-RestMethod http://127.0.0.1:8642/health
Invoke-RestMethod http://127.0.0.1:8642/v1/models -Headers @{ Authorization = "Bearer <api_server_key>" }
Invoke-RestMethod http://127.0.0.1:8642/v1/responses -Method Post -Headers @{ Authorization = "Bearer <api_server_key>" } -Body <redacted-json>
uv run --with pytest==8.3.4 --with fastapi==0.115.6 --with httpx==0.28.1 --with python-multipart==0.0.20 --with "uvicorn[standard]==0.34.0" python -m pytest tests -q
docker build -t ai-memory-watch-voice-endpoint:dev .
docker run --name ai-memory-watch-voice-endpoint-test -p 127.0.0.1:8789:8787 ...
.\server\watch_voice_endpoint\smoke_test.ps1
Invoke-RestMethod http://127.0.0.1:8642/openapi.json
docker run --name ai-memory-watch-voice-endpoint-asr-test -p 127.0.0.1:8790:8787 -e WATCH_ASR_PROVIDER=mimo ...
```

已观察到的结果：

- `hermes` 容器运行，`8642` 映射到宿主机，`9119` Dashboard 仍只作为本地管理界面。
- Hermes `/health` 返回 `status=ok`，`/v1/models` 返回 `hermes-agent`。
- Hermes `/v1/responses` 对中文手表记忆请求同步返回最终文本。
- watch voice endpoint 单元测试 3 项通过。
- watch voice endpoint Docker smoke test 返回 `status=done`、`action=memory_saved`、`asr_text=记一下明天看电池日志`、非空 `reply_text`，并包含固定 7 字段。
- 常驻容器 `ai-memory-watch-voice-endpoint` 已启动，`127.0.0.1:8787` 的 smoke test 返回 `status=done`、`action=memory_saved` 和固定 7 字段。
- MiMo ASR adapter 的 source test 已锁定 `/chat/completions`、`mimo-v2.5-asr`、`input_audio` data URI、`Authorization: Bearer` 和 `trust_env=False`。
- Hermes API Server `GET /openapi.json` 当前返回 404，不能把 ASR 端点假设为 Hermes API Server 自带能力。
- 直接向 MiMo ASR 发送 `data:audio/ogg;base64,...` 返回 400，错误体要求 `audio/wav/audio/mp3/audio/mpeg`。
- 带 ffmpeg 的临时 `WATCH_ASR_PROVIDER=mimo` 容器已用本机中文 TTS Ogg 样本跑通：`asr_text=记一下，明天看电池日志。`，Hermes 返回 `status=done/action=memory_saved`。
- 常驻 `ai-memory-watch-voice-endpoint` 容器已重建到带 ffmpeg 的新镜像并切到 `WATCH_ASR_PROVIDER=mimo`；`smoke_test.ps1 -UseRealAsr -AudioPath <sample.ogg>` 与默认 mock smoke test 均通过。
- voice endpoint source tests 已覆盖完成请求复用、处理中重复请求共享同一任务、完成后 cancel 返回旧结果。
- 常驻 Docker 运行态已验证重复 `request_id` 不重复处理：两次响应完全一致，第二次 `asr_text` 仍为第一次请求文本。
- `docker compose -f compose.local.yml up -d --build` 已验证可启动容器，Docker healthcheck 状态为 `healthy`，真实 ASR smoke test 仍返回 `status=done/action=memory_saved`。
- Hermes API Server key 与 watch device token 轮换后复验通过：Hermes `/v1/responses` 返回 `status=completed`，watch endpoint 真实 ASR smoke 返回 `status=done/action=memory_saved`。
- `deploy/Caddyfile.example` 只包含占位域名和 `/v1/watch/*` 反向代理，不包含任何 key/token。
- `make_tts_sample.ps1` 已生成 `ai-memory-watch-tts.ogg`，随后 `smoke_test.ps1 -UseRealAsr -AudioPath <generated.ogg>` 返回 `status=done/action=memory_saved`；未提供 `AudioPath` 时会明确报错。
- voice endpoint 输入校验已验证：畸形 `request_id` 运行态返回 `status=error/action=error/error_code=asr_or_agent_error` 和固定 7 字段 JSON；单元测试覆盖畸形 `request_id` 与空音频。
- `smoke_test.ps1 -SkipServiceHealth` 已验证可在不访问 `/health` 的情况下完成 `/v1/watch/health` 与 `/v1/watch/voice-command` 检查，适配只公开 `/v1/watch/*` 的公网反向代理形态；默认 mock 音频临时文件已改成 GUID 文件名，避免并发 smoke 抢占同一 `%TEMP%` 文件。
- 端到端请求预算已验证：单元测试覆盖超出 `WATCH_REQUEST_TIMEOUT_SECONDS` 时返回固定 7 字段 `timeout/server_timeout`；常驻容器 `/health` 显示 `request_timeout_seconds=115.0`，mock smoke 与真实 ASR smoke 均继续通过。
- `runtime_status.ps1` 已验证默认本机模式返回 `watch_voice_endpoint=healthy`、`watch_health=ok`、`hermes_status=online`、`hermes_models.model_count=1`；`-SkipDocker -SkipHermesApi` 模式可只检查公网 watch endpoint，不输出真实 key/token。
- 请求指标已验证：重建容器后先跑 mock smoke 与真实 ASR smoke，再读 `runtime_status.ps1` 得到 `processed=2/done=2`、最近请求 `status=done/action=memory_saved/audio_bytes=8345`，且最近请求摘要不包含 `asr_text` 或 `reply_text` 字段。
- cancel endpoint 运行态 smoke 已验证：`smoke_test.ps1 -IncludeCancel` 与 `smoke_test.ps1 -UseRealAsr -AudioPath <generated.ogg> -IncludeCancel` 均返回 `cancel_status=canceled/cancel_action=no_action`；随后 `runtime_status.ps1` 显示 `canceled_count=2`。
- 固定 7 字段契约已提升到 smoke 精确校验：mock `-IncludeCancel` 本机 smoke 通过，voice 与 cancel 均没有多余字段。
- 负向鉴权 smoke 已验证：`smoke_test.ps1 -IncludeCancel -IncludeAuthFailure` 本机返回 `auth_failure_status_code=403`，同时正向 voice/cancel 仍通过。
- 服务器侧 acceptance 已验证：`acceptance_test.ps1` 返回 `status=passed`，mock 与 real ASR smoke 均 `voice_status=done/action=memory_saved`，cancel 均 `canceled/no_action`，负向鉴权为 403，运行前后 `inflight_requests=0`，输出不含 ASR 文本、回复文本或任何 key/token。
- 公网形态验收参数已本机模拟验证：`acceptance_test.ps1 -SkipDocker -SkipHermesApi -SkipServiceHealth -SkipRealAsr` 只依赖 `/v1/watch/*` 设备入口，返回 `status=passed` 且 `service_health_skipped=true`。
- 设备 V1 契约一致性已验证：`tests/test_contract.py` 将 `watch_contract.v1.json` 的 7 字段响应、status/action 枚举、`request_id` 正则、最大音频大小、115 秒服务器预算和 `/v1/watch/*` 公网范围锁定到 `app.py`；server pytest 当前 13 项通过。

期望看到的结果：

- context 检查无错误警告。
- Hermes API Server 有服务监听，`/health` 可访问。
- `/v1/responses` 能同步返回 Hermes + MiMo 的最终文本。
- watch voice endpoint 能把 mock Ogg Opus 上传转换成手表 V1 7 字段 JSON。
- 新页面不改变现有 AI 页面进入/退出语义。
- 新 service 不直接操作 LVGL 对象，不绕过 `audio_codec` session。
- 新 service 使用 `AUDIO_CODEC_OWNER_HERMES`，不复用 `AUDIO_CODEC_OWNER_OFFICIAL_CHAT`。
- UI 不直接执行 HTTP、ASR、音频 session 或长阻塞等待。
- 取消等待后 UI 能回到 ready；服务器 cancel 失败不把 UI 卡死。

## Idempotence and Recovery

- 如果中途中断，下次从本计划继续，先补 `memory_watch_service` command/snapshot 草案和 V1 source tests。
- 如果 `server/watch_voice_endpoint` 原型出错，回退时只停止/删除该服务或容器；不要改动 `hermes` Dashboard、MiMo 配置、`official_chat` 主线或 ESP32 UI。
- 如果新页面方案失败，最小回退路径是只删除新增页面入口和新 service，不影响 `official_chat_service` 与现有 AI 页。

## Next Step

- 将 `server/watch_voice_endpoint` 接到真实 ASR adapter，保持外层 V1 JSON 不变。
- 用 ESP32-S3 实机麦克风 Ogg Opus 样本验证 `WATCH_ASR_PROVIDER=mimo`，确认手表端实际 MIME、音量、时长、语言和错误路径。
- 将本机 `127.0.0.1:8787` 联调入口按需要放到服务器域名后，例如反向代理到 `/v1/watch/*`，再增加 TLS 与公网访问控制。
- 再写 `memory_watch_service` 的 public header 草案、source test 和页面 wireframe。
- 继续把 `memory_watch_service` 从 skeleton 推进到真实上传：把 `memory_watch_recorder` 的 Ogg Opus 输出接入 HTTP multipart client，仍不复用 `official_chat` 主线；接入时不得让 service owner task 长时间阻塞导致 cancel command 无法消费，应使用 recorder worker task 或 owner 可观测的 stop flag。
- 固件侧只接 voice endpoint，不接 Hermes Dashboard，不保存 Hermes API key。
