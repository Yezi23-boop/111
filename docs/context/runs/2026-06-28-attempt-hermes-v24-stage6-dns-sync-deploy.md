---
id: attempt-hermes-v24-stage6-dns-sync-deploy
tags: context, runs, ai-memory-watch, hermes, v2.4, stage6, dns, sync, websocket, esp32s3
summary: 记录 AI Memory Watch / Hermes V2.4 阶段 6 中 Mihomo/Fake-IP DNS 修复、watch endpoint /sync 容器部署修正与 COM3 真机验证证据。
last_reviewed: 2026-06-28
memory_type: run
scope: project
owners: docs/context/runs
triggers: AI Memory Watch V2.4 Stage 6, ESP_ERR_HTTP_CONNECT, Mihomo Fake-IP, watch sync deploy, COM3 Hermes validation
evidence_level: observed
---

# AI Memory Watch / Hermes V2.4 Stage 6 DNS 与 /sync 部署验证

## 背景

V2.4 阶段 5 已完成代码侧瘦身，阶段 6 进入真机验收。早期日志中 ESP32 可进入 Wi-Fi ready，但访问 `watch.934000.xyz` 出现连接失败；Windows 上 DNS 查询也被 Mihomo Fake-IP 接管，返回 `198.18.x.x`，因此怀疑手表网络也拿到了不可达的 fake-ip DNS。

用户随后修复了该网络/DNS 问题。本 run log 记录修复后的证据，以及后续发现 watch endpoint 容器仍是旧镜像导致 `/v1/watch/sync` 404，并通过重建容器解决。

## 证据

- `board_logs/2026-06-28-19-27-07-hermes-v24-stage6-dns-fixed-verify.log`
  - 手表获得 IP：`192.168.103.11`。
  - 网络状态：`CONNECTING -> WIFI_READY -> SERVICE_READY`。
  - SNTP：`sntp sync ok source=SNTP`。
  - Hermes health：`watch endpoint health result: hermes_online=1 err=ESP_OK`。
  - inbox：`inbox: poll ok items=0 unread=0`。
  - 前台 WSS 真麦克风链路成功：`watch request result ... status=done action=conversation_reply error_code=none`。
  - `mw_upload` high-water 约 `3172` words。
  - 未见 Guru、panic、stack overflow、`Error parse url`。

- 部署检查：
  - 发现运行中的 watch endpoint 容器为旧镜像，本机和公网 `GET /v1/watch/sync` 曾返回 404。
  - 源码中已存在 `/v1/watch/sync` route，重建 watch endpoint 容器后，本机和公网 `/v1/watch/sync` 未授权请求返回 401，说明 route 已部署且鉴权生效。

- `board_logs/2026-06-28-19-30-47-hermes-v24-stage6-sync-deployed-verify.log`
  - 网络状态再次进入 `SERVICE_READY`，SNTP 同步成功。
  - Hermes health：`watch endpoint health result: hermes_online=1 err=ESP_OK`。
  - `/sync` 真机路径：`conversation: sync ok messages=0 session=none terminal=0`。
  - 前台 WSS 真麦克风链路再次成功：`watch request result ... status=done action=conversation_reply error_code=none`。
  - `mw_upload` high-water 约 `3172` words。
  - 未见 Guru、panic、stack overflow、`Error parse url`。

## 结论

- Mihomo/Fake-IP DNS 已不是当前阻塞点；如果后续同网络再次出现 `ESP_ERR_HTTP_CONNECT` 或公网域名连接异常，可以把 fake-ip DNS 当作复发线索，但不要继续按当前阻塞处理。
- `/v1/watch/sync` 已部署到本机与公网 watch endpoint，未授权返回 401 是预期行为；真机已能跑出 `conversation: sync ok`。
- 前台 Hermes 页面 WSS 真麦克风主链路在 DNS 修复和 `/sync` 容器重建后仍可用。

## 后台 /sync 脚本化验收

为尽量逼近“离页 pending 后台拉回回复”的数据面，在不依赖手表手动操作的前提下做了一次容器内注入测试：

- request_id：`codex-stage6-sync-bg-20260628`
- 写入位置：运行中 watch endpoint 容器的 `/data/session.db` 与 `/data/conversation.db`
- 写入内容：
  - session：`accepted -> asr_ready -> running -> done`
  - user message：`测试后台 sync 慢任务`
  - assistant message：`后台 sync 测试回复已到达`

验证结果：

```text
local /sync background + pending_request_id:
  session_state=done
  message_count=2
  roles=user,assistant
  last_text=后台 sync 测试回复已到达

public /sync background + pending_request_id:
  session_state=done
  message_count=2
  roles=user,assistant
  last_text=后台 sync 测试回复已到达

public /sync background + pending_request_id + after_message_id=<user message>:
  session_state=done
  message_count=1
  roles=assistant
  first_text=后台 sync 测试回复已到达
```

结论：server 数据面已满足离页 pending 所需的核心语义。ESP32 如果已经显示过 user message，再带 `after_message_id` 调用后台 `/sync`，只会拿到 assistant 增量，不会重复补用户消息。

验收后已清理运行容器中的测试数据：

```text
watch_conversation_deleted=2
watch_session_deleted=1
```

## 剩余验证

V2.4 阶段 6 还不能归档。剩余专项场景是：

1. 在 Hermes 页面发起一个慢任务。
2. 用户离开 Hermes 页面，确认前台 WS 关闭。
3. 后台通过 `/v1/watch/sync` 拉回 assistant reply。
4. 手表弹出“回复已到达”气泡。
5. 点气泡回 Hermes 页面后能看到连续对话。

该场景完成后，才能把阶段 6 勾选为完成并考虑归档 V2.4。
