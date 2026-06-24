---
id: ai-memory-watch-product-positioning
tags: project, product, ai-memory-watch, hermes, voice, watch, positioning
summary: 固定 ESP32-S3 手表作为 Hermes 随身输入与交互工具时的产品定位、核心闭环、V1 功能边界和端云分工。
last_reviewed: 2026-06-21
memory_type: project_knowledge
scope: repo
owners: docs/context/knowledge/project/ai-memory-watch-product-positioning.md
triggers: AI Memory Watch, Hermes, 产品定位, Hermes 输入工具, Hermes 交互终端, 语音手表, ESP32-S3 手表定位
evidence_level: design
status: active
route_area: "AI Memory Watch positioning"
---

# AI Memory Watch 产品定位

## 一句话定位

```text
AI Memory Watch
连接 Hermes 的随身输入与交互工具。

它不试图替代手机，而是让你在最短时间内把想法、提醒、问题和任务交给 Hermes，并在合适的时候把 Hermes 的反馈带回手腕。
```

## 产品不是做什么

V1 不把 ESP32-S3 手表定义成低配 Apple Watch，也不把它定义成端侧大模型设备。

明确不做：

- 不做 App 商店或完整智能手表生态。
- 不以运动健康、支付、地图或手机通知聚合为主卖点。
- 不在 ESP32-S3 端维护长期记忆数据库。
- 不在 ESP32-S3 端跑大模型、复杂 agent 编排或完整 ASR/TTS 模型。
- 不把语音入口直接变成任意系统命令或危险工具权限。

## 产品要做什么

AI Memory Watch 的核心价值是把 Hermes 从手机 App 和网页里拿出来，变成一个随身、低摩擦、可交互、可回执的入口。

V1 只围绕这个习惯闭环：

```text
想到事，按住说。
重要事，回到手表。
需要回顾，问 Hermes。
```

核心场景：

1. 语音记忆：用户按住手表按钮说一句，Hermes 记录、整理和归类。
2. 语音提醒：用户自然语言创建提醒，Hermes 到时把重要事带回手表。
3. 任务交互：用户把查询、整理、检查或其他 agent 任务交给 Hermes，手表显示简短回执。
4. 主动信息：Hermes 主动生成待办、提醒、重点记忆摘要或任务结果，并回到手表收件箱。
5. 快捷问答：用户询问今天事项、刚才记忆或某个上下文问题。

## Hermes 收件箱与系统级通知定位

2026-06-25 路线更新：旧的“收件箱只在 Hermes 页面内、不做全局通知、不主动打断”的 V2.0 口径已被新计划取代。当前以 `docs/context/plans/active/2026-06-25-hermes-inbox-global-notification-plan.md` 为准。

新定位：

```text
Hermes 收件箱是系统级消息中心的第一个来源。

V1 只接 Hermes 主动下发提示；
后续可扩展到 weather / safety / system / reminders / app events。
```

固定产品边界：

- 消息中心允许多个来源，但 V1 只接 Hermes。
- V1 收件箱只读：不回复、不确认、不继续任务、不删除消息。
- 收件箱主要放 Hermes 主动下发的提示，不把每一条普通 Hermes 对话回复都塞进收件箱。
- 用户主动问 Hermes 后的普通回复留在 Hermes 对话页；如果用户离开页面后回复才到达，可以弹气泡，点击回到 Hermes 对话并滚到底部。
- Hermes 主动提示进入收件箱，并可触发全局气泡；气泡由系统级 notification controller 挂在 `lv_layer_top()`，不属于某个页面。
- 全局气泡默认可在普通页面弹出，但录音/编码、安全告警、OTA/配网关键流程、熄屏/低功耗时暂缓。
- V1 用 HTTP 轮询打通业务和 UI；后续再重构升级为 MQTT，解决延迟和功耗。

V1 的最小交互链路：

```text
Hermes / server 主动产生一条提示
  -> watch endpoint 写入 device inbox
  -> memory_watch_service 按预算轮询 inbox
  -> service 更新 inbox snapshot 和未读数
  -> notification controller 发现新消息并显示全局气泡
  -> 用户点气泡进入收件箱详情
```

