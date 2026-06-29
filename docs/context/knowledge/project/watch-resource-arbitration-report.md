---
id: watch-resource-arbitration-report
tags: context, knowledge, project, resource-arbitration, ram, psram, freertos, power-policy, espdl, stack, pressure-signal
summary: 手表大厂资源/状态管理实践调研与本项目 RAM 仲裁方案，基于三阶段栈实测数据校准压力阈值和互斥规则。
last_reviewed: 2026-06-28
memory_type: semantic
scope: project
status: active
owners: docs/context/knowledge/project/watch-resource-arbitration-report.md, main/services/power_policy.c, main/services/background_service_manager.c, components/espdl_inference
triggers: resource arbitration, internal RAM, memory pressure, espdl crash, fbank, ble, display bounce, psram migration, power_policy, background_mgr, mic consumer, websocket ssl
evidence_level: observed
---

# 手表大厂资源/状态管理实践 与 本项目 RAM 仲裁方案

> 场景：ESP32-S3 手表，internal RAM 极紧张，已出现 BLE controller `emi.c` assert、display bounce buffer `ESP_ERR_NO_MEM`、SNTP task 创建失败、ESP-DL Fbank 构造 NULL 解引用崩溃等真实问题。本文档综合联网调研的 Apple Watch / WearOS / ZSWatch / ESP32 LVGL 工程实践，结合本项目上下文库已有的资源框架契约和三阶段栈实测数据，给出可落地的资源仲裁方案。
>
> 当前分支：`codex/ai-memory-watch-hermes-api`
> 最新基线（扩缩栈后，2026-06-28 实测）：
> - 冷启动：internal RAM 302/338 KB (89.4%) used，PSRAM 1400/8192 KB (17.1%) used，internal_free=35482B，largest block=17408B
> - 高压（2次AI对话）：internal RAM 311/338 KB (92.2%) used，internal_free=25726B，largest block=18432B
> - 扩缩栈前对比：冷启动 internal RAM 323/338 KB (95.6%)，internal_free=12246B，largest block=9728B
> - mw_upload high-water：扩缩栈后冷启动 free=22508 bytes（配置 24576B，不可缩）；扩缩栈前高压 free=3172 bytes（高压实际用 21404B）
> - 完整实测数据：`docs/context/knowledge/project/task-stack-measurement-full-summary.md`

---

## 一、大厂手表资源/状态管理实践（联网调研）

### 1. Apple Watch (watchOS)
- **内存分级**：LPDDR 物理 RAM，但系统按 `foreground / background / suspended / terminated` 四档给 app 分配预算，超出预算由 jetsam 机制杀进程，而非 OOM 崩溃。
- **状态机驱动**：app lifecycle 由 `NSApplicationActivationState` 显式驱动，状态切换前系统给 `applicationWillResignActive` 回调，要求 app 主动释放重资源（模型、大 buffer、网络长连接）。
- **核心原则**：**前台独占重资源，后台只保留最小状态**；后台音频、定位等长期能力需声明 background mode 并由系统仲裁，不允许 app 自行抢夺。
- **可借鉴**：状态档位 + 主动释放 + 系统仲裁（对应本项目 `power_policy` 的 `ACTIVE/IDLE_DIM/STANDBY/...` 预算）。

### 2. WearOS (Google)
- **ViewModel + SavedState**：UI 状态与数据分离，UI 只读 snapshot，状态推进由 ViewModel/Service 持有；配置变化或进程回收后状态可恢复。
- **内存压力回调**：`ComponentCallbacks2.onTrimMemory(TRIM_MEMORY_RUNNING_LOW / MODERATE / COMPLETE)` 分级触发，app 按级别释放缓存、bitmap、非必要对象。
- **Doze / App Standby**：系统按屏幕状态、运动、充电分档限制后台网络/CPU。
- **可借鉴**：内存压力分级回调（对应本项目应在 `power_policy` 聚合 `heap_caps_get_free_size` 后向 owner 发布 `memory_pressure_low/critical` 信号）。

### 3. ZSWatch (开源 Zephyr 手表)
- 基于 Zephyr RTOS，用 **memory slab / k_mem_slab** 做固定大小对象池，用 **mempool** 做可变大小缓冲，显式区分静态/动态。
- UI 用 LVGL，**单页面加载 + 切换即销毁**，非活动屏幕对象立即释放，避免链式屏幕驻留。
- 后台传感器用事件驱动 + 中断唤醒，避免轮询占 RAM/CPU。

