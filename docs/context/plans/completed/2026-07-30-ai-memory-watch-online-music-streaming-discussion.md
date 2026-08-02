---
id: ai-memory-watch-online-music-streaming-discussion
tags: context, plans, ai-memory-watch, music, streaming, esp32-s3, ncm, decision-record
title: AI Memory Watch 在线音乐播放需求决策
summary: 记录 ESP32-S3 手表扬声器在线音乐播放的已确认产品、协议、安全和资源边界；实施以独立执行计划为准。
memory_type: task
scope: task
owners: docs/context/plans/completed/2026-07-30-ai-memory-watch-online-music-streaming-discussion.md, docs/context/plans/completed/2026-07-31-ai-memory-watch-online-music-streaming-execution-plan.md
evidence_level: design
status: archived
last_reviewed: 2026-07-31
triggers: ai-memory-watch, online-music, music-streaming, netease-cloud-music, esp32-speaker
---

# AI Memory Watch 在线音乐播放需求决策

## 定位

让 ESP32-S3 手表通过自身扬声器稳定在线播放用户有权播放的音乐。它是独立音乐功能，不是桌面播放器遥控，也不改变 AI Memory Watch 作为 Hermes 随身输入与交互终端的定位。

## 首版范围

- 音乐入口为独立页面，目录只有“今日推荐、我喜欢、我的歌单、最近播放”。
- 手表每页显示 10 首，只缓存当前页；服务器以完整来源列表建立播放队列。
- 不做搜索、语音点歌、Hermes 音乐控制、远程封面、音量、精确进度、歌词、无损、离线下载、多设备或多账号。
- 播放页只显示歌名、歌手和状态，提供播放/暂停、上一首、下一首与播放模式切换。
- 系统上拉栏只放一个音乐按钮：短按播放/暂停；长按停止并销毁本次本地音乐运行服务，不要求二次确认。

## 端云架构与安全

```text
手表音乐 UI
  -> music-service（公网窄接口，现有 device_id + device_token）
  -> api-enhanced Node 模块（同一私有容器）
  -> 用户账号正常可播放的上游资源
  -> FFmpeg 实时转码
  -> HTTPS/TCP MP3 流
  -> ESP32 PSRAM 缓冲 -> 流式解码 -> ES8311 扬声器
```

- 新增一个独立 `music-service` Node.js 22 容器；它集成网易云适配、二维码授权、目录缓存、播放会话、队列与受控 FFmpeg。首版不部署独立 `ncm-api` 容器。
- Cloudflare 仅公开 `https://watch.934000.xyz/v1/music/*`。健康检查、调试、Cookie、FFmpeg 控制和 api-enhanced 通用端点仅留 Docker 内网或主机 loopback。
- 不公开、记录或下发网易云 Cookie、账号、密码、平台播放 URL、device token 或任何解锁能力。
- Cookie 写入阿里云主机受限挂载目录的私有数据文件，目录权限 `0700`、文件权限 `0600`；镜像、仓库、日志和 ESP32 均不保存 Cookie。
- 单用户首版只支持一个服务器私有网易云账号绑定 `watch-001`。

## 音频与播放会话

- 服务器实时转码、边转边发，不保存整首歌曲，统一输出 `MP3 / 24 kHz / mono / 64 kbps`。
- 音频走持续 `HTTPS / TCP` 分块 `audio/mpeg` 流；音乐控制走独立 HTTP JSON，不复用 Hermes WSS、HLS、UDP/RTP 或 MQTT。
- ESP32 使用 128 KB PSRAM 压缩音频环形缓冲，首次播放或重连至少积累 4 秒再出声；首版允许切歌时短暂缓冲，不做 gapless。
- `music_session_id` 是可持久化的播放状态标识；`stream_id` 是不含密钥的短期建流标识。首次取得 `stream_id` 后 60 秒内必须建立流；设备 token 始终放 `Authorization` 请求头，不放 URL。
- 全局仅允许一个 FFmpeg 转码。新点选、下一首、恢复或重连必须先终止旧流；重复 GET 不得创建第二个进程。
- 短按暂停时停止当前流与转码，只保留曲目、队列、模式和近似进度；继续时重新建流，优先近似续播，无法可靠 seek 时明确从头播放。
- 长按在线时显式停止服务器播放会话；离线时本地直接释放，服务器在 15 秒无有效流读取或控制请求后回收。

## 队列与恢复

