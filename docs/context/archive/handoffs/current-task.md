---
id: context-current-task
tags: context, handoff, current-task, ai-memory-watch, hermes, v1-archive, v2-archive, inbox, thin-watch-client, thick-watch-endpoint
summary: 历史接力页归档；不再代表当前任务状态。当前状态优先看 plans/active、runs 和稳定 knowledge。
last_reviewed: 2026-07-07
memory_type: task
scope: task
owners: docs/context/archive/handoffs
triggers: handoff, current-task, next-step, ai-memory-watch, hermes, watch_voice_endpoint, v1-archive, v2-archive, inbox, websocket, conversation_polling, thin_watch_client, thick_watch_endpoint, server_session, runtime_resource_gate, foreground_runtime_gate, background_https_gate, watchface, 表盘
evidence_level: observed
status: archived
garden_status: archived
garden_reviewed: 2026-07-07
---

# AI Memory Watch / Hermes 当前任务交接

> **2026-07-07 更新：QMI8658C public API 已完成简约命名收口**
>
> - 最新边界修正：`board_imu` 不并入 `qmi8658c driver`，但已收窄为纯板级硬件事实；当前只保存 QMI8658C I2C 地址、INT1 GPIO 和 IMU 安装方向中的表盘法向轴。
> - WoM 阈值、motion window、final pose threshold 等第一版运行策略已迁到 `imu_service.c` 的 `imu_service_profile_t` / `k_imu_service_profile`；后续调参不要改 `board_imu`。
> - `components/qmi8658c/include/qmi8658c.h` 不再公开 `qmi8658c_raw_sample_t`、`qmi8658c_read_raw()` 或 raw-to-gyro conversion API；raw register decode 只留在 driver 内部。
> - 对外采样接口现在只保留完整物理六轴 `qmi8658c_read()` / `qmi8658c_sample_t`；`qmi8658c_accel_t` 表示 `m/s^2`，`qmi8658c_gyro_t` 表示 `deg/s`，字段统一为 `x/y/z`。
> - 旧公开 API `qmi8658c_read_accel_mps2()`、`qmi8658c_read_gyro_dps()`、`qmi8658c_read_sample()`、`qmi8658c_configure()` 和 `qmi8658c_configure_wake_on_motion()` 已删除，不提供兼容层。
> - `board_imu` 删除 `accel_lsb_per_g`，表盘法向阈值改为 `face_axis_threshold_mg=-397`；service 不再知道 QMI raw LSB。
> - `imu_service` snapshot 使用 `last_wom_accel` / `last_wom_status` / `last_final_sample`；WoM 触发帧通过 `qmi8658c_read()` 读取完整六轴后只记录物理加速度，运动窗口与 final pose 使用 `physical_6axis`、`accel_mg`、`gyro_mdps` 日志字段。
> - 后续 fall window 应直接使用物理六轴样本，再做模型输入坐标系 `accX/accY/accZ/gyroX/gyroY/gyroZ` 命名与轴向映射；不要从旧 context 的 `raw_motion` 口径继续扩展。
> - 验证：source tests 23 passed；`git diff --check` 仅 LF/CRLF warning；`idf.py build` 通过，`111.bin` `0xac3230`，app free `0x33cdd0`/23%；context standard 校验错误 0、警告 0。attempt log：`docs/context/runs/2026-07-07-attempt-board-imu-hw-profile-split.md`。

> **2026-07-06 更新：摔倒检测旧模型路线已从当前仓库清理**
>
> - 用户已明确分工：`D:\esp32S3\imu` 只负责复杂训练、评估和模型导出；当前仓库 `D:\esp32S3\111` 只负责固件部署，不放训练脚本、完整数据集或临时训练资产。
> - 旧 Edge Impulse 208622 外部 `.espdl` 路线在板端 `new dl::Model(...)` 阶段触发 ESP-DL loader `LoadProhibited`，不要再把它当作可继续部署的当前状态。历史失败日志仍可参考 `board_logs/2026-07-06-21-45-54-fall-detection-board-test.log`。
> - 当前仓库已删除旧 `components/fall_detection_inference`、旧 `fall_detection_board_test`、旧 source test、旧 active plan 和旧 runner attempt 文档；不要再相信旧状态里“`CONFIG_FALL_DETECTION_BOARD_TEST` 可开启验证”的描述。
> - 后续若从 `D:\esp32S3\imu` 产出新的 `.espdl`，必须先确认 ESP-DL loader 可加载、算子属于 ESP-DL 支持范围，再回到当前仓库新增最小部署组件。
> - 保留部署侧地基：QMI8658C driver 统一输出物理六轴，后续用 `qmi8658c_read()` 获取 `m/s^2` 加速度与 `deg/s` 角速度；driver 只负责 raw-to-physical，不负责板级安装方向。
> - 保留未来窗口边界：IMU 4 秒窗口由 `imu_service` 维护 50Hz/200 帧采样，并投递完整窗口副本给未来 `fall_detection_service`；fall service 不主动读取 IMU 硬件或维护采样时钟。
> - 保留命名口径：模型输入坐标系使用 `accX/accY/accZ` 与 `gyroX/gyroY/gyroZ`；轴向重映射应在 board/imu 侧完成，不放进模型 runner。

