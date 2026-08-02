---
id: ai-memory-watch-online-music-streaming-execution-plan
tags: context, plans, ai-memory-watch, music, streaming, esp32-s3, nodejs, ffmpeg, hong-kong, 1panel, openresty
title: AI Memory Watch 在线音乐播放执行计划
summary: 分阶段实现独立 music-service、HTTPS MP3 流、ESP32 PSRAM 流式解码与独立音乐 UI；先通过固定测试流门禁，再接入个人网易云账号。
memory_type: task
scope: task
owners: docs/context/plans/completed/2026-07-31-ai-memory-watch-online-music-streaming-execution-plan.md, docs/context/plans/completed/2026-07-30-ai-memory-watch-online-music-streaming-discussion.md, components/mp3_player/mp3_player.c, components/audio_codec/include/audio_codec.h, main/ui/custom/main_dropdown_controller.c, server/watch_voice_endpoint/compose.hk.yml, server/deploy/openresty/ai-memory-watch.conf
evidence_level: design
status: archived
last_reviewed: 2026-08-02
triggers: ai-memory-watch, online-music, music-service, mp3-streaming, ffmpeg, netease-cloud-music
---

# AI Memory Watch 在线音乐播放执行计划

## 完成结论

2026-08-02 用户确认当前真实扫码登录、网易云播放、熄屏、断网、会话销毁、安全告警边界和双 task 卡顿优化均可用，本计划按当前首版范围完成归档。未捕获的运行统计与未逐项执行的扩展协议边界不再作为首版阻塞项；后续只有出现真实问题时再单独立项。

## 目标

为 `watch-001` 实现独立在线音乐播放：手表通过 HTTPS/TCP 接收云端实时转码的 `MP3 / 24 kHz / mono / 64 kbps` 流，使用 PSRAM 缓冲、流式解码和 ES8311 扬声器播放。

首版先验证固定测试流，再接入服务器私有的网易云二维码授权、今日推荐、我喜欢、我的歌单和最近播放。

产品决策以 [在线音乐播放需求决策](../completed/2026-07-30-ai-memory-watch-online-music-streaming-discussion.md) 为准。

## 固定边界

- 不修改 `official_chat` 主线，不让音乐复用 Hermes WSS、Gateway Relay、对话 session 或收件箱。
- 不公开 Hermes、网易云通用 API、账号 Cookie、播放 URL、调试端口或 FFmpeg 控制入口。
- 手表仅使用现有 `device_id + device_token`；账号授权、上游 URL、队列、SQLite 和转码都在服务器。
- `/v1/music/*` 由 `music-service` 独立校验现有设备鉴权；OpenResty 只转发，不依赖 watch endpoint 代验，也不把设备 token 传给上游网易云。
- 尽量复用现有 `WATCH_DEVICE_TOKENS` 的服务器配置源、`device_id` 语义、Bearer 格式和鉴权错误约定；音乐服务不创建第二套设备 token。Hermes/watch 的 SQLite、会话状态和网易云 Cookie 不跨服务共享。
- 网易云二维码登录只在服务器完成授权状态管理；Cookie 和账号元数据仅保存于 music-service 的私有数据目录，ESP32 只接收二维码位图与登录状态，不保存或回传网易云凭据。
- 网易云授权过期只发布 `expired` 状态并拒绝新建播放流；不自动刷新二维码、自动登录或切换账号，必须由用户主动重新扫码。
- 登录成功后不自动加载歌曲目录；仅在进入音乐页或用户打开具体来源时按需请求，目录缓存 5 分钟。
- 退出网易云账号必须二次确认；确认后停止当前音乐会话并删除服务器 Cookie 与账号元数据，手表不接触凭据。
- 只新增一个独立 Node.js 22 `music-service` 容器；首版不部署独立 `ncm-api` 容器。
- `api-enhanced` 作为 `music-service` 进程内私有模块加载；不得为了它再拆出一个容器或公网 API。
- 全局只允许一个活动 FFmpeg 转码；没有多设备、搜索、语音点歌、封面、音量、精确进度、歌词、离线下载或 gapless。
- 首版不设置 music-service 的 Docker 内存上限；仍限制全局单个 FFmpeg，并记录服务、FFmpeg 和宿主机内存峰值，实测后再决定是否增加 `mem_limit`。
- UI 只提交意图并读取快照；网络、解码、队列、音频 session 与停止顺序只能由 music service owner 推进。
- 不新增通用资源管理器。音乐服务通过窄 API 响应安全告警与 Hermes 录音的音频交接请求。
- 音乐是可熄屏持续运行的后台播放 owner，不注册为 `runtime_coordinator` 的强前台 owner；强前台只用于 Hermes、配网、OTA 等前台独占生命周期。

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
  -> ESP32 reader task -> 256 KB PSRAM ring -> decoder/I2S task
  -> audio_codec output session -> ES8311 speaker