### 4. ESP32 LVGL 迁移 PSRAM 工程实践（LinkedIn 案例，与本项目同芯片 ESP32-S3）
- **问题**：320KB internal SRAM 被 LVGL heap + MQTT + cJSON + OTA 同时争抢，满载时最小可用 SRAM <20KB，触发 WDT 复位，OTA 成功率仅 70%。
- **方案**：
  1. 引入**统一内存分配层** `artyc_malloc`，优先 `MALLOC_CAP_SPIRAM`，失败回退 `MALLOC_CAP_8BIT`（internal）并告警。
  2. 替换 LWIP `mem_malloc`、cJSON allocator 到自定义分配器。
  3. LVGL 启用 `CONFIG_LV_MEM_CUSTOM=y`，LVGL heap 走 PSRAM。
  4. GUI 架构重构：**有限状态机管理页面切换，强制单页面，切换即释放**。
- **结果**：internal SRAM 稳定保持 80KB+ 可用，72h 零复位，OTA 成功率 70% → 99.8%。
- **关键启示**：**PSRAM 不是银弹，必须配合分配路由 + GUI 架构清理驻留对象**。

### 5. RTOS 内存管理通用最佳实践（NerdyElectronics / 医疗嵌入式知识体系）
- **静态分配优先**：编译期确定所有缓冲，链接器捕获超限，零碎片、零运行时失败。
- **固定大小内存池**：必须运行时分配时用，O(1) 分配/释放，零碎片，用计数信号量让任务阻塞等待空闲块，而非忙轮询。
- **任务栈监控**：开发期用 `uxTaskGetStackHighWaterMark()`，生产期启用 `configCHECK_FOR_STACK_OVERFLOW=2` + 钩子。
- **反模式**：栈估算不足、忽略对齐、频繁 malloc/free 不同大小、资源泄漏、无并发保护、过度优化、忽略最坏情况、缺少监控。

### 6. ESP-IDF 堆能力（本项目所用 v5.5.3）
- 内存分类：`MALLOC_CAP_INTERNAL`（DRAM/IRAM/D/IRAM）、`MALLOC_CAP_SPIRAM`（PSRAM，≤4MiB/次）、`MALLOC_CAP_DMA`（必须 internal，不含 PSRAM）。
- `heap_caps_get_free_size(cap)` 返回总剩余，**但碎片化下可能无法分配连续块**；判断真实可分配用 `heap_caps_get_largest_free_block(cap)`。
- `heap_caps_malloc_extmem_enable(limit)`：小于 limit 走 internal，大于走 PSRAM——速度/容量平衡。
- ISR 内不建议 malloc/free；启用 PSRAM 后 `heap_caps_check_integrity` 耗时长，需加大 `CONFIG_ESP_INT_WDT_TIMEOUT_MS`。

---

## 二、本项目当前真实问题与根因

### 问题 1：internal RAM 紧张（扩缩栈后 89.4%~92.2% 占用）
- 扩缩栈前基线：internal RAM 323/338 KB (95.6%) used，仅剩 ~12KB，largest block 9728B。
- 扩缩栈后基线：internal RAM 302/338 KB (89.4%) used，剩余 ~35KB，largest block 17408B（冷启动）；高压 311/338 KB (92.2%)，剩余 ~26KB，largest block 18432B。
- 缩栈释放 8KB internal RAM（official_chat_s -4096、network_mgr -1024、network_service -2048、power_service -1024），internal_free 从 12246 提升到 35482B（2.9 倍）。
- 已观测症状（扩缩栈前）：
  - BLE controller 初始化需 ~30KB 连续 internal 块 → `BLE_INIT: Malloc failed` → `emi.c` assert → interrupt WDT panic。
  - Display bounce buffer / SPI DMA priv buffer 申请失败 → `Display flush failed: ESP_ERR_NO_MEM`。
  - SNTP 网络同步 task 创建失败 `pdFAIL`（SRAM 连续碎片不足）。
  - SSL 握手 `esp-aes: Failed to allocate memory`（largest block 4608B 时）。
  - **ESP-DL Fbank 构造 NULL 解引用崩溃**（internal RAM 不足导致 `heap_caps_aligned_alloc` 返回 NULL，见问题 5）。
