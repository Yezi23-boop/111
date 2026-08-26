---
id: attempt-hermes-aliyun-cloud-deploy
tags: context, runs, ai-memory-watch, hermes, aliyun, docker, cloudflare, mem0
summary: 记录 AI Memory Watch 的 Hermes、watch endpoint 与 cloudflared 迁移到阿里云 2C2G 主机时的镜像拉取、Mem0 懒安装超时和公网门禁证据。
last_reviewed: 2026-07-13
memory_type: run
scope: project
owners: server/watch_voice_endpoint, docs/context/runs
triggers: Hermes 阿里云部署, Docker Hub timeout, mem0 lazy install, server_timeout, cloudflared token permission
evidence_level: observed
---

# AI Memory Watch / Hermes 阿里云部署验证

## 目标与边界

- 在阿里云 Ubuntu 24.04、2 vCPU / 2 GiB 主机上保留一套独立的 Hermes、watch endpoint 和 cloudflared。
- Windows 本地数据与 Compose 保留，不因云端迁移删除或覆盖。
- Hermes `8642`、Dashboard `9119`、watch endpoint `8787` 只绑定服务器 `127.0.0.1`；公网继续只允许 Cloudflare Tunnel 的 `/v1/watch/*` ingress。
- 密钥只存在 `/opt/ai-memory-watch/secrets`，未写入仓库或验证输出。

## 部署结果

- Docker `29.1.3` 与 Compose `2.40.3` 已安装并设置开机启动。
- 主机增加 2 GiB swap，降低 2 GiB 内存规格下镜像构建和 Hermes 峰值 OOM 风险。
- Hermes 数据目录从 Windows 迁移到 `/opt/ai-memory-watch/hermes-data`；watch SQLite 持久化到 `/opt/ai-memory-watch/watch-data`。
- 云端 Compose 管理：
  - `hermes-agent:cloud`
  - `ai-memory-watch-voice-endpoint:cloud`
  - `cloudflare/cloudflared:latest`
- cloudflared 使用 token file；宿主机文件权限为容器 UID `65532:65532`、mode `400`。

## 失败路线与修复

### Docker Hub 拉取超时

直接拉取 `nousresearch/hermes-agent:latest` 时，阿里云到 `registry-1.docker.io:443` 超时。`docker.1ms.run` 的 Hermes multi-arch manifest 缓存不完整，拉取层时报 `manifest ... not found`。最终通过 DaoCloud 国内代理拉取并重新标记为官方镜像名。

阿里云官方文档同时说明 ACR 旧镜像加速已停止同步最新镜像，`latest` 可能滞后。长期生产路线应使用账号自己的 ACR 制品订阅或私有仓库，而不是把公共镜像加速器当唯一来源。

### watch endpoint 构建慢

`python:3.12-slim` 当前为 Debian 13 `trixie`，默认 `deb.debian.org` 下载 `ffmpeg` 较慢。Dockerfile 在 `apt-get` 前把 `debian.sources` 域名替换为 `mirrors.aliyun.com`，云端镜像随后构建成功。

### Hermes 请求超过 115 秒

初次 `/v1/responses` 与 watch smoke 超时，但证据显示 MiMo `/models` 0.16 秒、最小 chat completion 约 1.64 秒返回。Hermes 日志显示每次任务都因 `MEM0_API_KEY` 启用 Mem0，并尝试懒安装 `mem0ai==2.0.10`；默认 PyPI 下载等待约 3 分钟后报 `No module named 'mem0'`，真正的 MiMo agent 调用仅需约 3 到 8 秒。

修复为通过阿里 PyPI 源把 `mem0ai==2.0.10` 安装到 `/opt/hermes/.venv`，并新增 `deploy/Dockerfile.hermes.cloud` 使该云端镜像可重建。修复后 `mem0_import=ok`，Hermes 文本 smoke 在超时内完成。

### cloudflared token 权限

token file 初始为 `root:root 600`，而 cloudflared 镜像默认 UID/GID 为 `65532:65532`，connector 因 `permission denied` 重启。将该文件改为 `65532:65532 400` 后，Tunnel 建立 4 条 HTTP/2 连接并收到既有 ingress 配置。

## 验证证据

- Hermes：`/health` 返回 `ok`，版本 `0.18.2`；`/v1/models` 与中文 `/v1/responses` smoke 通过。
- watch endpoint：容器 `healthy`，`asr_provider=mimo`，Hermes 状态 `online`。
- 公网 runtime gate：`https://watch.934000.xyz/v1/watch/health` 可用。
- 私有路径：公网 `/health`、`/v1/models`、`/v1/responses` 均返回 404。
- 公网 smoke：模拟 Ogg 上传与文本命令均为 `done`，无效 device token 返回 403，voice response 为固定 7 字段。
- 观测内存：Hermes 约 316-462 MiB，watch endpoint 约 38-45 MiB；模型由 MiMo API 执行，本机不运行 LLM。

## 后续

- 在阿里云 ACR 创建私有仓库后，把 `hermes-agent:cloud` 与 `ai-memory-watch-voice-endpoint:cloud` 推入 ACR，并改为 digest 或不可变版本标签。
- 真机继续使用 `https://watch.934000.xyz`，无需把阿里云公网 IP 或 Hermes API key 写入固件。

## Cloudflare 协议 A/B

阿里云 connector 的 4 条连接由 Cloudflare Anycast 自动选择到洛杉矶 `lax01/lax05/lax07/lax10`。对公网未授权 watch health 做同口径 10 次测量：

- HTTP/2：min `0.738s`，median `0.765s`，avg `1.103s`，max `2.105s`。
- QUIC：min `0.977s`，median `1.277s`，avg `1.255s`，max `1.666s`。

QUIC 虽降低本轮最差值，但中位数增加约 67%，且没有改变 LAX POP，因此已回退并继续固定 HTTP/2。不要再把 `--protocol quic` 当作当前线路的延迟优化；若要显著降低公网 RTT，需要改变 connector 所在地域、使用更合适的境外中转，或采用 Cloudflare 中国网络等不同网络产品，而不是继续调 Tunnel transport 参数。
