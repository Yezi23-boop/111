---
id: context-current-task
tags: context, handoff, current-task
summary: 记录 AI Memory Watch / Hermes 真实接入链路、Cloudflare Tunnel 公网联调状态、当前阻塞点和下一步。
last_reviewed: 2026-06-09
memory_type: task
scope: task
owners: docs/context/handoffs
triggers: handoff, current-task, next-step, ai-memory-watch, hermes, watch_voice_endpoint, cloudflare, tunnel
evidence_level: design
---

# AI Memory Watch / Hermes 当前任务交接

## 目标

- 长期推进 `AI Memory Watch / Hermes` 真实接入链路，直到服务器侧可稳定联调并可进行版本迭代。
- 当前主链路为：`ESP32-S3 手表 -> watch voice endpoint -> Hermes API Server /v1/responses -> MiMo/Hermes 回复 -> 手表 V1 JSON`。
- 当前先不做需要用户手动操作的真机“按住说话”测试，优先用服务器侧脚本模拟手表 multipart Ogg Opus 上传，打通公网 watch endpoint 验收。

## 当前状态

- 当前分支：`codex/ai-memory-watch-hermes-api`。
- Hermes API Server 已启用，并已验证 `/health`、`/v1/models`、`/v1/responses`。
- `watch voice endpoint` 已实现三端点，常驻容器 `ai-memory-watch-voice-endpoint` 当前为 `healthy`，绑定 `127.0.0.1:8787`。
- Cloudflare 已收敛到 `watch.934000.xyz`：CNAME 指向 Tunnel `8900f692-6507-432a-b811-62b66ce6c44c`，ingress 只代理 `/v1/watch/* -> http://host.docker.internal:8787`，fallback 为 `http_status:404`。
- Tunnel 当前仍为 `down`，公网返回 Cloudflare `530/1033`；本机缺少仓库外 token 文件 `D:\Docker_data\hermes\cloudflared_tunnel_token.txt`，因此 `ai-memory-watch-cloudflared` connector 尚未启动。
- ESP32-S3 已刷回当前 `111` 主应用并自动连上 Wi-Fi，串口已出现 `network_service_ready`、`official_chat_ready`、`memory_watch_ready`；板端仍缺 watch endpoint NVS 配置。
- 任务尚未结束，因为公网 connector 未运行，`https://watch.934000.xyz/v1/watch/health`、公网 runtime gate 和公网 smoke 仍未通过。

## Progress

- Hermes Docker 已部署，Dashboard 仅本地 `127.0.0.1:9119` 可用；Dashboard 不是 ESP32-S3 设备接口。
- Hermes API Server 已在仓库外 `.env` 中启用，`8642` 已映射到宿主机；API key 不进入仓库或固件。
- `server/watch_voice_endpoint` 已实现 `GET /v1/watch/health`、`POST /v1/watch/voice-command`、`POST /v1/watch/request/{request_id}/cancel`。
- MiMo ASR 真实链路已跑通：`Ogg Opus -> ffmpeg 转 16 kHz mono WAV -> MiMo ASR -> Hermes /v1/responses -> 手表 V1 固定 7 字段 JSON`。
- watch endpoint 已支持 mock ASR、MiMo ASR、request 幂等、cancel、115 秒服务器请求预算、输入校验、运行态指标、auth 诊断和公网私有路径门禁。
- `release_gate.ps1` 已作为服务器侧版本门禁，最近一次通过包含 server pytest `40 passed`、Hermes text smoke、mock voice、real MiMo ASR、cancel 与 invalid token 403。
- 新增 `server/watch_voice_endpoint/deploy/start_cloudflared_connector.ps1`：默认读取仓库外 `D:\Docker_data\hermes\cloudflared_tunnel_token.txt`，用只读挂载和 `TUNNEL_TOKEN_FILE` 启动独立 `cloudflared` 容器，避免 token 进入 Docker command args、仓库或日志。
- 本机脚本模拟手表已通过：mock ASR smoke 返回 `voice_status=done/action=memory_saved/field_count=7`；`make_tts_sample.ps1 -> smoke_test.ps1 -UseRealAsr` 返回 `asr_mode=real/voice_status=done/action=memory_saved/field_count=7`。

## Decision Log

