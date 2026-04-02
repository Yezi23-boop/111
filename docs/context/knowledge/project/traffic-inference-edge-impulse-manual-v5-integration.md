---
id: traffic-inference-edge-impulse-manual-v5-integration
tags: [project, audio, traffic-inference, edge-impulse, esp32s3]
summary: 记录当前仓库将 Edge Impulse `v2-cpp-mcu-v5.zip` 作为 `manual_v5` 接入 `components/traffic_inference` 的最小改动与兼容结论。
last_reviewed: 2026-04-02
---

# traffic_inference Edge Impulse manual_v5 接入

## 结论

- 当前仓库的 `traffic_inference` 组件通过 [CMakeLists.txt](D:\esp32S3\111\components\traffic_inference\CMakeLists.txt) 中的 `EDGE_IMPULSE_MODEL_DIR` 选择模型目录。
- 2026-04-02 已把模型切换到 `components/traffic_inference/edge_impulse/manual_v5`，来源是用户提供的 `D:\浏览器下载\v2-cpp-mcu-v5.zip`。
- 新导出包结构仍兼容现有接法，继续包含：
  - `edge-impulse-sdk`
  - `model-parameters`
  - `tflite-model`
  - `porting/espressif`
  - `porting/espressif/ESP-NN`
  - `porting/espressif/esp-dsp`

## 新模型关键参数

- 项目：`v2`
- `EI_CLASSIFIER_PROJECT_ID = 942602`
- `EI_CLASSIFIER_PROJECT_DEPLOY_VERSION = 5`
- `EI_CLASSIFIER_FREQUENCY = 16000`
- `EI_CLASSIFIER_RAW_SAMPLE_COUNT = 16000`
- 标签仍是 `background / horn / siren`

## 与旧模型的关键差异

- 旧的编译目标是 `manual_v4_3s`，其 `EI_CLASSIFIER_RAW_SAMPLE_COUNT = 48000`，对应 `3s` 窗口。
- 新的 `manual_v5` 改成 `16000`，对应 `1s` 窗口。
- 现有运行时代码没有写死 `48000`，而是统一通过 `EI_CLASSIFIER_RAW_SAMPLE_COUNT` 和 `EI_CLASSIFIER_FREQUENCY` 取值，因此无需额外改 `traffic_inference_runner.cc`、`traffic_inference_realtime.cc` 或后处理标签映射。
- `traffic_inference_runner_internal.h` 里的 `TRAFFIC_INFERENCE_RUNTIME_STRIDE_MS = 1000U` 与当前 `1s` 模型是对齐的。

## 最小接入步骤

1. 将 Edge Impulse 导出包完整放到 `components/traffic_inference/edge_impulse/manual_v5`。
2. 把 [CMakeLists.txt](D:\esp32S3\111\components\traffic_inference\CMakeLists.txt) 的 `EDGE_IMPULSE_MODEL_DIR` 指向 `manual_v5`。
3. 重新拉起 `ESP-IDF 5.5.3` 环境并执行 `idf.py build`。

## 已验证结果

- 2026-04-02 在 `D:\esp32S3\111` 执行：
  - `& 'D:\esp-idf\v5.5.3\esp-idf\export.ps1'; idf.py build`
- 结果：构建通过，生成 `build/111.bin`。
- 构建期间出现的是新 Edge Impulse SDK 自带的 `std::is_pod` deprecated warning，不影响当前链接成功。

## 适用边界

- 本结论适用于当前三分类标签仍为 `background / horn / siren` 的模型。
- 如果后续更换模型后标签名、采样率或窗口长度变化，需要重新检查：
  - `traffic_inference_postprocess.cc`
  - `traffic_inference_runner.cc`
  - `traffic_inference_runner_internal.h`
  - `components/traffic_inference/assets/traffic_sample_*.h`