- 模式为 `repeat_one`、`repeat_all`、`shuffle`，偏好保存在设备级配置中。
- 手表只上报模式和上一首/下一首意图；music-service 决定完整队列、随机历史和下一首。
- `repeat_one` 只影响自然结束；手动上一首/下一首永远优先。
- 模式切换不打断当前歌曲，从下一次选曲起生效。
- 当前播放会话持有开始时的来源队列快照；只有长按销毁服务或从目录重新点选歌曲才创建新快照。
- 新来源点选立即停止旧流与转码，从新来源完整列表中按点选曲目创建新会话。
- “最近播放”只保存 watch-001 实际开始播放的最近 20 首；“今日推荐”按 `Asia/Shanghai` 自然日固定；我喜欢和歌单目录在服务器缓存 5 分钟。
- 用户直接点选不可播放歌曲时明确报错；自动队列最多跳过 3 首后停止，不走解锁或替代源。
- 网络中断时在 30 秒内自动恢复此前正在播放的歌曲；用户主动暂停、手表重启或长按销毁后均不自动播放。
- music-service 重启后从 SQLite 恢复会话元数据；ESP32 断流后按同一重连策略请求新流，不能恢复才显示错误。

## UI 与资源仲裁

- 离开音乐页或熄屏后音乐继续后台播放；普通通知只弹气泡，不播放提示音。
- 安全告警必须抢占扬声器并停止音乐；进入 Hermes 录音前必须由音乐 owner 停流、释放 output session，再允许麦克风采集。
- `audio_codec` 继续是实际 input/output session owner。音乐状态、网络、缓冲、解码与停止顺序由新的 music service owner task 串行处理；UI 只提交意图并读取快照。
- 不新增音乐私有低电量策略，后续依据现有系统预算和真实续航证据再评估。

## 后续修订

- 2026-08-02：统一恢复策略。网络中断、music-service 重启和 ESP32 重启均不自动重新播放；仅保留曲目、队列、模式和近似进度，用户主动继续时才重新建立音频流。该规则覆盖此前“网络中断自动恢复”和“服务重启后自动请求新流”的旧表述，避免设备在用户没有操作时突然出声。
- 2026-08-02：Hermes 录音抢占音乐后也保持暂停；录音结束或离开 Hermes 页面不自动恢复，必须由用户再次短按音乐控制继续。
- 2026-08-02：进一步简化为进入 Hermes 页面立即暂停音乐，不等待录音开始；离开页面后仍保持暂停，必须用户再次短按音乐控制继续。
- 2026-08-02：网易云授权过期时只进入 `expired` 状态并停止新建播放流；不自动刷新二维码、自动登录或切换账号，必须用户主动重新扫码。
- 2026-08-02：云端音乐能力只增加一个 `music-service` 容器，`api-enhanced` 作为进程内私有模块使用，不部署独立 `ncm-api` 容器或公网通用 API。
- 2026-08-02：音乐服务部署在当前香港 1Panel 主机，与 Hermes、watch endpoint、Relay 同机；公网只由香港 OpenResty 转发 `/v1/music/*`，不迁回阿里云或新增第二条公网入口。
- 2026-08-02：退出网易云账号必须二次确认；确认后停止音乐会话并删除服务器 Cookie 与账号元数据。
- 2026-08-02：music-service 首版不设置 Docker 内存上限；保留单 FFmpeg 和进程数限制，先记录真实播放内存峰值，再决定是否增加 `mem_limit`。

2026-08-02：音乐播放期间完全关闭安全告警：暂停 `danger_detection_service` 的检测与危险状态机，同时关闭音频、视觉和震动；事件不产生、不缓存、不补发。音乐停止后从新的采样窗口重新启动危险检测。该修订覆盖本文原先“安全告警必须抢占扬声器并停止音乐”的表述，也覆盖上一版“保留视觉与震动”的中间决定；首版执行以当前修订为准。该策略与危险提醒默认高优先级存在冲突，后续实现必须在 `danger_detection_service` owner 入口显式处理。

## 账号操作

- 手表音乐页可发起二维码登录并全屏显示二维码；二维码最长显示 120 秒，过期或取消后由用户主动重新获取。
- 二维码状态只在登录页临时轮询；音乐播放状态不做固定后台轮询。
- 授权失效时显示“需要重新登录”，不自动使用游客、其他账号或其他上游。
- 音乐页二级设置提供退出账号，需确认；确认后停止播放并删除服务器私有授权和账号元数据，保留模式偏好与最近播放记录。

## 进度

- [x] 已完成产品、协议、安全和资源决策。
- [x] 已修正旧讨论稿中 Hermes 点歌、搜索、封面、音量及双容器架构的冲突表述。
- [x] 已创建独立实施计划；所有后续代码、部署、验证和提交以该计划为准。

执行结果见已归档的 [在线音乐播放执行计划](2026-07-31-ai-memory-watch-online-music-streaming-execution-plan.md)。