> **2026-07-05 更新：危险识别页已新增麦克风测试按钮**
>
> - 危险识别页新增“测麦克风”手动诊断入口；UI 显示“未测试 / 测试中 / 通过 / 失败原因”。
> - 新增 `audio_mic_test_service` 作为麦克风测试 owner：点击按钮后在一次性 FreeRTOS task 中暂停 Safety Monitor、申请 `AUDIO_CODEC_OWNER_AUDIO_RECORDER`、设置 36dB 增益、录制 5 秒硬件原始 PCM。
> - 输出为 `/sdcard/mic_tests/<timestamp>_mic_raw.wav` 与 JSON 报告；串口会打印 `MIC_TEST: STATS ...` 和 `MIC_TEST: DONE status=pass|fail`。
> - 该入口只验证麦克风/I2S/ES7210/SD 录音链路，不经过 ESP-DL、不触发危险告警、不联网。
> - 验证：相关 source tests `32 passed`，host preview 截图 `main/ui/agent_preview/artifacts/danger-mic-test-preview.png` 已生成，完整 `idf.py build` 通过。
> - 最新真机反馈：两次测试均完整读取 `480000/480000` 字节并写出 WAV，但 `ch0_rms≈40~42`、`ch0_peak≈182~194`，用户回放反馈“几乎听不到声音”。这说明 `audio_codec -> I2S -> SD` 通路是活的，但输入幅度异常低；优先查通道映射、ES7210 channel mask、麦克风偏置/焊接/声孔，不要先从 ESP-DL 模型阈值排查。
> - 已增强诊断：串口会逐通道打印 `MIC_TEST: CH0/CH1 rms/peak/zero_pct/clip/samples/role`，并记录 36dB 增益设置结果；如果 M 通道无输入但其他通道有输入，会显示 `通道不匹配`。下一次真机测试重点看 CH0(role=M) 和 CH1(role=R) 谁有明显 peak。

> **2026-07-05 更新：危险识别三档灵敏度已接入**
>
> - `sensitivity_mode` 首版已实现为 `保守 / 标准 / 敏感` 三档，默认 `标准`，当前不做 NVS 持久化。
> - 三档由 `danger_detection_service` 持有并映射 ESP-DL 单窗阈值：保守 `0.95`、标准 `0.90`、敏感 `0.85`；`espdl_model_runner` 只接收数值 threshold，不理解用户语言。
> - 危险识别页新增三段式选择控件；UI 只显示中文模式，不显示原始阈值数字。
> - 验证：目标 source tests `24 passed`，完整 `idf.py build` 通过；host preview 已新增 `-OpenDanger`，截图为 `main/ui/agent_preview/artifacts/danger-sensitivity-preview.png`。

> **2026-07-05 更新：危险告警首次强震 owner 已接入**
>
> - 新增 `haptic_alert_player` 作为震动提醒 owner；`board_ds2413_motor` 仍只负责 DS2413 硬件开关，危险识别服务不直接调用马达。
> - `app_alert_manager` 在新的 `APP_ALERT_SEVERITY_DANGER` 首次 raise 时异步触发首次强震；重复同源 active 告警只更新标签，不重复震动。
> - 首版强震模式为 `220ms on -> 90ms off -> 220ms on`，由短生命周期 FreeRTOS task 执行，任务退出前兜底关闭马达并自删。
> - 本轮不做持续提醒 `realert_rule`、用户通知模式、持续提醒开关或事件记录。
> - 已通过相关 source tests；完整构建与 context 校验结果见本轮收尾。

> **2026-07-04 更新：DS2413 马达板级能力已合入主线**
>
> - 本轮按“板级能力”范围从 `59e29713` 抽取 DS2413 马达控制，不 cherry-pick `app_main_test.c` 或 `scratch/ds2413_motor_migration`。
> - 新增 `components/ds2413` 最小 1-Wire 驱动和 `main/app/board_ds2413_motor.c/.h`；GPIO18 为 1-Wire 总线，只使用 RMT backend（已按用户要求删除 UART1 兜底），FreeRTOS mutex 串行化访问，PIOA pull-low 为马达关闭态。
> - `hardware_init()` 已在 NVS 后、SD/codec 等较慢初始化前调用 `board_ds2413_motor_init()`；初始化失败只 warning，不阻断 Board Foundation。
> - 已补充 DS2413 公开接口、RMT-only、FreeRTOS mutex、open-drain release/pull-low 和协议字节注释；这轮只改注释，不改变马达运行行为。
> - 本轮未把马达接入 `app_alert_manager`、危险提醒、低电量提示、Hermes 通知或 UI；`power_policy.haptic_alert_allowed` 语义未变。
> - 闭环验证：`python -m unittest tests.test_board_ds2413_motor_source tests.test_nonblocking_boot_source tests.test_power_integration_source tests.test_runtime_resource_gate_board_test_source` 31 passed；发现并修复 `sdkconfig` 漂移，已恢复 `# CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST is not set`，避免正常固件自动运行 runtime gate 板端压测。按规则执行 `idf.py fullclean; idf.py build` 通过，`111.bin` `0xac7130`，最小 app 分区剩余 `0x338ed0`/23%。
> - 板端证据：COM7 `app-flash` 成功，pyserial 复位日志 `board_logs/2026-07-04-20-47-24-ds2413-normal-com7-pyserial.log` 出现 `DS2413 ROM via RMT: BA DC CD 73 50 05 10 46`、`DS2413 motor default off: raw=0x78 PIOA(state=0 latch=0)`、`boot_stage: startup_sequence_done`、`boot_stage: ui_first_frame_ready` 和 `boot_stage: cold_boot_resource_snapshot_done`；未出现 `runtime_gate_test`、Guru、panic、watchdog、`ESP_ERR_NO_MEM`。