- 扩缩栈后 SSL 分配失败不再出现，但高压时 internal_free 仍降至 25726B，BLE 启动（需 ~30KB 连续块）仍有风险。

### 问题 2：高压资源并发未做互斥
- 实测证据（2026-06-28 高压场景）：AI 语音对话 + WebSocket 上传 + ESP-DL 推理三者并发时，internal_free 降至 6030B，largest block 降至 5376B。
- 同时活跃：LVGL flush（SPI DMA internal）+ 音频采集/播放（I2S DMA internal）+ Wi-Fi 高吞吐（LWIP internal）+ SD 大 IO + 模型加载（ESP-DL）。
- **ESP-DL 崩溃直接证据**：第二次 AI 对话语音录制完成后（52.1s），`background_mgr` 判断 mic 释放立即启动 ESP-DL 推理（52.2s），此时 WebSocket+SSL 仍在占用大量 internal RAM（54.9s），`Fbank` 构造函数 `heap_caps_aligned_alloc(MALLOC_CAP_INTERNAL)` 返回 NULL → `memcpy(m_cache=NULL)` → StoreProhibited panic。
- 资源框架计划已列出冲突规则但**未落地运行时仲裁**：当前只有 `power_policy` 发布预算，没有 internal RAM 压力信号回流到 owner，`background_mgr` 只看"mic 空闲"不看"internal RAM 是否够"就启动推理。

### 问题 3：PSRAM 利用率低（17.1%）
- 8MB PSRAM 只用了 ~1.4MB，理论上可承接 LVGL heap、HTTP/JSON/WS response buffer、worker task stack、inbox staging 等。
- 已迁移到 PSRAM：mw_upload/mw_health/mw_conv/mw_cancel/mw_inbox/memory_watch task stack（`xTaskCreateWithCaps`）、SNTP task、sync response buffer、time task。
- 实测确认 PSRAM 栈 task 高压安全：mw_upload 22508B free（不可缩）、mw_health/mw_conv 扩栈后 4240/4296B free。
- 仍有大量 internal buffer 未迁移（ESP-DL `Fbank` 的 `m_cache` 2KB 强制 `MALLOC_CAP_INTERNAL`、LWIP buffer、HTTP response buffer）。

### 问题 4：缺少内存预算账本与压力信号
- 没有运行时 internal RAM 预算表（谁该用多少、上限多少）。
- 没有 `memory_pressure_low/critical` 事件回流，owner 无法主动收缩。
- **实测数据可用于建立预算账本**：WiFi+LWIP+PHY 启动吃 47KB（最大单项）、LVGL UI 渲染吃 37KB（含 bounce buffer 2×8200B）、6 个 mw_* task 创建吃 6.7KB、imu_service 吃 4.5KB。
- 已有局部先例 `ble_presence_check_internal_heap()` 硬编码 64KB/40KB 门槛，但未统一到 power_policy 信号。

### 问题 5：ESP-DL 推理 runtime 缺乏 internal RAM 保护（2026-06-28 实测发现）
- `espdl_feature_pipeline.cpp` 每次（每 300ms）构造局部 `Fbank` 对象，其 `m_cache`（2KB）、`fft_config`、`mel_filter`、`win_func` 强制 `MALLOC_CAP_INTERNAL` 分配。
- esp-dl `Fbank` 构造函数不检查 `heap_caps_aligned_alloc` 返回值（同库 `Spectrogram` 类有 `assert` 但 `Fbank` 没有），分配失败时 `m_cache=NULL`。
- **已做临时修复**：构造前预检 `internal_free < 6144 || largest < 4096` 时返回 `ESP_ERR_NO_MEM`；`runtime_task` 对 `ESP_ERR_NO_MEM` 跳过当前窗口而非退出。
- **根因未解决**：`background_mgr` 在 mic 释放后立即启动 ESP-DL，不等 WebSocket 上传完成，两者争抢 internal RAM。这是资源仲裁方案必须解决的核心场景。

---

## 三、资源仲裁方案（落地到本项目 owner 合同）

> 严格遵循 `runtime-owner-contract.md`：**不新增大而全 ResourceManager / resource_policy / session_router**。仲裁通过 `power_policy` 发布预算 + owner snapshot + 压力信号回流实现。

### 方案分层

