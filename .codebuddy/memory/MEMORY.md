# AI Memory Watch 项目长期记忆

## 公网数据链路（生产架构，2026-08-01 确认与代码一致）

```
ESP32-S3
  HTTPS / WSS
  ↓
https://watch.934000.xyz  (Cloudflare Tunnel: ai-memory-watch)
  ↓ 阿里云 cloudflared connector
watch endpoint :8787
  ↓ 私有 Docker 网络 (http://watch-relay-connector:9080)
watch-relay-connector :9080
  ↓ 单条持久 Relay WebSocket
Hermes Gateway → Hermes Agent + MiMo
```

- ESP32 使用接口：
  - `GET /v1/watch/health?device_id=watch-001`
  - `WS wss://watch.934000.xyz/v1/watch/ws`（前台 Hermes 对话主通道）
  - `POST /v1/watch/voice-command`（multipart audio/ogg，V1 兼容/回退）
  - `POST /v1/watch/text-command`（额外已有）
  - `POST /v1/watch/alerts`（危险提醒推送，额外已有）
  - `GET /v1/watch/sync?device_id=&mode=&pending_request_id=&after_message_id=&max_messages=`（V2.4 后台统一 delta）
  - `POST /v1/watch/request/{request_id}/cancel`
  - `GET /v1/watch/inbox`、`POST /v1/watch/inbox/{id}/read`
  - `GET /v1/watch/ota/manifest?channel=stable`
- 统一认证：`Authorization: Bearer <watch_device_token>`。
- ESP32 只保存 `base_url`（默认 `https://watch.934000.xyz`）、`device_id=watch-001`、`device_token`（生产经 SoftAP/NVS 配置，Kconfig 默认空）。绝不保存 Hermes API key、MiMo key、Relay secret、Cloudflare key。
- 前台 Hermes WSS 流程：auth → conversation_snapshot → audio_start → 二进制 Ogg Opus 帧 → audio_end → asr_result → task_started/task_progress → conversation_message。页面离开后 ESP32 关闭 WSS，服务器任务不取消；未完成任务通过 `GET /v1/watch/sync` 恢复最终状态和回复。
- 服务器内部：watch endpoint 通过私有 Docker 网络 `http://watch-relay-connector:9080` 内部 HTTP 提交；Connector 与 Hermes Gateway 复用一条服务器侧持久 WebSocket，不为每个任务重建。
- 生产配置 `WATCH_HERMES_TRANSPORT=relay`；不自动 Relay/Direct 双路竞速，不自动切 Direct（避免重复执行 Hermes 任务）。
- 私有边界：Hermes API Server :8642、Hermes Dashboard :9119、Relay Connector :9080、`/internal/*` 不能暴露公网；Cloudflare Tunnel 只允许 `/v1/watch/*`。
- 服务器运维：`ssh -i "C:\Users\ye\.ssh\hermes.pem" root@8.134.203.76`，远程目录 `/opt/ai-memory-watch/watch-endpoint`，主要容器：`hermes`、`ai-memory-watch-voice-endpoint`、`ai-memory-watch-relay-connector`、`ai-memory-watch-cloudflared`。
- 代码落点：`main/services/memory_watch/`（`memory_watch_voice_client.c` 是 HTTP 客户端、`watch_endpoint_service.c` 是危险警报 worker、另有 WS client）。Kconfig 默认值见 `main/Kconfig.projbuild` 的 "AI Memory Watch" menu。

## 服务器选型评估结论

- 当前架构（Hermes Gateway/Agent + watch-endpoint + relay-connector + cloudflared，语音走云端 API）4核4G 足够（约 1.5-3GB 内存）。
- 若叠加本地 ASR（FunASR/sherpa-onnx），最简 2核4G、全模块 4核8G；本地 TTS（GPT-SoVITS/CosyVoice）需 16G 起步。
- 用户考虑香港服务器（Vapeline IDC 85.137.240.x 网段）。

## 危险样本 SD 卡闭环（2026-07-04 完成）

- 危险样本录制器：buffer=5000ms, rate=16000Hz, dir=/sdcard/danger_samples，输出 WAV+JSON。
- 已构建烧录，等待用户 UI 启用危险识别后真机闭环验证。
