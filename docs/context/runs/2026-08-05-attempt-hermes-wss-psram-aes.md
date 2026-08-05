---
id: attempt-2026-08-05-hermes-wss-psram-aes
tags: hermes, websocket, psram, internal-ram, esp-aes
summary: Hermes WSS 接收栈 PSRAM 与 AES 内存峰值；结果：partial。
last_reviewed: 2026-08-05
memory_type: episodic
scope: task
status: completed
result: verified
owners: components/official_chat/net/esp_ssl.cc, main/services/official_chat_service.c, main/services/memory_watch/memory_watch_service.c, tests/test_official_chat_ram_alignment_source.py
triggers: official_ssl failed to create ssl receive task, esp-aes Failed to allocate memory
evidence_level: observed
record_reasons: error-signature, high-cost, evidence
force_reason: 
---

# Attempt Log: Hermes WSS 接收栈 PSRAM 与 AES 内存峰值

## 背景

- 本次要验证什么：修复 Memory Watch 前台 WSS 创建 oc_ssl_rx 失败，并验证板端链路
- 对应任务或计划：docs/context/plans/active/2026-08-04-hermes-music-control-mcp-plan.md
- 结果状态：partial
- 长期记录理由：error-signature, high-cost, evidence

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：COM7 ESP32-S3
- 关键前置条件：PSRAM 8MB，WebSocket 使用 official_chat EspSsl，当前固件 internal RAM 紧张

## 操作

- 修改过的文件或 owner：
- components/official_chat/net/esp_ssl.cc
- tests/test_official_chat_ram_alignment_source.py
- 执行的命令或动作：
- app-flash-monitor COM7 60s
- 已尝试但不应直接重复的路径：
- 不要把 official_chat_service 栈迁到 PSRAM；其 flash mmap/cache 冻结路径要求 internal RAM。
- 将 official chat 文本/消息历史与 Memory Watch owner/worker scratch、inbox pending-read 迁到显式 PSRAM；保留 internal 栈、FreeRTOS 控制块、DMA、Wi-Fi/NimBLE 和 ESP-DL。

## 观测

- 关键日志/证据：
- board_logs/2026-08-05-06-48-51-ssl-psram-stack-fix.log: WSS connected at 18723ms; internal_free=2327B largest=1152B; esp-aes failed at 21073ms。
- 链接器复核：internal `.bss` 从 48984B 降至 42176B；`official_chat_service` static RAM 从 2857B 降至 341B，`memory_watch_service` 从 6963B 降至 2663B。
- board_logs/2026-08-05-07-30-03-psram-static-audit.log: cold snapshot internal_free=9027B largest=8704B；录音期 internal_free=9363B；WSS connected -> voice-ws-done -> conversation_reply，未出现 esp-aes / panic / Guru / WDT / stack overflow。
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：oc_ssl_rx 改用 PSRAM 栈后 WSS 预连接成功；随后只迁移普通任务上下文中的长期业务缓存，恢复 AES DMA 所需的 internal 连续块，真实 WSS 语音上传已成功。
- 仍然不能确认的事实：
- 尚未量化 AES DMA 的精确最小连续块阈值；本轮不将 WSS 语音成功外推为 Hermes 音乐 MCP 的真实点歌验收。

## 未验证风险

- 下一轮仍需补证据的边界：
- 若将来再次出现 AES 分配失败，先采集 `internal_free/largest` 和动态 owner 生命周期；不要回退到把 official_chat_service 栈迁入 PSRAM。
