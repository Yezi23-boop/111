---
id: hermes-multi-agent-architecture
tags: project, architecture, hermes, multi-agent, ai-memory-watch, watch-notify, v2.1, draft
summary: AI Memory Watch / Hermes V2.1 框架需求草案：先做单用户、单手表、单入口的 Hermes Brain + watch_notify 工具；后续 V3 也继续保持单入口/单手表，不做多入口和多设备，只演进下发 agent、任务执行、conversation store 与 MQTT。
last_reviewed: 2026-06-26
memory_type: project_knowledge
scope: repo
owners: docs/context/knowledge/project/hermes-multi-agent-architecture.md
triggers: Hermes multi-agent, Hermes 多 Agent, watch_notify, 下发 agent, 通讯 agent, AI Memory Watch V2.1, AI Memory Watch V3
evidence_level: design
status: draft
route_area: "Hermes watch_notify and multi-agent architecture"
---

# Hermes watch_notify 与多 Agent 架构草案

> 日期：2026-06-26
> 状态：V2.1 需求框架草案

## 0. 当前结论

当前不要直接搭完整多 agent 系统。先做最短、最有价值的闭环：

```text
V2.1 = 让 Hermes 能主动通知手表。
```

V2 已完成范围以 `docs/context/plans/completed/2026-06-25-hermes-inbox-global-notification-plan.md` 为准：

```text
V2 = Hermes 主动提示回到手表：server inbox + ESP32 收件箱 + 全局气泡通知。
```

V2.1 的新增目标不是重写 ESP32 固件架构，也不是一次性做微信、多入口、多设备、TTS、完整执行 agent，而是让 Hermes 在合适条件下调用一个稳定工具：

```text
Hermes Brain -> watch_notify -> watch endpoint inbox -> ESP32 收件箱/气泡
```

## 1. 阶段边界

V2.1 固定为单用户、单手表、单入口增强：

```text
V2.1 不解决“从哪里来”和“发给谁”的复杂路由，
只解决“Hermes 如何把该通知手表的内容稳定送到 watch-001”。
```

### V2.1 必须做

- 增加 Hermes 可调用的 `watch_notify` 能力。
- `watch_notify` 只写入手表的 proactive inbox。
- 通知目标先固定为 `watch-001`。
- Hermes 生成稳定 `notification_id`。
- Hermes 负责把消息压缩成适合手表看的短文本。
- watch endpoint 只校验字段、鉴权、去重、持久化，不总结、不截断、不理解语义。
- 触发规则写进 Hermes instructions / tool-use policy，而不是写成 Python 关键词匹配。

### V2.1 不做

- 不做完整多 agent 编排。
- 不做微信 agent。
- 不做网页、终端、微信等多入口接入或入口路由。
- 不做多设备路由。
- 不做 TTS 语音下发。
- 不做手机通知聚合。
- 不做收件箱内回复、确认、删除、继续任务按钮。
- 不让 ESP32 端新增多个 Hermes 后台服务。
- 不把其他入口的普通聊天回复顺手通知手表。
- 不把“其他入口明确要求通知手表”作为 V2.1 或 V3 目标；多入口不是当前产品路线。
- 不把普通 conversation reply 混入 proactive inbox。

### V2.2 / V3 再做

- conversation store：保证手表对话回复在离页、断网或后台完成后仍可找回。
- Watch Notifier / Delivery Agent：从工具升级为真正的下发 agent。
- Task Executor Agent：负责长任务、脚本、API、定时任务。
- 单手表下的更稳通知策略、后台任务结果回传和低功耗同步。
- MQTT：作为低功耗同步唤醒；HTTP 完整快照继续用于首次上线、断网恢复和漏消息补偿。

## 2. 核心消息通道

V2.1 必须把两个通道分清楚：

```text
conversation_reply
proactive_inbox
```

### 2.1 conversation_reply

含义：用户和 Hermes 的普通对话回复。

规则：

- 哪里发起的对话，普通回复就回到哪里。
- 手表发起的普通聊天，只返回手表 Hermes 页面。
- 网页、终端、微信等其他入口发起的普通聊天，只返回原入口。
- conversation reply 不进入手表 inbox。
- conversation reply 不触发 `watch_notify`。

示例：

```text
手表用户：今天要带伞吗？
Hermes：下午可能下雨，建议带伞。
```

这条回复应该显示在 Hermes 页面连续对话里，而不是进入收件箱。

当前风险：

- 如果手表用户离开 Hermes 页面，而普通回复稍后才到达，当前若没有 conversation store，存在丢失或用户看不到的风险。
- 这个问题不通过把普通回复塞进 inbox 解决；正确方向是 V2.2 增加 conversation store / conversation reply channel。

