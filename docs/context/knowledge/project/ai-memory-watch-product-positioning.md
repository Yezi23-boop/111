---
id: ai-memory-watch-product-positioning
tags: project, product, ai-memory-watch, hermes, voice, watch, positioning
summary: 固定 ESP32-S3 手表接入 Hermes 个人 AI 大脑时的产品定位、核心闭环、V1 功能边界和端云分工。
last_reviewed: 2026-06-08
memory_type: project_knowledge
scope: repo
owners: docs/context/knowledge/project/ai-memory-watch-product-positioning.md
triggers: AI Memory Watch, Hermes, 产品定位, 随身记忆手表, 语音手表, 个人 AI 大脑, ESP32-S3 手表定位
evidence_level: design
status: active
---

# AI Memory Watch 产品定位

## 一句话定位

```text
AI Memory Watch
连接个人 AI 大脑的随身记忆手表。

它不试图替代手机，而是让你在最短时间内把想法、提醒和问题交给 Hermes，并在合适的时候把答案带回手腕。
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

AI Memory Watch 的核心价值是把个人 AI 从手机 App 里拿出来，变成一个随身、低摩擦、可主动反馈的入口。

V1 只围绕这个习惯闭环：

```text
想到事，抬腕说。
重要事，手表提醒。
需要回顾，问 Hermes。
```

核心场景：

1. 语音记忆：用户抬腕说一句，Hermes 记录、整理和归类。
2. 语音提醒：用户自然语言创建提醒，Hermes 到时推回手表。
3. 今日简报：Hermes 主动生成待办、提醒和重点记忆摘要。
4. 快捷问答：用户询问今天事项、刚才记忆或某个上下文问题。

## 端云分工

```text
Hermes
  -> 个人 AI 大脑：长期记忆、上下文、任务整理、提醒、工具调用。

服务器
  -> 语音桥：ASR、TTS、HTTPS/WebSocket、鉴权、日志与失败重试。

ESP32-S3 手表
  -> 随身终端：录音、按键/触摸、屏幕、震动、播放、轻量传感器和状态显示。
```

ESP32-S3 的消费级价值不是智能算力，而是：

- 低摩擦输入：抬腕按一下就能说，比掏手机快。
- 即时反馈：震动、屏幕和短语音让用户知道已记录、已提醒或已失败。
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
用户是否愿意每天自然地抬腕说一句“记一下”。
```

如果这个动作能形成习惯，产品成立。后续功能扩展都应优先增强“记录、整理、提醒、回顾”闭环，而不是把产品扩张成通用智能手表。
