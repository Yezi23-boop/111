# ESP32-S3 Internal RAM 优化指南

**项目**: ESP32-S3 手表固件  
**日期**: 2026-06-25  
**目标**: 优化 internal RAM 使用，释放空间给关键任务

---

## 1. 当前 Internal RAM 使用概况

### 1.1 PSRAM 配置（sdkconfig）

| 配置项 | 值 | 说明 |
|--------|-----|------|
| `CONFIG_SPIRAM` | y | PSRAM 已启用 |
| `CONFIG_SPIRAM_MODE_OCT` | y | Octal 模式（80MHz） |
| `CONFIG_SPIRAM_USE_MALLOC` | y | malloc 大于 64KB 自动走 PSRAM |
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` | 65536 | ≤64KB 优先 internal |
| `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` | 131072 | 保留 128KB internal |
| `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY` | y | 允许任务栈放 PSRAM |

### 1.2 Internal RAM 占用分布

**静态分配（.bss 段）**：
- 任务栈：24 KB（`s_service_task_stack[6144]`）
- 队列存储：~4.2 KB（`s_command_queue_storage` 最大，含 `cmd_t` 内嵌 response）
- 静态控制块（StaticQueue/Task/Semaphore）：~1 KB
- 业务数据缓冲区：~12 KB（通知中心、聊天历史、天气缓冲等）
- **小计**：~40 KB

**动态分配（heap）**：
- LVGL bounce buf：16 KB（2×8200B，必须在 internal，DMA 要求）
- `s_tx_silence_preload[2048]`：2 KB（I2S DMA 静音数据）
- 网络缓冲区：默认 internal
- **小计**：~50-100 KB（取决于运行时）

**系统保留**：
- main task 栈：3.5 KB
- system event task 栈：2.25 KB
- timer task 栈：3.5 KB
- idle task ×2：3 KB
- ISR 栈：1.5 KB
- **小计**：~14 KB

---

## 2. 优化方案详解

### 2.1 任务栈优化

#### 2.1.1 `s_service_task_stack[6144]` → PSRAM

**当前**：`memory_watch_service.c:172`
```c
static StackType_t s_service_task_stack[6144]; // 24 KB internal
```

**优化方案**：
```c
// 删除静态栈定义
// static StackType_t s_service_task_stack[6144]; // 删除

// 改用 xTaskCreateWithCaps
s_service_task_handle = xTaskCreateWithCaps(
    memory_watch_service_task,
    "memory_watch",
    6144,  // 栈大小保持不变
    NULL,
    5,
    &s_service_task_handle,
    MALLOC_CAP_SPIRAM  // 分配到 PSRAM
);
```

**风险评估**：
- 访问延迟：PSRAM 比 internal RAM 慢 2-3x
- 中断安全性：此任务不处理 time-critical 操作，风险低
- 栈溢出检测：`xTaskCreateWithCaps` 支持，与 static 行为一致

**收益**：释放 **24 KB** internal RAM  
**风险等级**：⚠️ 中  
**建议**：✅ 可执行

#### 2.1.2 `lvgl_task` 栈大小调整

**当前**：`app_main.c:122`
```c
xTaskCreatePinnedToCore(lvgl_task, "lvgl_task", 1024 * 10, ...); // 40 KB
```

**问题**：40 KB 是否过大？

**分析**：
- LVGL 渲染涉及多层调用：事件处理 → 绘制 → 缓冲区管理 → flush 回调
- 图片解码、字体加载可能需要较大栈空间
- 但 40 KB 仍可能过大

**优化方案**：
1. **先测量**：添加栈使用监控
   ```c
   UBaseType_t high_water_mark = uxTaskGetStackHighWaterMark(NULL);
   ESP_LOGI("LVGL", "Stack high water mark: %u words", high_water_mark);
   ```
2. **根据结果调整**：
   - 如果 < 8 KB：降到 24 KB（1024 * 6）
   - 如果 8-16 KB：降到 32 KB（1024 * 8）
   - 如果 > 16 KB：保持 40 KB

**收益**：潜在释放 **8-16 KB** internal RAM  
**风险等级**：🔴 高（盲目降低可能导致栈溢出）  
**建议**：⚠️ 必须先测量再调整

#### 2.1.3 `power_policy` / `power_service` 栈调整

**当前**：
- `power_policy.c:472`：16 KB（4096 words）
- `power_service.c:342`：16 KB（4096 words）

**分析**：
- 电源策略计算通常较简单
- 状态轮询和事件处理栈需求不大
- 但需确认是否有深度递归或大局部变量

**优化方案**：
1. 先用 `uxTaskGetStackHighWaterMark` 测量
2. 通常可安全降到 8 KB（2048 words）

**收益**：释放 **16 KB** internal RAM  
**风险等级**：⚠️ 中  
**建议**：✅ 可执行（需实测验证）

### 2.2 静态缓冲区优化

#### 2.2.1 可迁移到 PSRAM 的对象

| 文件 | 行号 | 变量 | 大小 | 迁移可行性 | 说明 |
|------|------|------|------|------------|------|
| `memory_watch_service.c` | 160-167 | 4 个 queue storage | ~8 KB | ❌ 不可迁移 | FreeRTOS 静态队列必须在 internal |
| `watch_notification_center.c` | 64 | `s_surfaced[20][64]` | 1.28 KB | ✅ 可迁移 | 通知 ID 缓存，非 ISR 访问 |
| `watch_notification_center.c` | 68 | `s_active_ids[20][64]` | 1.28 KB | ✅ 可迁移 | 同上 |
| `memory_watch_service.c` | 2237 | `s_pending_read[20][64]` | 1.28 KB | ✅ 可迁移 | inbox 已读缓存 |
| `hptts.c` | 48 | `weather_buffer[1024]` | 1 KB | ✅ 可迁移 | HTTP 响应缓冲区 |
| `official_chat_service.c` | 57 | `s_last_user_text[192]` | 192 B | ✅ 可迁移 | 用户文本快照 |
| `official_chat_service.c` | 58 | `s_last_assistant_text[256]` | 256 B | ✅ 可迁移 | 助手文本快照 |

**迁移方法**：
```c
// 原代码
static char s_surfaced[NC_SURFACED_MAX][64];

