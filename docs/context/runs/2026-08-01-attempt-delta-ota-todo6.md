---
id: attempt-2026-08-01-delta-ota-todo6
tags: ota, delta-ota, esp-delta-ota, board-test, attempt-log
summary: 差分 OTA Todo 6 真机闭环 1.0.8 -> 1.0.9；结果：success。
last_reviewed: 2026-08-01
garden_status: keep-evidence
garden_reviewed: 2026-08-01
memory_type: episodic
scope: task
owners: main/services/ota/ota_transport.c, main/services/ota/ota_service.c, tools/ota_host, board_logs
triggers: delta OTA detools esp_delta_ota 1.0.8 1.0.9 COM7 ota_metrics
evidence_level: observed
---

# Attempt: 差分 OTA Todo 6 真机闭环（1.0.8 -> 1.0.9）

## 结论

2026-08-01 已在 COM7 真机完成一次可审计的 detools/esp_delta_ota 差分升级闭环：设备从 `1.0.8` 启动，自动拉取差分 manifest，下载并应用 patch，校验目标镜像，切换到 `ota_1`，重启后运行 `1.0.9`，并通过 `PENDING_VERIFY` 有效性确认。无 panic、无 task WDT。

## 工件与校验

- 基线：`build_delta_board_1_0_8/111.bin`，设备启动版本 `1.0.8`，`ota_0`。
- 目标：`build_delta_target_1_0_9/111.bin`，`11,246,928` bytes。
- 目标 SHA-256：`fba09aa858370ebabaa761eb31916c3db382dbcc823a1742124610e027a1f09c`。
- 差分 patch：`delta_release/1.0.8_to_1.0.9.patch`，`223,923` bytes。
- patch SHA-256：`70d9afbfcf3552c4b94a6f1cc930d5bc7c2e77e6982d4ae23f92376405639fa7`。
- detools 本地还原校验：通过。

## 板测证据

串口日志：`board_logs/2026-08-01-17-25-45-delta-1_0_8-to-1_0_9-fixed.log`。

- 启动确认 `ota_0`：日志约第 299 行。
- manifest 阶段：第 887 行，`result=ESP_OK`、`delta=1`。
- 差分下载达到 `223923/223923`：第 971 行。
- download 阶段：第 983 行，`result=ESP_OK`、耗时 `93998ms`、`delta=1`。
- activate 阶段：第 1003 行，`result=ESP_OK`、耗时 `939ms`、`delta=1`。
- 重启后运行 `ota_1`：第 1305 行。
- 回滚保护确认有效：第 1307 行，`PENDING_VERIFY confirmed valid: version=1.0.9`。
- 监视器摘要：`board_logs/2026-08-01-17-25-45-delta-1_0_8-to-1_0_9-fixed.summary.json`，`panic_log_seen=false`。

## 根因与修复

原差分下载在消费 64 字节 patch 头后仍从 `received=0` 计数，导致已消费的头部被重复计数，最终停在 `223925/223989`。修复为从头部长度开始计数并先累计头部 SHA-256；每次 `esp_delta_ota_feed_patch` 后让出一次调度，避免长时间解压触发任务看门狗。修复后下载完整结束并成功激活。

## 发布收尾

板测期间发现 Cloudflare 对无查询参数的旧 artifact 仍返回旧缓存；发布 URL 增加 `rev=delta-20260801` 后，公网返回目标文件的正确大小和 SHA-256。板测结束后已恢复服务器纯全量 manifest（删除五个差分字段），保留新 `1.0.9` 全量包；临时 patch URL 返回 `404`。临时差分发布状态备份在服务器：

`/opt/ai-memory-watch/watch-data/ota/boardtest-backup-20260801/`

## 验证范围与非阻塞告警

- `uv run python -m pytest tests/test_ota_service_source.py`：6 passed。
- 目标和基线两套 ESP-IDF 构建均通过。
- 监视器 240 秒采集完成，无 panic；监视器因达到时长返回非零，不代表固件失败。
- 日志中的历史 `ESP_ERR_INVALID_STATE` 是修复前的旧 metrics；本次新记录为 manifest/download/activate 全部 `ESP_OK`。
- 重启后出现过一次 memory_watch 连接重置和 DS2413 总线拉低告警，均不影响 OTA，且不属于本次差分路径。
