---
id: online-music-micro-decoder-migration
tags: context, plans, music, micro-decoder, esp32-s3
title: 在线音乐 micro-decoder 迁移计划
summary: 用 micro-decoder 原版 reader/ring/decoder 流程替换手表端自研流式播放器，保留 music_service 与 audio_codec owner。
memory_type: task
scope: task
owners: main/services/music, server/music_service, main/idf_component.yml
evidence_level: design
status: archived
last_reviewed: 2026-08-03
---

# 在线音乐 micro-decoder 迁移计划

## 固定边界

- `music_service` 继续拥有命令、快照、暂停/恢复、销毁和页面无关的播放生命周期。
- `audio_codec` 继续拥有扬声器 output session；`micro-decoder` 只负责 HTTPS reader、ring 和音频解码。
- 使用 `micro-decoder 0.3.x` 原版行为：无 32 KB 起播门槛、reader 栈使用 internal RAM、网络错误后允许解码完 ring、`stop()` 原生等待内部任务退出。
- 256 KB 压缩音频 ring 保持在 PSRAM；I2S0、ES8311 与 ES7210 统一使用 48 kHz，媒体流固定为 Ogg/Opus 48 kHz 单声道 128 kbps，micro-decoder 的 48 kHz PCM 直接写入 `audio_codec`；Opus decoder task 使用 16 KB PSRAM 栈。
- 服务端固定输出 Ogg/Opus 48 kHz 单声道 128 kbps；256 KB ring 继续提供弱网余量，设备端不依赖 FFmpeg `-re`。
- 控制接口继续使用 Bearer 设备鉴权；音频流使用短时、随机、会话绑定的 `stream_id` 作为 media capability token，不把长期 `device_token` 放入 URL。
- 不重复 30 分钟持续测试；只做短时真实播放、暂停/恢复、断网缓存耗尽和销毁验证。

## 进度

- [x] 固定迁移边界和原版行为取舍。
- [x] 服务端允许有效 `stream_id` 在无 Authorization Header 时连接媒体流，其他控制接口仍强制设备鉴权。
- [x] 手表端接入 `micro-decoder` 薄适配层并保持 `music_service -> audio_codec` owner 链路。
- [x] 删除本地 reader/ring/MP3 decoder 和流式 HTTP 重复实现。
- [x] 聚焦测试、完整 build、COM7 app-flash 和短时真实起播验证通过。
- [x] 服务端改为 Ogg/Opus 输出，手表端启用 Opus decoder；保持 media capability token 和全局 24 kHz 声卡不变。
- [x] 根据真机 `md_decoder` stack overflow，将 Opus decoder 的 PSRAM 栈从 8 KB 提升至 16 KB，并加入一次性历史栈余量日志。
- [x] 按用户要求回退最近的服务端 FFmpeg `-re`、队列恢复和旧控制面改动；保留 256 KB PSRAM ring 与 Opus/48 kHz。
- [x] 对齐已实测纯 C 播放器调度：micro-decoder decoder 优先级 5、reader 优先级 4，避免普通 HTTPS 工作抢占 PCM/I2S 实时路径。
- [x] I2S0、ES8311、ES7210 迁移为 48 kHz；删除音乐 48→24 kHz 抽样与 8 KB PSRAM 临时缓冲；告警 PCM 2×线性重采样为 48 kHz。
- [x] 香港音乐服务改为 Ogg/Opus 48 kHz 单声道 128 kbps 并重建 healthy。
- [x] `music_service` 持有独立控制面 HTTP/1.1 长连接，micro-decoder 媒体流使用另一条连接；控制连接失效时最多重建一次，销毁会话时释放控制 socket/TLS。
- [x] 修复控制面首次 GET 清理可选 header 时把 `esp_http_client_set_header(..., NULL)` 当作删除操作的问题；改用 `esp_http_client_delete_header()` 并忽略不存在 header 的 `ESP_ERR_NOT_FOUND`。
- [x] 在最新 COM7 固件上点播后刷新一次歌单；确认不再出现控制请求 `ESP_ERR_NOT_FOUND`、歌单返回成功且媒体流不中止；不执行 30 分钟持续测试。

## 验证