// 优化后
static char *s_surfaced = NULL;
void init(void) {
    s_surfaced = heap_caps_malloc(NC_SURFACED_MAX * 64, MALLOC_CAP_SPIRAM);
}
```

**收益**：释放 ~5 KB internal RAM  
**风险等级**：✅ 低  
**建议**：✅ 可执行

#### 2.2.2 必须在 Internal RAM 的对象

| 对象 | 原因 | 大小 |
|------|------|------|
| StaticQueue 存储 | FreeRTOS 要求，ISR 安全路径 | ~8 KB |
| LVGL bounce buf | SPI DMA 直读要求 | 16 KB（2×8200B） |
| `s_tx_silence_preload[2048]` | I2S DMA 要求 | 2 KB |
| ISR 栈 | 中断上下文 | 1.5 KB |

#### 2.2.3 已在 PSRAM 的对象（无需改动）

| 对象 | 文件 | 说明 |
|------|------|------|
| LVGL 显示缓冲（single 路径） | `lv_port_display.c:116-117` | 已用 `MALLOC_CAP_SPIRAM` |
| 录音缓冲区（hw_pcm/opus_pcm/opus_out） | `memory_watch_recorder.c:348-354` | 已用 `MALLOC_CAP_SPIRAM` |
| 任务栈（mw_upload/mw_health/mw_cancel/inbox） | `memory_watch_service.c:1945-2807` | 已用 `xTaskCreateWithCaps` |

### 2.3 SDK 配置优化

#### 2.3.1 `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`

**当前**：65536（64 KB）

**问题**：所有 ≤64 KB 的 malloc 都走 internal RAM

**优化方案**：
- 降到 32768（32 KB）：让更多中等大小分配走 PSRAM
- 降到 16384（16 KB）：更激进，但可能影响性能

**风险**：
- 小对象分配到 PSRAM 会增加访问延迟
- 可能影响网络和音频性能

**收益**：潜在释放 20-50 KB internal RAM  
**风险等级**：⚠️ 中  
**建议**：⚠️ 需要性能测试

#### 2.3.2 `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`

**当前**：131072（128 KB）

**说明**：为 internal RAM 保留的最小空间

**优化方案**：
- 如果 internal RAM 紧张，可降到 65536（64 KB）
- 但需确保关键任务有足够空间

**风险**：可能导致 internal RAM 耗尽  
**风险等级**：🔴 高  
**建议**：❌ 不建议调整

### 2.4 编译时优化

#### 2.4.1 删除未使用的 Edge Impulse 版本

**已完成**：删除 baseline_v1, manual_v2, v3, v4, v4_3s, v5_1s

**收益**：
- 磁盘空间：131 MB
- 编译时间：减少
- tensor_arena 重复定义：消除

#### 2.4.2 条件编译排除预览代码

**当前**：`host_runner/main.c` 包含 `s_screen_mask_data[823 KB]`

**优化方案**：
```c
#ifdef CONFIG_PREVIEW_MODE
static uint8_t s_screen_mask_data[PREVIEW_W * PREVIEW_H * 4];
#endif
```

**收益**：编译时排除，不进固件  
**风险等级**：✅ 低  
**建议**：✅ 可执行

---

## 3. 优化执行计划

### Phase 1：低风险优化（立即执行）

| 优化项 | 收益 | 风险 | 优先级 |
|--------|------|------|--------|
| `s_service_task_stack` 改 PSRAM | 24 KB | 中 | P1 |
| 静态缓冲区迁移到 PSRAM | 5 KB | 低 | P1 |
| 条件编译排除预览代码 | 0 KB（编译时） | 低 | P2 |

### Phase 2：中风险优化（需测量）

| 优化项 | 收益 | 风险 | 优先级 |
|--------|------|------|--------|
| `lvgl_task` 栈调整 | 8-16 KB | 高 | P2 |
| `power` 任务栈调整 | 16 KB | 中 | P2 |
| SDK 配置调整 | 20-50 KB | 中 | P3 |

### Phase 3：高风险优化（谨慎评估）

| 优化项 | 收益 | 风险 | 优先级 |
|--------|------|------|--------|
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` 调整 | 20-50 KB | 中-高 | P3 |

