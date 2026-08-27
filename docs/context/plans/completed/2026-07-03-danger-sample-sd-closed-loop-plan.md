---
id: danger-sample-sd-closed-loop-plan
tags: context, plans, execplan, danger-detection, espdl, sd-card, model-loop
summary: 危险识别样本 SD 卡缓存与后续上传闭环执行计划。
last_reviewed: 2026-08-07
memory_type: task
scope: task
owners: main/features/danger_detection/danger_sample_recorder.c, components/espdl_inference/include/espdl_audio_runtime.h, components/espdl_inference/espdl_audio_runtime.cpp, main/features/danger_detection/danger_detection_service.c, main/services/memory_watch/watch_endpoint_service.c, server/watch_voice_endpoint/app.py
triggers: danger sample recorder, SD 卡, /sdcard/danger_samples, 模型闭环, Alerting 样本, ESP-DL 音频缓存
evidence_level: design
status: completed
---

# 危险样本 SD 卡缓存与模型闭环执行计划

## 目标与全局

- 任务目标：危险识别进入 `Alerting` 后，先不影响现有手机通知链路，同时把触发前后短音频样本和元数据保存到 SD 卡，后续再补传到服务器形成模型闭环。
- 为什么现在做：当前 `手表 Alerting -> 云服务器 -> Android App -> 手机通知栏` 已经真机跑通，下一步需要积累真实环境样本，支撑误报分析、人工标注和模型再训练。
- 完成后用户会看到什么变化：真机识别到危险后，手机仍会立即收到通知；SD 卡 `/sdcard/danger_samples` 下会多出一组 `.wav + .json` 样本文件。

## 当前仓库事实

- SD 卡能力已存在：`components/sd_card/sd_manager.c/.h`，挂载点固定为 `/sdcard`。
- SD 卡走 `SPI3_HOST`，当前引脚口径为 `MOSI=1 / MISO=3 / CLK=2 / CS=17`，用于避开屏幕显示总线。
- ESP-DL 主线音频入口在 `components/espdl_inference/espdl_audio_runtime.cpp`，当前链路为 `audio_codec_read -> 主麦克风通道提取 -> 24kHz 转 16kHz -> Fbank -> ESP-DL 单模型推理`。
- `danger_detection_service.c` 是危险状态机 owner，负责 `Monitoring / Suspicious / Alerting / Cooldown` 和连续窗口确认。
- `watch_endpoint_service.c` 是中性 endpoint owner，当前负责异步 POST 危险告警到 `/v1/watch/alerts`。
- 当前没有可直接导出最近事件上下文 PCM 的通用音频环形缓存；推理滑窗只服务模型窗口步进，不适合作为样本文件来源。
- 服务器 `server/watch_voice_endpoint/app.py` 已有 `UploadFile` 处理语音命令的基础，但还没有独立的危险样本上传接口。
- 危险识别后台能力已经由 `safety_monitor_policy -> safety_monitor_session -> danger_detection_service -> espdl_audio_runtime` 管理；样本保存必须跟随这条运行链路，不新增独立常驻监听能力。
- `components/espdl_inference` 属于底层模型 runtime，不能直接依赖 `main/services/danger_sample_recorder`；PCM 暴露必须走 runtime callback/tap API，保持依赖方向为上层注册、底层回调。

## 范围与非目标

- 本轮第一阶段明确要做：
  - 新增 `danger_sample_recorder`，维护最近 3 秒 `16kHz / mono / int16` PCM 环形缓存；第一版只保存前 1 秒 + 后 1 秒，ring 额外容量用于吸收 worker 调度延迟。
  - `espdl_audio_runtime` 新增 PCM tap callback API，重采样后只调用已注册的 callback，不 include recorder。
  - `danger_detection_service.c` 在启动 ESP-DL runtime 时注册 recorder PCM callback，在停止时注销。
  - `danger_detection_service.c` 首次进入 `Alerting` 时触发样本捕获事件，并携带触发窗口的 sample index / window id。
  - recorder 后台 worker 收集前 1 秒 + 后 1 秒样本，写入 SD 卡 WAV 和 JSON。
  - recorder 生命周期跟随危险识别后台服务：危险识别未运行时不采集、不缓存、不保存。
  - 为 SD manager 补齐 rename 文件 API，用于 `.tmp -> final` 原子提交。
  - 加 source test 或静态测试锁定依赖方向、关键路径和 WAV header 基本字段。
