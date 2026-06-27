---
id: context-current-task
tags: context, handoff, current-task, ai-memory-watch, hermes, v1-archive, v2-archive, inbox
summary: 记录 AI Memory Watch / Hermes V1/V2 归档、V2.1 WebSocket 完成、V2.2 前台 WS + 后台 conversation polling 已完成，以及后续 hardening 方向。
last_reviewed: 2026-06-27
memory_type: task
scope: task
owners: docs/context/handoffs
triggers: handoff, current-task, next-step, ai-memory-watch, hermes, watch_voice_endpoint, v1-archive, v2-archive, inbox, websocket, conversation_polling
evidence_level: observed
---

# AI Memory Watch / Hermes 当前任务交接

## 目标

- 当前目标已从“打通 V1 主链路”推进到“V1 已归档 + V2 已归档 + V2.1 WebSocket 完成 + V2.2 前台 WS/后台 conversation polling 完成，后续进入体验 hardening”。
- V1 主链路已完成：`ESP32-S3 真机麦克风 -> Ogg Opus -> watch endpoint -> MiMo ASR -> Hermes -> 手表 V1 固定 7 字段 JSON`。
- V2 主链路已完成：`Hermes/脚本模拟主动提示 -> watch endpoint inbox -> SQLite -> 公网 GET 读回 -> ESP32 收件箱/全局气泡能力`。
- V2.1 主链路已完成：前台 Hermes 页面使用 `WSS /v1/watch/ws` 上传 Ogg Opus、ASR 先显用户消息、assistant reply 后显 Hermes 消息。
- V2.2 服务器与固件骨架已完成：前台 Hermes 页面仍用 WS，离页 pending 时关闭 WS，后台 HTTP `GET /v1/watch/conversation` 每 5 秒轮询取回结果。
- V2.2 最新真机反馈：前台 Hermes 页面可正常使用，离开 Hermes 页面后也能通过气泡收到 Hermes 回复；同一回复重复显示的问题已在代码侧修复并经用户复测确认当前没有问题。
- 不要再相信旧 handoff 里“真机按住说话未验证”“板端缺 endpoint 配置导致无法验证”的状态；这些已经被 2026-06-17 之后的证据反转。
- 不要再相信旧状态里“V2 通知箱尚未实现代码”或“需要新建 V2 计划”的表述；`2026-06-25-hermes-inbox-global-notification-plan.md` 已在 `completed/` 归档。

## 当前状态

- 当前分支：`codex/ai-memory-watch-hermes-api`。
- 主计划 `2026-06-05-ai-memory-watch-hermes-page-plan.md` 已从 `plans/active/` 移到 `docs/context/plans/completed/`，状态为 `archived`。
- SoftAP/NVS 配置计划 `2026-06-17-ai-memory-watch-softap-nvs-config-plan.md` 已在 `docs/context/plans/completed/` 归档。
- V2 收件箱与全局气泡计划 `2026-06-25-hermes-inbox-global-notification-plan.md` 已在 `docs/context/plans/completed/` 归档，状态为 `archived`。
- Hermes API Server、watch voice endpoint、Cloudflare Tunnel、公网 `watch.934000.xyz/v1/watch/*`、文本命令和真实麦克风语音链路均已有成功证据。
- 当前 watch endpoint 容器已重建到 V2.2 server 代码，本机 `127.0.0.1:8787/health` healthy，公网 `watch.934000.xyz` runtime gate、WS smoke、conversation polling smoke 均通过。
- 新版 watch endpoint 在本机 `127.0.0.1:8787` 暴露 `/v1/watch/inbox` 和 `/v1/watch/inbox/{notification_id}/read`；旧 LAN 调试容器 `8788` 曾无 inbox，后续调试优先走公网或新版 `8787`。
- 开发阶段允许本机 `sdkconfig` 或 NVS 持有 watch device token 进行联调；提交前必须确认 `sdkconfig`、文档、日志和 diff 不包含真实 token。

## Progress

