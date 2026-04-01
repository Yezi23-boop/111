---
id: danger-signal-detection-port
tags: project, audio, traffic-inference, lvgl, alert, esp32-s3
summary: 记录危险音频识别移植、option_6 页面生命周期、统一提醒链路和资源释放边界。
last_reviewed: 2026-04-01
---

# 危险信号识别移植

## 入口与页面生命周期

- 主页面入口绑定在 `screen_main_option_6` 和 `screen_main_Microphone`。
- 进入页面调用 `danger_detection_ui_open()`，页面会先确保自定义 screen 已创建，再调用 `danger_detection_service_start()`。
- 返回主页时由自定义页面返回按钮触发 `danger_detection_service_stop(2000U)`，然后切回 `screen_main`。
- 当前实现复用同一个手写 screen，不在每次退出时销毁页面对象；真正需要释放的是识别任务、回调和音频资源。
- 页面视觉最终收敛为极简状态页：监听时整屏纯白，识别到危险时整屏纯红；顶部只保留一个放大后的返回箭头按钮，不显示中文状态文案。

## 识别与提醒链路

- 识别组件保留为独立 `components/traffic_inference`，内部继续使用 `audio_codec_init() -> audio_codec_read() -> 抽主麦通道 -> 24k 到 16k 重采样 -> 滑窗推理 -> 后处理告警`。
- `main/danger_detection_service.c` 负责注册 `traffic_inference_postprocess_set_alert_callback()`，并将告警映射成项目内状态快照。
- 告警提升链路为 `traffic_inference -> danger_detection_service -> app_alert_manager -> {display_alert_adapter, audio_alert_player}`。
- `app_alert_manager_raise()` 会在危险态首次进入时显示顶层危险覆盖层，并通过 `audio_alert_player_play_warning_once()` 播放一次提示音。
- `app_alert_manager_clear()` 会在后处理发出 clear 时关闭危险覆盖层。

## LVGL 线程边界

- 不沿用参考仓库里再次启动 `lv_port_init_small()` 的显示提醒任务。
- 当前仓库保持单一 `lvgl_task` 线程；`display_alert_adapter` 只缓存 show/hide 请求，由 `display_alert_adapter_process_ui()` 在 `lvgl_task` 循环中真正操作 LVGL 对象。
- 危险识别页面状态也通过 `danger_detection_controller_poll_ui()` 在 `lvgl_task` 中轮询刷新，避免后台任务直接操作 LVGL。

## 音频资源边界

- 危险识别运行时由 `traffic_inference` 内部调用 `audio_codec_init()` 和 `audio_codec_deinit()` 管理麦克风采集资源。
- 提示音播放器不重新初始化第二套 codec，只复用同一套 `audio_codec_write()` 输出一次 PCM。
- 当前项目内的 `main/assets/tishiyinpin_pcm.h` 已由参考仓库 `16kHz` 版本离线重采样并替换为 `24kHz mono PCM`，避免直接按 `24kHz` 硬件播放 `16kHz` 资源导致提示音音调和速度异常。

## 验证结论

- 源码契约测试覆盖：
  - `traffic_inference` 组件接入
  - `danger_detection_service` 与提醒链路接线
  - `option_6` 与 `lvgl_task` UI 同步接线
- `idf.py build` 已通过，说明组件依赖、主工程链接和手写页面源码均已接通。
- 真机 `flash + monitor` 已验证：
  - 进入识别页后出现 `danger detection runtime started`
  - `警笛` 和 `喇叭` 均成功触发后处理告警
  - 告警时出现 `app_alert_manager` 提升日志、`audio_alert_player` 一次性提示音日志，以及 `display_alert` 红屏显示日志
  - 返回主页后出现 `audio_codec deinitialized`、`realtime demo exiting status=ESP_OK` 和 `danger detection runtime stopped`
- 监控日志同时出现 `i2s_channel_disable(...): the channel has not been enabled yet`，但本轮释放链路最终仍成功完成；若后续继续优化，可从 `audio_codec_flush_output()` 的 TX 通道状态判断入手减少这类退出噪声日志。