- 2026-08-03：`npm test` 10/10、`uv run python -m pytest tests/test_music_service_source.py` 8/8、`git diff --check` 均通过。
- 2026-08-03：`idf.py fullclean build` 与最终增量 `idf.py build` 通过；`111.bin=0xacda60`，最小 app 分区剩余 `0x1325a0`（10%）。
- 2026-08-03：COM7 两次 `app-flash` 均完成且 hash 已校验；启动观察无 panic/Guru/WDT/stack overflow，`music_service` 栈余量约 11772 B。日志：`board_logs/2026-08-03-00-53-13-music-micro-decoder-boot.log`。
- 2026-08-03：香港 `ai-memory-watch-music-service` 已重建并 healthy；公网无凭据无效 stream capability 返回 409，而普通 `/v1/music/sources` 仍返回 401。
- 2026-08-03：COM7 真实起播捕获 `audio_codec` output session、`micro_decoder.audio_reader` capability URL 与 `24 kHz / 16-bit / mono` PCM；无 panic/WDT。日志：`board_logs/2026-08-03-01-00-13-music-micro-decoder-playback.log`。
- 2026-08-03：Ogg/Opus 切换后 `npm test` 10/10、`uv run python -m pytest tests/test_music_service_source.py` 8/8、`idf.py fullclean build` 和 COM7 `app-flash` 均通过；`111.bin=0xac8670`，最小 app 分区剩余 `0x137990`（10%）。香港 `ai-memory-watch-music-service` 已重建且 healthy。启动日志无 panic/Guru/WDT/stack overflow：`board_logs/2026-08-03-01-20-27-music-opus.log`。
- 2026-08-03：首次 Opus 起播在 `decoded Opus PCM: 48000 Hz -> 24000 Hz` 后触发 `md_decoder` stack overflow，定位为 8 KB decoder task 栈不足；修复后的 source test 8/8、build 和 COM7 `app-flash` 已通过，启动回归无 panic。日志：`board_logs/2026-08-03-01-27-16-music-opus-stack-fix.log`。仍待一次实际 Opus 起播读取栈余量。
- 2026-08-03：删除媒体 FFmpeg 的 `-re` 后，服务端 `npm test` 10/10、手表播放链路 source test 8/8 通过；香港 `ai-memory-watch-music-service` 已重建且 healthy。固件未改动，无需重新烧录。
- 2026-08-03：发现 micro-decoder 未显式设置任务优先级，落回组件默认 reader/decoder 均为 2，而旧纯 C 播放器为 reader 4、decoder 5；已恢复该调度关系。`uv run python -m pytest tests/test_music_service_source.py` 8/8、`git diff --check` 与 `idf.py build` 均通过，`111.bin=0xac86f0`，最小 app 分区剩余 `0x137910`（10%）。COM7 `app-flash` 因端口被占用失败，尚未产生本轮真机结论。
- 2026-08-03：48 kHz 迁移的聚焦 source tests 24/24、音乐服务 `npm test` 10/10、`git diff --check` 与 `idf.py build` 通过；`111.bin=0xadd0f0`，最小 app 分区剩余 `0x122f10`（9%）。用户确认 COM7 已完成本轮烧录，尚待串口和交互证据。香港音乐服务已重建为 48 kHz/128 kbps 并 healthy。香港 Hermes 虽有 TTS provider 配置，但 `auto_tts: false`，watch endpoint 当前只上传音频、向 Hermes 传递 ASR 文本且不接收 TTS 音频，因此本轮未伪造 Hermes 下行采样率改动。
- 2026-08-03：为隔离“播放中新增 TLS 握手”风险，控制面改为由 `music_service` owner 持有的 HTTP/1.1 Keep-Alive；微解码媒体 reader 保持独立。歌单/模式请求在播放中只复用该连接，失效后不重连；起播、暂停、换曲和销毁等无媒体 reader 的路径可新建连接。`uv run python -m pytest tests/test_music_service_source.py` 9/9、`git diff --check` 和 `idf.py build` 通过；`111.bin=0xadd210`，最小 app 分区剩余 `0x122df0`（9%）。
- 2026-08-03：COM7 `app-flash` 后冷启动到新 ELF（SHA256 前缀 `e47359ac6`）并采集 20 秒日志；无 panic/WDT，`music_service` 栈余量 `11756 B`，I2S 输入输出均为 48 kHz。日志：`board_logs/2026-08-03-03-18-13-music-control-keepalive-boot.log`。
- 2026-08-03：修好串口工具链对 10.9 MB 镜像的烧录效率与超时。根因：`agent_serial_monitor.py` 对 `app-flash` 阶段复用 monitor 的 115200 波特率，理论传输约 13 分钟，超过 agent Bash 的 64 秒窗口。实测 COM7 为板载 USB-Serial/JTAG，921600 稳定。已给脚本新增 `--flash-baud`（默认 921600），flash 与 monitor 波特率分离（`app-flash monitor --monitor-baud`）；并在命令前 `Remove-Item Env:MSYSTEM`，规避 Git Bash 注入 MSYSTEM 触发 `idf_tools.py` 的 MSYS 拒绝逻辑。本轮 `app-flash -b 921600` 用时 81.9 秒写完 11.39 MB（压缩 5.22 MB），`Hash of data verified`，冷启动到 `e47359ac6` 无 panic。日志：`board_logs/2026-08-03-03-27-15-keepalive-boot.log`（脚本退出码 0、残留 monitor 0）。
- 2026-08-03：按用户要求恢复服务端 FFmpeg `-re`。`npm test` 10/10、手表播放链路 source test 9/9 与 `git diff --check` 通过；香港 `ai-memory-watch-music-service` 已重建为 `running healthy`，容器内确认 `-re` 位于 FFmpeg `-i` 前。固件未改动，无需重新烧录。
- 2026-08-03：服务端重建后手表仍保留旧 Keep-Alive，而旧实现仅按 `s_player != NULL` 禁止控制面重连，导致非播放状态的歌单请求也失败。改为仅在 `BUFFERING/PLAYING` 禁止新 TLS；source test 9/9、`idf.py build` 通过，`111.bin=0xadd240`，最小 app 分区剩余 `0x122dc0`（9%）。COM7 `app-flash` 已完成且 `Hash of data verified`，新 ELF SHA256 前缀 `58d3aa82` 启动无 panic。
- 2026-08-03：按用户要求改为双 HTTP 长连接后，首版歌单 GET 在清理不存在的 `Content-Type` header 时收到本地 `ESP_ERR_NOT_FOUND`，尚未发出网络请求；已改用 `esp_http_client_delete_header()` 并忽略该返回值。source test 9/9、`git diff --check`、`idf.py build` 通过；COM7 最新固件冷启动到 ELF SHA256 前缀 `d843ba9ad`，无 panic/WDT/stack overflow，`music_service` 栈余量 `11756 B`。日志：`board_logs/2026-08-03-04-30-27-music-dual-http-header-fix.log`。
- 2026-08-03：串口采集脚本 `scripts/board/agent_serial_monitor.py` 由 `idf.py monitor` 包装重写为 pySerial 直读（`serial.Serial(exclusive=True)` 独占 + `board_logs/<port>.lock` 锁文件），不再 spawn PowerShell/export.ps1，规避 MSYSTEM 与残留 monitor 占端口；烧录拆为 `idf.py app-flash -b 921600` 子进程，采集在烧录后自动重试打开端口并接住冷启动日志；`--duration-seconds` 兼作 flash 等待预算。已验证：锁冲突自愈（`STATUS=busy`）、20 s 静默采集 36 行、端到端 `app-flash-monitor` 冷启动采集 382 行无 panic（`board_logs/2026-08-03-04-22-01-e2e-pyserial.log`）。注意：该 E2E 烧录（板上 ELF `0659997f3`）发生在 04:24 `music_http_client.c` 修改期间，ninja 增量可能未拾取，板上固件当时与最新源码不一致；并行会话 04:30 已重烧当前源码（ELF `d843ba9ad`），板上现为最新固件。
- 2026-08-03：用户确认最新 COM7 固件点播后刷新歌单成功，无控制请求 `ESP_ERR_NOT_FOUND`，媒体流未中止；双 HTTP 长连接闭环完成。

## 下一步

已完成 COM7 点播、歌单刷新和媒体不中止确认；不追加 30 分钟播放测试。本计划可归档。