- V1 独立 Hermes 页面已落地，不复用 `official_chat`。
- V1 服务器侧 watch endpoint 已支持 health、voice-command、text-command、cancel、request 幂等、auth 诊断、运行态指标、115 秒服务器预算和公网私有路径门禁。
- V1 固件侧已支持 `memory_watch_service` owner task、upload/health/cancel worker、Ogg Opus recorder、voice client、text command、NVS 配置读取/保存、SoftAP 配置入口和 Kconfig 开发默认项。
- `mw_upload` 实机栈问题已修复：upload worker 栈迁移到 PSRAM 并提升到 `24576` words，大 `job/result` 对象移出任务栈。
- FreeRTOS queue copy 后的 `client_config` 指针 rebind 已修复：upload/health/cancel worker 收到 job 后重新绑定指针到 job 内部字符数组。
- COM3 真机麦克风链路已成功：串口 high-water mark 约 `3248` words，返回 `status=done/action=memory_saved/error_code=none`；服务器 `/health` 最近请求摘要显示真实 Ogg Opus、`asr_provider=mimo` 和成功耗时，不包含正文或 token。
- `system_time_sync` 临时网络同步 HTTP 任务栈外移至 PSRAM：解决设备开机连网时 SRAM 连续碎片不足报 `create network time sync task failed`（返回 `pdFAIL`）的崩溃。
- `memory_watch_controller` 渲染快照引入 `inbox_generation` 数据版本跟踪：解决后台短消息轮询到达时页面静默死锁不刷新的 Bug。
- `memory_watch_service` 收件箱大段 PSRAM 内存拷贝脱离 `portENTER_CRITICAL` 自旋锁：引入互斥锁 `s_inbox_store_mutex` 替换硬件自旋锁 `s_worker_lock`，消除 PSRAM 慢速访问引发的 CPU 双核中断屏蔽与 Cache 异常。
- V2 公网脚本验收已通过：脚本模拟 Hermes 写入一条 inbox 消息，公网 GET 成功读回创建项，摘要为 `201 Created`、`found_created_item=true`、`unread_count=2`；未记录真实 token/key。
- V2.2 公网脚本验收已通过：`conversation_polling_smoke_test.ps1 -BaseUrl "https://watch.934000.xyz"` 证明 WS 发完音频断开后，HTTP conversation polling 可拉到 user message 和 assistant `done` reply。
- V2.2 真机反馈已证明前台 WS 和离页气泡链路可用；重复回复修复为后台 polling terminal reply 标记 `conversation_already_appended`，避免 server conversation 已合并后 worker done 二次 append 到本地对话；用户复测确认当前无问题。

## Decision Log

- V1 到此停止加功能；后台通知、TTS、历史列表、长任务主动反馈都放到 V2。
- V2 当前定义固定为：`Hermes 主动提示回到手表：server inbox + ESP32 收件箱 + 全局气泡通知`，并已归档完成。
- ESP32-S3 只调用 watch endpoint，不直接调用 Hermes Dashboard、Hermes API Server 或 MiMo API。
- Hermes/MiMo/API key 只保留在服务器或仓库外 env；ESP32 固件最多保存 watch endpoint 的 `device_id/device_token/base_url`。
- 公网第一版只允许代理 `/v1/watch/*`；Hermes `8642` 和 Dashboard `9119` 保持私有。
- 开发阶段可把 watch device token 放本机 `sdkconfig`，但不得提交；正式/演示前建议轮换 token。
- V2.2 当前定稿：前台 Hermes 页面 `WS` 实时；离开 Hermes 页面后如果有 pending，关闭 WS 并通过 HTTP conversation polling 取回；无 pending 时只保留 inbox 低频轮询。

## 已验证