> **2026-07-03 更新：危险 Alerting 手机通知链路已真机跑通**
>
> - ESP32 固件已接入首版危险告警云端 POST：`danger_detection_service` 在 `Alerting` 首次确认时投递中性 `watch_endpoint_service_post_danger_alert()`，不再直接依赖 Memory Watch / Hermes 命名。
> - 固件 POST 目标复用当前 watch endpoint 配置，路径为 `/v1/watch/alerts`；服务器已在 2026-07-03 打开设备 token 鉴权，固件端 `device_token` 必须与 server `WATCH_DEVICE_TOKENS` 匹配。
> - `watch_endpoint_service` 已接管危险告警 worker queue/task：自己创建单槽静态 FreeRTOS queue 和 `watch_alert` PSRAM worker；HTTPS 在该 worker 内执行，危险识别回调只投递队列，不阻塞 ESP-DL 音频推理路径。
> - `memory_watch_service` 不再暴露 `memory_watch_service_post_danger_alert()`，只继续作为 endpoint 配置/NVS owner，并通过 `memory_watch_service_copy_endpoint_config()` 给中性服务提供只读配置快照。
> - 验证：相关 source tests `44 passed`；`. D:\esp-idf\v5.5.3\esp-idf\export.ps1; ninja -C build __idf_main` 通过，`main` 组件链接成功；完整 `idf.py build` 通过，`111.bin` `0xabde50`，最小 app 分区剩余 `0x3421b0`/23%。
> - 真机证据：2026-07-03 用户反馈“真机测试没问题”，确认 `Alerting -> HTTPS POST -> Android App -> 手机通知栏` 第一版链路可用；未保存原始串口日志、设备 token 或手机截图。
> - 鉴权验证：watch endpoint server tests `144 passed`；Docker 容器已重建；公网无 token POST 返回 `401 missing_bearer_token`，合法 token 公网 smoke 返回 `HTTP 200 ok=True`，验证未打印真实 token。
> - 后续正式化：Android App 补电池优化白名单提示、最近告警历史和断线状态更醒目的提醒。
> - 2026-07-02 更新：`resources` LittleFS `LFS_ERR_NOSPC` 阻塞已解除；删除 `resources/watchface` 后完整 `idf.py build` 已通过。

> **2026-07-04 更新：危险样本 SD recorder 语义修复已完成**
>
> - 第一版本地模型闭环仍是前 1 秒 + 后 1 秒 WAV/JSON，不上传服务器，不新增 UI 开关，跟随危险识别后台服务开关。
> - ESP-DL PCM tap 已改为发布连续 `resampled_samples` chunk；推理结果带 `window_end_sample_index`，Alerting capture 按该 index 切片。
> - recorder 使用 3 秒 PSRAM ring，capture 先复制前 1 秒，再 pending 收集后 1 秒，凑满 32000 样本后投递 SD 写入 worker。
> - 子代理复查发现的两项风险已修复：service 使用 PCM tap adapter，不再强转不兼容函数指针；capture 会回填 ring 中已存在的 post 样本，不依赖默认 chunk 对齐。
> - 普通 `danger_detection_service_stop()` 只调用 `danger_sample_recorder_reset_session()`，不 deinit recorder，避免 stop 后再次 start 时 recorder 不再初始化。
> - 验证：相关 source tests `26 passed`；完整 `idf.py build` 通过，`111.bin` `0xabfea0`，最小 app 分区剩余 `0x340160`/23%。
> - 真机证据：用户补充确认已实测，不只是 host 仿真；`Alerting -> recorder capture -> /sdcard/danger_samples` 样本保存链路已按用户反馈通过。未保存原始串口日志、WAV/JSON 文件名或 SD 卡截图。
> - 2026-07-04 新增默认关闭板端自测入口：`CONFIG_DANGER_SAMPLE_RECORDER_BOARD_TEST` 开启后启动 60 秒注入合成 PCM 并模拟 capture；COM3 日志 `board_logs/2026-07-04-03-21-14-danger-sample-recorder-board-test.log` 已证明新增 `/sdcard/danger_samples/20260704/032222_1_95.wav/.json`，`wav_before=3 -> wav_after=4`、`json_before=3 -> json_after=4`，无 FAIL/Guru/panic。测试后已刷回默认关闭测试的正常固件。
> - 2026-07-04 随后按用户要求删除上述临时板端自测代码：正常固件不再包含 `danger_sample_recorder_board_test`、`CONFIG_DANGER_SAMPLE_RECORDER_BOARD_TEST` 或 60 秒自动写 SD 入口；正式 recorder/SD 保存链路仍保留。

> **2026-07-01 插入：表情表盘重执行状态**
>
> - 用户已回退上一轮表情表盘代码；当前不要继续修补旧实现。
> - 当前事实：`main/ui/custom/watchface_view.c` 不存在，`scripts/watchface/` 仍存在，`resources/watchface/` 已删除，`partitions.csv` 中 `resources` 分区为 `4M`。
> - active plan 已重写为 `docs/context/plans/active/2026-06-30-watchface-emoji-root-ui-plan.md`。
> - 用户最新决策：SD 卡已经从电脑拔下并放进手表；当前直接走 SD 卡路径，电脑端目录 `E:\watchface` 对应板端 `/sdcard/watchface`。
> - 当前表盘资源路径口径：`/sdcard/watchface` 是板端表盘动画仓库，电脑端预览帧默认生成到 `sdcard/watchface/frames`，不能进入 `resources/`。
> - 2026-07-01 最新结果：`scripts/watchface/pack_watchface_rawanim.py` 已完成，按 8MB resources 上限实测 raw animation 总量 `22,238,518 bytes`，超过用户阈值，因此已生成 SD 卡 staging。`components/sd_card/sd_manager.c` 已补齐头文件声明的通用文件 API，后续按 SD 卡路线实现 cache loader。
> - 2026-07-02 最新修复：删除 tracked `resources/watchface`，修复 `LFS_ERR_NOSPC`；当前 `resources/` 约 `3.07 MiB`，`idf.py build` 已通过。
> - 后续实现硬约束：LVGL draw 阶段不能直接从 resources/SD/LittleFS 流式读取全屏表盘资源；必须先加载到 PSRAM，再通过内存 `lv_image_dsc_t` 渲染。
> - 上一轮崩溃签名是 `Cache disabled but cached memory region accessed`，回溯涉及 LittleFS/flash 读取和 `lv_bin_decoder_get_area`；Wi-Fi 只是触发时序，不是根因。resources 测试也不能让 LVGL 直接读文件绘制。

