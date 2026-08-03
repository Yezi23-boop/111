---
id: 2026-08-03-attempt-music-stream-server-correlation
summary: 将 ESP32 micro-decoder 的媒体连接重置与香港音乐服务访问日志对齐，确认无 HTTP 应用错误但缺少流关闭原因观测。
tags: [music, micro-decoder, esp-tls, server-logs, standby]
owners: main/services/music/music_stream_player.cc, server/music_service/src/engine.js, server/music_service/src/app.js
triggers: MBEDTLS_ERR_NET_CONN_RESET, HTTP read error, music stream, STANDBY, Wi-Fi power save, OpenResty access log
record_reasons: 非平凡 ESP-TLS 错误签名；包含真机日志、服务器访问日志和跨 owner 的定位边界，后续排查不应重复把 200 或 errno 113 当成单一根因。
last_reviewed: 2026-08-03
status: active
garden_status: keep-evidence
garden_reviewed: 2026-08-03
---

# 在线音乐媒体连接与服务器日志对齐

## 现象

板端在 `183166 ms` 报告：

```text
esp-tls-mbedtls: read error :-0x004C
transport_base: esp_tls_conn_read error, errno=Software caused connection abort
micro_decoder.audio_reader: HTTP read error
micro_decoder.decoder_source: Reader error
Decode drained after reader error
```

随后释放 `music_player` 输出会话。`-0x004C` 是 `MBEDTLS_ERR_NET_CONN_RESET`；`errno=113` 不能单独证明是 ESP32 本地主动中止。

## 服务器证据

当前香港音乐服务容器健康，容器日志在对应窗口只有启动信息，没有 FFmpeg 或应用错误。OpenResty 访问日志 `/opt/1panel/www/sites/watch-endpoint/log/access.log` 与板端流 ID 对齐（访问日志时间为 UTC）：

- `stream-35fac9e7-7a25-4244-92f9-bea5e2f378e4` 在 `20:50:53` 以 HTTP `200` 结束，返回 `2337476` 字节；该时刻与板端 `183166 ms` 的读错误对齐。
- 新的 `stream-b251709c-8c10-4578-be4a-3ab7533deffe` 在 `20:51:09` 返回 HTTP `200`、`32242` 字节；同一秒出现 `pause` 和 `resume`，符合客户端切换流，不是服务端拒绝。
- 后续 `stream-fc616a68-c507-4f26-991e-7af936adec3b` 返回 HTTP `200`、`3428418` 字节，板端随后成功解码 `48000 Hz` Opus。
- 对应窗口没有 `4xx/5xx`、OpenResty error log 记录或音乐服务容器崩溃。

访问日志的 `200` 只说明响应头已建立，不能区分正常 EOF、下游连接关闭和中间层 TCP reset。当前服务端 `engine.js` 丢弃 FFmpeg stderr，`app.js` 使用 chunked 音频响应，因此缺少“FFmpeg 退出原因、响应 close、实际发送字节数”的直接证据。

## 当前判断

1. 这次不是音乐服务返回鉴权失败、队列失败或编码启动失败。
2. 控制请求在板端 `192836 ms` 的 `poll_write ... errno=113` 是待机启用 Wi-Fi power save 后复用旧 HTTP/1.1 handle 的失效；随后一次重连成功，和媒体错误是两个连接上的问题。
3. 媒体错误发生在服务器访问记录结束的同一时刻，最可信范围是“媒体响应结束/被关闭后 ESP 收到 TCP reset”；仅凭现有日志不能在服务器自然 EOF与下游/中间层 reset之间二选一。

## 下一步证据

不做 30 分钟测试。若需彻底区分两种关闭原因，只需在服务端补最小观测：`stream_id`、FFmpeg `close` 的 code/signal、response `close/finish`、累计字节数，并在 OpenResty access format 增加 request/upstream duration；再做一次保持 ACTIVE 的短时播放与一次允许 STANDBY 的短时播放对照。

## 已实施修复

