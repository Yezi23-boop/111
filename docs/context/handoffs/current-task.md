---
id: context-current-task
tags: context, handoff, current-task
summary: 记录 AI Memory Watch / Hermes 真实接入链路、Cloudflare Tunnel 公网联调验证状态、ESP32 NVS 待办和下一步。
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
- Tunnel 当前为 `healthy`，`ai-memory-watch-cloudflared` 独立容器已启动；token 文件位于仓库外 `D:\Docker_data\hermes\cloudflared_tunnel_token.txt`，容器只通过只读挂载和 `TUNNEL_TOKEN_FILE` 使用 token，Docker args 不含 token。
- ESP32-S3 已刷回当前 `111` 主应用并自动连上 Wi-Fi，串口已出现 `network_service_ready`、`official_chat_ready`、`memory_watch_ready`；板端仍缺 watch endpoint NVS 配置。
- 公网服务器侧链路已通过脚本验收：`https://watch.934000.xyz/v1/watch/health` 可用，公网 runtime gate、mock smoke 与真实 MiMo ASR smoke 均通过。

## Progress

- Hermes Docker 已部署，Dashboard 仅本地 `127.0.0.1:9119` 可用；Dashboard 不是 ESP32-S3 设备接口。
- Hermes API Server 已在仓库外 `.env` 中启用，`8642` 已映射到宿主机；API key 不进入仓库或固件。
- `server/watch_voice_endpoint` 已实现 `GET /v1/watch/health`、`POST /v1/watch/voice-command`、`POST /v1/watch/request/{request_id}/cancel`。
- MiMo ASR 真实链路已跑通：`Ogg Opus -> ffmpeg 转 16 kHz mono WAV -> MiMo ASR -> Hermes /v1/responses -> 手表 V1 固定 7 字段 JSON`。
- watch endpoint 已支持 mock ASR、MiMo ASR、request 幂等、cancel、115 秒服务器请求预算、输入校验、运行态指标、auth 诊断和公网私有路径门禁。
- `release_gate.ps1` 已作为服务器侧版本门禁，最近一次通过包含 server pytest `40 passed`、Hermes text smoke、mock voice、real MiMo ASR、cancel 与 invalid token 403。
- 新增 `server/watch_voice_endpoint/deploy/start_cloudflared_connector.ps1`：默认读取仓库外 `D:\Docker_data\hermes\cloudflared_tunnel_token.txt`，用只读挂载和 `TUNNEL_TOKEN_FILE` 启动独立 `cloudflared` 容器，避免 token 进入 Docker command args、仓库或日志。
- 本机脚本模拟手表已通过：mock ASR smoke 返回 `voice_status=done/action=memory_saved/field_count=7`；`make_tts_sample.ps1 -> smoke_test.ps1 -UseRealAsr` 返回 `asr_mode=real/voice_status=done/action=memory_saved/field_count=7`。
- 公网脚本模拟手表已通过：mock multipart Ogg Opus 上传返回 `voice_status=done/action=memory_saved/field_count=7`，真实 MiMo ASR 上传返回 `asr_mode=real/voice_status=done/action=memory_saved/field_count=7`。

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
- Cloudflare 只读复查：Tunnel 名称 `ai-memory-watch`、状态 `healthy`、连接数 `1`；DNS `watch.934000.xyz CNAME -> 8900f692-6507-432a-b811-62b66ce6c44c.cfargotunnel.com` 且 proxied。
- `start_cloudflared_connector.ps1 -Pull` 已启动 `ai-memory-watch-cloudflared`；安全复查确认容器无端口映射，env 只有 `TUNNEL_TOKEN_FILE` 路径，Docker args 不含 token，token 文件长度只用于存在性确认且不打印内容。
- `runtime_status.ps1 -BaseUrl https://watch.934000.xyz -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed` 已通过：watch health `ok/hermes_status=online`，公网 `/health`、`/v1/models`、`/v1/responses` 均为 404 未暴露。
- `smoke_test.ps1 -BaseUrl https://watch.934000.xyz -SkipServiceHealth` 已通过 mock ASR；`make_tts_sample.ps1 -> smoke_test.ps1 -UseRealAsr -AudioPath <generated.ogg>` 已通过真实 MiMo ASR，二者均返回固定 7 字段 JSON。

## 当前风险

- ESP32-S3 已联网但缺 NVS endpoint 配置，真机 `按住说话 -> Ogg Opus 上传 -> 120 秒内返回固定 7 字段` 仍未验证。
- PC 侧对板端 `192.168.41.11` ping 与 HTTP 探测超时，无法在用户休息期间无人工通过 SoftAP/门户写入 NVS；等待用户回来后进入同网可达或 SoftAP 配置。
- 真实用户语音样本、弱网重试、长耗时 agent 工具执行、token 首次配置体验仍待真机场景复测。
- 工作区有大量已有未提交 context 相关改动，不要回滚或混入 AI Memory Watch 提交。

## 下一步

- 继续保持 `ai-memory-watch-cloudflared` 与 `ai-memory-watch-voice-endpoint` 常驻，后续改动后先跑公网 runtime gate 与 smoke，确认 Tunnel、私有路径门禁和 7 字段 JSON 未回退。
- 每次 server 改动后运行：`uv run --with-requirements server/watch_voice_endpoint/requirements.txt python -m pytest server/watch_voice_endpoint/tests -q`。
- 需要运行态验证时运行：`.\server\watch_voice_endpoint\release_gate.ps1 -SkipDocker`；涉及容器构建时运行 `.\server\watch_voice_endpoint\release_gate.ps1 -RebuildContainer`。
- ESP32 侧下一步是在不泄露 token 的前提下写入 NVS：`base_url=https://watch.934000.xyz`、`device_id=watch-001`、`device_token=<watch device token>`、`timeout_ms=120000`、`allow_http=false`；若需要用户操作 SoftAP，则等用户回来再做真机按住说话测试。

## 证据入口

- 相关计划：`docs/context/plans/active/2026-06-05-ai-memory-watch-hermes-page-plan.md`
- 服务器目录：`server/watch_voice_endpoint/`
- 机器可读契约：`server/watch_voice_endpoint/watch_contract.v1.json`
- 产品定位：`docs/context/knowledge/project/ai-memory-watch-product-positioning.md`
- 仓库画像：`docs/context/knowledge/project/project-profile.md`