> **2026-06-28 插入：全国嵌入式比赛报告收尾状态**
>
> - 本轮完成 paper-spine-research 阶段：已补充 `source_index.md`、`research_dossier.md`、`style_profile.md`、`sota_gap_map.md`、`motivation_options_after_research.md`、`confirmed_motivation.md`。
> - 已更新 `evidence_bank.md` 与 `figure_asset_map.md`，填入模型大小、板端状态机/Hermes 日志等可验证证据。
> - 已重写 `paper_rewriting_output/final_paper/main.tex`，补入实测性能指标和第 3.5 小节“关键测试证据整理”。
> - 本会话内命令执行工具异常，无法编译 PDF 与运行服务器测试，需用户本地编译并补拍实物照片/界面截图。
> - 下一步：补拍 F07 手表实物照片、F08 危险提醒界面截图、F09 Hermes 页面/回执截图；录制断网本地提醒视频；运行 `pytest tests/test_app.py` 并截图；编译 PDF 并检查匿名与字数。


## 目标

- 当前目标已从“打通 V1 主链路”推进到“V1 已归档 + V2 已归档 + V2.1 WebSocket 完成 + V2.2 前台 WS/后台 conversation polling 完成 + V2.3 Thin Watch Client / Thick Watch Endpoint 完成并归档”，现在进入 V2.4 ESP32 真实瘦身计划。
- V1 主链路已完成：`ESP32-S3 真机麦克风 -> Ogg Opus -> watch endpoint -> MiMo ASR -> Hermes -> 手表 V1 固定 7 字段 JSON`。
- V2 主链路已完成：`Hermes/脚本模拟主动提示 -> watch endpoint inbox -> SQLite -> 公网 GET 读回 -> ESP32 收件箱/全局气泡能力`。
- V2.1 主链路已完成：前台 Hermes 页面使用 `WSS /v1/watch/ws` 上传 Ogg Opus、ASR 先显用户消息、assistant reply 后显 Hermes 消息。
- V2.2 服务器与固件骨架已完成：前台 Hermes 页面仍用 WS，离页 pending 时关闭 WS，后台 HTTP `GET /v1/watch/conversation` 每 5 秒轮询取回结果。
- V2.2 最新真机反馈：前台 Hermes 页面可正常使用，离开 Hermes 页面后也能通过气泡收到 Hermes 回复；同一回复重复显示的问题已在代码侧修复并经用户复测确认当前没有问题。
- V2.3 已完成：把 ESP32-S3 手表继续做薄的第一轮 server 侧地基已落地；server session_repo 成为任务状态真相源，watch endpoint 增加 session 状态查询和 WS session 状态转移，ESP32 侧保持已验证显示去重路径。
- V2.4 新目标：真正减少 ESP32-S3 端 Hermes 状态理解、重复去重、协议细节扩散和资源占用；这是 server + ESP32 端到端瘦身计划，ESP32 端允许修改，但必须围绕职责变薄、体验不回退、资源不恶化推进。
- 不要再相信旧 handoff 里“真机按住说话未验证”“板端缺 endpoint 配置导致无法验证”的状态；这些已经被 2026-06-17 之后的证据反转。
- 不要再相信旧状态里“V2 通知箱尚未实现代码”或“需要新建 V2 计划”的表述；`2026-06-25-hermes-inbox-global-notification-plan.md` 已在 `completed/` 归档。

## 当前状态

- 当前分支：`codex/ai-memory-watch-hermes-api`。
- 主计划 `2026-06-05-ai-memory-watch-hermes-page-plan.md` 已从 `plans/active/` 移到 `docs/context/plans/completed/`，状态为 `archived`。
- SoftAP/NVS 配置计划 `2026-06-17-ai-memory-watch-softap-nvs-config-plan.md` 已在 `docs/context/plans/completed/` 归档。
- V2 收件箱与全局气泡计划 `2026-06-25-hermes-inbox-global-notification-plan.md` 已在 `docs/context/plans/completed/` 归档，状态为 `archived`。
- Hermes API Server、watch voice endpoint、Cloudflare Tunnel、公网 `watch.934000.xyz/v1/watch/*`、文本命令和真实麦克风语音链路均已有成功证据。
- 当前 watch endpoint 容器已重建到 V2.2 server 代码，本机 `127.0.0.1:8787/health` healthy，公网 `watch.934000.xyz` runtime gate、WS smoke、conversation polling smoke 均通过。
- V2.3 计划已归档：`docs/context/plans/completed/2026-06-27-ai-memory-watch-hermes-v2.3-thin-watch-client-thick-watch-endpoint-plan.md`。
- V2.4 active plan 当时已新增并开始执行；该计划现已归档到 `docs/context/plans/completed/2026-06-27-ai-memory-watch-hermes-v2.4-esp32-thin-client-slimming-plan.md`。
- V2.4 阶段 0/1/1.5/2/3/4/5 已完成：基线复核、ESP32 职责审计、server `/v1/watch/sync` 契约测试与 endpoint、ESP32 `/sync` 窄客户端、后台 pending/foreground reconcile sync 换芯、WS client 意图级收口、旧 `/conversation` poll client 删除和 `done` 空回复兜底修正已落地。当前验证：server tests `140 passed`，ESP32 Memory Watch source tests `40 passed`，`idf.py build` 通过（`111.bin` `0xabef80`，最小 app 分区剩余 `0x341080`/23%）。
- V2.4 阶段 6 已完成部分真机验收：用户修复 Mihomo/Fake-IP DNS 后，COM3 日志显示手表联网到 `192.168.103.11`、进入 `SERVICE_READY`、SNTP 同步成功、Hermes health online、inbox poll 正常；watch endpoint 容器重建后 `/v1/watch/sync` 路由已部署，本机/公网未授权请求返回 401，真机日志出现 `conversation: sync ok messages=0 session=none terminal=0`；前台 WSS 真麦克风链路再次成功，返回 `status=done/action=conversation_reply/error_code=none`，`mw_upload` high-water 约 `3172` words。
- Runtime Resource Gate active plan：`docs/context/plans/active/2026-06-29-watch-runtime-resource-gate-plan.md`。阶段 1-5 代码已完成：强前台 owner、Safety Monitor/ESP-DL 让路、Hermes 前台 acquire/release、后台 HTTPS gate、Bluetooth quiet-window 单次重试均已接入；阶段 6 已完成板端自动 gate/BLE fail-closed 回归，公网 HTTPS 成功路径和 ESP-DL running -> 强前台让路仍待 Wi-Fi 可用后补测。
- 新版 watch endpoint 在本机 `127.0.0.1:8787` 暴露 `/v1/watch/inbox` 和 `/v1/watch/inbox/{notification_id}/read`；旧 LAN 调试容器 `8788` 曾无 inbox，后续调试优先走公网或新版 `8787`。
- 开发阶段允许本机 `sdkconfig` 或 NVS 持有 watch device token 进行联调；提交前必须确认 `sdkconfig`、文档、日志和 diff 不包含真实 token。

