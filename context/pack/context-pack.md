# 上下文包

- 生成时间(UTC): 2026-03-11T10:19:15.722535+00:00
- 查询: co5300 面板
- 范围: mixed
- 包含导航文档: False

## 命中文档

1. `docs/context/knowledge/project/repo-overview.md` (score=22)
   - 标题: 当前仓库概览
   - 标签: project, architecture, modules, lvgl, audio, wifi, esp32-s3
   - 摘要: 当前仓库的模块地图、启动链路和构建依赖摘要，便于定位 UI、音频和配网相关改动。
2. `docs/context/knowledge/project/plan-mode-rules.md` (score=10)
   - 标题: 计划模式规则
   - 标签: project, rules, planning, plan-mode, validation
   - 摘要: 本仓库在上层环境进入计划模式后使用的深度计划规则，约束问题建模、方案比较、验证闭环和回滚设计。

## 可直接粘贴给 Codex 的上下文

### 来源: docs/context/knowledge/project/repo-overview.md
- 相关分数: 22
- 关键片段(L19): - `components/co5300_panel`：`CO5300` 面板驱动和 `TE` 同步处理。
- 摘要: 当前仓库的模块地图、启动链路和构建依赖摘要，便于定位 UI、音频和配网相关改动。

### 来源: docs/context/knowledge/project/plan-mode-rules.md
- 相关分数: 10
- 关键片段(L96): - 合格示例：在 `components/lvgl_port/lv_port.c` 中为显示刷新增加等待点统计和超时保护，并在 `components/co5300_panel/co5300_panel.c` 中补 TE 中断观测与失败回退路径。
- 摘要: 本仓库在上层环境进入计划模式后使用的深度计划规则，约束问题建模、方案比较、验证闭环和回滚设计。


> 已打包片段数: 2/2，片段字符预算: 4000