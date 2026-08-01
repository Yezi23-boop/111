---
name: fix-standby-ws-preconnect-gate
overview: 修复 memory_watch 服务在 STANDBY 下仍尝试预连接云端 WebSocket 的问题：在预连接入口补上网络就绪门控，使待机时跳过预连接、回 ACTIVE 后自动补连。
todos:
  - id: fix-ws-preconnect-gate
    content: 在 memory_watch_service.c 的 start_foreground_ws 门控中补 network_service_is_service_ready() 检查（方案 B，含 ESP_LOGD）
    status: completed
  - id: verify-fix
    content: 运行 test_memory_watch_ws_client_source.py 聚焦测试，随后 idf.py build 验证编译通过
    status: completed
    dependencies:
      - fix-ws-preconnect-gate
---

## 需求概述

修复 AI Memory Watch（Hermes）在 STANDBY 待机状态下仍发起云端 WebSocket 预连接的问题。系统进入 STANDBY 后已按节能预算暂停非关键网络同步（`network_sync_allowed=false`，`network_service_is_service_ready()` 为 false），但 `start_foreground_ws()` 未检查该信号，仍在 Wi-Fi modem-sleep 省电模式下硬建 TLS 连接，导致 `select() timeout` 报 `foreground websocket preconnect failed`，白费电量并产生误导性错误日志。

## 核心功能

- 预连接跟随待机预算：STANDBY 下不发起 WebSocket 预连接
- 保持既有预连接体验：进入 Hermes 页面且网络就绪（ACTIVE）时正常预连接
- 自动恢复：回 ACTIVE 后由主循环下一轮自动补连，无需额外恢复逻辑
- 与同文件 health check / 录音 / 文字命令的网络门控口径完全一致

## 边界

- 仅修改 `start_foreground_ws()` 入口门控，不动预连接复用、上传、离页关闭等既有逻辑
- 不改测试（现有断言为包含式 `assertIn`，加一行门控不破坏）

## 技术方案

### 技术栈

- 现有 ESP-IDF C 工程，无新增依赖、无新文件
- 仅修改 `main/services/memory_watch/memory_watch_service.c`

### 实现方案

**修改点**：`memory_watch_service_start_foreground_ws()`（3079-3121 行）入口门控（3082-3084 行），补 `!network_service_is_service_ready()`。

修改前：

```c
static void memory_watch_service_start_foreground_ws(void)
{
#if CONFIG_MEMORY_WATCH_WEBSOCKET_ENABLED
    if (!memory_watch_service_is_foreground_ready() ||
        memory_watch_ws_client_is_connected() ||
        memory_watch_service_is_upload_worker_busy())
    {
        return;
    }
```

修改后（方案 A：并入现有门控，最小改动）：

```c
static void memory_watch_service_start_foreground_ws(void)
{
#if CONFIG_MEMORY_WATCH_WEBSOCKET_ENABLED
    if (!memory_watch_service_is_foreground_ready() ||
        !network_service_is_service_ready() ||
        memory_watch_ws_client_is_connected() ||
        memory_watch_service_is_upload_worker_busy())
    {
        return;
    }
```

修改后（方案 B：单独分支 + debug 日志，便于排查，推荐）：

```c
static void memory_watch_service_start_foreground_ws(void)
{
#if CONFIG_MEMORY_WATCH_WEBSOCKET_ENABLED
    if (!memory_watch_service_is_foreground_ready() ||
        memory_watch_ws_client_is_connected() ||
        memory_watch_service_is_upload_worker_busy())
    {
        return;
    }
    if (!network_service_is_service_ready())
    {
        /* STANDBY 预算暂停非关键同步时跳过预连接，待回 ACTIVE 后主循环自动补连 */
        ESP_LOGD(TAG, "foreground ws preconnect skipped: network not ready");
        return;
    }
```

### 关键决策

- **选用 `network_service_is_service_ready()` 而非直接读 `power_policy` 预算**：与同文件 begin_recording（2052 行）、send_text（2127 行）、check_health（2196 行）的门控口径一致，语义为"Wi-Fi 连通且云端已验证"，正是 STANDBY 暂停的语义；避免引入第二套判断来源。
- **方案 B 优于方案 A**：单独分支打一条 debug 日志，可区分"待机跳过（预期行为）"与"连接失败（真问题）"，符合本仓库"先可观测再调优"原则；其余门控保持静默 return 的既有风格。
- **无需恢复逻辑**：主循环每轮（2956-2965 行）都会调用该函数，`network_service_is_service_ready()` 回 ACTIVE 后自动变 true，下一轮即自动补连。
- **无头文件改动**：`network_service.h` 已在文件第 18 行 include。

### 性能与可靠性

- 仅减少一次无效 TLS 连接尝试（原来在 STANDBY 下每 5 秒重试一次并超时），无额外开销
- 不改连接复用、上传 worker busy、离页关闭等既有行为
- 影响面仅限该函数入口，风险极低

## 目录结构

```
main/services/memory_watch/memory_watch_service.c  # [MODIFY] start_foreground_ws 入口门控补网络就绪检查（约 3082-3087 行区域）
tests/test_memory_watch_ws_client_source.py        # [验证] 现有断言无需修改，运行确认通过
```

## 验证路径

1. 聚焦测试：`uv run python -m pytest tests/test_memory_watch_ws_client_source.py`，确认 `test_service_preconnects_ws_and_reuses_it_for_pending_request` 等断言仍通过
2. 构建：确认 ESP-IDF shell 后 `idf.py build` 通过（常规 service C 改动，不需 fullclean）
3. 可选板测：进入 STANDBY 后不再出现 `foreground websocket preconnect failed: ... timeout`；触摸唤醒回 ACTIVE 后出现 `foreground websocket preconnected`