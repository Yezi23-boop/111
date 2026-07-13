---
id: attempt-hk-watch-relay
tags: context, runs, ai-memory-watch, hong-kong, relay, autossh, nginx, lxc, cloudflare, letsencrypt, acme
summary: 记录 256 MiB 香港 NAT LXC 到阿里云 watch endpoint 的低延迟中转、DNS-only 域名、Let's Encrypt 证书与公网 HTTP/WebSocket 闭环证据。
last_reviewed: 2026-07-13
memory_type: run
scope: project
owners: server/watch_voice_endpoint/deploy/hk-relay, docs/context/runs
triggers: 香港中转, WireGuard, no dev net tun, autossh latency, nginx upstream keepalive, watch-hk, DNS-01, acme.sh
evidence_level: observed
---

# AI Memory Watch 香港中转验证

## 环境

- Debian 12 LXC，2 vCPU、256 MiB RAM、2 GiB 磁盘。
- 公网 NAT IP 的 SSH 映射端口转到容器内部 `22`。
- 空载内存约 18 MiB；部署 autossh 与 nginx-light 后约 27 MiB，available 约 228 MiB。
- LXC 不允许 `swapon`；未生效的 swapfile 已删除。

## 路线证据

- Windows 到香港公网 SSH TCP：min `37.0ms`，median `40.3ms`，avg `41.0ms`，max `49.8ms`。
- 香港到阿里云公网：10 次 ping 0% 丢包，RTT min/avg/max=`8.958/10.569/16.418ms`。
- LXC 缺少 `/dev/net/tun`，且 capability 不含 `CAP_NET_ADMIN`，因此不能在该套餐内运行 WireGuard 或 Tailscale。不要重复安装后再验证。

## 当前实现

- 阿里云创建 `watchrelay` 锁定密码用户，公钥使用 `restrict,port-forwarding,permitopen="127.0.0.1:8787"`，只能建立到 watch endpoint 的本地转发。
- 香港 `watch-relay-autossh.service` 常驻转发：`127.0.0.1:18787 -> 阿里云 127.0.0.1:8787`。
- autossh + ssh 常驻 RSS 约 10 MiB；服务已设置开机启动和断线重连。
- Nginx 监听香港容器 `20340/TCP`，经 NAT 暴露为 `156.233.234.206:20340`；仅代理 `/v1/watch/*`，其他路径返回 404。
- Nginx upstream 使用 HTTP/1.1 keepalive 连接池；WebSocket 使用 Upgrade 头并保持长连接。

## 延迟与门禁

- 直接为每次 HTTP 请求新开 SSH forwarding channel 时，香港到阿里云约 `0.48-0.54s`，不适合作为低延迟入口。
- 经 Nginx upstream keepalive 后，稳定请求约 `0.168s`；首次预热请求约 `0.33s`。
- 经本地临时 SSH 转发验证香港 Nginx：watch health `ok`、Hermes `online`；`/health`、`/v1/models`、`/v1/responses` 均为 404。

## 当前切换边界

- 正式入口 `https://watch.934000.xyz` 仍由阿里云 cloudflared 提供，不在本轮切换，保证当前手表服务不断线。
- 香港测试入口为 `https://watch-hk.934000.xyz:20340`；后续只有在真机验证通过后，才考虑修改手表 `base_url`。
- 香港入口依赖 NAT 高端口，URL 必须保留 `:20340`；除非供应商后续提供公网 `443` 映射，否则不能省略端口。

## NAT 20340 直连 A/B

NAT 面板确认已有 `156.233.234.206:20340 -> 容器 20340/TCP`。香港 Nginx 暂时使用 7 天自签名证书监听 `20340`，只用于 `curl --resolve -k` 线路测试，尚未切换手表或生产 DNS。

- 10 次带 device token 的 `/v1/watch/health`：min `0.303s`，median `0.316s`，avg `0.357s`，max `0.562s`。
- 前两次约 `0.50-0.56s`，上游预热后稳定约 `0.30-0.32s`。
- 同路径 `/health` 返回 404，公网私有路径边界保持。
- 对照 Cloudflare HTTP/2 中位 `0.765s`，香港直连中位延迟降低约 59%。

Windows 当前启用 Mihomo TUN 时，SSH 管理连接会从代理出口发起并在认证后中断；使用 OpenSSH `-b 172.23.244.193` 显式绑定物理网卡可恢复。该现象只影响本机管理 SSH，不影响手表生产域名。

## DNS-only 与可信 TLS 闭环

- Cloudflare 新增 DNS-only A 记录：`watch-hk.934000.xyz -> 156.233.234.206`；未修改原 `watch.934000.xyz` Tunnel 记录。
- Cloudflare API Token 仅授权 `934000.xyz` 的 `DNS:Edit` 与 `Zone:Read`。Token 只保存在香港机 root-only 文件和 acme.sh 配置中，权限均为 `0600`，未写入仓库或日志。
- 香港机安装 acme.sh `3.1.4` 与 Debian cron；Let's Encrypt DNS-01 已成功签发 ECC 证书，issuer 为 `YE2`，有效期至 `2026-10-11`。
- acme.sh 已安装每 6 小时执行一次的续期检查 cron job，只有进入续期窗口才重新签发，成功后执行 `systemctl reload nginx`；原 7 天自签名证书保留为带 UTC 时间戳的回退副本。
- `nginx`、`cron`、`watch-relay-autossh` 均为 `active`。安装 cron 后内存约 31 MiB，available 约 224 MiB，仍满足 256 MiB 中转机边界。

公网验证结果：

- `runtime_status.ps1 -AssertPrivateNotExposed` 通过：授权 watch health 为 `ok`、Hermes 为 `online`；`/health`、`/v1/models`、`/v1/responses` 均为 404。
- multipart mock Ogg smoke 通过，返回 `done`、固定 7 字段且无空 ASR/reply；无效 device token 返回 403。
- text command smoke 通过；WebSocket smoke 通过，收到 `asr_result -> task_started -> conversation_message(done)`。
- 受信任 HTTPS 下 7 次 watch health：min `0.351s`、median `0.382s`、avg `0.462s`、max `0.661s`。对照 Cloudflare HTTP/2 中位 `0.765s`，香港入口本轮中位延迟降低约 50%。