- 第二阶段明确要做：
  - 服务器新增 `POST /v1/watch/danger-samples`，使用设备 token 鉴权。
  - 固件新增后台补传 worker，扫描未上传样本并上传，成功后标记已上传。
- 第三阶段明确要做：
  - Android App 增加样本标注入口，例如“真危险 / 误报 / 其他”。
  - 服务器保存标注结果，作为训练集筛选依据。
- 本计划明确不做：
  - 不改变现有危险告警手机通知链路。
  - 不在 `danger_detection_service` 内直接写 SD 卡或做长耗时 I/O。
  - 不把样本闭环混入 Hermes 语音命令接口。
  - 不默认上传所有环境录音，只保存 `Alerting` 事件样本。
  - 不新增一个独立于危险识别后台服务的样本保存总开关或常驻录音服务。
  - 不让 `components/espdl_inference` 直接 include 或链接 `main/services/danger_sample_recorder`。
  - 不把 `glass_break / crash / impact` 静默并入 active danger 主线。

## 推荐文件落点

- `main/services/danger_sample_recorder.h`
  - 对外接口：
    - `danger_sample_recorder_init()`
    - `danger_sample_recorder_handle_pcm(const danger_sample_recorder_pcm_chunk_t *chunk)`
    - `danger_sample_recorder_capture_event(const danger_sample_recorder_event_t *event)`
    - `danger_sample_recorder_enable(bool enabled)` 或等价 start/stop API
    - `danger_sample_recorder_get_snapshot(...)`
- `main/services/danger_sample_recorder.c`
  - 持有 PCM ring buffer、绝对 sample index、capture generation、事件 queue、worker task、SD 文件写入和快照。
- `components/sd_card/sd_manager.h/.c`
  - 新增 `sd_manager_rename_file(const char *old_path, const char *new_path)`，封装 VFS `rename()`。
  - recorder 写完 `.wav.tmp` 和 `.json.tmp` 后，通过该 API 提交为最终文件。
- `components/espdl_inference/include/espdl_audio_runtime.h`
  - 新增 PCM tap callback 类型和注册 API，例如：
    - `espdl_audio_runtime_set_pcm_callback(callback, user_data)`
  - callback 参数应包含 `const int16_t *samples`、`sample_count`、`sample_rate_hz`、`first_sample_index`。
- `components/espdl_inference/espdl_audio_runtime.cpp`
  - 在 `resample_24k_to_16k()` 之后调用 PCM tap callback。
  - 只发布 PCM chunk 和 sample index，不知道 recorder、不做状态判断、不写文件。
  - 推理结果回调需要能让上层知道本次窗口的 `window_end_sample_index` 或等价 `window_id`，避免按“当前时间”粗略切样本。
  - `window_end_sample_index` 由 ESP-DL runtime 基于 16kHz 单声道输出流维护：每输出一个重采样样本，absolute sample index 单调递增；推理窗口的 end index 等于该窗口最后一个样本 index + 1。
- `main/features/danger_detection/danger_detection_service.c`
  - 在 `should_raise_alert` 路径里，和 `danger_detection_post_cloud_alert()` 并行触发 `danger_sample_recorder_capture_event()`。
  - capture event 应携带 `window_end_sample_index`、`alert_sequence`、`danger_prob`、`runtime_generation` 等元数据。
  - 只投递事件，不等待文件保存完成。
  - 在危险识别 runtime start/stop 路径中启停 recorder，确保用户关闭后台危险识别后样本缓存也停止。
