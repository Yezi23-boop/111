---
id: attempt-2026-08-05-hermes-dashboard-data-permission-fix
tags: hermes, dashboard, permission, production, agent-run
summary: Hermes Dashboard 固定英文异常的持久数据权限修复；结果：success。
last_reviewed: 2026-08-05
memory_type: episodic
scope: task
status: completed
result: verified
owners: server/deploy/1panel/hermes-agent/docker-compose.yml, docs/context/plans/active/2026-07-14-ai-memory-watch-hermes-v2.5-conversation-reliability-plan.md
triggers: Hermes unexpected error, PermissionError /opt/data/.env, Hermes Dashboard agent error
evidence_level: observed
record_reasons: error-signature, evidence, repeat-risk
force_reason:
---

# Attempt Log: Hermes Dashboard 持久数据权限修复

## 背景

- 症状：Dashboard 显示 `Sorry, I encountered an unexpected error`，而 Hermes 仍显示在线。
- 影响范围：Dashboard 和 watch endpoint 共用的 Hermes agent run；不是 ESP32、WSS、ASR 或模型连接错误。

## 观测

- Hermes gateway 与 Dashboard 实际以 UID/GID `10000:10000` 运行。
- `/opt/ai-memory-watch/hermes-data/.env` 与 `auth.json` 曾为 `root:root 0600`；日志在 `dotenv.load_dotenv()` 报 `PermissionError: [Errno 13] Permission denied: '/opt/data/.env'`。
- 容器 health 与公网 watch health 均正常，说明 health endpoint 不能代替一次真实 agent run 验证。

## 最小修复与验证

- 仅将上述两文件的属主改为 `10000:10000`，模式保持 `0600`；没有读取、输出或修改任何 secret 内容，也没有重启健康容器。
- 容器内 UID `10000` 随后可读取两个文件。
- 使用新的无工具文本诊断会话调用私有 `/v1/responses`，得到 `HTTP 200`、`status=completed`、非空回复；修复后最近日志未新增 `PermissionError` 或 `Agent error`。

## 后续边界

- 以后通过 root/sudo 更新 Hermes 数据目录内 `.env` 或 `auth.json` 后，必须恢复 `10000:10000 0600`。
- 不要仅依据 `/health` 判断 Dashboard 或 watch agent 可用；权限或 provider 配置变化后补一次无副作用的真实 response 探针。