---

## 4. 验证方法

### 4.1 栈使用监控

```c
// 在每个任务中添加
UBaseType_t high_water_mark = uxTaskGetStackHighWaterMark(NULL);
ESP_LOGI(TAG, "Task %s stack high water mark: %u words", 
         pcTaskGetName(NULL), high_water_mark);
```

### 4.2 内存使用监控

```c
// 定期打印内存状态
void print_memory_status(void) {
    ESP_LOGI(TAG, "Internal RAM: %lu bytes free", 
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM: %lu bytes free", 
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
```

### 4.3 压力测试

1. 运行所有功能模块
2. 监控栈高水位
3. 检查是否有内存泄漏
4. 验证性能无明显下降

---

## 5. 风险缓解

### 5.1 栈溢出检测

确保 `CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y`（当前已启用）

### 5.2 内存不足处理

```c
void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
if (ptr == NULL) {
    ESP_LOGE(TAG, "PSRAM allocation failed, trying internal");
    ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
}
```

### 5.3 回滚方案

1. 保留 git 提交历史
2. 每个优化项单独提交
3. 出现问题可快速回滚

---

## 6. 预期收益总结

| 优化阶段 | 释放 Internal RAM | 风险等级 |
|----------|-------------------|----------|
| Phase 1 | ~29 KB | 低-中 |
| Phase 2 | ~24-32 KB | 中-高 |
| Phase 3 | ~20-50 KB | 高 |
| **总计** | **~73-111 KB** | - |

**当前 Internal RAM 估算**：~300 KB  
**优化后预期**：~190-230 KB  
**释放比例**：24-37%

---

## 7. 附录

### 7.1 相关文件

- `memory_watch_service.c`：任务栈和队列定义
- `lv_port_display.c`：LVGL 显示缓冲区
- `app_main.c`：任务创建入口
- `sdkconfig`：PSRAM 配置

### 7.2 参考文档

- [ESP-IDF SPIRAM 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/external-ram.html)
- [FreeRTOS 任务栈文档](https://www.freertos.org/Documentation/02-Kernel/04-API-references/01-Task-creation/00-xTaskCreate)

### 7.3 已完成优化

- [x] 删除旧版 Edge Impulse SDK（131 MB）
- [x] `s_service_task_stack` 改 PSRAM（24 KB）— 2026-06-25 完成
- [ ] 静态缓冲区迁移（5 KB）
- [ ] 任务栈大小调整（24-32 KB）

---

**文档维护者**: MiMo Code Agent  
**最后更新**: 2026-06-25
