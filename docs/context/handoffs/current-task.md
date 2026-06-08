---
id: context-current-task
tags: context, handoff, current-task
summary: 记录 AI Memory Watch / Hermes 真实接入链路当前进展、验证状态和下一步。
last_reviewed: 2026-06-08
memory_type: task
scope: task
owners: docs/context/handoffs
triggers: handoff, current-task, next-step, ai-memory-watch, hermes, watch_voice_endpoint
evidence_level: design
---

# AI Memory Watch / Hermes 当前任务交接

## 目标

- 长期推进 `AI Memory Watch / Hermes` 真实接入链路，直到服务器侧可稳定联调并可进行版本迭代。
- 当前主链路为：`ESP32-S3 手表 -> watch voice endpoint -> Hermes API Server /v1/responses -> MiMo/Hermes 回复 -> 手表 V1 JSON`。
- 当前 ESP32-S3 暂不能联网，等待用户回来提供热点；等待期间优先完善服务器侧联调门禁、协议契约、安全边界和真机接入准备。

## 当前状态

- 当前分支：`codex/ai-memory-watch-hermes-api`。
- 当前 HEAD：`e6443ab server: 清理手表 smoke 临时音频`。
- Hermes API Server 已启用，并已验证 `/health`、`/v1/models`、`/v1/responses`。
- `watch voice endpoint` 已实现三端点，常驻容器 `ai-memory-watch-voice-endpoint` 当前为 `healthy`，绑定 `127.0.0.1:8787`。
- 任务尚未结束，因为 ESP32-S3 真机端到端联网录音上传仍待热点和实机验证，公网域名/Caddy 也需真实部署后验收。

## Progress

- Hermes Docker 已部署，Dashboard 仅本地 `127.0.0.1:9119` 可用；Dashboard 不是 ESP32-S3 设备接口。
- Hermes API Server 已在仓库外 `.env` 中启用，`8642` 已映射到宿主机；API key 不进入仓库或固件。
- `server/watch_voice_endpoint` 已实现 `GET /v1/watch/health`、`POST /v1/watch/voice-command`、`POST /v1/watch/request/{request_id}/cancel`。
- MiMo ASR 真实链路已跑通：`Ogg Opus -> ffmpeg 转 16 kHz mono WAV -> MiMo ASR -> Hermes /v1/responses -> 手表 V1 固定 7 字段 JSON`。
- watch endpoint 已支持 mock ASR、MiMo ASR、request 幂等、cancel、115 秒服务器请求预算、输入校验、运行态指标、auth 诊断和公网私有路径门禁。
- `release_gate.ps1` 已作为服务器侧版本门禁，最近一次通过包含 server pytest `40 passed`、Hermes text smoke、mock voice、real MiMo ASR、cancel 与 invalid token 403。

## Decision Log

- V1 不使用 webhook，因为手表侧需要同步等待最终文本结果。
- ESP32-S3 只调用 watch endpoint，不直接调用 Hermes Dashboard、Hermes API Server 或 MiMo API。
- Hermes/MiMo/API key 只保留在服务器或仓库外 env；ESP32 固件只保存 watch endpoint 的 `device_id/device_token/base_url`。
- 公网第一版只允许代理 `/v1/watch/*`；Hermes `8642` 和 Dashboard `9119` 保持私有。
- `docs/context/CHANGELOG.md` 当前有无关脏改动；如需追加本任务 changelog，只暂存本任务新增行。

## 已验证

- `docker ps`：`hermes` 正常运行，`ai-memory-watch-voice-endpoint` 为 `healthy`。
- Hermes API Server：`GET /health`、`GET /v1/models`、`POST /v1/responses` 已用中文手表记忆请求验证成功。
- watch endpoint：mock smoke、cancel smoke、invalid-token 403、real MiMo ASR smoke 均已通过。
- server pytest 最近通过数：`40 passed`。
- `.\server\watch_voice_endpoint\release_gate.ps1 -SkipDocker` 最近通过；输出不包含真实 key/token、ASR 正文或回复正文。
- `uv run python scripts/context/validate_context.py --level standard --q "AI Memory Watch smoke temp audio cleanup" --brief` 已通过，context 检查 0 错误 0 警告。

## 当前风险

- ESP32-S3 当前无法联网，真机 `按住说话 -> Ogg Opus 上传 -> 120 秒内返回固定 7 字段` 仍未验证。
- 公网域名/Caddy 仍需真实部署后用 `-AssertPrivateNotExposed` 验证私有路径没有暴露。
- 真实用户语音样本、弱网重试、长耗时 agent 工具执行、token 首次配置体验仍待真机场景复测。
- 工作区有大量已有未提交 context 相关改动，不要回滚或混入 AI Memory Watch 提交。

## 下一步

- 继续完善服务器侧联调门禁与公网验收脚本，优先补能直接提升版本迭代稳定性的测试覆盖。
- 每次 server 改动后运行：`uv run --with-requirements server/watch_voice_endpoint/requirements.txt python -m pytest server/watch_voice_endpoint/tests -q`。
- 需要运行态验证时运行：`.\server\watch_voice_endpoint\release_gate.ps1 -SkipDocker`；涉及容器构建时运行 `.\server\watch_voice_endpoint\release_gate.ps1 -RebuildContainer`。
- 真机可联网后，先验证 NVS endpoint 配置、`/v1/watch/health`、按住说话上传、120 秒内返回 `done/timeout/error/canceled` 固定 7 字段，并确认日志不打印 token/key。

## 证据入口

- 相关计划：`docs/context/plans/active/2026-06-05-ai-memory-watch-hermes-page-plan.md`
- 服务器目录：`server/watch_voice_endpoint/`
- 机器可读契约：`server/watch_voice_endpoint/watch_contract.v1.json`
- 产品定位：`docs/context/knowledge/project/ai-memory-watch-product-positioning.md`
- 仓库画像：`docs/context/knowledge/project/project-profile.md`