```
power_policy (预算+压力信号 owner)
   ↓ 发布 budget + memory_pressure
background_service_manager / 各 domain owner (消费 budget，自己决定收缩)
   ↓ owner 内部降级/暂停/释放
Driver adapter / Vendor SDK (只执行 owner 决策)
```

### A. 内存分类与归属预算表（新增，写入 power_policy）

| 内存类别 | 能力标志 | 典型用途 | 预算 owner | 迁移目标 |
| --- | --- | --- | --- | --- |
| Internal DRAM | `MALLOC_CAP_INTERNAL\|MALLOC_CAP_8BIT` | DMA buffer、ISR 数据、栈关键路径 | power_policy 统筹 | 保留，设上限 |
| Internal IRAM | `MALLOC_CAP_INTERNAL\|MALLOC_CAP_32BIT` | 时间关键代码、中断 | 链接脚本 | 不动 |
| DMA-capable | `MALLOC_CAP_DMA` | SPI/I2S/Display bounce | 各 driver owner | 必须 internal，设上限 |
| PSRAM | `MALLOC_CAP_SPIRAM` | LVGL heap、HTTP/JSON/WS buffer、worker stack、inbox staging、模型权重 | 各 owner 主动迁移 | **大幅扩容** |

**internal RAM 预算红线**（基于 338KB 总量，实测数据来自 `task-stack-measurement-full-summary.md`）：

| 模块 | 实测占用 | 预算上限 | 超限动作 |
| --- | --- | --- | --- |
| LVGL display bounce / SPI DMA priv | ~16KB×2=32KB | 保留 | 不可迁移（DMA 必须 internal） |
| Wi-Fi/LWIP/PHY | ~47KB（init 阶段实测降幅） | 50KB | 收紧 LWIP `CONFIG_LWIP_TCPIP_RECVMBOX_SIZE`、TCP wnd |
| BLE NimBLE controller | ~30KB(启动峰值) | 30KB | 非 charging/maintenance 禁止自动启动 |
| 音频 I2S DMA ring | ~8KB×2=16KB | 保留 | DMA 必须 internal |
| ESP-DL Fbank m_cache | ~2KB(每次构造) | 2KB | 预检 internal RAM 不足时跳过窗口（已修复） |
| internal task stack 合计 | ~50KB（缩栈后：cpu_monitor 4096+sleep_coord 3072+power_service 3072+power_policy 4096+background_mgr 4096+network_service 4096+network_mgr 3072+wakeup_evidence 4096+imu_service 4096+official_chat_s 4096+lvgl_task 10240） | 保持 | 已缩栈优化，暂不迁 PSRAM |
| FreeRTOS heap (heap_4) | 剩余 | **≥26KB 安全余量**（高压实测最低 internal_free=25726B，含所有 internal heap region） | 低于 26KB 触发 CRITICAL 压力信号 |

### B. 统一内存分配路由（轻量，不新增 manager）

在 `main/services/` 下新增**薄分配 helper**（不是新 owner，是工具函数），各 owner 主动调用：

```c
// 优先 PSRAM，失败回退 internal 并告警；大块强制 PSRAM
void *watch_alloc_large(size_t size);   // >512B 走 PSRAM
void *watch_alloc_dma(size_t size);     // 必须 internal DMA
void *watch_alloc_string(size_t size);  // 短字符串 internal，长字符串 PSRAM
```

- **不强制全局替换 malloc**（风险大），只在新增/重构路径主动用。
- 现有 `memory_watch_voice_client_alloc()` 已是此模式，推广到 inbox staging、conversation staging、HTTP response buffer。

### C. 内存压力信号回流（核心仲裁机制）

在 `power_policy` 增加轻量内存压力监控（**单点事实，不新增 owner**）：

```c
typedef enum {
    MEM_PRESSURE_NORMAL,    // internal free > 35KB, largest block > 17KB（冷启动稳态）
    MEM_PRESSURE_LOW,       // free 26-35KB 或 largest 10-17KB（高压运行中）
    MEM_PRESSURE_CRITICAL,  // free < 26KB 或 largest < 10KB（ESP-DL Fbank/SSL 分配失败区间）
} memory_pressure_level_t;

// power_policy 周期采样（5s），通过 task notification 广播
void power_policy_check_memory_pressure(void);
memory_pressure_level_t power_policy_get_memory_pressure(void);
```

