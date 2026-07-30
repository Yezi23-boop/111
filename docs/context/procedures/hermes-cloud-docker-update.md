---
id: hermes-cloud-docker-update
tags: procedure, ai-memory-watch, hermes, docker, aliyun, image-mirror, rollback
summary: 阿里云 AI Memory Watch 云端 Hermes 镜像更新、健康验证与回退的固定操作流程。
last_reviewed: 2026-07-29
memory_type: procedural
scope: project
status: active
owners: server/watch_voice_endpoint/compose.cloud.yml, server/watch_voice_endpoint/deploy/Dockerfile.hermes.cloud
triggers: Hermes upgrade, docker compose build hermes, image mirror, Hermes update, cloud rollback
evidence_level: observed
---

# Hermes 云端 Docker 更新流程

## 适用边界

- 此流程仅用于阿里云上的 AI Memory Watch 云端 Hermes 容器。
- `server/watch_voice_endpoint/compose.cloud.yml` 管理 Hermes、watch endpoint 与 cloudflared；密钥、数据与 Tunnel token 均在宿主机私有目录，不进入仓库或命令输出。
- 公网仍只允许 Cloudflare 访问 `/v1/watch/*`；Hermes API `8642` 与 Dashboard `9119` 仅绑定宿主机回环地址。

## 更新契约

- Docker 容器内的 `hermes update` 不适用：官方 CLI 会拒绝在已发布镜像中更新，因为容器不是可拉取的源码工作树。
- Hermes 更新的正式方式是：重建自定义 `hermes-agent:cloud` 镜像，再 recreate `hermes` 容器。
- `deploy/Dockerfile.hermes.cloud` 的 `HERMES_BASE_IMAGE` 默认指向当前可达的镜像代理 `docker.1panel.live/nousresearch/hermes-agent:latest`，避免日常构建依赖阿里云直连 Docker Hub 或 Docker daemon 的全局 mirror。
- `latest` 会跟随上游发布；需要可控升级时，在构建时将 `HERMES_BASE_IMAGE` 覆盖为经过验证的精确 tag。不要用未验证的镜像源覆盖当前健康镜像。

## 标准更新

1. 先记录当前镜像为带日期的本地回滚标签，确认现有 `hermes` healthy。
2. 在云端部署目录执行：

   ```bash
   docker compose -f compose.cloud.yml build --pull hermes
   docker compose -f compose.cloud.yml up -d --no-deps --force-recreate hermes
   ```

3. 等待 healthcheck 通过，再检查：

   ```bash
   docker compose -f compose.cloud.yml ps hermes
   curl -fsS http://127.0.0.1:8642/health
   ```

4. 从仓库执行公网门禁，确认 watch endpoint 已恢复且私有路径未暴露：

   ```powershell
   .\server\watch_voice_endpoint\runtime_status.ps1 `
     -BaseUrl "https://watch.934000.xyz" `
     -SkipDocker -SkipHermesApi -SkipServiceHealth `
     -AssertPrivateNotExposed
   ```

5. 涉及 Hermes 大版本或 API Server 改动时，再运行 HTTP 和 WSS smoke；只在两者通过后才把升级视为完成。

## 回退与故障处理

- 镜像代理下载失败时，不要先修改 `/etc/docker/daemon.json`。全局 Docker daemon 重启会中断 Hermes、watch endpoint 与 cloudflared，风险高于更换单次构建的 `HERMES_BASE_IMAGE`。
- 重建后 health 或公网门禁失败时，使用步骤 1 的回滚标签重新构建/启动 Hermes，并保留失败日志到 `runs/`。
- Docker daemon 确需修改时，先验证 JSON 内容；daemon 重启后必须单独确认 cloudflared 是否重新注册 Tunnel。

## 证据来源

- [Hermes 0.19.0 云端 Docker 升级](/D:/esp32S3/111/docs/context/runs/2026-07-29-attempt-hermes-v019-docker-upgrade.md)
- `server/watch_voice_endpoint/deploy/Dockerfile.hermes.cloud`
- `server/watch_voice_endpoint/compose.cloud.yml`
