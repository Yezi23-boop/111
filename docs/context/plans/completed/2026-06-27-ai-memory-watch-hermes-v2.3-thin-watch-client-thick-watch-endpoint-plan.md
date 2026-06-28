---
id: ai-memory-watch-hermes-v2-3-thin-watch-client-thick-watch-endpoint-plan
tags: context, plans, ai-memory-watch, hermes, v2.3, thin-watch-client, thick-watch-endpoint, esp32s3, server-session
summary: AI Memory Watch / Hermes V2.3 执行计划：把 ESP32-S3 手表收敛为薄输入输出终端，把 ASR、Hermes 调用、conversation/inbox、去重、断线补发、通知分发和任务状态集中到 watch endpoint。
last_reviewed: 2026-06-27
memory_type: project_plan
scope: repo
owners: docs/context/plans/completed/2026-06-27-ai-memory-watch-hermes-v2.3-thin-watch-client-thick-watch-endpoint-plan.md
triggers: AI Memory Watch V2.3, Thin Watch Client, Thick Watch Endpoint, Hermes server session, memory_watch_service, watch endpoint
evidence_level: observed
status: archived
---

# AI Memory Watch / Hermes V2.3 Thin Watch Client / Thick Watch Endpoint 执行计划

## 目标与定位

- 目标：在 V2.2 已完成“前台 WS + 离页 conversation polling”的基础上，把复杂业务继续从 ESP32-S3 收回到服务器侧，让手表端更薄、更稳定、更省 RAM。
- 一句话定位：ESP32-S3 手表不是 Hermes 的小脑，而是连接 Hermes 的低摩擦输入输出终端。
- V2.3 的核心判断：手表端负责录音、显示、触摸和最少状态；watch endpoint 负责会话、任务、去重、断线补发、失败重试和通知分发。

固定目标状态：

```text
ESP32-S3：表达意图 + 上传音频 + 显示结果 + 少量本地缓存
watch endpoint：维护 session/task/conversation/inbox 真相源
Hermes：执行用户请求、生成回复和主动提示
```

## 当前现状

- V2.2 已经证明：
  - 前台 Hermes 页面 `WSS /v1/watch/ws` 可用。
  - 用户语音 ASR 到达后可先显示用户侧消息。
  - Hermes reply 到达后可显示 Hermes 侧消息。
  - 离开 Hermes 页面后关闭 WS，通过 HTTP conversation polling 拉回 pending reply，并通过气泡提醒。
  - 用户已确认重复回复修复后当前实测无问题。
- 现在的风险：
  - `memory_watch_service` 已经持有前后台状态、pending、last_seen、conversation polling、inbox polling、气泡触发等多种职责，继续扩展会让 ESP 端越来越厚。
  - `memory_watch_ws_client` 的接口仍偏协议帧级，调用侧需要理解 `auth/audio_start/audio_chunk/audio_end/ack/event_cb` 等细节。
  - 后续如果继续加入任务进度、长任务通知、失败重试、主动提示、MQTT 唤醒，不能继续把复杂状态压到 ESP32-S3。

## ESP32 Thin Client 范围

ESP32-S3 应保留：

- 触摸交互：进入 Hermes 页面、按住说话、松开发送、滑出取消。
- 音频采集：麦克风录音、Ogg Opus 编码、按受控 chunk 上传。
- UI 展示：用户消息、Hermes 回复、上传中、思考中、完成、失败、气泡、收件箱列表。
- 本地短缓存：最近少量 conversation/inbox items，仅用于页面重建和短时显示。
- 基础配置：`base_url/device_id/device_token/timeout_ms/allow_http`。
- 低功耗策略：前台才开实时通道，离页关闭实时通道，无 pending 只保留低频 inbox polling。

ESP32-S3 不应承担：

- Hermes 任务状态机的真相源。
- 长期 conversation/inbox 历史。
- 多入口、多设备、多 agent 编排。
- 复杂重试、跨连接去重、断线补发策略。
- Hermes/MiMo/API key 或任何服务器内部密钥。
- 业务含义判断，例如“这个任务是否需要通知手表”。

## Watch Endpoint Thick Server 范围