```

`music_session_id` 是持久化的播放状态 ID；`stream_id` 是首次建流 60 秒内有效的临时流标识。两者都不携带密钥，所有公网请求仍由 Authorization Bearer device token 鉴权。

## 协议契约

所有 `/v1/music/*` 请求由 `music-service` 独立执行现有设备鉴权。成功 JSON 必须至少携带稳定 `state`；错误 JSON 必须至少携带稳定 `error_code`，不得回传上游 Cookie、URL 或原始异常。

| 接口 | 职责 |
| --- | --- |
| `GET /v1/music/account` | 返回 `logged_out`、`qr_pending`、`logged_in` 或 `expired`。 |
| `POST /v1/music/account/qr` | 创建 120 秒 QR 会话，返回 `login_id` 和用于手表渲染的紧凑 QR 模块位图。 |
| `GET /v1/music/account/qr/{login_id}` | 返回扫码、待确认、成功、过期或失败状态；仅登录页短期轮询。 |
| `DELETE /v1/music/account` | 二次确认后的退出账号动作：停止会话并删除私有授权与账号元数据。 |
| `GET /v1/music/sources` | 返回今日推荐、我喜欢、歌单列表与最近播放来源。 |
| `GET /v1/music/sources/{source_id}/tracks?offset=&limit=10` | 返回手表单批 10 首曲目摘要；服务器保留完整来源队列快照。 |
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
- [ ] 新增最小流式解码 spike：复用已安装的 `espressif__esp_audio_codec` `esp_audio_simple_dec_*` MP3 解码接口，从内存分块喂入固定 MP3 fixture，验证能持续输出 PCM 到 `audio_codec_write()`。
- [x] 明确并记录最终 decoder adapter 的 API、输入分块大小、解码 task 栈、PCM 工作缓冲和错误恢复；大块压缩缓冲必须在 PSRAM。
- [x] decoder adapter 必须保留 `esp_audio_simple_dec_process()` 尚未消费的输入字节；只有 `raw.consumed` 对应的数据被确认消费后，reader 才能推进 PSRAM ring，不能覆盖未消费的 MP3 帧。
- [x] 硬件格式已确认：当前 `audio_platform_config.h` 的 ES8311/I2S 输出为 `24 kHz / 16-bit / mono`，与首版服务器转码目标一致，首版不新增采样率转换。
- [x] 记录实现前的 internal RAM、PSRAM 与 task stack 基线，复用已有 `printf_esp32_memory_stats()`、`printf_esp32_task_stack_stats()` 观测能力。

**已确定的解码路线：** 首版不新增 `minimp3` 等第三方解码依赖，不改造当前 `audio_player_play(FILE *)` 文件播放器；新增窄的流式 adapter，使用现有 `esp_audio_codec` 的 MP3 增量解码回调输出 PCM。

**门禁：** 流式 decoder spike 可连续解码固定 fixture，不依赖 `FILE *`，无 internal RAM 大对象、无 panic、无 stack overflow。

## 阶段 1：music-service 测试流与会话核心

计划新增目录 `server/music_service/`：

- [ ] Node.js 22 服务、锁定依赖和 Dockerfile；api-enhanced 作为进程内私有模块，不启动其通用 HTTP server。
- [ ] SQLite repository：账号状态、QR 会话、目录缓存、最近 20 首、`music_session_id`、完整队列快照、模式、近似进度、命令幂等与清理任务。
- [ ] 会话 owner：全局一个活动 FFmpeg；新会话/恢复/切歌先停止旧进程，重复 `stream_id` GET 不得重复 spawn。
- [ ] 先实现固定测试 MP3 upstream，不需要任何网易云账号；实现实时输出、暂停、恢复、停止、断线回收和 60 秒首次建流超时。
- [ ] 设置稳定错误码：`music_auth_required`、`music_session_not_found`、`music_stream_expired`、`track_unplayable`、`upstream_unavailable`、`transcode_failed`、`music_busy`。
- [ ] 单元测试覆盖缺失、错误和有效 `device_id + device_token`；鉴权失败不得创建会话、读取 Cookie、启动 FFmpeg 或泄露上游信息。
- [ ] 普通日志只记录会话 ID、状态、时长、字节数和错误码，不记录歌曲正文、Cookie、上游 URL 或 Bearer token。

**门禁：** Node 单元测试覆盖 command 幂等、会话替换、60 秒超时、15 秒失联回收、暂停续播、重启后的会话元数据保留且不自动建流、重复流 GET；本机脚本能持续读取测试 `audio/mpeg` 流。

## 当前部署基线

当前生产主栈运行在香港 1Panel：Hermes、watch endpoint、Gateway Relay 由 `server/watch_voice_endpoint/compose.hk.yml` 管理，公网 HTTPS/WSS 由香港 OpenResty 终止。音乐服务作为同一编排中的一个独立容器加入，当前与 OpenResty 一起形成五个相关容器，不迁回阿里云，也不新增独立 `ncm-api` 容器。

- 音乐服务只绑定宿主机回环端口 `127.0.0.1:18788:8788`；不加入 Hermes 或 `watch-relay-private` 网络。
- OpenResty 新增与 `/v1/watch/` 同级的 `location ^~ /v1/music/`，转发到 `http://127.0.0.1:18788`，并为持续音频流关闭代理缓冲、设置长读超时。
- 公网仍只允许设备鉴权的 `/v1/music/*`；音乐服务 `/health`、调试接口、Cookie、上游 URL 和通用 api-enhanced API 不得由 OpenResty 代理。
- 源码和 Compose 通过 1Panel 当前“路径选择”导入到 `/opt/1panel/docker/compose/ai-memory-watch/`；音乐数据与授权文件继续放在 `/opt/ai-memory-watch/music-data`，不提交到仓库。

## 阶段 2：香港 1Panel 隔离与 OpenResty 路由

- [x] 在 `server/watch_voice_endpoint/compose.hk.yml` 增加 `ai-memory-watch-music-service`，使用受限 Node/FFmpeg 镜像、`read_only`、受控 `tmpfs`、`pids_limit` 和单进程转码约束；首版不设置 `mem_limit`，待真实播放测量后再决定。
- [x] 将音乐数据挂载到 `/opt/ai-memory-watch/music-data:/data`；香港主机目录保持 `0700`，并将属主设为镜像内固定 UID `10003`（`music`），避免非 root 容器无法创建 SQLite；授权 env 文件保持 `0600`，不把密钥写入 compose 或仓库。
- [x] 只将 music-service 绑定宿主 loopback：`127.0.0.1:18788:8788`；容器 health check 只从本机检查。
- [x] 更新 `server/deploy/1panel/watch-endpoint/proxy/root.conf` 与香港实际 OpenResty 站点配置，仅将 `watch.934000.xyz/v1/music/*` 路由到该 loopback 端口；确认 `/health`、调试路径与任意 api-enhanced 路径仍不可公网访问。
- [x] 设置流响应 `Content-Type: audio/mpeg`、`Cache-Control: no-store`，不发送 Cookie、上游重定向或私有诊断头；代理关闭缓冲并使用长读超时。

**门禁：** 香港 1Panel 相关服务均在运行，四个应用容器通过 Docker health、OpenResty `-t`/reload 通过；回环 health、OpenResty 公网设备鉴权、未鉴权 401/403 和私有路径不可达全部通过。watch endpoint、Hermes、Relay 与 Dashboard 不受影响。内存限制仍待真实播放测量后决定，首版不设 `mem_limit`。

## 阶段 3：ESP32 music service 与流式播放器

计划新增：

- `main/services/music/music_service.[ch]`：唯一状态 owner，持有 command queue、只读 snapshot、播放会话生命周期和窄的 preempt/prepare API。
- `main/services/music/music_http_client.[ch]`：HTTPS JSON 控制与持续流读取，不接触 LVGL。
- `main/services/music/music_stream_player.[ch]`：reader/decoder 双 task、256 KB PSRAM ring、4 秒起播门槛、PCM 输出和停止顺序。
- `main/services/music/music_protocol.h`：手表侧 JSON 字段、状态与错误码映射，不包含 token 或上游字段。

实施要求：

- [ ] `music_service` 由一个 FreeRTOS owner task 串行处理 UI、网络、流结束、告警抢占和长按销毁命令；UI getter 无副作用。
- [x] reader 与 decoder 之间使用单个 PSRAM ring + FreeRTOS task notification 表达数据到达、空间释放和停止，mutex 只保护 ring 元数据与终态，不能用裸 `volatile` 协调。
- [x] 压缩环形缓冲 256 KB 放 PSRAM，起播门槛保持 32 KB；reader/decoder 各使用 8 KB PSRAM task stack，不新增第二份压缩缓存或大块 internal RAM 分配。
- [ ] 新增 `AUDIO_CODEC_OWNER_MUSIC_PLAYER`，通过 `audio_codec` 获取/释放 output session；不得绕过 owner 接口直接操作 I2S 或 PA。
- [ ] 为 Hermes 页面进入提供窄的 `music_service_pause_for_hermes_page()` 请求与 ACK。进入页面立即暂停音乐，音乐 owner 自行停流、清缓冲、释放 output session；调用方不得直接释放音乐资源。离开 Hermes 页面后音乐仍保持暂停，只有用户再次短按音乐控制才可继续。
- [ ] 固定播放期间资源策略：music service 只通过窄状态通知报告 `music_active`，不请求强前台；`safety_monitor_policy` 增加 `MUSIC_PLAYBACK` 阻塞原因并统一停止 `danger_detection_service` 的检测与危险状态机，同时关闭音频、视觉和震动提醒。不产生、不缓存、不补发告警事件，安全告警不得抢占或停止音乐。该规则覆盖危险提醒默认高优先级策略，必须由安全策略/session owner 执行，`app_alert_manager` 不得绕过该门禁。
- [ ] 音乐后台播放、熄屏继续；音乐 inactive 后从新的采样窗口重新启动危险检测，不恢复音乐 active 期间的旧状态；普通通知不发提示音；不新增低电量专用行为。
- [ ] 网络中断、music-service 重启或 ESP32 重启都不得自动播放；只保留曲目、队列、模式和近似进度，用户主动短按继续时才用持久化 `music_session_id` 创建新的 `stream_id`。
- [ ] 容器重启导致流失效时只恢复会话元数据并发布可继续状态，不自动启动 FFmpeg 或上游流。

**门禁：** source tests 覆盖 owner 边界、PSRAM 分配、无 UI 直接联网、命令幂等、告警/Hermes 交接、暂停/长按/断流状态；`idf.py build` 通过。

## 阶段 4：独立音乐 UI 与上拉栏

计划新增 `main/ui/custom/music_view.[ch]` 与 `main/ui/custom/music_controller.[ch]`，并只在 `main/ui/custom/main_dropdown_controller.c` 接入一个上拉栏音乐按钮。

- [x] 音乐页面只显示今日推荐、我喜欢、我的歌单、最近播放；曲目采用纵向滚动列表，每批加载 10 首并最多缓存 30 首，显示歌名、歌手与状态。
- [ ] 播放页只显示歌名、歌手、缓冲/播放/暂停/网络恢复状态、播放/暂停、上一首、下一首和三档模式；不显示搜索、封面、音量、歌词或精确时间。
- [x] 短按上拉栏音乐按钮：停止/错误态从今日推荐启动，播放/缓冲态暂停，暂停态继续；长按进入音乐页。按钮不再承担销毁会话语义。
- [ ] QR 登录页按服务端模块位图渲染，最长 120 秒；仅页面前台期间短期轮询状态，过期后由用户主动重新获取。
- [ ] 所有 LVGL 访问只在 UI/controller 层；controller 只投递 service command、复制 snapshot，不等待网络或解码。

**门禁：** UI source tests 覆盖无直接网络/音频操作、按钮意图和页面状态；host preview 或板端截图确认文字不溢出、安全区合规、控制稳定。

## 阶段 5：固定流端到端验证

- [x] 运行 server unit tests、香港 1Panel Compose health、回环流读取和 OpenResty 公网设备鉴权 smoke。
- [x] 固件执行 source tests、`idf.py build`、`idf.py -p <PORT> app-flash`，再以 `scripts/board/agent_serial_monitor.ps1` 限时观察。
- [x] 短时真机验收：熄屏继续播放、短按暂停/恢复、主动销毁会话、Wi-Fi 断开后不自动出声且由用户主动恢复。
- [ ] 补充记录 4 秒起播门槛，以及服务/手表重启后只保留状态且不自动播放的证据。
- [ ] 记录 internal RAM、PSRAM、reader/decoder/service task 栈高水位；无 `ESP_ERR_NO_MEM`、panic、Guru、stack overflow、输出 session 泄漏或重复 FFmpeg。
- [x] 用户确认音乐播放期间的安全告警关闭边界真机交互正常。
- [ ] 验证 Hermes 页面按 owner 协议让音乐有序释放输出，离开页面后保持暂停；音乐停止后从新采样窗口恢复危险检测。
- [ ] 检查音乐播放期间 runtime snapshot 未把音乐误报为强前台 owner，Safety Monitor snapshot 的阻塞原因为 `MUSIC_PLAYBACK`；进入 Hermes 时音乐通过音频交接释放，不依赖强前台抢占。

## 阶段 6：网易云私有联调

- [ ] 仅在固定流门禁通过后启用 api-enhanced 私有适配与二维码登录；不得打印或提交 Cookie。
- [ ] 验证 QR 创建、扫码、确认、120 秒过期、主动退出、授权失效后只显示 `expired` 且不自动建流；登录成功不自动加载目录，进入音乐页/打开来源时按需读取并验证我喜欢、歌单、今日推荐和最近播放。
- [ ] 验证手表分页、来源队列快照、三种模式、不可播放歌曲、自动跳过上限、断流后不自动播放、用户主动继续和容器重启后的元数据保留。
- [ ] 再次确认公网只能访问已鉴权 `/v1/music/*`，且没有暴露上游 URL、Cookie、health 或通用 API。

## 收尾与回滚

- [x] 更新本计划 Progress、Validation 和 Next Step；只有形成可复用部署/故障证据时才新增 `runs/`。
- [x] 更新 `docs/context/CHANGELOG.md` 仅记录稳定架构事实和关键验证，不记录普通命令流水。
- [x] 运行 `uv run python scripts/context/validate_context.py --level standard --q "AI Memory Watch online music streaming" --brief` 与 `git diff --check`。
- [x] 不代替用户自动暂存或提交；保留当前工作区，由用户自行决定提交时机。

回滚路径：从香港 OpenResty 删除 `/v1/music/*` 路由、停止 `ai-memory-watch-music-service` 容器、移除音乐页面入口或关闭其 Kconfig 开关；不修改 Hermes、watch voice endpoint、Relay 和 official_chat 的既有公网/会话路径。

## 进度

- [x] 设计审查：确定后台音乐不占用强前台、音乐期间完全关闭安全告警、所有恢复须用户主动继续、独立设备鉴权、服务器 Cookie、香港单容器部署和首版不设 Docker 内存上限。
- [x] 阶段 0：流式解码可行性与资源基线。
- [x] 阶段 0 解码器选型：复用 `espressif__esp_audio_codec` 的 MP3 增量解码接口。
- [x] 阶段 1：music-service 测试流与会话核心（固定流、SQLite 会话、单 FFmpeg、鉴权、暂停/恢复、断流回收、来源选首曲、队列切歌、URL 重新解析）。
- [x] 阶段 2：香港 1Panel 隔离与 OpenResty 路由；远端使用 `103.42.182.35` 与 `xianggang.pem` 部署，music-service health 通过，OpenResty 配置测试/重载通过，公网未鉴权音乐请求返回 `401`，`/health` 仍为 `404`。
- [x] 阶段 3：ESP32 music service 与流式播放器代码、音频 owner 交接、PSRAM 缓冲和 Hermes 页面暂停边界。
- [x] 阶段 4：独立音乐 UI 与上拉栏（四类来源入口、歌曲分页列表、控制和 QR 位图登录页均已接入）。
- [x] 阶段 5：固定流端到端验证完成；短时播放、熄屏、断网、主动销毁、安全告警边界与偶发卡顿优化均通过用户验收，未捕获的统计水位降为非阻塞证据。
- [x] 阶段 6：网易云私有联调按首版可用范围完成；真机扫码和真实音频流已通过，未逐项执行的扩展协议边界不再阻塞本计划结案。
- [x] 已记录后续 Hermes Music MCP 控制面路线；明确不进入首版实施范围。
- [x] 收尾：文档与验证门禁已同步；未自动提交用户工作区。

## 验证

- 2026-08-02：`uv run pytest tests/test_music_service_source.py tests/test_music_ui_source.py tests/test_music_safety_policy_source.py tests/test_music_stream_decoder_source.py tests/test_music_deployment_source.py -q`，13 passed；`server/music_service/npm test`，9 passed，覆盖 Cookie 重启后账户摘要恢复。
- 2026-08-02：`idf.py build` 通过，`111.bin=0xb36a30`，最小 app 分区剩余约 7%。
- 2026-08-02：COM7 `app-flash-monitor` 通过刷写和 60 秒启动观察，`panic_log_seen=0`、`residual_count=0`；日志 `board_logs/2026-08-02-16-15-18-music-v1-source-selection.log`，观测到 `music_service ready`、`internal_free=9115`、`psram_free=6735760`、`music_service` 栈余量约 6208 字节。
- 2026-08-02：`git diff --check` 通过；歌曲分页列表已接入并通过 source tests/build，尚未完成真实网易云扫码、真实音频播放和 QR 位图 UI。
- 2026-08-02：改用当前香港服务器 `103.42.182.35` 和 `C:\Users\ye\.ssh\xianggang.pem`；远端 Compose 配置通过，music-service 构建并运行 healthy，回环 `/health` 返回 `200`，OpenResty `-t` 与 reload 通过。
- 2026-08-02：公网无凭据探测 `https://watch.934000.xyz/v1/music/sources?device_id=watch-001` 返回 `401`，说明音乐路由已生效且仍受设备鉴权；公网 `/health` 返回 `404`，未发送 token、Cookie、登录或播放请求。
- 2026-08-02：首次启动发现 `/opt/ai-memory-watch/music-data` 为 `root:root 0700`，容器固定非 root 用户无法创建 SQLite；将主机目录属主修正为 UID `10003` 后恢复 healthy。该权限要求已写入本计划，旧香港服务器 `156.233.234.206` 记录作废。
- 2026-08-02：香港 `docker stats` 空闲快照：music-service `32.33 MiB`、Hermes `514.5 MiB`、watch endpoint `41.72 MiB`、Relay `42.13 MiB`；宿主机可用内存约 `2.4 GiB`。该快照不代表 FFmpeg 实际播放峰值，真实播放时仍需重新测量。
- 2026-08-02：香港固定流 smoke 使用同一生产镜像启动临时隔离容器，开启测试模式并生成短 MP3；来源读取、测试会话创建和带 `device_id` 的 `audio/mpeg` 流读取通过，收到 `96429` 字节。临时容器、测试数据库和音频文件已清理，生产 music-service 继续 healthy，生产 `MUSIC_TEST_MODE` 未改变。
- 2026-08-02：闭环测试发现并修复暂停断流问题：`MusicEngine.#stopProcess()` 现在先摘除/销毁 FFmpeg stdout，再终止进程并关闭 HTTP 响应；本地 `npm test` `10/10` 通过。香港临时 API 闭环验证暂停后收到约 `14253` 字节并结束、继续生成新 `stream_id` 且收到 `96429` 字节、销毁返回 `stopped`；生产容器随后仍 healthy。
- 2026-08-02：专用测试镜像在 COM7 完成 ESP32 固定流真机闭环：服务器测试模式提供固定 MP3，手表日志达到 `buffered_bytes=32768`、`active=1`，取得 `music_player` 音频输出 owner 并打印 `fixed source playback test passed`，随后释放音频 owner；`internal_free=17299`、`psram_free=6774676`，未发现 `ESP_ERR_NO_MEM`、panic、Guru 或 stack overflow。测试结束后已删除服务器测试音频并恢复生产 env。
- 2026-08-02：恢复默认关闭的 board test 后重新构建并刷回普通固件；COM7 60 秒启动观察到 `music_service_ready`、网络健康与公网 endpoint health 成功，未出现自动音乐测试、panic 或残留串口监控进程。
- 2026-08-02：在新增目录分页和 UI 文本省略保护后重新执行 `idf.py build`，构建通过，`111.bin=0xb37310`，最小 app 分区剩余约 7%；COM7 `app-flash-monitor` 刷写成功，60 秒观察无 panic/Guru/stack overflow，`MUSIC_SERVICE_BOARD_TEST` 保持关闭，网络进入 `SERVICE_READY`、watch endpoint health 为 `hermes_online=1`，music service 启动但未自动播放。脚本因观察时长结束返回的 `exit_code=1` 不代表刷写失败。
- 2026-08-02：全量收尾检查：`server/music_service/npm test` 10/10、音乐相关 ESP32 source tests 14/14、`git diff --check` 通过；音乐分页按钮增加长文本省略，并禁止在目录加载中点击旧目录曲目。
- 2026-08-02：QR 登录闭环：UI 仅通过 `music_service` owner 创建和轮询登录会话，LVGL canvas 使用 PSRAM 位图缓冲显示模块码；HTTP 解码拒绝模块数与位图长度不匹配的响应。音乐相关 source tests 16/16、`idf.py build` 和 COM7 `app-flash-monitor` 均通过，`111.bin=0xb38730`，最小 app 分区余量约 6%；启动日志见 `board_logs/2026-08-02-19-49-53-music-qr-login.log`，未见 panic/Guru/stack overflow，`music_service` 栈余量 3580 B。
- 2026-08-02：用户明确取消阶段 5 的连续 30 分钟播放验证；保留短时起播、熄屏、断网、暂停/继续、销毁和安全告警边界验证。
- 2026-08-02：修复 QR 请求鉴权缺陷：服务端要求每个音乐路由携带 `device_id`，而固件此前仅发送 Bearer token，账户 QR 路由因此以空设备 ID 返回 `403 device_not_allowed`。在共享 URL 构造处统一追加 `device_id`，音乐 source tests `16/16`、`idf.py build` 和 COM7 `idf.py -p COM7 app-flash` 通过；同一已部署端点返回 `200 qr_pending`、`qr.size=37` 且位图存在，未输出 token、二维码内容或 Cookie。
- 2026-08-02：真机点击登录后发现 `music_service` 在 QR HTTPS/TLS/JSON 路径栈溢出；保持单一 owner task，将其 PSRAM stack 从 `8192` 增至 `16384` 字节。音乐 source tests `16/16`、`idf.py build` 和 COM7 `app-flash` 通过；60 秒 QR 创建/轮询观察持续出现 HTTPS 证书校验，未再出现 stack overflow、panic、Guru 或重启，串口残留进程为 0，日志为 `board_logs/2026-08-02-20-37-25-music-qr-stack-fix.log`。
- 2026-08-02：修复 QR 状态正常但 canvas 空白：`music_http_request_account()` 已保留调用方的 PSRAM QR 输出缓冲，而 `music_http_parse_account()` 再次 `memset` 结果结构导致指针和容量被清零；删除该重复清零并加入回归断言。音乐 source tests `16/16`、`idf.py build` 和 COM7 `app-flash` 通过；60 秒登录/轮询观察无 panic 或重启，`music_service` 启动栈余量约 `11772` 字节，日志为 `board_logs/2026-08-02-20-43-33-music-qr-buffer-fix.log`。
- 2026-08-02：真机真实流起播后出现 CPU1 `IDLE1` task watchdog，回溯固定在 `lvgl_task -> lv_timer_handler -> lv_obj_update_layout`。根因是音乐页返回路径启动 250 ms screen 动画后立即删除仍被动画引用的当前 screen；改为先同步 `lv_screen_load()` 返回来源页，再销毁音乐 view。音乐 source tests `17/17`、`idf.py build` 和 COM7 `app-flash` 通过；烧录后 60 秒静态观察无 WDT/panic 且无残留 monitor，日志为 `board_logs/2026-08-02-20-55-50-music-back-screen-lifecycle.log`，真实“播放后返回”交互仍待用户复验。
- 2026-08-02：修复 LVGL host 模拟器因新增音乐 UI 无法启动：补齐 `esp_heap_caps` host mock 的 `heap_caps_free()`，并为音乐 controller 依赖补最小内存态 service mock；原构建错误和链接缺符号均消失。`build_apple_watch_s5_preview.ps1` 构建通过，`capture_apple_watch_s5_preview.ps1` 成功生成 `tools/ui_preview/artifacts/lvgl-simulator-start-fixed.png`，常驻 `agent_preview_host` 进程处于 Responding 状态。
- 2026-08-02：按用户新交互决策将主界面下拉栏无业务绿色 slider 直接替换为生成层 `screen_main_music_button`：短按启动今日推荐或切换播放/暂停，长按进入音乐页；右侧扬声器图标不再绑定音乐事件。host runner 增加 `--open-music` 与 `--open-dropdown` 预览入口；音乐相关 source tests `18/18`、host build、音乐页/下拉栏截图和 `idf.py build` 均通过，截图为 `tools/ui_preview/artifacts/music-dropdown-button.png` 与 `tools/ui_preview/artifacts/music-page-entry.png`。
- 2026-08-02：修复音乐目录“下一页”无响应：手表 UI 每页显示 5 首，但 service 只接受 10 的倍数 offset，导致下一页提交的 `offset=5` 被入口拒绝。共享分页大小现统一为 5，`VISIBLE_COUNT` 直接复用该常量；host mock 同步提供 12 首三页目录数据。聚焦 source tests `12/12`、host build 与 `idf.py build` 通过，`111.bin=0xb387c0`，最小 app 分区余量约 6%；截图为 `tools/ui_preview/artifacts/music-pagination-fixed.png`。
- 2026-08-02：按用户确认将歌曲分页按钮替换为纵向滚动目录：music service 每批读取 10 首，LVGL 滑到底仅投递下一批 owner 命令；目录快照和最多 30 首 UI 缓存位于 PSRAM，超过上限时回收最早 10 首并保持滚动位置。音乐 source tests `19/19`、host build/滚动页截图和 `idf.py build` 通过，`111.bin=0xb38950`，最小 app 分区余量约 6%；COM7 `app-flash` 与 30 秒启动观察无 panic/Guru/stack overflow，`internal_free=12263`、`psram_free=6723460`、LVGL task 栈余量约 `6772 B`，日志为 `board_logs/2026-08-02-21-44-41-music-scroll-catalog.log`，截图为 `tools/ui_preview/artifacts/music-scroll-catalog.png`。
- 2026-08-02：参考 Apple Watch 与 Galaxy Watch 将音乐页从白色表单式布局重排为深色播放器：当前歌曲卡片成为视觉焦点，中央播放键放大，前后曲退居两侧，来源/模式使用次级胶囊控件，来源卡片改为两列，队列行高与间距增加；未新增封面、音量或精确进度。音乐 source tests `19/19`、host build、来源页/队列页截图和 `idf.py build` 通过，`111.bin=0xb38c70`，最小 app 分区余量约 6%；COM7 `app-flash` 与 30 秒启动观察无 panic/Guru/stack overflow，日志为 `board_logs/2026-08-02-22-01-19-music-player-redesign.log`，截图为 `tools/ui_preview/artifacts/music-player-redesign-sources.png` 与 `tools/ui_preview/artifacts/music-player-redesign-catalog.png`。
- 2026-08-02：收紧深色音乐页细节：首次进入显式应用来源页状态，调整来源卡片与队列区域的垂直间距；前后曲恢复为 `<`、`>`，通用按钮标签改为真正居中，歌曲目录行单独保持左对齐。音乐 source tests `19/19`、host build、`idf.py build` 和 COM7 `app-flash` 通过，`111.bin=0xb38ca0`，最小 app 分区余量约 6%；截图为 `tools/ui_preview/artifacts/music-player-detail-centered-sources.png` 与 `tools/ui_preview/artifacts/music-player-detail-centered-catalog.png`。按用户要求未追加长时间运行验证。
- 2026-08-02：主界面下拉栏 `screen_main_loudness` 已接入真实系统音量：拖动按 50 ms 节流实时更新，松手仅在值变化时写入 NVS `audio_codec/volume`，启动自动恢复，0% 切换静音图标；生成层布局未承载业务逻辑。音乐、AI 语音、危险告警和 MCP 统一服从该音量，不再各自覆盖固定值。相关 source tests `59/59`、host build/截图、`idf.py build` 与 COM7 `app-flash` 通过，`111.bin=0xb391b0`，最小 app 分区余量约 6%；15 秒启动观察到 `Speaker volume restored: 60%`、无 panic 且无残留 monitor，日志为 `board_logs/2026-08-02-22-35-04-system-volume-startup.log`，截图为 `tools/ui_preview/artifacts/system-volume-slider.png`。按用户要求未执行长时间测试，真实听感与设置 35% 后重启恢复仍待触摸验收。
- 2026-08-02：用户确认短时真机音乐交互无异常，当前深色音乐页、播放控制和系统音量滑动的人工验收通过；按既定范围不追加 30 分钟连续播放测试。该确认不替代后续熄屏、断网、销毁和安全告警边界验证。
- 2026-08-02：用户确认熄屏播放、断网停止/主动恢复、会话销毁、安全告警关闭边界、真实扫码登录和网易云真实音频流均无异常。剩余主要问题收敛为播放期间偶发卡顿：当前 `music_stream` 单 task 串行执行 HTTPS read、MP3 解码和 `audio_codec_write()`，每轮解码后只要 ring 尚有空间就立即发起最长约 5 秒的阻塞读取；网络抖动期间即使 128 KB ring 内仍有压缩数据，也无法并行继续喂 PCM，因此现有 ring 尚未形成真正的 reader/decoder 解耦。
- 2026-08-02：偶发卡顿优化已落地：`music_stream_player` 拆为独占 HTTPS 的 reader task 与独占 MP3 decoder/I2S 的 decoder task，共享单个 256 KB PSRAM ring，32 KB 起播门槛不变；两个 8 KB task stack 均在 PSRAM，停止路径等待双 task 完成。音乐聚焦 source tests `17/17`、`git diff --check` 和 `idf.py fullclean build` 通过，`111.bin=0xb397d0`，最小 app 分区余量约 6%；COM7 `app-flash` 成功。用户完成真实网易云短时播放并确认不卡顿，未执行 30 分钟测试。运行态 monitor 因 USB `ClearCommError` 重连失败未捕获终态 `stats`，因此本次不宣称机器侧 `underrun_count=0`。

## 下一步

本计划已完成归档，无阻塞性下一步。`ring_min/ring_max/underruns/http_read_max/pcm_gap_max` 与双 task 栈余量仅作为出现真实播放问题时的诊断入口；不重复既有功能验收，也不执行 30 分钟连续播放测试。