- `server/watch_voice_endpoint/app.py`
  - 第二阶段新增 `/v1/watch/danger-samples` multipart 接口。

## 后台服务开关合同

```text
用户关闭危险识别后台服务
  -> safety_monitor_session 停止 danger_detection_service
  -> ESP-DL runtime 停止读麦克风
  -> danger_sample_recorder 停止接收 PCM
  -> 清空未完成 capture
  -> 不写 SD 卡样本

用户开启危险识别后台服务
  -> safety_monitor_session 启动 danger_detection_service
  -> ESP-DL runtime 开始读麦克风并推理
  -> danger_sample_recorder 维护最近 1 秒 PCM ring buffer
  -> 仅在 Alerting 时保存前 1 秒 + 后 1 秒样本
```

- 第一版不在 UI 上新增单独开关，样本保存视为危险识别后台服务的附属能力。
- 后续若需要隐私控制，可在危险识别设置下增加次级选项，例如“允许保存危险样本用于模型改进”；该选项只能进一步禁用样本保存，不能绕过危险识别后台服务独立启动麦克风。

## 依赖方向合同

正确依赖方向：

```text
danger_detection_service
  -> 注册 espdl_audio_runtime PCM callback
  -> 注册 espdl_audio_runtime result callback
  -> Alerting 时投递 recorder capture event

espdl_audio_runtime
  -> 只调用已注册 callback
  -> 不 include danger_sample_recorder
  -> 不知道 SD 卡、WAV、JSON、上传

danger_sample_recorder
  -> 接收 PCM chunk
  -> 接收 capture event
  -> 只负责 ring buffer、切片、SD 文件
```

禁止依赖方向：

```text
components/espdl_inference -> main/services/danger_sample_recorder
components/espdl_inference -> sd_manager
danger_sample_recorder -> danger_detection_service 内部状态
```

## FreeRTOS 与资源设计

- PCM 环形缓存：
  - 第一版默认 ring 容量：`3s * 16000 samples/s = 48000 samples`，约 96KB。
  - 当前 ESP-DL 训练和固件推理窗口均为 1 秒；实际保存样本仍为前 1 秒 + 后 1 秒，额外 ring 容量只用于吸收 capture event queue、worker 调度和短时锁竞争延迟。
  - 优先放 PSRAM，避免挤压 internal RAM。
  - ring slot 需要记录绝对 sample index，capture 时按 `window_end_sample_index` 切片，而不是按当前时刻猜测。
  - 使用 mutex 或 critical section 保护写索引、sample index 和快照拷贝。
- 捕获事件：
  - 使用 FreeRTOS queue 投递 `Alerting` 事件，队列深度第一版可为 1 或 2。
  - capture event 记录当前 `runtime_generation`；`danger_sample_recorder_enable(false)` 或 recorder stop 时由 recorder 自己递增 generation，worker 发现 generation 不一致必须丢弃旧事件。
  - 队列满时丢弃新样本并打 warning，不能影响告警通知。
- recorder worker：
  - 长期 task，收到事件后复制前置 PCM，再继续收集后置 1 秒 PCM。
  - SD 卡写文件只在 worker 中执行。
  - 任务栈和大缓冲不要放栈上，后置 PCM 缓冲使用 PSRAM 或分块写文件。
  - 危险识别 stop 时应收到 stop/disable 命令，放弃未完成 capture 并清空 ring buffer。
- SD 文件写入：
  - 目录固定为 `/sdcard/danger_samples`。
  - WAV 文件和 JSON 元数据同名不同扩展。
  - 先写 `.wav.tmp` 和 `.json.tmp`，全部成功后再 rename 为 `.wav` / `.json`；上传扫描只认最终扩展名。
  - 写入失败只记录错误并更新 snapshot，不阻塞危险识别 runtime。

## 文件格式

- WAV：`16kHz / mono / signed 16-bit PCM`。
- 文件名建议：
  - `/sdcard/danger_samples/20260703_183000_watch-001_000124.wav`
  - `/sdcard/danger_samples/20260703_183000_watch-001_000124.json`
