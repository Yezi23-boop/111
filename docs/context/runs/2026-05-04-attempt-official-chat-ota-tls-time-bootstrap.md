---
id: attempt-2026-05-04-official-chat-ota-tls-time-bootstrap
tags: official-chat, tls, sntp, attempt-log
summary: official-chat-ota-tls-time-bootstrap；结果：success。
last_reviewed: 2026-05-04
memory_type: episodic
scope: task
owners: components/official_chat/ota.cc, main/services/official_chat_service.c, main/services/network_service.c, docs/context/knowledge/project/official-chat-ota-tls-time-bootstrap.md
triggers: official_chat TLS -0x2700 OTA HTTPS SNTP time bootstrap
evidence_level: observed
---

# Attempt Log: official-chat-ota-tls-time-bootstrap

## 背景

- 本次要验证什么：记录 official_chat 冷启动 HTTPS/TLS 失败的已定位根因和修复路径，避免后续误判为 Wi-Fi、DNS 或 CA 证书问题。
- 对应任务或计划：official_chat OTA TLS 首次授时修复
- 结果状态：success

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：未涉及或未记录
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- components/official_chat/ota.cc
- main/services/official_chat_service.c
- main/services/network_service.c
- docs/context/knowledge/project/official-chat-ota-tls-time-bootstrap.md
- 执行的命令或动作：
- 在 OTA HTTPS JSON 请求前检查系统时间是否进入 TLS 可用区间
- 时间无效时启动或复用 SNTP，并在有限窗口内等待首次授时
- 为 HTTP/TLS 失败补充 errno、TLS code、mbedtls code、TLS flags 和 UTC 快照诊断
- 已尝试但不应直接重复的路径：
- 不要在看到 ESP_ERR_HTTP_CONNECT 时直接重写联网链路
- 不要只依赖 OTA response 的 server_time 解决首次 HTTPS 前的时间死锁

## 观测

- 关键日志/证据：
- 典型失败日志为 mbedtls_ssl_handshake returned -0x2700 与 ESP_ERR_HTTP_CONNECT
- official-chat-ota-tls-time-bootstrap.md 记录 system time invalid before HTTPS request 与 SNTP bootstrap 判读
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：冷启动首轮 official_chat OTA HTTPS 需要先完成时间 bootstrap；否则 Wi-Fi/DHCP/DNS 正常也会在证书有效期校验失败。
- 仍然不能确认的事实：
- SNTP 服务器在弱网或受限网络下的超时策略仍需现场验证

## 未验证风险

- 下一轮仍需补证据的边界：
- 再次遇到 official_chat 激活 HTTPS 失败，先查当前 UTC 时间和 TLS diagnostics，再决定是否排查 CA/网络