- V1 不使用 webhook，因为手表侧需要同步等待最终文本结果。
- ESP32-S3 只调用 watch endpoint，不直接调用 Hermes Dashboard、Hermes API Server 或 MiMo API。
- Hermes/MiMo/API key 只保留在服务器或仓库外 env；ESP32 固件只保存 watch endpoint 的 `device_id/device_token/base_url`。
- 公网第一版只允许代理 `/v1/watch/*`；Hermes `8642` 和 Dashboard `9119` 保持私有。
- Cloudflare connector 与 watch endpoint 部署在同一台 Docker Desktop 机器，但保持独立容器，避免把 Tunnel token 烘进 watch endpoint 镜像。
- `docs/context/CHANGELOG.md` 当前有无关脏改动；如需追加本任务 changelog，只暂存本任务新增行。

## 已验证

- `docker ps`：`hermes` 正常运行，`ai-memory-watch-voice-endpoint` 为 `healthy`。
- Hermes API Server：`GET /health`、`GET /v1/models`、`POST /v1/responses` 已用中文手表记忆请求验证成功。
- watch endpoint：mock smoke、cancel smoke、invalid-token 403、real MiMo ASR smoke 均已通过。
- server pytest 最近通过数：`41 passed`。
- `.\server\watch_voice_endpoint\release_gate.ps1 -SkipDocker` 最近通过；输出不包含真实 key/token、ASR 正文或回复正文。
- `uv run python scripts/context/validate_context.py --level standard --q "AI Memory Watch Cloudflare Tunnel public endpoint" --brief` 已通过，context 检查 0 错误 0 警告。
- Cloudflare 只读复查：Tunnel 名称 `ai-memory-watch`、状态 `down`、连接数 `0`；DNS `watch.934000.xyz CNAME -> 8900f692-6507-432a-b811-62b66ce6c44c.cfargotunnel.com` 且 proxied。
- `start_cloudflared_connector.ps1` 在缺 token 文件时安全失败：`reason=token_file_missing`，未创建 connector 容器，未输出 token。
- `runtime_status.ps1 -BaseUrl https://watch.934000.xyz -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed` 当前返回 Cloudflare `530`，与 Tunnel down 一致。

## 当前风险

- Cloudflare Tunnel token 文件尚未放到仓库外目标路径，connector 不能自动启动；不能把 token 写入仓库、日志、文档或最终输出。
- ESP32-S3 已联网但缺 NVS endpoint 配置，真机 `按住说话 -> Ogg Opus 上传 -> 120 秒内返回固定 7 字段` 仍未验证。
- 公网域名需在 connector 启动后用 `-AssertPrivateNotExposed` 验证私有路径没有暴露；当前 `530/1033` 只能证明 connector 未连接，不能算验收通过。
- 真实用户语音样本、弱网重试、长耗时 agent 工具执行、token 首次配置体验仍待真机场景复测。
- 工作区有大量已有未提交 context 相关改动，不要回滚或混入 AI Memory Watch 提交。

## 下一步

- 将 Cloudflare Tunnel token 写入仓库外 `D:\Docker_data\hermes\cloudflared_tunnel_token.txt` 后，运行 `.\server\watch_voice_endpoint\deploy\start_cloudflared_connector.ps1 -Pull` 启动独立 connector 容器。
- Connector 启动后先确认 Tunnel 不再 `down`，再运行公网 runtime gate：`.\server\watch_voice_endpoint\runtime_status.ps1 -BaseUrl "https://watch.934000.xyz" -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed`。
- 公网 runtime gate 通过后，运行 `.\server\watch_voice_endpoint\smoke_test.ps1 -BaseUrl "https://watch.934000.xyz" -SkipServiceHealth`，再按条件用 `make_tts_sample.ps1` 生成 Ogg Opus 做真实 ASR smoke。
- 每次 server 改动后运行：`uv run --with-requirements server/watch_voice_endpoint/requirements.txt python -m pytest server/watch_voice_endpoint/tests -q`。
- 需要运行态验证时运行：`.\server\watch_voice_endpoint\release_gate.ps1 -SkipDocker`；涉及容器构建时运行 `.\server\watch_voice_endpoint\release_gate.ps1 -RebuildContainer`。
- ESP32 侧下一步是在不泄露 token 的前提下写入 NVS：`base_url=https://watch.934000.xyz`、`device_id=watch-001`、`device_token=<watch device token>`、`timeout_ms=120000`、`allow_http=false`；若需要用户操作 SoftAP，则等用户回来再做真机按住说话测试。

## 证据入口

- 相关计划：`docs/context/plans/active/2026-06-05-ai-memory-watch-hermes-page-plan.md`
- 服务器目录：`server/watch_voice_endpoint/`
- 机器可读契约：`server/watch_voice_endpoint/watch_contract.v1.json`
- 产品定位：`docs/context/knowledge/project/ai-memory-watch-product-positioning.md`
- 仓库画像：`docs/context/knowledge/project/project-profile.md`
