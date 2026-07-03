---
id: attempt-danger-sample-recorder-pending-capture-fix
tags: context, runs, attempt-log, danger-detection, espdl, sd-card, freertos, recorder, model-loop
summary: 修复危险样本 recorder 的连续 PCM、窗口对齐、pending 后置采集与 stop/start 生命周期。
created: 2026-07-04
updated: 2026-07-04
last_reviewed: 2026-07-04
status: completed
evidence_level: verified
owners: components/espdl_inference, main/features/danger_detection, components/sd_card
---

# Attempt Log: danger-sample-recorder-pending-capture-fix

## 背景

上一轮实现已经搭出 PCM tap、recorder、SD 写入和 `.tmp -> rename` 框架，但核心语义不达标：

- ESP-DL PCM tap 发布的是重叠 1 秒推理滑窗，不是连续 16kHz PCM 流。
- `danger_sample_recorder_capture()` 直接读取当前整个 ring，没有收集触发后的 1 秒样本。
- Alerting capture 没有携带 `window_end_sample_index`，无法按真正触发窗口对齐。
- `danger_detection_service_stop()` deinit recorder，但 service 自身仍保持 initialized，下一次 start 可能不再初始化 recorder。

## 本轮修改

- `espdl_audio_runtime` 在 `resample_24k_to_16k()` 后发布 `resampled_samples` 连续 chunk，`absolute_sample_index` 表示 chunk 首样本索引；推理窗口结果增加 `window_end_sample_index`。
- `danger_detection_service` 在 Alerting capture 时传入 `result->window_end_sample_index`，stop 路径改为 `danger_sample_recorder_reset_session()`，不在普通后台开关中销毁 recorder worker/queue。
- `danger_sample_recorder` 改为 3 秒 PSRAM ring；capture 时复制 `[window_end_sample_index - 16000, window_end_sample_index)` 作为前 1 秒，进入 pending 状态，由后续 PCM tap 补齐后 1 秒，满 32000 样本后投递写入队列。
- reset/session 边界递增 `runtime_generation`、清空 ring、取消 pending capture；旧 generation 的队列请求会被写入任务丢弃。
- source tests 锁定连续 PCM tap、窗口末尾 index、pending capture、stop reset、`.tmp -> rename` 和底层组件依赖方向。

## 子代理复查修复

- 修复 `danger_detection_service` 中 ESP-DL PCM tap 注册的函数指针强转：新增 `danger_detection_on_espdl_pcm_tap()` adapter，显式把 `espdl_audio_pcm_window_meta_t` 拷贝为 `danger_sample_pcm_window_meta_t`，避免不兼容函数指针调用。
- 修复 recorder capture 的后置样本回填：如果 capture 发生时 ring 中已经包含 `[window_end_sample_index, next_sample_index)` 的 post 样本，先拷贝到输出缓冲并设置 `post_collected`；若后 1 秒已完整存在，则直接投递写入队列。
- source tests 增加 adapter 与 post backfill 检查。

## 验证

- `uv run python -m pytest tests/test_espdl_single_model_runtime_source.py tests/test_danger_detection_service_source.py tests/test_sd_manager_source.py tests/test_danger_sample_recorder_source.py`
  - 结果：`26 passed`
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 结果：通过
  - 产物：`build/111.bin`
  - app size：`0xabfea0`
  - 最小 app 分区剩余：`0x340160` / 23%
- 用户补充确认：板端危险样本 SD 闭环已经实测通过，不只是 host 仿真；本轮未保存原始串口日志、WAV/JSON 文件名或 SD 卡截图。

## 结论

阶段 1A-1D 的本地 SD 样本闭环已经从“框架存在”修到“语义对齐”：

- ring 中是连续 PCM chunk；
- capture 按触发窗口末尾 index 对齐；
- 保存的是前 1 秒 + 后 1 秒；
- capture 能处理 ring 中已存在的 post 样本，不依赖默认 chunk 对齐；
- 普通 stop/start 不再破坏 recorder 初始化状态；
- ESP-DL 组件仍不依赖 recorder 或 SD manager。

## 剩余风险

- 真机 SD 闭环已由用户反馈实测通过，但本轮没有保存可复查 artifact；后续若要论文/验收证据，需要补拍 SD 文件列表、WAV/JSON 示例或串口日志。
- reset 时已经 dequeued 且正在写 SD 的旧请求可能完成写入；当前控制重点是取消 pending 与丢弃队列中旧 generation 请求，不在本轮引入更复杂的写入中断协议。
