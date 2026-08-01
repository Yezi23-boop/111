---
id: ai-memory-watch-online-music-streaming-execution-plan
tags: context, plans, ai-memory-watch, music, streaming, esp32-s3, nodejs, ffmpeg, cloudflare
title: AI Memory Watch 在线音乐播放执行计划
summary: 分阶段实现独立 music-service、HTTPS MP3 流、ESP32 PSRAM 流式解码与独立音乐 UI；先通过固定测试流门禁，再接入个人网易云账号。
memory_type: task
scope: task
owners: docs/context/plans/active/2026-07-31-ai-memory-watch-online-music-streaming-execution-plan.md, docs/context/plans/completed/2026-07-30-ai-memory-watch-online-music-streaming-discussion.md, components/mp3_player/mp3_player.c, components/audio_codec/include/audio_codec.h, main/ui/custom/main_dropdown_controller.c, server/watch_voice_endpoint/compose.cloud.yml
evidence_level: design
status: active
last_reviewed: 2026-07-31
triggers: ai-memory-watch, online-music, music-service, mp3-streaming, ffmpeg, netease-cloud-music
---

# AI Memory Watch 在线音乐播放执行计划

## 目标

为 `watch-001` 实现独立在线音乐播放：手表通过 HTTPS/TCP 接收云端实时转码的 `MP3 / 24 kHz / mono / 64 kbps` 流，使用 PSRAM 缓冲、流式解码和 ES8311 扬声器播放。

首版先验证固定测试流，再接入服务器私有的网易云二维码授权、今日推荐、我喜欢、我的歌单和最近播放。

产品决策以 [在线音乐播放需求决策](../completed/2026-07-30-ai-memory-watch-online-music-streaming-discussion.md) 为准。

## 固定边界

- 不修改 `official_chat` 主线，不让音乐复用 Hermes WSS、Gateway Relay、对话 session 或收件箱。
- 不公开 Hermes、网易云通用 API、账号 Cookie、播放 URL、调试端口或 FFmpeg 控制入口。
- 手表仅使用现有 `device_id + device_token`；账号授权、上游 URL、队列、SQLite 和转码都在服务器。
- 只新增一个独立 Node.js 22 `music-service` 容器；首版不部署独立 `ncm-api` 容器。
- 全局只允许一个活动 FFmpeg 转码；没有多设备、搜索、语音点歌、封面、音量、精确进度、歌词、离线下载或 gapless。
- UI 只提交意图并读取快照；网络、解码、队列、音频 session 与停止顺序只能由 music service owner 推进。
- 不新增通用资源管理器。音乐服务通过窄 API 响应安全告警与 Hermes 录音的音频交接请求。

## 后续扩展：Hermes Music MCP（不属于首版）

首版稳定后，可将 `music-service` 以受限 Music MCP 工具面接入 Hermes，使 Hermes 能将自然语言转换为音乐控制意图，例如获取来源、播放指定曲目、暂停、上一首、下一首、切换模式和查询状态。

- MCP 仅是控制面，仍调用同一个服务器 `music-service` 和同一份 `music_session_id`；它不传输音频、Cookie、播放 URL 或设备 token。
- 音频主链保持手表直连 `GET /v1/music/streams/{stream_id}` 的 HTTPS/TCP `audio/mpeg` 流，不经过 Hermes、MCP、Relay 或 WSS。
- MCP 工具必须是窄白名单，首批仅允许 `sources`、`play`、`pause`、`previous`、`next`、`set_mode`、`status`；不得开放搜索、账号操作、Cookie、通用网易云 API 或 FFmpeg 管理。
- 该能力不改变首版独立音乐页、上拉栏播放控制和资源仲裁。Hermes 不可用时，音乐 V1 必须仍可完整使用。
- 在首版端到端验证通过前，不实现或配置该 MCP，避免将 Agent 推理延迟引入实时播放控制主路。

## 目标架构

```text
ESP32 music UI / 上拉栏
  -> music_service command queue
  -> HTTPS JSON control -> music-service (Node.js 22)
  -> api-enhanced Node module + server-side Cookie
  -> normal upstream media URL
  -> one FFmpeg process, realtime MP3 transcode
  -> HTTPS/TCP chunked audio/mpeg stream
  -> ESP32 reader -> 128 KB PSRAM ring -> incremental MP3 decoder
  -> audio_codec output session -> ES8311 speaker
```