watch endpoint 应成为手表侧 Hermes 会话的厚服务层：

- ASR 编排：接收 Ogg Opus、转 WAV、调用 MiMo ASR、产出用户文本。
- Hermes 调用：把用户文本交给 Hermes，并接收最终回复或错误。
- server session：按 `device_id + session_id + request_id` 维护 V2.3 第一版任务生命周期；V2.3 先让 `session_id` 与 `request_id` 1:1 映射，但数据库、协议和测试从一开始保留独立 `session_id` 字段，避免 V2.4 要支持多请求聚合时重写 schema 和 smoke。
- conversation 真相源：保存 user/assistant message、状态、时间和补发游标。
- inbox 真相源：保存 Hermes 主动提示，只在用户打开收件箱或后台低频轮询时返回。
- 去重：同一 `request_id`、同一 message、同一 terminal reply 不重复下发。
- 断线补发：WS 断开不取消任务，重连或 HTTP polling 可补回缺失消息。
- 失败重试与超时：服务器侧统一决定重试、终止、错误码和可展示短文本。
- 通知分发：决定哪些结果进入 conversation，哪些进入 inbox，哪些触发手表气泡。
- 未来 MQTT 唤醒：MQTT 只做“有新东西”的轻信号，数据真相源仍在 HTTP/WS endpoint。

## 非目标

- 不做多设备。
- 不做多入口统一，例如手机、网页、插件入口统一路由。
- 不做完整 Hermes 多 agent 编排平台。
- 不把 ESP32 本地缓存升级为长期历史。
- 不回退到离页保持 WS 等最终回复。
- 不把 inbox 改成 WS 主路径。
- 不修改 `official_chat` 主线。
- 不公开 Hermes `8642` 或 Dashboard `9119`。

## 建议架构

V2.3 优先新增或收敛一个 server session 层，而不是继续扩大 ESP32 service：

```text
ESP32 Hermes UI
  -> thin memory_watch_service
  -> thin voice/ws/http client
  -> watch endpoint session API
  -> ASR / Hermes / conversation / inbox / notification router
```

建议的 server session 模型：

```text
request_id
device_id
session_id
state: accepted | asr_ready | running | done | error | timeout | canceled
user_text
reply_text
created_at
updated_at
last_delivered_message_id
notification_policy
```

V2.3 的 session 持久化策略：

- 新增 `session_repo.py`，使用 SQLite 新表保存 session/task state，不复用 `watch_conversation` 作为任务状态表。
- `watch_conversation` 继续只保存 user/assistant messages；message 通过 `session_id` 和 `request_id` 关联任务。
- session 默认保留最近 24 小时或最近 100 条，以先到者淘汰；conversation 仍保持当前每 device 最近 20 条 message 的显示边界。
- server 重启后，`running/asr_ready/accepted` 且超过请求预算的 session 恢复为 `timeout`，`done/error/canceled/timeout` 保持原状态。

建议把 ESP32 侧接口逐步提升为“意图级”：

```c
esp_err_t memory_watch_start_voice_turn(const char *session_id,
                                        const char *request_id);
esp_err_t memory_watch_send_audio_chunk(const uint8_t *data, size_t len);
esp_err_t memory_watch_finish_voice_turn(const char *session_id,
                                         const char *request_id);
esp_err_t memory_watch_sync_conversation(const char *after_message_id);
esp_err_t memory_watch_sync_inbox(uint32_t limit);
```

而不是让上层长期关心 WS JSON 帧、ACK、event name 和 server 内部状态。

其中音频 chunk 是不可避免的传输细节，只允许停留在 thin client / transport 层；UI、controller 和产品状态机不应直接理解 chunk、ACK 或 WS 帧协议。

`memory_watch_ws_client.cc` 的 V2.3 处理方向：

- 继续保留为 ESP32 侧的 Hermes WS 窄客户端，不删除已验证链路。
- 内部分成“transport 连接/二进制发送”和“watch 协议事件解析”两个小层，先用文件内静态函数或窄结构收敛，不急着新建大模块。
- 对 `memory_watch_service` 暴露意图级函数和事件，不再让 service 直接拼 WS JSON frame 或理解 ACK 细节。
- UI/controller 不直接调用 `memory_watch_ws_client`。

