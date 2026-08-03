---
id: 2026-08-04-online-music-pure-c-raw-opus-migration
title: 纯 C 48 kHz Opus 播放器迁移
status: archived
tags: [music, opus, esp-idf, audio]
summary: 以纯 C 双任务播放器承载 48 kHz 裸 Opus 音乐流，并验证真实设备播放稳定性。
last_reviewed: 2026-08-04
---

# 纯 C 48 kHz Opus 播放器迁移

## Goal

以历史纯 C 双任务播放器替代 micro-decoder 路线；媒体改为长度前缀裸 Opus，保持 48 kHz、单声道、128 kbps、设备鉴权和既有音乐 service/UI 契约。

## Progress

- [x] 确认 `esp_audio_codec` C API 支持逐帧 `RAW_OPUS`，但不解析 Ogg 容器。
- [x] 决定服务端输出 `[uint16 BE packet_length][Opus packet]`，20 ms 一帧。
- [x] 服务端 Ogg packet transform、媒体响应与测试，并已部署香港 `ai-memory-watch-music-service`（healthy）。
- [x] 纯 C reader/decoder 双任务及 HTTPS 媒体 client，含 1 Hz ring/HTTP read 观测日志。
- [x] 删除音乐 micro-decoder 依赖并更新 source tests。
- [x] COM7 已确认新媒体响应可读，reader 已写入 ring；原始 Opus 首次解码暴露 8 KB decoder 栈溢出。
- [x] 将 decoder task 栈增至 16 KB PSRAM（reader 保持 8 KB）；源测试 11/11、完整 build、COM7 app-flash 均通过。
- [x] COM7 40 秒起播窗口无 `music_decoder` stack overflow、panic 或重启；reader ring/HTTP read 实时日志正常输出（观测水位约 4-50 KB，最长 read 约 4.5 s）。
- [x] 将压缩音频 ring 增至 512 KB，起播门槛降至 4 KB；reader 改为 512 B 小读并对媒体 read timeout/EAGAIN 做非致命处理。
- [x] 服务端增加 Ogg 低延迟页/flush 参数、TCP_NODELAY 和单次长度前缀帧写出；source tests 已锁定容量、起播、网络观测与服务端参数。
- [x] 香港 1Panel 服务已按既有流程重新部署本轮低延迟参数：远端源码先备份，单独重建 `ai-memory-watch-music-service` 并恢复 healthy；回环 `/health` 返回 `200`，公网无凭据音乐请求仍为 `401`。
- [x] 最终增量 build 通过（`111.bin` `0xb4f4c0`，应用分区余量约 6%），并用 `idf.py -p COM7 app-flash` 写入校验成功。
- [x] 刷写后 45 秒串口观测确认 `ring=524288B` PSRAM 分配成功；起播从 `ring=512B` 增长至满缓存，`eagain=0`，无 panic/重启。
- [x] 用户确认本轮 COM7 真机播放与交互无问题；约 3 分钟播放、刷新歌单、熄屏播放和停止路径完成验收，不执行 30 分钟持续测试。

## Boundaries

- `music_service` 仍是唯一生命周期 owner；UI 不直连网络。
- 控制请求仍复用现有鉴权及独立 control HTTP 长连接；媒体只使用短时 capability `stream_id`。
- 512 KB compressed ring、4 KB 起播门槛、512 B HTTP read；media read timeout 为 250 ms，EAGAIN 只记录并继续等待；reader 使用 8 KB、decoder 使用 16 KB PSRAM task stack。
- ring、PCM 和 Opus packet 缓冲均强制从 PSRAM 分配；分配失败记录 PSRAM free/largest block 并结束本次播放，不回退到 internal RAM。
- 不修改托管组件源码，不执行 30 分钟持续测试。

## Validation

- 服务端 parser 测试覆盖 Ogg 分块、跨页 packet、头包跳过与异常长度。
- 固件 source tests 锁定 512 KB ring、4 KB 起播、512 B media read、250 ms timeout、EAGAIN 非致命和实时水位日志；另跑 `git diff --check`、完整 `idf.py build`。
- 服务端源码测试、香港 1Panel 单容器重建和健康检查已通过；本轮 build、刷写、45 秒启动/播放日志及用户真机播放/交互验收均已完成。

## Completion

- 2026-08-04：用户确认真机播放与交互验收无问题；512 KB PSRAM ring、4 KB 快速起播、服务端低延迟裸 Opus 和 COM7 固件闭环完成。