- JSON 第一版字段：

```json
{
  "device_id": "watch-001",
  "event_id": "watch-001-000124",
  "danger_type": "danger",
  "danger_prob": 0.93,
  "alert_sequence": 124,
  "runtime_generation": 7,
  "window_end_sample_index": 12345678,
  "sample_rate": 16000,
  "channels": 1,
  "bits_per_sample": 16,
  "pre_ms": 1000,
  "post_ms": 1000,
  "uploaded": false
}
```

## 进度

- `[x]` 阶段 1A：为 ESP-DL runtime 增加 PCM tap callback 和窗口 sample index 元数据，并用 source test 锁定底层不依赖 recorder。
- `[x]` 阶段 1B：新增 recorder 接口、3 秒 ring buffer、sample index、generation、事件 queue 和 worker 骨架。
- `[x]` 阶段 1C：接入 `Alerting` 捕获事件，按 `window_end_sample_index` 生成 WAV + JSON 到 SD 卡。
- `[x]` 阶段 1D：补 `sd_manager_rename_file()`、原子写入 source test、构建验证和真机 SD 卡文件验证。
- `[x]` 代码审查修复（2026-07-04）：修复多 subagent 审查发现的14项问题，包括：
  - 解耦 recorder 与 runtime（recorder 不再直接依赖 espdl_audio_runtime.h）
  - 实现 .tmp→rename 原子写入（WAV header + PCM 数据 + JSON 元数据）
  - 修复 start_sample 计算公式
  - 用哨兵消息替代 vTaskDelete 安全删除写入任务
  - 替换所有 C++ 语法为 C 风格（static_cast→C cast, nullptr→NULL）
  - 写入任务栈从 4096 增加到 8192
  - 添加 runtime_generation 机制（stop 时递增使旧事件失效）
  - PCM tap 注册/注销移至 danger_detection_service.c
  - `danger_sample_recorder.c` 添加到 CMakeLists.txt
  - 文件格式从 PCM+meta 改为 WAV+JSON
- `[x]` 语义修复（2026-07-04）：重新修复另一轮实现偏差，确认阶段 1A-1D 语义成立：
  - ESP-DL PCM tap 改为发布 `resampled_samples` 连续 chunk，不再发布重叠推理滑窗。
  - `espdl_model_result_t` 增加 `window_end_sample_index`，Alerting capture 按该 index 对齐。
  - recorder capture 复制前 1 秒后进入 pending，由后续 PCM tap 收集后 1 秒，满 32000 样本后写入。
  - 普通危险识别 stop 只 reset recorder session，不 deinit worker/queue。
  - 子代理复查后补修：service 使用 PCM tap adapter，不再强转不兼容函数指针；capture 会回填 ring 中已存在的 post 样本。
  - source tests `26 passed`，完整 `idf.py build` 通过。
- `[x]` 无人值守 host 仿真脚本（2026-07-04）：新增 `scripts/danger_detection/simulate_danger_trigger.py`，安全模拟连续 danger 触发 Alerting、recorder capture、post backfill、早期窗口跳过和 stop/reset，生成 JSON 测试报告；新增 pytest 包装验证脚本可自动运行。相关危险识别测试 `28 passed`。
- `[x]` 真机 SD 样本闭环确认（2026-07-04）：用户补充说明已经实测，不只是 host 仿真；`Alerting -> recorder capture -> /sdcard/danger_samples` 样本保存链路按用户反馈通过。本轮未保存原始串口日志、WAV/JSON 文件名或 SD 卡截图。
- `[x]` 板端无人值守 recorder/SD 自测入口（2026-07-04）：新增默认关闭 `CONFIG_DANGER_SAMPLE_RECORDER_BOARD_TEST`，测试固件启动 60 秒后注入合成 16kHz PCM、模拟 `danger_sample_recorder_capture(1U, 0.95f, 32000ULL)`、等待 SD worker，并通过 `.wav/.json` 文件数量增量确认成功。COM3 日志 `board_logs/2026-07-04-03-21-14-danger-sample-recorder-board-test.log` 证明 `/sdcard/danger_samples/20260704/032222_1_95.wav/.json` 写入成功，`wav_before=3 -> wav_after=4`、`json_before=3 -> json_after=4`，未见 FAIL/Guru/panic；收尾已刷回默认关闭测试的正常固件。
- `[x]` 临时板端自测代码清理（2026-07-04）：COM3 自测通过后按用户要求删除 `danger_sample_recorder_board_test.c/.h`、对应 Kconfig、`app_main` 调用、CMake 接线和自测 source test；正式 recorder 与真实 `Alerting` 保存链路保留。
- `[ ]` 阶段 2A：服务器新增 `/v1/watch/danger-samples` 上传接口与测试。
- `[ ]` 阶段 2B：固件新增未上传样本补传 worker。
- `[ ]` 阶段 3：Android App 增加样本人工标注入口。