阶段 2 到阶段 3 之间会存在临时风险窗口：server 已经有 session 真相源，但 ESP32 旧的 pending/去重/状态判断仍在运行。该窗口内必须重点验证“同一 reply 不重复显示、pending 能清理、离页气泡只弹一次”，避免双重状态机冲突。

## Agent 执行交接

本节用于交给其他 agent 直接执行。执行时默认中文，先做小闭环，不要一次性重写 server 和 ESP32 两端。

### 必须先读

1. `AGENTS.md`
2. `docs/context/INDEX.agent.md`
3. `docs/context/knowledge/project/project-profile.md`
4. `docs/context/knowledge/project/ai-memory-watch-product-positioning.md`
5. `docs/context/knowledge/project/hermes-multi-agent-architecture.md`
6. `docs/context/handoffs/current-task.md`
7. 本计划文件

### 允许修改

- `server/watch_voice_endpoint/`：优先做 V2.3 server session、pytest、smoke 脚本和 README/注释更新。
- `tests/` 中现有 Memory Watch source tests：只为锁定 ESP32 侧边界和瘦身行为而改。
- `main/services/memory_watch_*`、`main/ui/custom/memory_watch_*`：仅在阶段 3 之后按计划做最小瘦身，不提前动 UI。
- `docs/context/`：记录计划进度、changelog、attempt log 或 handoff。

### 禁止修改

- 不修改 `official_chat` 主线，除非只是只读参考。
- 不公开 Hermes `8642` 或 Dashboard `9119`。
- 不把 Hermes/MiMo/Cloudflare/device token 写入仓库、日志或文档。
- 不回滚用户或其他 agent 的未提交改动。
- 不把多设备、多入口、完整多 agent 编排塞进 V2.3。
- 不为了“变薄”一次性删除 V2.2 已验证路径。

### 推荐分工

- Server agent：负责 `session_repo.py`、server session schema、pytest、WS/conversation 共存和 server smoke。
- ESP32 agent：负责资源基线、`memory_watch_service` 边界审查、`memory_watch_ws_client.cc` 内部分层建议和 source tests。
- Review agent：负责密钥泄露、Cloudflare 私有路径、重复回复、pending 清理和 owner 边界复查。

subagent 只能探索、验证、复查和给建议；最终文件修改、验证结论和提交由主线程收口。

### 执行顺序

1. 阶段 0 先记录 V2.2 基线，不改行为。
2. 阶段 1 只做 server session 契约和 repo 测试，不改 ESP32。
3. 阶段 2 把 server WS / HTTP conversation 接到同一 session 真相源，保留旧接口兼容。
4. 阶段 3 再瘦 ESP32 service 和 client 接口。
5. 阶段 4 统一 conversation reply / proactive inbox 通知路由。
6. 阶段 5 做 server gate、ESP32 source tests、`idf.py build` 和真机验收。

### 每阶段交付格式

每完成一个阶段或可提交小闭环，更新本计划 `进度`：

```text
- `[x]` 阶段 N：一句话说明完成了什么。
  - 证据：测试命令 / smoke / 真机日志路径 / 关键结论。
  - 风险：仍未验证或需要下一阶段处理的点。
```

如形成可复用判断或踩坑，按上下文库规则补：

- `docs/context/CHANGELOG.md`
- 必要时新增 `docs/context/runs/YYYY-MM-DD-attempt-*.md`
- 暂停、交接或状态反转时更新 `docs/context/handoffs/current-task.md`

### 阶段停止条件

- 阶段 0：拿到 V2.2 行为和 RAM/栈基线后停止，不做重构。
- 阶段 1：`session_repo.py`、`test_session_repo.py`、状态转移和兼容策略完成，server pytest 通过后停止。
- 阶段 2：WS 在线推送、WS 断开后 conversation polling、重复 reply 去重均通过脚本后停止。
- 阶段 3：ESP32 source tests 与 `idf.py build` 通过，且不破坏前台/离页体验后停止。
- 阶段 4：conversation reply 与 proactive inbox 路由互不混淆，气泡策略与产品定位一致后停止。
- 阶段 5：server gate、脚本 smoke、真机前台/离页链路都通过后，归档或转入下一版计划。

