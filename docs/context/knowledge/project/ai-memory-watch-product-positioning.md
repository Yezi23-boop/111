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

## V2.0 Hermes 收件箱定位

V2.0 在 V1“按住说 -> Hermes 处理 -> 手表回执”的基础上，增加 Hermes 主动信息回到手表的能力。

Hermes 收件箱不是通知中心，也不参与与 Hermes 的对话。它只接收 Hermes 或服务器主动写给手表、适合用户稍后查看的短文本信息。

V2.0 固定以下产品边界：

- Hermes 主动下发的信息只进入收件箱，不震动、不亮屏、不主动打断用户。
- 收件箱入口只出现在 Hermes 页面内，不放到主菜单，不做全局通知中心。
- 收件箱只读：不回复、不确认、不继续任务、不删除消息。
- 收件箱不参与 Hermes 长期记忆；已读/未读只属于 watch endpoint 的收件箱状态，V2.0 不回写 Hermes。
- V2.0 先用脚本模拟 Hermes 主动写入，后续再接 Hermes agent 工具。

V2.0 的最小交互链路：

```text
Hermes / server 主动产生一条信息
  -> watch endpoint 内部接口写入收件箱
  -> 用户进入 Hermes 页面时拉取第一页
  -> Hermes 页面显示收件箱未读数
  -> 用户打开收件箱列表
  -> 点开消息后立即标记已读
```

V2.0 的收件箱数据规则：

- 收件箱由 watch endpoint/server 持久化，每个 device 保留最近 50 条。
- ESP32-S3 每次最多拉取 20 条，支持 `before` 分页继续拉取更早消息。
- 消息按 `created_at` 倒序显示，最新在上，不把未读强行置顶。
- `unread_count` 统计服务器保留的 50 条内全部未读消息。
- 消息字段只保留 `notification_id`、`created_at`、`read`、`text`。
- `text` 原文返回，不做服务器摘要或裁剪；列表页只显示预览，详情页显示完整原文并支持纵向滚动。
- 用户点开消息即标记已读，但消息不从列表移除，直到被最近 50 条滚动淘汰。

V2.0 的固件缓存规则：

- Hermes 收件箱是 `memory_watch_service` 的后台服务能力，不是 UI 页面私有状态；UI 只投递命令并读取 snapshot。
- V2.0 可以做无用户触发的后台轮询，由 `memory_watch_service` 按电源、网络和前台状态预算低频拉取收件箱未读状态或第一页消息；后台轮询只更新收件箱状态，不震动、不亮屏、不主动打断用户。
- 收件箱 items 缓存放 PSRAM，退出 Hermes 页面后由 `memory_watch_service` 释放。
- 语音录音/上传优先级高于收件箱加载；收件箱 late result 不能覆盖录音、上传或思考状态。

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
