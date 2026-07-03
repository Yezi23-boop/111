---
name: squash-danger-sample-commits
overview: 将 8ac951bd 到 HEAD 的 5 个文档提交与当前工作区未提交的代码实现变更（测试清理后的最终状态）合并为一个提交。
todos:
  - id: squash-1
    content: 暂存所有目标文件（排除 .codebuddy/），创建临时提交
    status: completed
  - id: squash-2
    content: git reset --soft 03b8b8da，将 5 个提交+工作区变更合并为暂存区
    status: completed
    dependencies:
      - squash-1
  - id: squash-3
    content: 创建最终 squash 提交，包含完整功能描述
    status: completed
    dependencies:
      - squash-2
  - id: squash-4
    content: git log 验证 squash 结果，确认仅一个新提交
    status: completed
    dependencies:
      - squash-3
---

## 用户需求

将 `8ac951bd`（docs: add danger sample sd closed loop plan）到当前 HEAD `afdd1c6a` 之间的 4 个提交，连同工作区未提交的修改和新增文件，一起 squash 合并为单个提交。

## 当前状态

- **分支**：`codex/ai-memory-watch-hermes-api`，领先 origin 6 个提交
- **待 squash 提交**（4 个）：

1. `8ac951bd` docs: add danger sample sd closed loop plan
2. `6b1d4ddf` docs: set danger sample window to one second context
3. `5046de45` docs: tie danger samples to background switch
4. `1166c38b` docs: decouple danger sample recorder plan
5. `afdd1c6a` docs: fix danger sample recorder execution gaps

- **父提交**（squash 的锚点）：`03b8b8da` 接入危险告警手机通知链路
- **未提交修改**（8 个已修改文件）：PCM tap callback、recorder 模块、SD rename API、CMakeLists、计划文件等
- **未跟踪文件**（3 个）：`danger_sample_recorder.c`、`danger_sample_recorder.h`、`test_danger_sample_loop.ps1`
- **排除项**：`.codebuddy/` 目录不提交

## 技术方案

### 合并策略：git reset --soft

使用 `git reset --soft` 将所有变更合并为单个提交：

1. **暂存所有文件**（排除 `.codebuddy/`）→ `git add` 所有已修改和新增的目标文件
2. **创建临时提交** → `git commit -m "temp: squash staging"`，将工作区变更纳入版本控制
3. **软重置到父提交** → `git reset --soft 03b8b8da`，将 HEAD 移至 `03b8b8da`（接入危险告警手机通知链路），但保留所有文件在暂存区
4. **创建最终提交** → `git commit -m "feat: 危险样本 SD 卡闭环功能完整实现（阶段 1A-1D + 测试 + 清理）"`
5. **验证** → `git log --oneline -5` 确认只有一个新提交取代了原来 5 个

### 为什么选择 reset --soft

- 比 `git rebase -i` 更简单直接，避免编辑器交互
- 不需要 squash 每个 commit 的消息，直接将所有变更一次性提交
- 仅影响本地分支，未推送到 origin 的提交可以安全重写

### 风险控制

- 操作前记录当前 HEAD 哈希 `afdd1c6a`，可随时 `git reset --hard afdd1c6a` 回退
- `.codebuddy/` 目录不在 `.gitignore` 中，需显式排除
- `scripts/test_danger_sample_loop.ps1` 属于本次功能开发的一部分，包含在提交中