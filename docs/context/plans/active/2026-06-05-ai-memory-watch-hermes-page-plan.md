---
id: 2026-06-05-ai-memory-watch-hermes-page-plan
tags: plan, active, ai-memory-watch, hermes, voice, ui, official-chat, owner
summary: 将 AI Memory Watch 作为独立 Hermes 功能页面推进，固定 V1 触摸录音、Ogg Opus 一次性 HTTP 上传、服务器侧 ASR/Hermes 处理和当前 owner 边界。
created: 2026-06-05
last_updated: 2026-06-05
last_reviewed: 2026-06-05
status: active
owners: docs/context/plans/active/2026-06-05-ai-memory-watch-hermes-page-plan.md, docs/context/knowledge/project/ai-memory-watch-product-positioning.md
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
- V1 原型允许 Hermes 执行外部工具；ESP32 不做工具白名单判断。
- 外部工具失败时，手表只显示简短失败，详细错误写服务器日志。
- Hermes watch-specific instructions 必须固定：输入来自手表短语音；优先识别记忆、提醒、问答或工具动作；回复适合小屏；需要追问时一次只问一个问题。

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
- `[ ]` 落地页面与 service skeleton。

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

期望看到的结果：

- context 检查无错误警告。
- 新页面不改变现有 AI 页面进入/退出语义。
- 新 service 不直接操作 LVGL 对象，不绕过 `audio_codec` session。
- 新 service 使用 `AUDIO_CODEC_OWNER_HERMES`，不复用 `AUDIO_CODEC_OWNER_OFFICIAL_CHAT`。
- UI 不直接执行 HTTP、ASR、音频 session 或长阻塞等待。
- 取消等待后 UI 能回到 ready；服务器 cancel 失败不把 UI 卡死。

## Idempotence and Recovery

- 如果中途中断，下次从本计划继续，先补 `memory_watch_service` command/snapshot 草案和 V1 source tests。
- 如果新页面方案失败，最小回退路径是只删除新增页面入口和新 service，不影响 `official_chat_service` 与现有 AI 页。

## Next Step

- 先写 `memory_watch_service` 的 public header 草案、source test 和页面 wireframe，再开始落源码。