## Progress

- V1 独立 Hermes 页面已落地，不复用 `official_chat`。
- V1 服务器侧 watch endpoint 已支持 health、voice-command、text-command、cancel、request 幂等、auth 诊断、运行态指标、115 秒服务器预算和公网私有路径门禁。
- V1 固件侧已支持 `memory_watch_service` owner task、upload/health/cancel worker、Ogg Opus recorder、voice client、text command、NVS 配置读取/保存、SoftAP 配置入口和 Kconfig 开发默认项。
- `mw_upload` 实机栈问题已修复：upload worker 栈迁移到 PSRAM 并提升到 `24576` words，大 `job/result` 对象移出任务栈。
- FreeRTOS queue copy 后的 `client_config` 指针 rebind 已修复：upload/health/cancel worker 收到 job 后重新绑定指针到 job 内部字符数组。
- COM3 真机麦克风链路已成功：串口 high-water mark 约 `3248` words，返回 `status=done/action=memory_saved/error_code=none`；服务器 `/health` 最近请求摘要显示真实 Ogg Opus、`asr_provider=mimo` 和成功耗时，不包含正文或 token。
- `system_time_sync` 临时网络同步 HTTP 任务栈外移至 PSRAM：解决设备开机连网时 SRAM 连续碎片不足报 `create network time sync task failed`（返回 `pdFAIL`）的崩溃。
- `memory_watch_controller` 渲染快照引入 `inbox_generation` 数据版本跟踪：解决后台短消息轮询到达时页面静默死锁不刷新的 Bug。
- `memory_watch_service` 收件箱大段 PSRAM 内存拷贝脱离 `portENTER_CRITICAL` 自旋锁：引入互斥锁 `s_inbox_store_mutex` 替换硬件自旋锁 `s_worker_lock`，消除 PSRAM 慢速访问引发的 CPU 双核中断屏蔽与 Cache 异常。
- V2 公网脚本验收已通过：脚本模拟 Hermes 写入一条 inbox 消息，公网 GET 成功读回创建项，摘要为 `201 Created`、`found_created_item=true`、`unread_count=2`；未记录真实 token/key。
- V2.2 公网脚本验收已通过：`conversation_polling_smoke_test.ps1 -BaseUrl "https://watch.934000.xyz"` 证明 WS 发完音频断开后，HTTP conversation polling 可拉到 user message 和 assistant `done` reply。
- V2.2 真机反馈已证明前台 WS 和离页气泡链路可用；重复回复修复为后台 polling terminal reply 标记 `conversation_already_appended`，避免 server conversation 已合并后 worker done 二次 append 到本地对话；用户复测确认当前无问题。
- V2.4 server `/sync` 首个闭环已完成：`GET /v1/watch/sync` 支持 `mode=background|foreground_reconcile`、`pending_request_id`、`after_message_id`、`max_messages`，返回公开 `session_state`、conversation delta、`inbox.unread_count` 与 `latest_unread` 摘要；新增 `server/watch_voice_endpoint/tests/test_sync.py` 覆盖契约。
- V2.4 ESP32 `/sync` client 已完成：`memory_watch_voice_client_sync()` 可构建统一 sync URL，解析公开 `session_state`、conversation messages 和 inbox 最新未读摘要；response buffer 走 PSRAM 优先分配，固件源码不包含 `poll_after_ms` 或 server 内部 session state 名。
- V2.4 `memory_watch_service` 后台 pending 首轮已改薄：保留原 worker/queue 外壳以降低回归风险，但 HTTP 调用改为 `memory_watch_voice_client_sync()`；离页 pending 走 `mode=background`，进入 Hermes 页面走 `mode=foreground_reconcile`。本地 10 分钟 `conversation_poll_timeout` 已删除，长任务终态改看 server `session_state`。
- V2.4 WebSocket 首轮已收口：`memory_watch_ws_client` 负责把原始 frame 映射成业务 event kind，并提供 `memory_watch_ws_client_send_audio_turn()`；`memory_watch_service` 不再直接判断 `asr_result/conversation_message/error` frame 名，也不再手写 start/chunk/end 发送序列。
- V2.4 阶段 5 代码侧瘦身已完成：ESP32 `memory_watch_voice_client` 不再暴露旧 `memory_watch_voice_client_conversation_poll()`，后台 conversation delta 只走 `/v1/watch/sync`；`memory_watch_service` 不再保留旧本地 pending 起始时间计时；`session_state=done` 但没有 assistant message 时继续补拉，不生成空回复。
- V2.4 阶段 6 部分验收证据已记录：`board_logs/2026-06-28-19-27-07-hermes-v24-stage6-dns-fixed-verify.log` 证明 DNS 修复后 health/inbox/WSS 真麦克风链路恢复；`board_logs/2026-06-28-19-30-47-hermes-v24-stage6-sync-deployed-verify.log` 证明 `/sync` 容器部署后真机可完成 `conversation: sync ok` 并再次完成前台 WSS 真麦克风请求。两轮均未见 Guru、panic、stack overflow、`Error parse url`。
- V2.4 后台 `/sync` 数据面已脚本化验证：容器内注入 `codex-stage6-sync-bg-20260628` 测试 session/conversation 后，本机和公网 `/sync?mode=background&pending_request_id=...` 均返回 `session_state=done` 与 user/assistant messages；带 `after_message_id=<user message>` 时公网 `/sync` 只返回 assistant 增量。该验证不打印真实 token。
- V2.4 真机发现新的离页时序问题并已代码修复：日志 `board_logs/2026-06-28-19-52-48-hermes-v24-stage6-background-sync-bubble-60s-retry.log` 中，ESP32 在 `audio_end` 后立即关闭 WS 并开始后台 polling，但 server 侧该 request 的 session/conversation 计数均为 0，导致 `/sync` 持续 `session=none/messages=0`，UI 一直“思考中”。已改为收到 `TURN_ASR_READY` 后设置 `kWsWaitAsrReadyBit`，离页时只有 `asr_ready_seen=true` 才关闭 WS 并切后台 `/sync`。验证：Memory Watch source tests `40 passed`，server `test_sync.py` `14 passed`，`idf.py build` 通过（`111.bin` `0xabef90`，app free `0x341070`/23%），`idf.py -p COM3 app-flash` 成功。
- V2.4 阶段 6 用户真机复测已完成：重新执行“Hermes 页面按住说，松手后立刻离开页面”后，后台 `/sync` 不再长期 `session=none`，此前离页后一直“思考中”的阻塞解除。
- Watch Runtime Resource Gate 阶段 1 已完成最小地基：`foreground_runtime_gate` 使用 `portMUX_TYPE` 保护当前强前台 owner 和 quiet window，无动态内存、无 task、无 queue、无 callback，不直接 stop Wi-Fi/BLE/ESP-DL/Hermes；source test 已用 `python -m unittest tests.test_foreground_runtime_gate_source` 通过，`idf.py build` 通过（`111.bin` `0xabc140`，最小 app 分区剩余 `0x343ec0`/23%）。
- Watch Runtime Resource Gate 阶段 2 已接入 Safety Monitor / ESP-DL 让路：`background_service_manager` 在合成 Safety Monitor 目标态时读取 `foreground_runtime_gate`，强前台 active 时阻塞原因为 `FOREGROUND_RUNTIME`，ESP-DL 不启动或恢复；gate 仍不直接 suspend/delete ESP-DL task。
- Watch Runtime Resource Gate 阶段 3-5 已完成：Hermes 前台通过 `memory_watch_service_set_foreground_active()` 持有 `FOREGROUND_RUNTIME_OWNER_HERMES`；后台 Memory Watch health、`/sync`、inbox poll、mark-read 和天气 HTTPS 通过 `background_https_gate` 串行/quiet window 错峰；主界面 Bluetooth 显式点击路径增加 BLE 前台 owner、后台 HTTPS quiet window 和 `ESP_ERR_NO_MEM` 单次重试。
- Watch Runtime Resource Gate 阶段 6 板端自动测试入口已落地并默认关闭：`CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST` 默认 `n`，正常固件中 `runtime_resource_gate_board_test_start()` 空返回；开启后可自动触发 Hermes foreground、background HTTPS busy/quiet、Memory Watch health/inbox、BLE owner 和可选真实 BLE toggle。
- Watch Runtime Resource Gate 阶段 6 COM3 自动 gate 压测已通过：`board_logs/2026-06-29-10-18-11-runtime-resource-gate-board-test-auto.log` 完整跑完，无 Guru/panic/stack overflow/NO_MEM；验证强前台 owner、后台 HTTPS busy/quiet 拒绝和 BLE owner acquire/release。
- Watch Runtime Resource Gate 阶段 6 真实 BLE toggle 首轮发现 PSRAM task stack + NVS/flash 写入 cache-disabled 断言，已修复为真实 BLE toggle 测试模式使用 internal stack；修复后日志 `board_logs/2026-06-29-10-31-09-runtime-resource-gate-board-test-real-ble-internal-stack.log` 未见崩溃，BLE guard 以 `ESP_ERR_NO_MEM` fail closed。
- Watch Runtime Resource Gate 收尾已刷回默认关闭测试的正常固件：`board_logs/2026-06-29-10-42-20-runtime-resource-gate-normal-after-test.log` 显示 45 秒监控通过，无 `runtime_gate_test` 自动压测、Guru、panic 或 stack overflow。
- 2026-06-30 处理 `official_chat` 前台 SR 崩溃：全量刷入 291KB wake word 模型后仍在 `esp_srmodel_init("model") -> srmodel_load` 崩溃，已证伪“打开模型即可解决”。当前产品不需要本地唤醒词，已改为 `official_chat` 启动时跳过本地 SR model loader，并关闭 `CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS`；同时把 AFE 兜底改成模型表为空时直接透传，不再偷偷回到 SR loader。构建和全量 flash 通过，90 秒 COM3 启动监控未见 `MODEL_LOADER`/Guru/panic。