> 阈值依据实测：
> - NORMAL > 35KB：扩缩栈后冷启动 internal_free=35482B，largest=17408B，所有功能正常。
> - LOW 26-35KB：高压（2 次 AI 对话）internal_free 降至 25726B，largest=18432B，功能正常但余量紧。
> - CRITICAL < 26KB：扩缩栈前高压 internal_free 曾降至 10750B（largest 4608B），触发 SSL `esp-aes: Failed to allocate memory`；ESP-DL 崩溃前 internal_free=6030B（largest 5376B）。26KB 是 BLE 启动（需 ~30KB 连续块）和 ESP-DL Fbank（需 6KB）同时失败的临界点。

**压力响应合同**（各 owner 必须实现，写在 owner 合同）：

| 压力级别 | background_service_manager | audio_codec | network_manager | lvgl_task | ble_presence | espdl_runtime |
| --- | --- | --- | --- | --- | --- | --- |
| NORMAL | 正常 | 正常 | 正常 | 正常 | 用户显式开关 | 正常 |
| LOW | Safety Monitor 降频(2x周期) | 拒绝新 P2 录音 | 暂停后台 sync | 降刷新率 | 拒绝启动 | 正常（预检 internal RAM） |
| CRITICAL | 暂停 Safety Monitor | 只允许 P0 告警 | 停后台 sync/inbox | 灭屏保最小刷新 | 强制 stop | 跳过推理窗口（已实现） |

- **关键**：这**不是**新 manager，是 `power_policy` 已有 budget 机制的内存维度扩展，owner 各自消费。
- 已有先例：`ble_presence_check_internal_heap()` 已是局部实现，只是没统一到 power_policy 信号。
- **ESP-DL 已实现 CRITICAL 响应**：`espdl_feature_pipeline.cpp` 预检 internal RAM 不足时返回 `ESP_ERR_NO_MEM`，`runtime_task` 跳过窗口。但这是被动防御，资源仲裁方案需让 `background_mgr` 在启动 ESP-DL 前主动检查压力级别，CRITICAL 时不启动。

### D. 高压资源互斥窗口（MAINTENANCE 已有，扩展到运行时）

现有 `MAINTENANCE` 窗口处理 OTA/模型替换。扩展到运行时高压组合：

**禁止并发组合**（写入 power_policy 冲突规则，基于实测崩溃时序）：
- **ESP-DL 推理 + WebSocket/SSL 上传**（实测崩溃：52.2s 推理启动 + 54.9s SSL 握手 → Fbank m_cache 分配失败 → panic）。`background_mgr` 必须等 WebSocket 上传完成后才启动 ESP-DL。
- 模型推理 + OTA 下载（都是 PSRAM + Flash 大 IO）
- BLE advertising 启动 + Display 大 flush（都抢 internal DMA）
- 音频录制 + Wi-Fi 大上传 + SD 写（都抢 internal DMA + PSRAM 带宽）

**仲裁规则**：
1. P0 告警可抢占一切。
2. P1 前台用户操作（语音助手/录音）暂停 P2 Safety Monitor。P1 结束后不立即启动 P2，需等 P1 的 WebSocket 上传也完成。
3. P3 维护任务必须申请 MAINTENANCE 窗口，进入后 P2 暂停。
4. 新增：内存压力 CRITICAL 时，拒绝启动任何新 P2/P3 重资源任务，返回 `ESP_ERR_NO_MEM` 并打点 `resource_acquire_denied`。
5. 新增：mic 消费者串行化（Hermes 语音 OR ESP-DL 推理，不重叠）；ESP-DL 启动前检查 `memory_pressure != CRITICAL` 且 `background_mgr` 确认无活跃 WebSocket 上传。

### E. 固定大小内存池（针对高频分配路径）

对**高频 malloc/free 且大小固定**的路径引入内存池（FreeRTOS 静态池，非新 manager）：
- inbox notification staging（已部分静态化）
- conversation message 节点
- HTTP response buffer（按档位：4KB/16KB/64KB 三档池）

收益：消除碎片、O(1) 分配、可观测上限。

### F. GUI 架构清理（借鉴 ESP32 LVGL PSRAM 案例）

- 审计 `main/ui/` 下是否有非活动屏幕对象驻留。
- 页面切换强制销毁旧屏幕 LVGL 对象（`lv_obj_del_async`）。
- LVGL heap 迁移 PSRAM：`CONFIG_LV_MEM_CUSTOM=y` + 自定义 allocator 优先 PSRAM。
- **注意 DMA 约束**：display bounce buffer / flush buffer 必须 internal DMA，不可迁 PSRAM。

