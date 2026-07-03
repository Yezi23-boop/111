# 项目长期记忆

## SD 卡性能基线

- ESP32-S3 + SPI3_HOST + SD 卡：10MHz SPI 实测 ~527 KB/s 有效吞吐（理论 1.25 MB/s 的 42%）
- 20MHz SPI：675 KB/s 有效吞吐（`load_state` 12帧 4.7MB 耗时 7145ms）
- **SPI 频率上限**：25MHz 挂载失败（`cmd=52 command CRC error`），20MHz 是当前硬件可靠极限
- **软件优化瓶颈**：`max_transfer_sz` 4KB→16KB 无明显效果；连续读取（1次 fseek+1次 fread）反而略慢。FATFS 底层已优化，瓶颈在 SPI 总线本身（20MHz 理论 2.5MB/s，实测 675KB/s，效率 26%）
- `sd_manager_read_range` 每帧 fopen+fseek+fread+fclose 开销约 50-100ms（FAT 目录查找）
- `max_transfer_sz=4096` 对齐 4KB 页边界
- 文件句柄缓存可消除 fopen 重复开销，`sd_manager_invalidate_cache()` 管理生命周期
- SPI 频率可通过 `SD_SPI_FREQ_KHZ` 宏配置（`sd_manager.c`），默认 20000
- 真正的优化方向：换 SDMMC 4-bit 模式（需硬件改造 +2 GPIO，理论 4 倍提升）或压缩帧数据

## PSRAM 使用策略

- ESP32-S3 octal PSRAM 80MHz，`CONFIG_SPIRAM_USE_MALLOC=y`
- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`：小于 16KB 的分配走 internal RAM
- 全帧预加载：12帧 × 411KB ≈ 5MB，PSRAM 占用 ~77%
- 预加载策略将每帧 ~780ms SD 读取集中到一次性 7.25s，后续 read_frame 0ms

## 表盘动画架构

- rawanim 格式：28字节 header + 帧表 + 帧数据（RGB565 LE，410×502，每帧 411,640 bytes）
- `watchface_anim_cache` 在 `load_state` 时预加载全部帧到 PSRAM
- `read_frame` 零 SD 读取，仅更新 `lv_image_dsc_t.data` 指针
- LVGL timer 实际周期 ~70ms（受系统负载影响），理论帧率 ~14fps
- **多重缓冲/流水线加载**（2026-07-03）：状态切换时只加载元数据，立即显示第0帧，后续帧通过 tick 逐步加载
- 双缓冲区方案：front/back buffer 各 411KB，帧加载完成后零拷贝交换
- 流水线性能：状态切换 645ms（原 7145ms，提升 11 倍），单帧加载 49ms（7 次 × 64KB 读取）
- PSRAM 占用：2 × 411KB = 822KB（比整段预加载 5MB 减少 83%）

## V1 完成状态

- V1 已完成（2026-07-03），采用"整段预加载到 PSRAM"最终策略
- 性能基线：load_state 7.25s，read_frame 0ms，poll 0-1ms，有效帧率 ~14fps
- PSRAM 占用：6341KB/8192KB (77.4%)
- 已清理 V1A/V1B 分离，标记计划为 completed

## V2 优化计划

- 目标：PSRAM 占用从 5MB 降至 2-3MB，帧率从 ~14fps 提升至 20-30fps，加载时间从 7.25s 降至 3-4s
- 优化方向：压缩优化、帧率提升、内存优化、功能增强、启动优化
- 计划文件：`docs/context/plans/active/2026-07-03-watchface-optimization-v2-plan.md`

## 多重缓冲优化（2026-07-03）

- 实现流水线加载：状态切换时只加载元数据（header+frame table），立即加载第0帧并显示
- 双缓冲区方案：front/back buffer 各 411KB PSRAM，帧加载完成后零拷贝交换
- 流水线 tick：每次读取 64KB（约 7ms），每帧 7 次读取，约 49ms 加载一帧
- 性能提升：状态切换从 7145ms 降至 645ms（11 倍），首帧显示延迟从 7145ms 降至 645ms
- PSRAM 优化：从 5MB（整段预加载）降至 822KB（双缓冲），减少 83%
- 剩余瓶颈：SPI 总线带宽限制（20MHz 理论 2.5MB/s，实测 675KB/s），要达到稳定 10+ FPS 需硬件改造
- **rawanim frame_duration_us bug**（已修复 2026-07-03）：打包脚本误用 `1000*fps` 计算帧持续时间，导致 delay_ms=6ms（应为 167ms@6FPS）。已修复脚本并修补所有 rawanim header
- 源动画帧率：所有状态均为 6 FPS，低于用户要求的 10 FPS；需要更高帧率素材才能满足要求
- **FreeRTOS 异步 SD 卡加载**（2026-07-03）：SD 卡读取从阻塞 poll 改为后台任务异步执行
  - 后台任务 `sd_load_task`（优先级 5，核心 1，栈 4096）通过 `sd_load_queue`（深度 2）接收请求
  - 任务通知 `xTaskNotifyGive()`/`ulTaskNotifyTake(pdTRUE, 0)` 零阻塞同步
  - LVGL 线程 poll 阻塞从 ~7ms 降至 0ms，触摸响应延迟理论上降为 0
  - host preview 用 `#ifdef AGENT_PREVIEW_HOST` 保持同步兼容
  - 构建通过：app 11.1MB，剩余 23%；板端测试待完成

## 串口监控技巧

- `idf_monitor` 需要 TTY，`Start-Process` 后台启动会失败
- 替代方案：`python -m serial` 直接用 pyserial 读 COM 口
- 命令：`python -c "import serial; port = serial.Serial('COM3', 115200, timeout=1); ..."`

## 危险样本 SD 卡闭环系统（2026-07-04）

- **架构**：ESP-DL runtime → PCM tap callback → danger_sample_recorder → SD 卡
- **PCM tap callback**：在推理窗口准备完成时调用，传递 int16 PCM 和窗口元数据
- **环形缓冲区**：默认 5 秒（80000 样本），PSRAM 分配，mutex 保护
- **写入队列**：深度 4，非阻塞发送，独立写入任务执行 SD 卡 I/O
- **文件格式**：WAV（44字节header，16kHz/16bit/mono）+ JSON 元数据，`.tmp→rename` 原子写入
- **文件命名**：`/sdcard/danger_samples/{date}/{time}_{label}_{confidence}.wav/.json`
- **生命周期**：跟随危险识别后台服务开关，不独立运行
- **关键 API**：
  - `espdl_audio_runtime_set_pcm_tap_callback()` - 底层 PCM 回调
  - `danger_sample_recorder_capture()` - 触发样本录制
  - `sd_manager_rename_file()` - 文件重命名（用于原子提交）
- **自动化测试**（2026-07-04 验证通过 9/9 PASS）：
  - 测试代码已清理（验证通过后删除）
  - 测试注入 API：`danger_sample_recorder_inject_test_pcm()`
  - 冷启动 + 10秒延迟自动执行，无需人工干预
  - 输出机器可读格式：`TEST_RESULT: N/9 PASS wav=YES/NO json=YES/NO`
- **Git 提交**（2026-07-04 squash 完成）：
  - `234da322 feat: 危险样本 SD 卡闭环功能完整实现（阶段 1A-1D + 测试 + 清理）`
  - 将 5 个文档提交 + 工作区变更合并为单个提交