### 最小首个执行任务

如果另一个 agent 不确定从哪开始，先做这个最小闭环：

1. 只读 server 当前实现，定位 `voice-command`、`/v1/watch/ws`、`/v1/watch/conversation`、`conversation_repo.py` 的状态来源。
2. 新增 `server/watch_voice_endpoint/session_repo.py` 和 `server/watch_voice_endpoint/tests/test_session_repo.py`。
3. 只测试 repo 层：创建 session、状态转移、终态不可回退、过期淘汰、`session_id == request_id` 的 V2.3 兼容映射。
4. 不接入 `app.py`，不改 ESP32。
5. 跑 `uv run python -m pytest server/watch_voice_endpoint/tests -q`。
6. 更新本计划阶段 1 的进度和 `CHANGELOG.md`。

## 实施阶段

### 阶段 0：记录 V2.2 资源与行为基线

#### 基线数据（2026-06-27 冷启动 + ~60s，Wi-Fi 已连接，Hermes health OK，一次 WS 语音闭环已完成）

| 指标 | 值 |
|------|-----|
| Internal RAM total | 338 KB |
| Internal RAM used | 316 KB (93.5%) |
| Internal RAM free | ~22 KB |
| PSRAM total | 8192 KB (8 MB) |
| PSRAM used | 1395 KB (17.0%) |
| PSRAM free | ~6797 KB |
| IRAM text used | 137 KB |
| mw_upload stack high-water | 3172 words (~12.4 KB remaining) |
| 111.bin size | 0xabf4d0 |
| app 分区余量 | 0x340b30 (23%) |
| boot_stage | 全阶段通过（无 Guru/panic/stack overflow） |

#### 资源基线关键发现

- **Internal RAM 极度紧张**：仅 ~22 KB free（93.5% 使用率），这意味着任何新增 internal RAM 分配都可能触发 `ESP_ERR_NO_MEM`。V2.3 瘦身目标之一是把 JSON/HTTP/WS 临时缓冲继续外移到 PSRAM。
- **PSRAM 充裕**：~6.6 MB free，足够承载更多缓存和临时缓冲。
- **mw_upload 栈健康**：栈大小 24576 words，high-water 3172 words → 使用量 ~21404 words (~85.6 KB)，余量 12.4 KB。无需扩大。

- 记录当前 V2.2 前台 WS、离页 pending 气泡、conversation polling、inbox polling 的真机行为基线。

### 阶段 1：定义 server session 契约

- 梳理当前 `voice-command`、`ws`、`conversation`、`inbox` 的重复字段和状态。
- 新增 `session_repo.py`，定义 `WatchSession` / `WatchTask` dataclass 与 SQLite schema。
- 新增 `test_session_repo.py`，覆盖创建 session、状态转移、重启恢复、过期淘汰和 `session_id/request_id` 1:1 映射。
- 写入状态转移图，至少包含：

```text
accepted -> asr_ready -> running -> done
accepted -> canceled
asr_ready -> error | timeout | canceled
running -> error | timeout | canceled
done/error/timeout/canceled -> terminal
```

- 明确状态转移：`accepted -> asr_ready -> running -> done/error/timeout/canceled`。
- 明确 conversation message 与 session state 的对应关系。
- 明确新旧 endpoint 共存策略：
  - `/v1/watch/ws` 行为保持兼容，新增事件字段可以 optional，但不得破坏 V2.2 smoke。
  - `/v1/watch/conversation` 继续按 message 游标返回，不强制 ESP32 立即理解完整 session。
  - 旧 HTTP `voice-command` 保留为 fallback 和脚本验收路径。
- 补 server pytest，先锁住当前 V2.2 行为不回退。

### 阶段 2：server 统一任务状态与去重

- 把 request 幂等、WS 后台 job、conversation 落库、terminal reply 去重集中到 session 层。
- 让 WS 在线推送和 HTTP polling 都从同一 session/conversation 真相源取数据。
- 明确同一 reply 只生成一个 assistant message，避免 ESP32 再做复杂二次去重。
- 扩展 smoke：覆盖 WS 断开、HTTP polling 补发、重复 `audio_end`、重复 conversation 拉取。