`music_session_id` 是持久化的播放状态 ID；`stream_id` 是首次建流 60 秒内有效的临时流标识。两者都不携带密钥，所有公网请求仍由 Authorization Bearer device token 鉴权。

## 协议契约

所有 `/v1/music/*` 请求使用现有设备鉴权。成功 JSON 必须至少携带稳定 `state`；错误 JSON 必须至少携带稳定 `error_code`，不得回传上游 Cookie、URL 或原始异常。

| 接口 | 职责 |
| --- | --- |
| `GET /v1/music/account` | 返回 `logged_out`、`qr_pending`、`logged_in` 或 `expired`。 |
| `POST /v1/music/account/qr` | 创建 120 秒 QR 会话，返回 `login_id` 和用于手表渲染的紧凑 QR 模块位图。 |
| `GET /v1/music/account/qr/{login_id}` | 返回扫码、待确认、成功、过期或失败状态；仅登录页短期轮询。 |
| `DELETE /v1/music/account` | 二次确认后的退出账号动作：停止会话并删除私有授权与账号元数据。 |
| `GET /v1/music/sources` | 返回今日推荐、我喜欢、歌单列表与最近播放来源。 |
| `GET /v1/music/sources/{source_id}/tracks?offset=&limit=10` | 返回单页曲目摘要；服务器保留完整来源队列快照。 |
| `POST /v1/music/sessions` | 从指定来源与曲目创建或替换会话，返回 `music_session_id`、`stream_id`、曲目和 `buffering` 状态。 |
| `POST /v1/music/sessions/{music_session_id}/pause` | 停止 FFmpeg/上游流，保留曲目、队列、模式和近似进度。 |
| `POST /v1/music/sessions/{music_session_id}/resume` | 创建新的 `stream_id`，优先近似续播，无法 seek 时显式返回从头播放状态。 |
| `POST /v1/music/sessions/{music_session_id}/previous` | 按服务器队列或随机历史切换上一首。 |
| `POST /v1/music/sessions/{music_session_id}/next` | 按模式选择下一首；自动队列最多跳过 3 首不可播放歌曲。 |
| `POST /v1/music/sessions/{music_session_id}/mode` | 设置 `repeat_one`、`repeat_all` 或 `shuffle`；从下次选曲生效。 |
| `DELETE /v1/music/sessions/{music_session_id}` | 终止会话、FFmpeg 与上游流；长按在线时调用。 |
| `GET /v1/music/streams/{stream_id}` | 连续输出 `audio/mpeg`；请求头鉴权、`Cache-Control: no-store`、首次连接 60 秒有效。 |

所有变更会话的请求携带客户端生成的 `command_id`，服务器以 `(device_id, command_id)` 做短期幂等，防止 Wi-Fi 重试重复启动/停止 FFmpeg。

## 阶段 0：解码可行性与资源基线

- [ ] 盘点当前 `components/mp3_player` 的文件句柄式解码路径，确认它不能直接作为网络流播放器；不得为了复用而把整首流落盘。
- [ ] 新增最小流式解码 spike：从内存分块喂入固定 MP3 fixture，验证选定 decoder 能持续输出 PCM 到 `audio_codec_write()`。
- [ ] 明确并记录最终 decoder adapter 的 API、输入分块大小、解码 task 栈、PCM 工作缓冲和错误恢复；大块压缩缓冲必须在 PSRAM。
- [ ] 记录实现前的 internal RAM、PSRAM 与 task stack 基线，复用已有 `printf_esp32_memory_stats()`、`printf_esp32_task_stack_stats()` 观测能力。

**门禁：** 流式 decoder spike 可连续解码固定 fixture，不依赖 `FILE *`，无 internal RAM 大对象、无 panic、无 stack overflow。

## 阶段 1：music-service 测试流与会话核心

计划新增目录 `server/music_service/`：

- [ ] Node.js 22 服务、锁定依赖和 Dockerfile；api-enhanced 作为进程内私有模块，不启动其通用 HTTP server。
- [ ] SQLite repository：账号状态、QR 会话、目录缓存、最近 20 首、`music_session_id`、完整队列快照、模式、近似进度、命令幂等与清理任务。
- [ ] 会话 owner：全局一个活动 FFmpeg；新会话/恢复/切歌先停止旧进程，重复 `stream_id` GET 不得重复 spawn。
- [ ] 先实现固定测试 MP3 upstream，不需要任何网易云账号；实现实时输出、暂停、恢复、停止、断线回收和 60 秒首次建流超时。
- [ ] 设置稳定错误码：`music_auth_required`、`music_session_not_found`、`music_stream_expired`、`track_unplayable`、`upstream_unavailable`、`transcode_failed`、`music_busy`。
- [ ] 普通日志只记录会话 ID、状态、时长、字节数和错误码，不记录歌曲正文、Cookie、上游 URL 或 Bearer token。