- 服务器 release gate、mock/real ASR smoke、cancel、invalid-token 403、Cloudflare 私有路径门禁均已通过过。
- 公网 `https://watch.934000.xyz/v1/watch/health` 可用，公网 `/health`、`/v1/models`、`/v1/responses` 不公开。
- 真机文本命令和真机麦克风 Ogg Opus 端到端链路均已成功。
- context 校验在最近文档更新中多次通过；V1 归档后仍需再跑一次 standard 校验。
- 最新 V2.2 验证：server pytest `91 passed`，Memory Watch source tests `39 passed`，`idf.py build` 通过，`idf.py -p COM3 app-flash` 通过，30 秒启动 smoke 通过，公网 private exposure gate/WS smoke/conversation polling smoke 通过。

## 当前风险

- 工作区有大量已有未提交改动，且 `sdkconfig` 现在可能含开发期 watch device token；不要误提交。
- `docs/context/handoffs/current-task.md` 是当前接力页，不是历史总账；历史细节看 changelog 和 completed plan。
- 最新用户日志中的问题不是 Hermes 链路问题。第一轮是点击主界面 Bluetooth 后普通 BLE presence 进入 BT controller 初始化时 internal heap 不足触发 `BLE_INIT: Malloc failed` / `emi.c` assert / interrupt WDT；已加 `ble_presence` preflight 和 `network_manager` 回滚保护。第二轮复测不再崩溃，但 BLE enabled 偏好在开机 latest Wi-Fi 路径自动启动普通 BLE，抢占 LVGL/SPI DMA internal RAM，导致 display bounce / flush `ESP_ERR_NO_MEM`；已改成后台路径只收口不自动启动，只有用户显式 Bluetooth 开关才允许启动 BLE。第三轮冷启动复测已通过：未自动 BLE advertising，display bounce buffer 分配成功，Wi-Fi 到 `SERVICE_READY`，Hermes health online。第四轮手动连续点击 Bluetooth 复测也已通过防护目标：internal heap 约 `30 KiB`、最大连续块约 `14 KiB` 时稳定返回 `ESP_ERR_NO_MEM` 并显示失败 toast，无 `emi.c`、Guru、interrupt WDT 或显示链路回退。
- V2.2 离页 pending 主链路和重复回复修复均已由用户真机反馈确认可用；后续主要风险转为体验细节、异常路径和低功耗参数，不再是主链路可用性。
- app 分区余量曾接近 4%，后续新增 V2.1/V3 功能前要继续关注二进制体积。

## 下一步

- 跑 V2.2 收尾后的 context 校验：`uv run python scripts/context/validate_context.py --level standard --q "AI Memory Watch Hermes V2.2 foreground websocket background conversation polling" --brief`。
- 提交前检查密钥卫生：`sdkconfig`、`memory_watch_dev_endpoint_local.h`、日志和文档都不能带真实 token/key。
- 如果继续做 V2.2 hardening：优先使用 COM3 `app-flash-monitor`，观察 `conversation`、`voice-ws`、`watch request result`、`reply arrived`，确认异常路径无 Guru/panic/stack overflow/URL 乱码。
- 如果继续 BLE 问题，下一步不是放宽 guard，而是单独做 internal RAM 预算收敛；当前已证明普通运行态下手动 Bluetooth 会因 internal heap 不足 fail closed。
- V2.2 剩余不是重新设计通讯，而是真机体验验收和必要 hardening；不要回退到“离页保持 WS 等最终回复”的旧口径。

## 证据入口

- V1 主计划归档：`docs/context/plans/completed/2026-06-05-ai-memory-watch-hermes-page-plan.md`
- SoftAP/NVS 计划归档：`docs/context/plans/completed/2026-06-17-ai-memory-watch-softap-nvs-config-plan.md`
- V2 收件箱与全局气泡计划归档：`docs/context/plans/completed/2026-06-25-hermes-inbox-global-notification-plan.md`
- 服务器目录：`server/watch_voice_endpoint/`
- 机器可读契约：`server/watch_voice_endpoint/watch_contract.v1.json`
- 产品定位：`docs/context/knowledge/project/ai-memory-watch-product-positioning.md`
- 变更记录：`docs/context/CHANGELOG.md`