### 2.2 proactive_inbox

含义：Hermes 主动提醒、任务结果通知，以及单手表路线中明确需要回到手表的主动消息。

规则：

- 写入 watch endpoint inbox。
- 触发 ESP32 收件箱未读数和全局气泡。
- 不参与 Hermes 对话流。
- 用户点开详情后标记已读。

示例：

```text
10 点开会，记得带充电器。
日志分析完成，主要问题是待机耗电偏高。
```

## 3. 通知触发规则

V2.1 的核心原则：

```text
不是所有任务完成都通知手表。
```

允许调用 `watch_notify` 的情况：

1. 请求来源是手表，并且结果属于后台完成、长任务完成、稍后触发或用户离开页面后仍需要回到手表的提醒。
2. Hermes 内部定时提醒、主动提示或任务创建时明确绑定 `notify_watch=true`，且目标设备固定为 `watch-001`。

禁止调用 `watch_notify` 的情况：

1. 普通聊天回复。
2. 用户已经在原入口前台看到的同步回复。
3. 其他入口发起的普通对话回复。
4. 内部工具日志、每个小步骤的进度、低价值状态。
5. 网页、终端、微信等其他入口任务；V2.1/V3 都不规划多入口到手表的通知路由。

推荐写入 Hermes instructions 的规则：

```text
仅在需要主动通知手表时调用 watch_notify。
普通回复必须回到原会话来源；不要把其他入口的普通回复发送到手表。
手表发起的普通回复回到手表 Hermes 页面；只有手表相关的主动提醒、任务完成通知或明确 notify_watch=true 的 Hermes 内部事件写入 inbox。
不要规划多入口路由；不要因为网页、终端或微信里的普通请求而通知手表。
```

## 4. ESP32 端 owner 原则

ESP32-S3 RAM 有限，不能因为产品上有两个通道就在固件里拆多个长期服务。

固定原则：

```text
通道分开，服务不分开。
```

ESP32 端只保留一个 Hermes owner：

```text
memory_watch_service
```

它统一负责：

- 按住说话。
- 上传语音。
- 接收当前同步回复。
- 拉取 inbox。
- 维护 inbox 小缓存、未读数和待同步已读。
- 向 UI 暴露只读 snapshot。
- 向 notification controller 提供新消息信号。

`watch_notification_center` / controller 只能是薄 UI 浮层：

- 只负责显示气泡、点击、右滑清除和跳转。
- 不联网。
- 不保存 inbox 数据。
- 不调用 Hermes。
- 不拥有业务状态。

禁止在 ESP32 端新增：

```text
conversation_service
inbox_service
reply_service
task_service
reminder_service
```

这些语义可存在于 Hermes/server 协议层，但固件 owner 不拆。

## 5. watch_notify 工具契约

### 5.1 工具职责

`watch_notify` 只负责写 proactive inbox：

```text
watch_notify = 通知手表，不是回复聊天。
```

它不负责：

- 写 conversation reply。
- 修改 Hermes 对话历史。
- 判断 ESP32 当前页面。
- 生成长文本总结。
- 调度提醒。
- 管理任务状态。

### 5.2 目标设备

V2.1 固定目标设备：

```text
watch-001
```

暂不做多设备路由、用户设备表、默认设备选择、在线状态选择。

同时暂不做多入口来源识别；工具配置层默认就是给 `watch-001` 写入通知。

### 5.3 调用地址

ESP32 走公网：

```text
https://watch.934000.xyz/v1/watch/*
```

Hermes / watch_notify 走服务器内部地址，不绕 Cloudflare：

```text
http://host.docker.internal:8787/v1/watch/inbox
```

未来如果 Hermes 与 watch endpoint 合并进同一进程，可从 HTTP 调用替换为内部函数或 repository 调用，但手表 API 不变。

### 5.4 鉴权

V2.1 继续复用现有 watch device token。

Hermes 调用时携带：

```http
Authorization: Bearer <watch-001 device token>
```

本仓库文档、日志、测试输出中不得记录真实 token/key。

V3 可再升级为：

```text
内部 WATCH_NOTIFY_TOKEN + device_id 权限映射
```

### 5.5 notification_id

`notification_id` 由 Hermes / 调用方生成。

原因：

```text
谁知道“这是同一个任务”，谁就生成 ID。
```

同一个任务、同一次提醒、同一个完成结果必须复用同一个 `notification_id`，以便 watch endpoint 按：