## 交给其他 agent 的执行顺序

1. 先做 ESP-DL PCM tap callback 和 source test，确认 `components/espdl_inference` 不依赖 recorder 或 SD manager。
2. 再做 recorder 的 3 秒 ring buffer、sample index、generation 和 worker。
3. 再接 `Alerting` capture，按 `window_end_sample_index` 保存前 1 秒 + 后 1 秒。
4. 最后补 `sd_manager_rename_file()` 和 `.tmp -> final` 原子写入验证。

## 决策记录

- 日期：2026-07-03
- 决策：样本闭环第一版先保存到 SD 卡，不直接在 Alerting 回调中上传大音频。
- 原因：手机通知链路要求实时可靠；音频样本是低优先级后台数据，写 SD 或上传失败不能影响告警。

- 日期：2026-07-03
- 决策：环形缓存接在 ESP-DL runtime 的 16kHz 重采样后，而不是接在 `danger_detection_service`。
- 原因：危险服务层只拥有状态机和业务语义，不拥有原始音频；ESP-DL runtime 已经持有稳定的 `16kHz / mono / int16` PCM。

- 日期：2026-07-03
- 决策：服务器样本上传使用新接口 `/v1/watch/danger-samples`，不复用 Hermes 语音命令接口。
- 原因：危险样本是训练闭环数据，生命周期、权限和存储目录都应与对话命令分开。

- 日期：2026-07-04
- 决策：样本保存跟随危险识别后台服务开关，第一版不新增独立 UI 开关。
- 原因：样本保存不是独立产品能力，而是危险识别运行时的附属证据链；关闭危险识别时必须停止麦克风采集、PCM 缓存和 SD 卡样本保存。

- 日期：2026-07-04
- 决策：ESP-DL runtime 只提供 PCM tap callback，不直接依赖 `danger_sample_recorder`。
- 原因：保持 `components/espdl_inference` 对上层 service 无感，避免底层 component 反向 include `main/services`，符合高内聚、低耦合的 owner 边界。

- 日期：2026-07-04
- 决策：样本切片按 `window_end_sample_index` / `window_id` 对齐触发窗口，不按 capture 发生时的“当前时间”粗略截取。
- 原因：Alerting 发生在推理结果回调之后，若只取当前 ring buffer，可能偏离真正触发模型的 1 秒窗口。

## 意外与发现

- 当前已有推理滑窗，但它只保存一窗和步长余量，且没有对外导出“最近事件上下文 PCM”的接口。
- SD manager 已经支持文件读写和目录创建，阶段 1 不需要新增底层 SD 驱动。
- 服务器已有 FastAPI `UploadFile` 基础，阶段 2 可以复用 multipart 处理经验，但仍需单独落库目录和鉴权测试。
- 当前 `sd_manager_write_file()` 是整文件写入；样本第一版约 64KB，可以接受，但为了避免断电留下坏样本，上传扫描必须依赖 `.tmp -> rename` 完整性规则。
- 当前 SD manager 尚无 rename API，阶段 1D 必须补 `sd_manager_rename_file()` 或明确等价 VFS 封装，否则 `.tmp -> final` 规则无法落地。