### G. 任务栈审计与迁移（✅ 已完成，2026-06-28）

三阶段实测已完成（owner init 逐步采样 + 冷启动基线 + 高压场景峰值 + 扩缩栈后回归），完整数据见 `docs/context/knowledge/project/task-stack-measurement-full-summary.md`。

**已完成改动**：
- 🔴 扩栈 `mw_health` 6144→10240、`mw_conv` 6144→10240（PSRAM，修复高压 free=208/200B 栈溢出风险）
- 🟡 缩栈 `official_chat_s` 8192→4096、`network_mgr` 4096→3072、`network_service` 6144→4096、`power_service` 4096→3072、`time` 8192→6144（释放 8KB internal RAM）
- 净效果：PSRAM +6KB，internal -8KB，internal_free 从 12246→35482B（2.9 倍），largest block 从 9728→17408B（+79%）

**实测确认已迁 PSRAM 的 task**：mw_upload(24576B)、mw_health(10240B)、mw_conv(10240B)、mw_cancel(3072B)、mw_inbox(8192B)、memory_watch(6144B)、time(6144B)、system_time_sync(4096B)。

**实测确认不可缩**：mw_upload（高压用 21404B，24576B 配置必要）、lvgl_task（高压 free=5140B，50.2% 边界保持）。

**暂不迁移 internal→PSRAM 的 task**：power_service/power_policy/background_mgr/network_service/network_mgr/official_chat_s/cpu_monitor/sleep_coord/wakeup_evidence/imu_service。缩栈后 internal RAM 已回到安全水位（89.4%），迁移到 PSRAM 的收益不大（总共才省 ~36KB internal，但 PSRAM 访问慢可能影响响应）。后续若 internal RAM 再次紧张可考虑。

**ESP-IDF 栈单位确认**：`xTaskCreateWithCaps` 栈参数单位为 bytes（非 words），`memory_watch_service.c` 的 `k*StackWords` 常量命名为误导性历史命名。

### H. 可观测性（强制打点）

在 power_policy / 各 owner 关键点记录（已有 `resource_acquire_denied` 等事件，补充内存维度）：
- `memory_pressure_change` (level, internal_free, largest_block, psram_free)
- `memory_budget_exceeded` (owner, requested, cap)
- `memory_pool_exhausted` (pool_name, requested_size)

冷启动日志必须包含：
```
boot mem snapshot: internal_free=XXX largest=YYY psram_free=ZZZ
```

---

## 四、落地优先级（建议分 4 个小 gate，每个可独立验证回退）

### Gate 1（最高优先，止血）：internal RAM 压力信号 + ESP-DL/WebSocket 互斥
- 实现 `power_policy_check_memory_pressure()`（阈值见 C 节，基于实测校准）。
- `background_mgr` 启动 ESP-DL 前检查压力级别 + 确认无活跃 WebSocket 上传（修复实测发现的 ESP-DL+SSL 崩溃）。
- ble_presence 启动前查压力信号（替代当前硬编码 64KB/40KB 门槛）。
- display bounce buffer 分配失败时记录压力级别。
- **验证**：冷启动 + 手动点 Bluetooth + AI 对话后触发 ESP-DL，日志有压力级别，CRITICAL 时 fail closed 不崩溃。
- **回退**：禁用压力信号，恢复当前硬编码门槛和 ESP-DL 预检（已实现）。
- **已有基础**：ESP-DL `Fbank` 预检 internal RAM 已实现（`espdl_feature_pipeline.cpp`），`runtime_task` 对 `ESP_ERR_NO_MEM` 跳过窗口已实现。

### Gate 2：内存分配 helper + 大 buffer 迁 PSRAM
- 新增 `watch_alloc_large/dma/string`。
- 把 HTTP response buffer、inbox staging、conversation staging 逐个迁 PSRAM。
- **验证**：internal free 提升，PSRAM 使用上升，功能无回归。
- **回退**：逐个 buffer 回退到 internal malloc。