**门禁：** Node 单元测试覆盖 command 幂等、会话替换、60 秒超时、15 秒失联回收、暂停续播、重启 SQLite 恢复与重复流 GET；本机脚本能持续读取测试 `audio/mpeg` 流。

## 阶段 2：云端隔离与 Cloudflare 路由

- [ ] 在 `server/watch_voice_endpoint/compose.cloud.yml` 增加 `ai-memory-watch-music-service`，使用受限 Node/FFmpeg 镜像、`read_only`、受控 `tmpfs`、`pids_limit`、初始 `mem_limit: 256m` 和单进程转码约束。
- [ ] 将音乐数据挂载到 `/opt/ai-memory-watch/music-data:/data`；部署脚本在主机创建 `0700` 目录和 `0600` Cookie 文件，不把任何密钥写入 compose 或仓库。
- [ ] 只将 music-service 绑定宿主 loopback，例如 `127.0.0.1:18788:8788`；容器 health check 只从本机/私网检查。
- [ ] 配置 Cloudflare Tunnel ingress，仅路由 `watch.934000.xyz/v1/music/*` 到该 loopback 端口；确认 `/health`、调试路径与任意 api-enhanced 路径仍不可公网访问。
- [ ] 设置流响应 `Content-Type: audio/mpeg`、`Cache-Control: no-store`，不发送 Cookie、上游重定向或私有诊断头。

**门禁：** 容器健康、内存限制、私网 health、Cloudflare 公网设备鉴权、未鉴权 401/403 和私有路径不可达全部通过；watch endpoint、Hermes、Relay 与 Dashboard 不受影响。

## 阶段 3：ESP32 music service 与流式播放器

计划新增：

- `main/services/music/music_service.[ch]`：唯一状态 owner，持有 command queue、只读 snapshot、播放会话生命周期和窄的 preempt/prepare API。
- `main/services/music/music_http_client.[ch]`：HTTPS JSON 控制与持续流读取，不接触 LVGL。
- `main/services/music/music_stream_player.[ch]`：reader/decoder worker、128 KB PSRAM ring、4 秒起播门槛、PCM 输出和停止顺序。
- `main/services/music/music_protocol.h`：手表侧 JSON 字段、状态与错误码映射，不包含 token 或上游字段。

实施要求：

- [ ] `music_service` 由一个 FreeRTOS owner task 串行处理 UI、网络、流结束、告警抢占和长按销毁命令；UI getter 无副作用。
- [ ] reader 与 decoder 之间使用 PSRAM ring + FreeRTOS task notification 或 queue 表达数据到达、空间释放、停止和错误，不能用裸 `volatile` 协调。
- [ ] 压缩环形缓冲 128 KB 放 PSRAM；decoder 临时 PCM 分块、HTTP read buffer 和 task stack 尺寸以阶段 0 实测为准，不允许把 128 KB 放任务栈或 internal RAM。
- [ ] 新增 `AUDIO_CODEC_OWNER_MUSIC_PLAYER`，通过 `audio_codec` 获取/释放 output session；不得绕过 owner 接口直接操作 I2S 或 PA。
- [ ] 为 Hermes 录音和 P0 告警提供窄的 `music_service_prepare_for_foreground_audio()` / `music_service_request_alert_preempt()` 请求与 ACK。音乐 owner 自行停流、清缓冲、释放 output session；调用方不得直接释放音乐资源。
- [ ] 音乐后台播放、熄屏继续；安全告警停止音乐，普通通知不发提示音；不新增低电量专用行为。
- [ ] 网络中断仅自动恢复此前播放状态，30 秒失败后暂停；用户主动暂停、重启或长按后不自动播放。
- [ ] 容器重启导致流失效时，使用持久化 `music_session_id` 请求恢复，不能恢复则发布稳定错误状态。

**门禁：** source tests 覆盖 owner 边界、PSRAM 分配、无 UI 直接联网、命令幂等、告警/Hermes 交接、暂停/长按/断流状态；`idf.py build` 通过。

## 阶段 4：独立音乐 UI 与上拉栏