## 验证与验收

- 计划运行的验证命令：
  - `uv run python -m pytest tests/test_danger_detection_service_source.py tests/test_espdl_single_model_runtime_source.py`
  - 新增 recorder 后补充对应 `tests/test_danger_sample_recorder_source.py`
  - source test 断言 `components/espdl_inference` 不 include `danger_sample_recorder.h` 或 `sd_manager.h`
  - source test 断言 capture event / JSON 包含 `window_end_sample_index`
  - source test 断言 `sd_manager.h/.c` 声明并实现 `sd_manager_rename_file()`
  - `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 真机触发危险识别并检查 SD 卡文件。
- 期望看到的结果：
  - 构建通过。
  - 串口日志出现 recorder 初始化、capture queued、sample saved。
  - SD 卡存在 `.wav + .json` 文件。
  - WAV 可播放，约 2 秒，采样率 16kHz 单声道。
  - JSON 字段与触发事件一致。
- 当前实际结果：
  - 代码语义修复已完成（2026-07-04）：source tests `26 passed`，完整 `idf.py build` 通过。
  - 构建产物：`111.bin` `0xabfea0`，最小 app 分区剩余 `0x340160` / 23%。
  - 新增无人值守 host 仿真报告：`artifacts/danger_trigger_sim/report.json`，覆盖 4 个安全场景，脚本不访问硬件、网络或 SD 卡。
  - 真机 SD 样本闭环：用户补充确认已实测通过；本轮未保存原始串口日志、WAV/JSON 文件名或 SD 卡截图。

## 风险与控制

- 风险：SD 卡未插入或挂载失败。
  - 控制：recorder 初始化失败时降级为禁用样本保存，告警通知不受影响。
- 风险：worker 写 SD 卡时间过长。
  - 控制：只在后台 worker 写文件，不在模型回调、UI getter 或危险状态机持锁区写文件。
- 风险：底层 ESP-DL component 反向依赖上层 recorder。
  - 控制：只通过 `espdl_audio_runtime_set_pcm_callback()` 暴露 PCM；用 source test 锁定禁止 include。
- 风险：保存样本没有对齐真正触发窗口。
  - 控制：PCM ring 使用绝对 sample index，capture event 使用 `window_end_sample_index` 切片。
- 风险：停止危险识别时写出半截样本。
  - 控制：recorder stop/disable 递增 generation，worker 丢弃旧 generation 的 pending capture。
- 风险：PSRAM 或任务栈压力增加。
  - 控制：3 秒 ring buffer 和样本缓冲使用 PSRAM，任务栈只放小对象，构建后补栈水位观察。
- 风险：隐私问题。
  - 控制：第一版只保存 Alerting 前后短样本，并严格跟随危险识别后台服务开关；后续 UI 可在危险识别设置下增加“样本改进模式”次级开关。
- 风险：SD 卡无限增长。
  - 控制：阶段 1D 增加数量或容量上限策略，第一版建议最多保留最近 100 条。
- 风险：断电或重启留下半写文件。
  - 控制：写 `.tmp` 文件，WAV 和 JSON 全部完成后再 rename；上传 worker 跳过 `.tmp`。

## 幂等与恢复

- 如果中途中断，下次从 `进度` 中第一个未完成项继续。
- 如果阶段 1 接入失败，可只移除 `espdl_audio_runtime` PCM callback 注册、`danger_detection_service.c` 的 capture 事件调用和 runtime start/stop 中的 recorder 启停调用，保留未使用模块不会改变运行行为。
- 如果 SD 文件验证失败，优先检查 `/sdcard` 挂载、`/sdcard/danger_samples` 目录创建、WAV header 长度字段和 SD 卡 FAT32 格式。

## 下一步

- 下一步最小动作：先实现阶段 1A 的 PCM tap callback 合同和 source test，确认 `components/espdl_inference` 不依赖 recorder；再实现 recorder 骨架。