### Gate 3：任务栈审计与迁移（✅ 已完成，2026-06-28）
- ~~逐个 task 审计 high-water，迁 PSRAM。~~
- **已完成**：扩栈 mw_health/mw_conv + 缩栈 5 个 internal task，释放 8KB internal RAM，消除 2 个栈溢出风险。internal_free 从 12246→35482B。
- **attempt log**：`docs/context/runs/2026-06-28-attempt-task-stack-resize.md`
- **实测汇总**：`docs/context/knowledge/project/task-stack-measurement-full-summary.md`
- **后续可选**：若 internal RAM 再次紧张，可将 power_service 等 internal 栈 task 迁 PSRAM（当前不需要）。

### Gate 4：内存池 + GUI 清理
- 高频路径引入内存池。
- 审计非活动屏幕驻留。
- LVGL heap 迁 PSRAM（需 DMA 约束验证）。
- **验证**：长时间运行碎片不增长，72h 稳定性测试。

---

## 五、与现有契约的一致性核对

| 现有契约 | 本方案是否冲突 | 说明 |
| --- | --- | --- |
| 不新增 ResourceManager/resource_policy/session_router | ✅ 一致 | 压力信号是 power_policy 扩展，helper 是工具函数 |
| owner snapshot + power_budget 模式 | ✅ 一致 | 压力级别作为 budget 的新维度 |
| UI 只读 snapshot 不推进状态 | ✅ 一致 | UI 读取压力级别只做展示降级 |
| 资源结束由 owner 自己完成 | ✅ 一致 | 各 owner 自己实现压力响应 |
| MAINTENANCE 互斥窗口 | ✅ 一致 | 扩展到运行时高压组合 |
| 不在 portENTER_CRITICAL 内访问 PSRAM | ✅ 一致 | 压力采样在 power_policy task 上下文，非 ISR |

---

## 六、验证闭环（按 AGENTS.md 强制收尾规则）

涉及 FreeRTOS/RAM/PSRAM 改动，每个 Gate 完成后必须：
1. 新建 `docs/context/runs/YYYY-MM-DD-attempt-ram-arbitration-gateN.md`（错误签名 + 证伪路径）。
2. `CHANGELOG.md` 顶部记录摘要。
3. 同步 `current-task.md`。
4. 执行 `validate_context.py --level standard`。
5. 板端冷启动日志含内存快照、压力级别、无 panic/Guru/`ESP_ERR_NO_MEM`。

---

## 七、关键参考来源

- ESP32 LVGL 迁移 PSRAM 工程案例（LinkedIn, Isaac Dharmaraja, 2025）
- NerdyElectronics: Memory Management Strategies for RTOS Applications (2026-03)
- 医疗嵌入式知识体系: RTOS 资源管理 (2026-02)
- ESP-IDF v6.0.2 堆内存分配官方文档（本项目用 v5.5.3，API 兼容）
- 本项目 `docs/context/plans/completed/2026-05-12-watch-resource-framework-plan.md`
- 本项目 `docs/context/knowledge/project/runtime-owner-contract.md`
- 本项目 `docs/context/knowledge/project/task-stack-measurement-full-summary.md`（三阶段实测汇总，本方案的事实依据）
- 本项目 `docs/context/runs/2026-06-28-attempt-task-stack-measurement-instrumentation.md`（观测能力建设）
- 本项目 `docs/context/runs/2026-06-28-attempt-task-stack-resize.md`（扩缩栈改动 + ESP-DL 崩溃修复）
- 本项目 `docs/context/runs/2026-06-27-attempt-ble-presence-internal-heap-guard.md`
- 本项目 `docs/context/runs/2026-06-25-attempt-hermes-inbox-and-sntp-task-stack-fix.md`

---

## 一句话结论

**本项目不需要新造资源管理器**。按大厂实践，正确的做法是：在现有 `power_policy` budget 机制上增加**内存压力信号维度**（阈值基于实测校准：NORMAL>35KB / LOW 26-35KB / CRITICAL<26KB），让各 owner 按合同自行响应（降频/暂停/fail-closed），同时把大 buffer 和 task stack 主动迁 PSRAM、引入固定大小内存池治碎片、清理 GUI 驻留对象。任务栈审计与扩缩栈（Gate 3）已完成，internal_free 从 12246 提升到 35482B。止血优先级最高的是 Gate 1：压力信号 + ESP-DL/WebSocket 互斥——实测已证明 `background_mgr` 在 mic 释放后立即启动 ESP-DL 推理会因 internal RAM 不足导致 `Fbank` 构造 NULL 解引用崩溃。