计划新增 `main/ui/custom/music_view.[ch]` 与 `main/ui/custom/music_controller.[ch]`，并只在 `main/ui/custom/main_dropdown_controller.c` 接入一个上拉栏音乐按钮。

- [ ] 音乐页面只显示今日推荐、我喜欢、我的歌单、最近播放；曲目每页 10 条，显示歌名、歌手与状态。
- [ ] 播放页只显示歌名、歌手、缓冲/播放/暂停/网络恢复状态、播放/暂停、上一首、下一首和三档模式；不显示搜索、封面、音量、歌词或精确时间。
- [ ] 短按上拉栏按钮：音乐服务未启动时进入音乐页，已启动时播放/暂停。长按：停止会话、销毁本地运行资源，不弹确认。
- [ ] QR 登录页按服务端模块位图渲染，最长 120 秒；仅页面前台期间短期轮询状态，过期后由用户主动重新获取。
- [ ] 所有 LVGL 访问只在 UI/controller 层；controller 只投递 service command、复制 snapshot，不等待网络或解码。

**门禁：** UI source tests 覆盖无直接网络/音频操作、按钮意图和页面状态；host preview 或板端截图确认文字不溢出、安全区合规、控制稳定。

## 阶段 5：固定流端到端验证

- [ ] 运行 server unit tests、compose health、私网流读取和 Cloudflare 设备鉴权 smoke。
- [ ] 固件执行 source tests、`idf.py build`、`idf.py -p <PORT> app-flash`，再以 `scripts/board/agent_serial_monitor.ps1` 限时观察。
- [ ] 验证固定测试流：4 秒起播、连续 30 分钟、熄屏继续、短按暂停/近似恢复、长按释放、Wi-Fi 短断 30 秒恢复、超时暂停、服务重启恢复。
- [ ] 记录 internal RAM、PSRAM、reader/decoder/service task 栈高水位；无 `ESP_ERR_NO_MEM`、panic、Guru、stack overflow、输出 session 泄漏或重复 FFmpeg。
- [ ] 触发安全告警与 Hermes 录音，验证音乐 owner 有序释放输出后才允许高优先级路径进入。

## 阶段 6：网易云私有联调

- [ ] 仅在固定流门禁通过后启用 api-enhanced 私有适配与二维码登录；不得打印或提交 Cookie。
- [ ] 验证 QR 创建、扫码、确认、120 秒过期、主动退出、授权失效、我喜欢、歌单、今日推荐和最近播放。
- [ ] 验证手表分页、来源队列快照、三种模式、不可播放歌曲、自动跳过上限、流断重连和容器重启恢复。
- [ ] 再次确认公网只能访问已鉴权 `/v1/music/*`，且没有暴露上游 URL、Cookie、health 或通用 API。

## 收尾与回滚

- [ ] 更新本计划 Progress、Validation 和 Next Step；只有形成可复用部署/故障证据时才新增 `runs/`。
- [ ] 更新 `docs/context/CHANGELOG.md` 仅记录稳定架构事实和关键验证，不记录普通命令流水。
- [ ] 运行 `uv run python scripts/context/validate_context.py --level standard --q "AI Memory Watch online music streaming" --brief` 与 `git diff --check`。
- [ ] 仅暂存本任务相关文件，提交信息使用中文。

回滚路径：删除 Cloudflare `/v1/music/*` ingress、停止 `ai-memory-watch-music-service` 容器、移除音乐页面入口或关闭其 Kconfig 开关；不修改 Hermes、watch voice endpoint、Relay 和 official_chat 的既有公网/会话路径。

## 进度

- [ ] 阶段 0：流式解码可行性与资源基线。
- [ ] 阶段 1：music-service 测试流与会话核心。
- [ ] 阶段 2：云端隔离与 Cloudflare 路由。
- [ ] 阶段 3：ESP32 music service 与流式播放器。
- [ ] 阶段 4：独立音乐 UI 与上拉栏。
- [ ] 阶段 5：固定流端到端验证。
- [ ] 阶段 6：网易云私有联调。
- [x] 已记录后续 Hermes Music MCP 控制面路线；明确不进入首版实施范围。
- [ ] 收尾：文档、门禁与相关提交。

## 验证

- 尚未开始实现。

## 下一步

从阶段 0 的流式 MP3 decoder spike 开始，先证明现有 ESP32 音频栈可安全接收分块网络数据，再创建或部署任何网易云账号能力。
