---
id: attempt-hermes-v019-docker-upgrade
tags: context, runs, ai-memory-watch, hermes, docker, aliyun, mirror, v0.19.0, cloudflare
summary: 记录 Hermes 0.19.0 云端升级中 Docker Hub 不可达、镜像代理选择、Docker daemon 恢复与公网 watch 回归证据。
last_reviewed: 2026-07-29
memory_type: run
scope: project
owners: server/watch_voice_endpoint/compose.cloud.yml, server/watch_voice_endpoint/deploy/Dockerfile.hermes.cloud, docs/context/runs
triggers: Hermes v0.19.0 upgrade, Docker Hub timeout, registry mirror, unexpected EOF, watch endpoint offline
evidence_level: observed
---

# Hermes 0.19.0 云端 Docker 升级

## 目标与结论

- 官方 release `v2026.7.20` 对应 Hermes `0.19.0`；官方 Docker Hub 同时发布精确 tag，镜像 digest 与 release tag 一致。
- 阿里云最终已运行 Hermes `0.19.0`，容器 healthy；watch endpoint、cloudflared 均恢复运行。
- 公网 runtime gate、HTTP mock Ogg smoke 与 WSS smoke 均通过，watch health 为 `online`，私有 Hermes/internal 路径继续不对公网暴露。

## 失败路线与证据

- Docker Hub 直连在阿里云 TCP 443 超时。
- 专属 ACR Docker mirror 配置生效后，对 `nousresearch/hermes-agent` 返回镜像不存在，不能作为本仓库的升级来源。
- 第一个公共代理可读取 manifest，但完整层下载以 `unexpected EOF` 失败；不应把 manifest 可读误判为镜像可稳定拉取。
- 第二个公共代理成功拉取官方精确 tag；其 digest 与 Docker Hub tag 列表一致。
- 云端 `deploy/Dockerfile.hermes.cloud` 的默认 `HERMES_BASE_IMAGE` 已改为该可达代理的 `latest` 路径；日常 `docker compose build --pull hermes` 不再依赖 Docker Hub 或全局 daemon mirror。构建参数仍可显式覆盖到固定 tag 或其他来源。

## Docker 恢复事项

- 初次写入 `/etc/docker/daemon.json` 时，PowerShell 转义将反斜杠写入 JSON key，dockerd 报 `invalid character '\\' looking for beginning of object key string` 并拒绝启动。
- 修复为有效 JSON 后 Docker 服务恢复；Hermes 与 watch endpoint 自动恢复，cloudflared 曾以 exit 0 停止，需显式启动并确认 Tunnel 已注册。
- 后续修改 daemon JSON 必须先验证 JSON 字节内容，再重启 Docker；Docker 重启会中断所有容器，不应与镜像下载并行执行。

## 升级验证

- `GET http://127.0.0.1:8642/health`：Hermes `0.19.0`。
- 公网 runtime gate：watch health `online`，`/health`、`/v1/models`、`/v1/responses`、`/internal/watch/inbox` 均未暴露。
- 公网 HTTP mock Ogg：`done`，固定 7 字段完整。
- 公网 WSS：`request_accepted -> asr_result -> task_started -> conversation_message`，最终 reply `done`。

## 后续

- 保留 `hermes-agent:cloud-pre-v0.19.0-20260729` 作为升级前回滚镜像。
- Hermes 0.19.0 已含 cold-start 优化；先在新版上量测真实 `/v1/runs` 与 WSS 分段延迟，再决定是否继续 V2.6 warm Agent 补丁。