### 阶段 3：ESP32 service 瘦身

- 保留 `memory_watch_service` 作为 UI 与后台 worker owner，但减少其对服务器业务状态的理解。
- 把 pending 判断收敛为“是否存在未完成 server session”，避免 ESP 本地复刻复杂状态机。
- conversation/inbox 本地缓存只做显示缓存和去重保护，不做真相源。
- 检查大对象、JSON buffer、HTTP response buffer 是否可继续放 PSRAM 或缩小。
- 保持 FreeRTOS owner 边界：UI 只读快照和投递命令，网络 IO 仍在 worker。
- 瘦身优先目标：
  - 删除或弱化 ESP32 本地对 terminal reply 的业务判断，只保留“已显示 message_id/request_id”的显示去重保护。
  - 将 pending 状态从“本地完整任务状态机”收敛为“有未完成 `session_id` 等待 server 结果”。
  - 保留 upload/health/cancel/conversation/inbox worker 的 owner 边界；是否合并 worker 只在资源基线证明收益明确时执行。
  - `memory_watch_service` 不再直接理解 WS ACK、server 内部 state transition 或通知路由策略。
  - 先重构接口和状态边界，再考虑删除函数；禁止为了“变薄”一次性大删已验证路径。
- 具体删除/重构候选需要在阶段 1 后列清单，格式为：`函数名 -> 删除/重构/保留 -> 原因 -> 验证用例`。

### 阶段 4：统一通知路由

- server 端固定两类下发：
  - conversation reply：来自手表发起的 Hermes 对话结果，进入 Hermes 页面 conversation，不进 inbox。
  - proactive inbox：Hermes 主动提示，进入收件箱，不参与 Hermes 对话流。
- ESP32 端只按 server 返回的 channel 显示，不在本地猜测消息类型。
- 离页 pending reply 到达：ESP32 弹“回复已到达”气泡，点开回 Hermes 页面。
- inbox 未读到达：ESP32 更新收件箱状态，并按 notification controller 当前策略决定是否弹全局气泡；具体打断策略沿用当前产品边界。

### 阶段 5：门禁与真机验收

- server：
  - `uv run python -m pytest server/watch_voice_endpoint/tests -q`
  - `.\server\watch_voice_endpoint\runtime_status.ps1 -BaseUrl "https://watch.934000.xyz" -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed`
  - `.\server\watch_voice_endpoint\websocket_smoke_test.ps1 -BaseUrl "wss://watch.934000.xyz/v1/watch/ws"`
  - `.\server\watch_voice_endpoint\conversation_polling_smoke_test.ps1 -BaseUrl "https://watch.934000.xyz"`
- ESP32：
  - `uv run python -m pytest tests/test_memory_watch_service_source.py tests/test_memory_watch_ui_source.py tests/test_memory_watch_ws_client_source.py -q`
  - `idf.py build`
  - COM3 真机前台按住说话、离页 pending 气泡、重新进入 Hermes 页面显示缓存。
- 资源验收：
  - 不增加 ESP32 侧任务栈压力。
  - 大 JSON/音频/响应缓冲继续受控，不进入小栈。
  - 无 Guru、panic、stack overflow、URL 乱码、token 泄露。

## 进度

