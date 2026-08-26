---
id: attempt-memory-watch-notification-psram-caches
tags: context, runs, memory-watch, notification-center, psram, internal-ram, ble
summary: 记录将 Memory Watch / notification center 静态业务缓存迁到 PSRAM，以缓解 BLE provisioning 启动时 internal heap 连续块不足。
created: 2026-07-29
last_reviewed: 2026-07-29
owners: main/services/memory_watch/memory_watch_service.c, main/ui/custom/memory_watch_controller.c, main/ui/custom/watch_notification_center.c
evidence_level: verified
status: active
---

# Memory Watch / Notification PSRAM Cache 迁移

## 问题签名

BLE provisioning 启动前曾出现 internal heap 门限不足：

```text
ble_presence start skipped: internal heap low free=42926 largest=25600 min_free=65536 min_largest=40960
```

`idf.py size` 本轮前基线约为：

```text
DIRAM used: 252355
.bss: 72096
```

`nm` 显示 Memory Watch / notification center 中存在多块 internal `.bss` 业务缓存，例如 command queue backing storage、conversation cache、inbox summaries 和 notification scratch。

## 本轮处理

- `memory_watch_service`：将 command queue / worker queue 的 item storage 和 conversation cache 改为 `heap_caps_calloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`。
- FreeRTOS `StaticQueue_t` 控制块仍留 internal RAM；它们是小型内核对象，迁移收益低且风险更高。
- `memory_watch_controller`：板端 conversation / inbox / detail UI 缓存改为 PSRAM 分配；host preview 继续保留静态 mock。
- `watch_notification_center`：将 `s_nc_scratch` 从 `.ext_ram.bss` hint 改为运行期 PSRAM 分配。当前 sdkconfig 未启用 external BSS，原 hint 不保证进入 PSRAM。

## 验证

```text
uv run python -m pytest tests/test_memory_watch_service_source.py tests/test_memory_watch_ui_source.py -q
38 passed

uv run python scripts/context/check_layering.py --verbose
warning_count=0, known_exception_count=2

git diff --check
无 whitespace error，仅 line ending warning

idf.py build
通过；111.bin=0xabe230，app free=0x341dd0/23%

idf.py size
DIRAM used=225147
.bss=44888
```

相较本轮前基线，internal 静态占用约下降 27 KiB。`nm` 复查后相关大数组不再出现在 internal `.bss`，只剩 4 字节指针和少量小符号。

## 结论

这次迁移能解决一部分 BLE/TLS 前台切换时的 internal RAM 压力，尤其是静态 `.bss` 长期占用问题。但它不能替代 BLE、Hermes 前台 WS、official_chat、ESP-DL 等重资源的互斥和生命周期治理；后续仍需通过真实 Wi-Fi 管理页 BLE provisioning 进入/退出验证 `internal_free/largest` 是否达到门限。
