---
id: context-current-task
tags: context, handoff, current-task, ai-memory-watch, hermes, v1-archive
summary: 记录 AI Memory Watch / Hermes V1 已完成归档后的当前状态、剩余 hardening 风险和 V2 下一步。
last_reviewed: 2026-06-20
memory_type: task
scope: task
owners: docs/context/handoffs
triggers: handoff, current-task, next-step, ai-memory-watch, hermes, watch_voice_endpoint, v1-archive
evidence_level: observed
---

# AI Memory Watch / Hermes 当前任务交接

## 目标

- 当前目标已从“打通 V1 主链路”切换为“V1 收尾归档 + hardening + V2 通知箱设计”。
- V1 主链路已完成：`ESP32-S3 真机麦克风 -> Ogg Opus -> watch endpoint -> MiMo ASR -> Hermes -> 手表 V1 固定 7 字段 JSON`。
- 不要再相信旧 handoff 里“真机按住说话未验证”“板端缺 endpoint 配置导致无法验证”的状态；这些已经被 2026-06-17 之后的证据反转。

## 当前状态

- 当前分支：`codex/ai-memory-watch-hermes-api`。
- 主计划 `2026-06-05-ai-memory-watch-hermes-page-plan.md` 已从 `plans/active/` 移到 `docs/context/plans/completed/`，状态为 `archived`。
- SoftAP/NVS 配置计划 `2026-06-17-ai-memory-watch-softap-nvs-config-plan.md` 已在 `docs/context/plans/completed/` 归档。
- Hermes API Server、watch voice endpoint、Cloudflare Tunnel、公网 `watch.934000.xyz/v1/watch/*`、文本命令和真实麦克风语音链路均已有成功证据。
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

## Decision Log

- V1 到此停止加功能；后台通知、TTS、历史列表、长任务主动反馈都放到 V2。
- ESP32-S3 只调用 watch endpoint，不直接调用 Hermes Dashboard、Hermes API Server 或 MiMo API。
- Hermes/MiMo/API key 只保留在服务器或仓库外 env；ESP32 固件最多保存 watch endpoint 的 `device_id/device_token/base_url`。
- 公网第一版只允许代理 `/v1/watch/*`；Hermes `8642` 和 Dashboard `9119` 保持私有。
- 开发阶段可把 watch device token 放本机 `sdkconfig`，但不得提交；正式/演示前建议轮换 token。

## 已验证

- 服务器 release gate、mock/real ASR smoke、cancel、invalid-token 403、Cloudflare 私有路径门禁均已通过过。
- 公网 `https://watch.934000.xyz/v1/watch/health` 可用，公网 `/health`、`/v1/models`、`/v1/responses` 不公开。
- 真机文本命令和真机麦克风 Ogg Opus 端到端链路均已成功。
- context 校验在最近文档更新中多次通过；V1 归档后仍需再跑一次 standard 校验。

## 当前风险

- 工作区有大量已有未提交改动，且 `sdkconfig` 现在可能含开发期 watch device token；不要误提交。
- `docs/context/handoffs/current-task.md` 是当前接力页，不是历史总账；历史细节看 changelog 和 completed plan。
- V1 hardening 仍可补异常路径验证：短音频、长音频、取消等待、token 错误、Hermes 离线、HTTPS 超时、late result 忽略。
- app 分区余量曾接近 4%，后续新增 V2 功能前要继续关注二进制体积。

## 下一步

- 跑 V1 归档后的 context 校验：`uv run python scripts/context/validate_context.py --level standard --q "AI Memory Watch Hermes V1 archived" --brief`。
- 提交前检查密钥卫生：`sdkconfig`、`memory_watch_dev_endpoint_local.h`、日志和文档都不能带真实 token/key。
- 如果做最终 V1 smoke：先 `idf.py fullclean && idf.py build`，再 `idf.py -p COM3 app-flash`，验证 health online 和一次真机按住说话成功。
- 新建 V2 计划时，从“服务器通知箱 + 手表低功耗短轮询 + 完成反馈 UI”开始，不要重开 V1 主计划。

## 证据入口

- V1 主计划归档：`docs/context/plans/completed/2026-06-05-ai-memory-watch-hermes-page-plan.md`
- SoftAP/NVS 计划归档：`docs/context/plans/completed/2026-06-17-ai-memory-watch-softap-nvs-config-plan.md`
- 服务器目录：`server/watch_voice_endpoint/`
- 机器可读契约：`server/watch_voice_endpoint/watch_contract.v1.json`
- 产品定位：`docs/context/knowledge/project/ai-memory-watch-product-positioning.md`
- 变更记录：`docs/context/CHANGELOG.md`