后台 Hermes 回复链路单独处理：

```text
用户在 Hermes 页面发起请求
  -> 退出 Hermes 页面后请求继续等待
  -> 回复到达时写入 Hermes 对话记录
  -> 如果 Hermes 页面不在前台，弹“回复已到达”气泡
  -> 用户点气泡回到 Hermes 对话页并滚到底部
```

轮询节奏：

- 首次联网：立刻拉 inbox。
- 正常亮屏/活跃：1 分钟拉一次。
- 普通运行：5 分钟拉一次。
- 失败重试：1 分钟。
- 低功耗/熄屏：暂停或 30 分钟。
- 打开收件箱：立即拉一次。

固件 owner 边界：

- 收件箱数据 owner 是 `memory_watch_service`，不是 UI 页面私有状态。
- UI 只投递“打开收件箱、打开详情、返回、点击气泡”等用户意图，并读取 snapshot。
- HTTP 请求由 `memory_watch_voice_client` 的窄 client 执行；LVGL 页面不直接联网。
- 全局气泡由独立 notification controller 管理，避免 Hermes 页面、天气页、主表盘互相调用。

## 端云分工

```text
Hermes
  -> Agent 大脑：长期记忆、上下文、任务整理、提醒、工具调用和主动信息生成。

服务器
  -> 设备桥：ASR、HTTPS、鉴权、watch endpoint、收件箱存储、日志与失败重试。

ESP32-S3 手表
  -> 随身交互终端：录音、按键/触摸、屏幕、轻量传感器、状态显示和 Hermes 回执展示。
```

ESP32-S3 的消费级价值不是智能算力，而是：

- 低摩擦输入：按住手表就能说，比掏手机快。
- 即时回执：屏幕短文本让用户知道 Hermes 已记录、已处理、需要等待或失败。
- 随身上下文：电量、时间、运动、按钮事件和联网状态可以成为 Hermes 的判断依据。

## 当前固件边界

在当前仓库中，AI Memory Watch 不应引入大而全新总管家。后续实现应沿用现有 owner 合同：

```text
UI
  -> 只表达开始录音、取消、查看状态等用户意图。

memory_watch_service / memory_watch_voice_client
  -> `memory_watch_service` 维护页面命令、状态快照、endpoint 在线状态和请求 ID；`memory_watch_voice_client` 作为窄 HTTP client 上传语音、检查 health 和 cancel。

audio_codec
  -> 继续作为麦克风 input session 和喇叭 output session owner。

network_manager / network_service
  -> 继续负责联网语义、service ready 和云端可达状态。

power_policy
  -> 决定录音、上传、播报和待机是否被当前电源预算允许。
```

UI 高频路径只读状态快照，不直接做网络、ASR/TTS、音频 session 管理或长阻塞等待。

## 页面定位

AI Memory Watch 应作为独立功能页面推进，不复用现有“小智 / official_chat”聊天页。

原因：

- 现有 AI 页语义是前台聊天会话，进入页面后请求 `official_chat_service_enter_foreground()`，退出页面后请求 `official_chat_service_leave_foreground()`。
- AI Memory Watch 的核心语义是“记录、整理、提醒、回顾”，需要自己的页面状态和文案。
- Hermes 接入应先走窄 service 和服务器 voice bridge，不把 Hermes 协议硬塞进 `components/official_chat` 的 OTA、MCP、WebSocket/MQTT、wake word 和激活状态机里。

`components/official_chat` 后续只作为设计参考：复用它的 owner task、command queue、audio session、事件回调、文本快照和 shutdown quiet period 思路，而不是直接复用它的产品语义。

## V1 验证指标

定位是否成立，先看一个行为指标：

```text
用户是否愿意每天自然地按住手表说一句“记一下”。
```

如果这个动作能形成习惯，产品成立。后续功能扩展都应优先增强“记录、整理、提醒、回顾”闭环，而不是把产品扩张成通用智能手表。
