---
name: "execute-plan"
description: "执行计划文件：读取 docs/context/plans/active/ 中的计划，逐步实现并验证"
---

# 计划执行与复查

从 `docs/context/plans/active/` 读取计划文件，逐步执行实现，并进行复查验证。

## 使用方式

用户指定计划文件路径，或自动查找最新的 active plan。

## 执行流程

### Phase 1: 读取计划
1. 读取计划文件（通常在 `docs/context/plans/active/` 下）
2. 理解目标、owner、边界、禁止项
3. 列出所有待完成项（checklist）

### Phase 2: 逐步实现
1. 按计划顺序逐项实现
2. 每完成一项，更新计划中的 checklist（`[ ]` → `[x]`）
3. 遵循 AGENTS.md 中的规则：
   - Think Before Coding：先理解再动手
   - Simplicity First：最小代码解决问题
   - Surgical Changes：只改必须改的
4. 涉及 ESP-IDF 构建时，使用 `/idf-build` 命令

### Phase 3: 验证
1. 运行 source tests：`uv run python -m pytest tests/... -q`
2. 运行 `idf.py build` 验证编译通过
3. 检查 diff 中无敏感信息泄露

### Phase 4: 复查
1. 可派 subagent 复查实现是否符合计划
2. 检查：
   - 后端实现完整性
   - 前端改动正确性
   - 测试覆盖度
   - 回调链路完整性
3. 输出复查报告

### Phase 5: 收尾
1. 更新计划文件（标记完成状态）
2. 更新 `docs/context/CHANGELOG.md`
3. 按需更新 `docs/context/handoffs/current-task.md`
4. 如果涉及 FreeRTOS/内存/硬件驱动，执行四步闭环：
   - 新建 `docs/context/runs/YYYY-MM-DD-attempt-<特征词>.md`
   - 更新 CHANGELOG
   - 同步 current-task.md
   - 运行 `validate_context.py --level standard`

## 注意事项

- 计划文件是 ground truth，不要偏离计划目标
- 每完成一项立即更新 checklist，避免遗漏
- 构建失败时先检查是否需要 `fullclean`
- 复查时重点关注：回调链路、依赖方向、安全边界