```text
device_id + notification_id
```

幂等去重。

如果 Hermes 写错内容且消息尚未成功落库，可缩短内容后复用同一个 `notification_id` 重试。若已成功落库且需要修正内容，必须生成新的 `notification_id`，不得覆盖旧消息。

### 5.6 文本生成责任

Hermes 负责生成适合手表显示的短文本。

watch endpoint 只做：

- 字段必填校验。
- UTF-8 字节长度校验。
- `kind` 枚举校验。
- 鉴权。
- 幂等去重。
- SQLite 持久化。

watch endpoint 不做：

- 总结。
- 截断。
- 改写。
- 语义判断。

### 5.7 kind

V2.1 允许 Hermes 指定 `kind`。

允许值：

```text
info
reminder
warning
```

server 只接受这三个枚举；未知类型返回 `422`。

ESP32 V2.1 先只保存和显示 `kind`，不根据 `kind` 改图标、颜色、震动、优先级或抢占策略。未来可以基于该字段扩展体验。

### 5.8 请求示例

```json
{
  "notification_id": "task-log-analysis-20260626-done",
  "kind": "info",
  "title": "日志分析完成",
  "preview": "主要问题是待机耗电偏高",
  "body": "日志分析完成，主要问题是待机耗电偏高。"
}
```

实际 HTTP：

```http
POST http://host.docker.internal:8787/v1/watch/inbox?device_id=watch-001
Authorization: Bearer <watch-001 device token>
Content-Type: application/json
```

## 6. 推荐演进架构

### V2.1：工具优先

```text
Hermes Brain
  -> watch_notify tool
      -> watch endpoint inbox
          -> ESP32 memory_watch_service
              -> 收件箱 + 全局气泡
```

这一阶段不必真正拆多个 agent。关键是让 Hermes 能按规则主动通知手表。

### V2.2：补 conversation store

```text
Hermes conversation reply
  -> conversation store
      -> ESP32 进入 Hermes 页面时拉取
      -> 后台回复可弹“回复已到达”
```

这解决“手表发起的普通回复在离页/断网后可能看不到”的问题，但不污染 inbox。

### V3：单手表多 agent / 低功耗演进

```text
Hermes Brain
  -> Task Executor Agent
  -> Delivery / Watch Notifier Agent
  -> watch_notify / conversation store / MQTT sync
```

V3 再处理：

- 单手表下的 Task Executor Agent。
- 单手表下的 Delivery / Watch Notifier Agent。
- conversation store 与后台回复找回。
- 下发 agent 的多格式输出。
- 可选 TTS。
- MQTT 低功耗同步。
- 内部 notify token 与更细权限边界。

## 7. 命名建议

第一阶段不要把所有东西都叫 agent。

推荐命名：

- `Hermes Brain`：主脑，负责语义、记忆、上下文和工具选择。
- `watch_notify tool`：V2.1 立刻要做的工具。
- `Task Executor Agent`：V3 后续长任务执行者。
- `Delivery Agent` / `Watch Notifier Agent`：V3 后续多格式下发者。
- `Watch Adapter`：只做手表 I/O 格式转换，不推理。

不推荐把 `watch endpoint` 叫手表 agent。它当前更准确的职责是：

```text
设备桥 / Watch Endpoint / Adapter
```

它负责 ASR、鉴权、Hermes API 调用、inbox 存储和 JSON 适配，不负责判断任务语义。

## 8. 待确认问题

以下问题不要阻塞 V2.1 `watch_notify`：

- 手表离开 Hermes 页面后的普通 conversation reply 是否必须持久化。
- conversation store 存在哪里：watch endpoint SQLite、Hermes memory，还是独立会话表。
- 后台 conversation reply 的气泡文案、点击目标和与 proactive inbox 气泡的排队规则。
- Hermes 的定时任务能力是否已经有足够证据；若无，先按“计划使用/待验证”记录。
- 如果未来路线变化重新引入多设备或多入口，需要另起计划，不从当前 V3 默认继承。
- V3 是否引入独立 `WATCH_NOTIFY_TOKEN`。

## 9. 当前定稿口径

```text
V2.1 先做 Hermes Brain + watch_notify 工具。
watch_notify 只写 proactive inbox。
普通回复按会话来源归还。
V2.1/V3 都不做多设备、多入口；默认只服务 watch-001。
手表端通道分开，但只保留 memory_watch_service 一个 Hermes owner。
notification_id、短文本、kind 由 Hermes 负责。
watch endpoint 只负责校验、鉴权、去重和持久化。
单手表多 agent 与低功耗同步放到 V3。
```