## Decision Log

- V1 到此停止加功能；后台通知、TTS、历史列表、长任务主动反馈都放到 V2。
- V2 当前定义固定为：`Hermes 主动提示回到手表：server inbox + ESP32 收件箱 + 全局气泡通知`，并已归档完成。
- ESP32-S3 只调用 watch endpoint，不直接调用 Hermes Dashboard、Hermes API Server 或 MiMo API。
- Hermes/MiMo/API key 只保留在服务器或仓库外 env；ESP32 固件最多保存 watch endpoint 的 `device_id/device_token/base_url`。
- 公网第一版只允许代理 `/v1/watch/*`；Hermes `8642` 和 Dashboard `9119` 保持私有。
- 开发阶段可把 watch device token 放本机 `sdkconfig`，但不得提交；正式/演示前建议轮换 token。
- V2.2 当前定稿：前台 Hermes 页面 `WS` 实时；离开 Hermes 页面后如果有 pending，关闭 WS 并通过 HTTP conversation polling 取回；无 pending 时只保留 inbox 低频轮询。
- V2.3 当前结果：Thin Watch Client / Thick Watch Endpoint 第一轮完成，server session 层已新增并接入 WS 路径；后续若继续推进，应以公网部署验证、COM3 前台/离页复测和进一步 ESP32 接口瘦身为主。

