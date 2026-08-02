---
id: online-music-micro-decoder-migration
tags: context, plans, music, micro-decoder, esp32-s3
title: 在线音乐 micro-decoder 迁移计划
summary: 用 micro-decoder 原版 reader/ring/decoder 流程替换手表端自研流式播放器，保留 music_service 与 audio_codec owner。
memory_type: task
scope: task
owners: main/services/music, server/music_service, main/idf_component.yml
evidence_level: design
status: active
last_reviewed: 2026-08-03
---

# 在线音乐 micro-decoder 迁移计划

## 固定边界

- `music_service` 继续拥有命令、快照、暂停/恢复、销毁和页面无关的播放生命周期。
- `audio_codec` 继续拥有扬声器 output session；`micro-decoder` 只负责 HTTPS reader、ring 和音频解码。
- 使用 `micro-decoder 0.3.x` 原版行为：无 32 KB 起播门槛、reader 栈使用 internal RAM、网络错误后允许解码完 ring、`stop()` 原生等待内部任务退出。
- 256 KB 压缩音频 ring 保持在 PSRAM；媒体流固定为 Ogg/Opus，服务端先转为 24 kHz 单声道 64 kbps，播放器适配层再将原版 decoder 的 48 kHz PCM 降为 ES8311 所需的 24 kHz；Opus decoder task 使用 16 KB PSRAM 栈。
- 服务端不使用 FFmpeg `-re` 实时限速，使手表 reader 可预填压缩 ring；ring 满后由 TCP 流量控制自然约束发送速度。
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
- [x] 服务端删除 FFmpeg `-re`，允许 256 KB 压缩 ring 预填，以隔离歌单刷新 HTTPS 请求造成的瞬时网络竞争。
- [x] 对齐已实测纯 C 播放器调度：micro-decoder decoder 优先级 5、reader 优先级 4，避免普通 HTTPS 工作抢占 PCM/I2S 实时路径。
- [ ] 在已刷写的 COM7 上真实起播一次 Ogg/Opus，播放中刷新一次歌单，并各复验一次暂停/恢复和断网后缓存耗尽；不执行 30 分钟持续测试。

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

## 下一步

释放 COM7 后烧录本轮固件；点播一次歌曲并播放中刷新一次歌单，确认无可感知卡顿且日志出现 `decoded Opus PCM: 48000 Hz -> 24000 Hz` 与 `md_decoder stack free`；随后各手工复验一次暂停/恢复和断网缓存耗尽。确认后归档本计划，不追加 30 分钟播放测试。