`network_service` 现在统一消费现有 `music_service_snapshot_t` 和 `official_chat_service_snapshot_t`：音乐处于 `BUFFERING/PLAYING`，或 Hermes 处于 `CONNECTING/LISTENING/SPEAKING` 时，仍允许屏幕进入 `STANDBY`，但不启用 Wi-Fi modem power save。聊天组件不再直接覆盖 Wi-Fi 策略，避免它从 `Speaking` 切回 `Idle` 时把音乐连接重新置入省电。这样避免待机电源策略影响持续媒体 TLS 连接，同时不改变控制 HTTP 连接的现有一次重连语义，也不把 TCP reset 误判为正常 EOF。

验证：音乐/网络/聊天/电源聚焦 source tests 共 `44 passed`；ESP-IDF 完整构建通过，应用分区余量约 9%。尚未宣称板端闭环，需短时确认日志出现 `Wi-Fi power save disabled ... (music stream active)` 或 `(... official chat audio active)`，并在允许熄屏播放时不再出现对应媒体 `HTTP read error`。

## COM7 短时板端证据

`2026-08-03` 这次串口日志确认：

- 启动阶段 ES8311/ES7210 均以 `48000 Hz` 初始化，PSRAM 自检通过；内部 RAM 使用率约 `95.7%`，但未出现栈溢出。
- 音乐在 `80.9 s` 起播，`110.6 s` 进入 `STANDBY`；此后没有 `micro_decoder.audio_reader: HTTP read error` 或 decoder 错误。`SERVICE_READY -> WIFI_READY (network sync paused by power budget)` 表示暂停后台探测，不表示断开 Wi-Fi。
- `331.7 s`、`405.6 s`、`690.7 s` 的 `Connection reset by peer` 发生在 `music_http` 控制面 HTTP/1.1 长连接，随后均重新建立控制连接并继续拉起新的媒体流。媒体连接与控制连接必须分开判断。
- 当前最可能的控制面原因是服务端/OpenResty 回收了长时间空闲的 keep-alive；这不会等同于 Opus/I2S 或媒体流故障。若要消除该告警，应调整控制连接生命周期或服务端 keep-alive，而不是恢复 `-re` 或修改解码器。

## 控制连接修复

已确认香港 OpenResty 的 `keepalive_timeout` 为 `60s`。设备端控制 client 现在记录最近一次成功请求完成时间；若下一次控制请求前连接空闲达到 `45s`，会主动关闭并重新建立 TLS/HTTP/1.1 连接，避免把已被服务器回收的 socket 交给 `esp_http_client_open()`。请求间隔较短时仍复用原控制长连接，媒体连接和 `micro-decoder` 不变。

验证：音乐/网络/聊天/电源聚焦 source tests `44 passed`；ESP-IDF 完整构建通过，应用分区余量约 9%。

本轮尝试使用 COM7 执行 `app-flash-monitor` 未完成：串口被已有监视器占用，脚本等待 150 秒后报告 `could not open COM7 exclusively`。因此上面的 COM7 日志仍只证明旧版板端行为；控制连接空闲轮换修复尚未在板上验证。

## PCM 实时诊断待板端取证

现有卡顿日志没有 `HTTP read error`、decoder 栈不足或 `audio_codec_write` 失败，不能仅凭现象判定是网络、decoder 调度还是 I2S 输出阻塞。为避免逐 PCM 块打印反过来干扰实时路径，`music_stream_player` 仅增加两类观测：每 2 秒一条 `PCM realtime` 汇总，以及每次 PCM callback 相对上一块的额外迟到超过 `30 ms` 时立即输出 `PCM late`。停止时输出一次 `PCM summary`。

- `PCM late` 且 `max_i2s_write` 接近该迟到量：优先检查 I2S/codec 写入阻塞或任务调度。
- `PCM late` 但 `max_i2s_write` 正常：PCM 在写入前就未及时到达，范围收敛到 micro-decoder reader、解码或网络供数。
- 没有 `PCM late`：听感问题不属于 PCM 连续性，后续改查音频格式、功放或音量链路。

验证：`uv run python -m pytest tests/test_music_service_source.py` 为 `9 passed`；ESP-IDF 完整构建通过，`111.bin` 为 `0xade1f0`，应用分区余量 `9%`。COM7 仍被占用，尚未烧录本诊断版。