## 已验证

- 服务器 release gate、mock/real ASR smoke、cancel、invalid-token 403、Cloudflare 私有路径门禁均已通过过。
- 公网 `https://watch.934000.xyz/v1/watch/health` 可用，公网 `/health`、`/v1/models`、`/v1/responses` 不公开。
- 真机文本命令和真机麦克风 Ogg Opus 端到端链路均已成功。
- context 校验在最近文档更新中多次通过；V1 归档后仍需再跑一次 standard 校验。
- 最新 V2.2 验证：server pytest `91 passed`，Memory Watch source tests `39 passed`，`idf.py build` 通过，`idf.py -p COM3 app-flash` 通过，30 秒启动 smoke 通过，公网 private exposure gate/WS smoke/conversation polling smoke 通过。
- 最新 V2.4 阶段 6 部分验证：用户已修复 Mihomo/Fake-IP DNS 影响；watch endpoint 容器重建后 `/v1/watch/sync` 本机/公网路由存在；COM3 证明 health、inbox、`conversation: sync ok` 和前台 WSS 真麦克风请求成功。
- 最新 V2.4 服务器数据面验证：后台 `/sync` 对 pending done session 能返回 assistant reply；带 `after_message_id` 时只返回 assistant 增量，满足离页 pending 的数据契约。
- 最新 V2.4 代码与真机验证：修复 WS 过早 detach 后，source tests、`idf.py build`、`idf.py -p COM3 app-flash` 均已通过；用户复测确认后台 `/sync` 不再长期 `session=none`。
- 最新 Runtime Resource Gate 代码验证：阶段 1-5 相关 source tests `75 passed`；阶段 6 板端测试相关 source tests `39 passed`；收尾时已恢复 `sdkconfig` 为 `# CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST is not set`，并执行 `idf.py fullclean; idf.py build` 通过（`111.bin` `0xabcc80`，最小 app 分区剩余 `0x343380`/23%），COM3 `app-flash-monitor` 45 秒通过。

## 当前风险

- 工作区有大量已有未提交改动，且 `sdkconfig` 现在可能含开发期 watch device token；不要误提交。
- `docs/context/archive/handoffs/current-task.md` 是历史接力页归档，不代表当前任务状态；当前状态优先看 `plans/active/`、`runs/` 和稳定 knowledge，历史细节看 changelog 与 completed plan。
- 最新用户日志中的问题不是 Hermes 链路问题。第一轮是点击主界面 Bluetooth 后普通 BLE presence 进入 BT controller 初始化时 internal heap 不足触发 `BLE_INIT: Malloc failed` / `emi.c` assert / interrupt WDT；已加 `ble_presence` preflight 和 `network_manager` 回滚保护。第二轮复测不再崩溃，但 BLE enabled 偏好在开机 latest Wi-Fi 路径自动启动普通 BLE，抢占 LVGL/SPI DMA internal RAM，导致 display bounce / flush `ESP_ERR_NO_MEM`；已改成后台路径只收口不自动启动，只有用户显式 Bluetooth 开关才允许启动 BLE。第三轮冷启动复测已通过：未自动 BLE advertising，display bounce buffer 分配成功，Wi-Fi 到 `SERVICE_READY`，Hermes health online。第四轮手动连续点击 Bluetooth 复测也已通过防护目标：internal heap 约 `30 KiB`、最大连续块约 `14 KiB` 时稳定返回 `ESP_ERR_NO_MEM` 并显示失败 toast，无 `emi.c`、Guru、interrupt WDT 或显示链路回退。
- V2.2 离页 pending 主链路和重复回复修复均已由用户真机反馈确认可用；后续主要风险转为体验细节、异常路径和低功耗参数，不再是主链路可用性。
- 不要继续把 Mihomo/Fake-IP DNS 当作当前阻塞点；用户已修复，后续只有在同网络再次出现 `ESP_ERR_HTTP_CONNECT`、公网域名连不上或解析异常时，才把 fake-ip DNS 作为复发线索。
- V2.4 当前可收尾归档：服务器 `/sync` 数据面已验证，WS 过早 detach 已修复并刷入，用户真机复测确认离页后不再长期 `session=none`。
- app 分区余量曾接近 4%，后续新增 V2.1/V3 功能前要继续关注二进制体积。
- 不要再把 `official_chat` 的前台崩溃归因于“模型分区没刷”；`CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y` + 291KB `srmodels.bin` 已实测仍会触发 ESP-SR loader 崩溃。当前路线是显式页面/按键触发，不需要本地 wake word，AFE 也不再兜底回 loader。

## 下一步