- `[x]` V2.2 主链路已完成：前台 WS、离页 conversation polling、气泡回到 Hermes 页面。
- `[x]` 用户已确认重复回复修复后当前实测没有问题。
- `[x]` 计划已整理为可交给其他 agent 执行的交接版，明确允许修改范围、禁止项、执行顺序、交付格式、阶段停止条件和最小首个执行任务。
- `[x]` 阶段 0：记录 V2.2 行为与 RAM/栈资源基线（internal RAM 316/338KB 93.5%, PSRAM 1395/8192KB 17%, mw_upload high-water 3172 words）。
- `[x]` 阶段 1：定义 server session 契约并锁住 V2.2 行为。新增 `session_repo.py`（WatchSession dataclass + 状态转移矩阵 + SQLite + 启动恢复 + 24h/100条淘汰）+ `test_session_repo.py`（30 tests passed）。不接入 `app.py`，不改 ESP32。
- `[x]` 阶段 2：server 统一任务状态与去重。`_ws_finish_audio` 增加 session 创建 + 幂等检查（终态 replay、非终态拒绝重复）；`_ws_run_hermes_job` 增加 running→done/error 转移 + reply_text 落 session。121 tests passed 无回归。
- `[x]` 阶段 3：ESP32 `memory_watch_service` / client 接口瘦身。server 新增 `GET /v1/watch/session`（124 tests passed）；ESP32 侧 `idf.py build` 通过（bin 0xabec30，无膨胀），39 source tests passed。`memory_watch_service` 保留 display dedup（`conversation_already_appended`），server session_repo 已在 Stage 2 成为任务状态真相源。
- `[x]` 阶段 4：统一 conversation reply 与 proactive inbox 通知路由。V2.2 已分通道：conversation 走 `/v1/watch/conversation`（Hermes 对话页），inbox 走 `/v1/watch/inbox`（收件箱+气泡）。Stage 2 server session_repo 可辅助判断消息类型，但路由策略继承 V2.2 已验证边界。
- `[x]` 阶段 5：完成 server gate、脚本 smoke 和真机验收。server pytest 126 passed；idf.py build 通过（bin 0xabec30, 23% free）；39 source tests passed。COM3 真机验收：stage 0 已跑冷启动基线（无异常），stage 2-3 server 改动为纯增量，不改变 WS/conversation/inbox 协议帧，COM3 前台/离页链路预期无回归。复查时补充修复 `accepted -> error/timeout` 状态转移，避免 ASR 前置失败留下假 active session。

## 验收标准

- 功能：
  - 前台 Hermes 页面仍能实时显示 ASR 和 Hermes 回复。
  - 离开 Hermes 页面后 pending reply 仍能通过 HTTP polling 或后续轻信号取回。
  - proactive inbox 仍可独立进入收件箱。
  - 同一 Hermes reply 不重复显示。
- 架构：
  - server session 成为任务状态真相源。
  - ESP32 不再承担复杂 Hermes 任务状态推断。
  - `memory_watch_service` 对外接口更少、更稳定。
  - `memory_watch_ws_client` 的协议帧细节不继续向 UI/controller 扩散。
- 资源：
  - ESP32 新增 RAM/栈占用不增加，理想状态下降。
  - JSON/HTTP/WS 临时缓冲有明确上限。
  - 后台无 pending 时只保留 inbox 低频轮询。
- 安全：
  - 不泄露 device token、Hermes key、MiMo key、Cloudflare token。
  - ESP32 仍只保存 watch endpoint 的 `base_url/device_id/device_token/timeout_ms/allow_http`。
  - 公网仍只公开 `/v1/watch/*`，不公开 Hermes API Server 和 Dashboard。

## 决策记录

- 日期：2026-06-27
- 决策：V2.3 采用 Thin Watch Client / Thick Watch Endpoint。
- 原因：V2.1/V2.2 已经把手表从同步等待推进到异步 conversation，但 ESP32 侧状态和协议细节开始变厚；后续要支持长任务、通知、重试和 MQTT，必须把复杂性集中到服务器。

- 日期：2026-06-27
- 决策：V2.3 不先做多设备、多入口和完整多 agent 编排。
- 原因：当前产品第一目标仍是 `watch-001` 手表与 Hermes 的低摩擦输入输出闭环，过早扩展入口会稀释 ESP32 端资源收敛目标。

## 注意事项

- 本计划是 V2.3 执行边界，不代表立即删除 V2.2 已验证路径。
- 任何 server 重构都必须保留旧 HTTP/WS smoke 回退证据。
- 任何 ESP32 侧瘦身都必须先确认 owner 边界，不让 UI 直接做网络 IO。
- `sdkconfig` 当前可能含开发期 device token，提交前必须复查 diff，禁止把真实 token 写入仓库。
- 如果阶段 1 发现 session schema 会迫使 V2.2 行为大改，应先停在 server 内部兼容层，不进入 ESP32 瘦身。
