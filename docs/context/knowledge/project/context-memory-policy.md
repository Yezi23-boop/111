---
id: context-memory-policy
tags: context, memory, policy, planning, trial-error, knowledge, framework
summary: 定义当前上下文系统的长期记忆边界：优先保存用户规划、试错总结、项目知识、项目框架、稳定约束和偏好，避免保存无复用价值的小事。
last_reviewed: 2026-06-07
memory_type: framework
scope: repo
owners: docs/context, scripts/context/log_attempt.py, scripts/context/garden.py
triggers: memory-policy, context-memory, project-plan, trial-error, framework, constraints, stable-preferences, 记忆政策, 上下文, 试错, 规划, 项目知识
evidence_level: design
status: active
---

# Agent Context Memory Policy

## 目标

构建低 token 的 agent 上下文管理系统，让 agent 能长期理解用户规划、试错经验、项目知识和项目框架。

上下文不是完整聊天记录，也不是命令流水账；它应该是项目操作系统、决策日志和经验库。

## 优先保存

- Project Plan：用户的项目规划、阶段目标、路线图和下一步重点。
- Decision Log：重要决策、取舍原因、不可逆边界和替代方案。
- Trial-and-Error：有复用价值的失败路径、试错总结、不要重复的动作和关键证据。
- Error Signature：大问题错误的原始报错、触发条件、已证伪路径、当前定位和下一步边界。
- Route Choice：曾经尝试过、放弃或选定的路线，以及取舍原因和证据。
- Project Knowledge：当前真实 owner、模块边界、硬件/协议/构建事实和已验证项目知识。
- Framework：用户的方法论、判断框架、分层原则和架构边界。
- Constraints：长期约束、安全红线、硬件限制、工具链约束和验证要求。
- Stable Preferences：稳定偏好，例如沟通语言、验证方式、代码风格和抽象偏好。

## 避免保存

- 临时闲聊、临时情绪和没有工程复用价值的聊天片段。
- 一次性小任务，例如只改一个错别字、只跑一次常规检查、没有新结论的格式调整。
- 无复用价值的细节，例如完整命令流水、重复验证输出、已经由稳定卡覆盖的中间操作。
- 未确认的随口想法；可以先留在当前任务讨论里，不直接沉淀为长期记忆。
- 已被新决策覆盖的旧信息；这类内容应标记 `superseded`、降权或归档。

## 写入门槛

写入 `runs/attempt` 前至少满足以下一项：

- 后续 agent 很可能重复同一路径，需要明确避免。
- 失败代价高，或重新试错会浪费明显时间。
- 出现大问题错误签名：构建失败、链接失败、panic/Guru、watchdog、断言、资源耗尽、串口/板测异常、硬件通信失败、context 检索/校验失败，且本轮已经形成了可复用判断。
- 做过有复用价值的路线选择：尝试过某路线并放弃、证伪了某判断、选择了下一条路线，且后续 agent 容易重复评估同一取舍。
- 影响 owner、架构边界、项目框架、长期约束或用户规划。
- 有关键证据，例如真机日志、构建结果、错误码、性能数据或明确复现步骤。
- 当前任务需要跨会话接手，且不记录会导致下一轮丢失状态。

不满足这些条件时，不写入长期上下文；保留在本轮对话或最终答复即可。

### 大问题错误与路线选择

错误记录主要面向大问题：阻塞构建/板测/联调、跨 owner、需要多轮定位、或会影响后续方案取舍。一次性拼写/格式问题不写。

路线选择记录主要回答：试过哪条路、为什么放弃、下一条路为什么更可信、哪些证据支撑这个取舍。

最小内容为：错误原文或路线问题、触发命令/动作、涉及 owner 或文件、已尝试且不要重复的路径、当前可信判断、下一步要补的证据。

记录时推荐理由：

```powershell
--record-because error-signature --record-because evidence
--record-because route-choice --record-because evidence
```

## 晋升门槛

`runs/attempt` 只有在能提炼为以下内容时，才适合进入 `promotion_candidates`：

- 项目知识：稳定事实、owner、模块边界或硬件/协议事实。
- 试错总结：失败路径、误判原因、不要重复的动作和可复用证据。
- 流程：可重复执行的排查顺序、验证闭环或标准操作。
- 决策：长期路线、不可逆取舍、兼容边界或架构原则。
- 约束或偏好：用户长期稳定要求、验证习惯、代码风格或抽象边界。

如果同主题已有稳定卡，优先在原卡上补证据或收紧边界，避免把同一结论拆成多张重复卡。

普通成功、小文档微调、一次性命令执行和没有新结论的操作，不应自动晋升。

## 使用原则

- Memory 存外部；prompt 只放当前任务必要上下文。
- 先 query，再 brief pack，最后只读必要原文。
- 记录经验时优先写“结论、证据、边界、不要重复什么”，少写命令流水。
- 记录错误时优先保留“原始错误签名 + 已证伪路径 + 下一步边界”，避免只写最终修复结论。
- 能被稳定卡覆盖的信息，不重复写多份；必要时从 attempt 链回稳定卡。
- 清理旧知识时先标记和降权，再归档或删除，不靠一次搜索结果直接删。