- **2026-06-29 IMU 关闭 + internal RAM 调优已完成**：`imu_service_start()` 用 `#if 0` 暂时关闭；`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` 从 65536 降到 4096。真机验证 internal_free=47.5KB / largest=24KB，系统稳定。恢复 IMU 只需取消 `#if 0`。attempt log：`docs/context/runs/2026-06-29-attempt-imu-disable-and-psram-alwaysinternal-tune.md`。
- **internal RAM 后续优化方向（方案 B 已完成）**：9 个任务栈已迁 PSRAM（official_chat_service/network_mgr/background_mgr/network_service/power_policy/power_service/wakeup_evidence + time/mw_* 已在 PSRAM）。internal free=73,982 B / largest=49,152 B，BLE 门槛已越过。剩余方向：方案 C 将 lvgl_task 10KB 栈迁 PSRAM（需测 UI 帧率）。不可迁移：oa_input/oa_output/RecTask（音频 DMA 路径）。
- **DMA 回归待验证**：ALWAYSINTERNAL=4096 后未做音频录放回归，若发现 I2S DMA 异常可回退为 8192。
- ~~跑 V2.2 收尾后的 context 校验~~ 已完成，light 级别通过。
- 提交前检查密钥卫生：`sdkconfig`、`memory_watch_dev_endpoint_local.h`、日志和文档都不能带真实 token/key。
- **V2.3 阶段 0 已完成**（2026-06-27）：V2.2 RAM/栈基线已记录。internal RAM 316/338 KB (93.5%), PSRAM 1395/8192 KB (17%), mw_upload stack high-water 3172 words。基线数据已写入计划文件和 CHANGELOG。
- **V2.3 阶段 1-5 全部完成**（2026-06-27）：计划已归档到 `docs/context/plans/completed/`。server session_repo 成为任务状态真相源（Stage 1-2），ESP32 保留 display dedup（Stage 3），通知路由继承 V2.2 分通道（Stage 4），门禁 126 server tests + 39 source tests + idf.py build 全部通过（Stage 5）。
- **V2.3 复查修复**（2026-06-27）：`SessionRepo` 允许 `accepted -> error/timeout`，避免 ASR 前置失败导致 session 假 active/pending；server pytest `126 passed`。
- **下一步**：server 公网部署验证 + COM3 真机 Hermes 前台/离页链路复测（Stage 0 冷启动基线已确认，Stage 2-3 主要为 server 增量，预期无回归）。
- **V2.4 历史下一步**：当时建议按用户节奏归档 V2.4 active plan；该计划现已归档到 `docs/context/plans/completed/`。后续不要再回到旧的“离页后 session=none 一直思考中”排查路线，除非新日志再次复现。
- ~~如果继续 BLE 问题，下一步不是放宽 guard，而是单独做 internal RAM 预算收敛~~（已通过方案 B 越过门槛）。
- **2026-06-29 方案 B（任务栈迁 PSRAM）全部完成**：9 个任务栈迁 PSRAM，COM3 冷启动 internal free=**73,982 B (72.3 KB)**、largest=**49,152 B (48 KB)**，BLE presence preflight 门槛（64 KB / 40 KB）已越过。累计释放 ~43 KB internal RAM（290 KB → 264 KB，78.3%）。**下一优先级：真机测试 BLE presence 启动与确认广播/配网流程**。attempt log：`docs/context/runs/2026-06-29-attempt-planb-task-stack-psram.md`。日志：`board_logs/2026-06-29-planb-task-stack-psram-cold-boot.log`。
- **Runtime Resource Gate 下一步**：阶段 6 只剩补测，不要重复已经完成的自动 gate 压测。Wi-Fi 可用后重点补测公网 HTTPS 成功路径（weather/inbox/health/sync 在 gate 下可延后但能成功）和 ESP-DL running -> 强前台让路；目标是无 panic/Guru/stack overflow/`esp-aes` 分配失败，BLE 失败路径可解释，Hermes 前台优先，后台 HTTPS 可延后。
- 不要回退到“离页保持 WS 等最终回复”的旧口径，也不要把多设备、多入口或完整多 agent 编排提前塞进 V2.3。

## 证据入口

- V1 主计划归档：`docs/context/plans/completed/2026-06-05-ai-memory-watch-hermes-page-plan.md`
- SoftAP/NVS 计划归档：`docs/context/plans/completed/2026-06-17-ai-memory-watch-softap-nvs-config-plan.md`
- V2 收件箱与全局气泡计划归档：`docs/context/plans/completed/2026-06-25-hermes-inbox-global-notification-plan.md`
- 服务器目录：`server/watch_voice_endpoint/`
- 机器可读契约：`server/watch_voice_endpoint/watch_contract.v1.json`
- 产品定位：`docs/context/knowledge/project/ai-memory-watch-product-positioning.md`
- V2.3 completed plan：`docs/context/plans/completed/2026-06-27-ai-memory-watch-hermes-v2.3-thin-watch-client-thick-watch-endpoint-plan.md`
- V2.4 计划（历史 active，现已归档）：`docs/context/plans/completed/2026-06-27-ai-memory-watch-hermes-v2.4-esp32-thin-client-slimming-plan.md`
- Runtime Resource Gate active plan：`docs/context/plans/active/2026-06-29-watch-runtime-resource-gate-plan.md`
- Runtime Resource Gate 阶段 1 run：`docs/context/runs/2026-06-29-attempt-foreground-runtime-gate.md`
- Runtime Resource Gate 阶段 2 run：`docs/context/runs/2026-06-29-attempt-espdl-foreground-runtime-yield.md`
- Runtime Resource Gate 阶段 3-5 run：`docs/context/runs/2026-06-29-attempt-runtime-resource-gate-hermes-https-ble.md`
- Runtime Resource Gate 阶段 6 板端自动回归 run：`docs/context/runs/2026-06-29-attempt-runtime-resource-gate-board-stress.md`
- 变更记录：`docs/context/CHANGELOG.md`
