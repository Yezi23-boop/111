---
description: "Git 未提交文件分类提交：分析 git status，按功能分组，中文提交信息"
---

# Git 分类提交

分析当前仓库的未提交文件，按功能分类并分组提交。

## 使用方式

`/git-classify-commit [push]`

- 无参数：只分类提交，不 push
- `push`：提交后自动 push 到远程

## 执行步骤

1. 运行 `git status` 获取所有未提交文件
2. 运行 `git diff --stat` 了解改动范围
3. 运行 `git log --oneline -5` 了解最近提交风格
4. 按以下分类规则分组：
   - **功能代码**：`main/`、`components/` 下的 `.c`/`.h`/`.cpp` 文件
   - **测试**：`tests/` 下的测试文件
   - **文档**：`docs/`、`*.md` 文件
   - **配置**：`sdkconfig`、`CMakeLists.txt`、`Kconfig*`、`.gitignore`
   - **前端**：`web/` 下的 `.html`/`.js`/`.css` 文件
   - **脚本**：`scripts/` 下的脚本文件
5. 为每组生成中文提交信息，格式：`<type>(<scope>): <描述>`
   - type：feat/fix/test/docs/chore/refactor
   - scope：模块名（如 memory-watch、ap-portal、voice-endpoint）
6. 依次执行 `git add <files> && git commit -m "<message>"`
7. 如果指定了 `push`，执行 `git push`

## 提交信息规范

- 使用中文描述
- 格式：`<type>(<scope>): <描述>`
- 示例：
  - `feat(memory-watch): 更新语音客户端与服务层`
  - `test(ap-portal): 新增 HTTP API 测试用例`
  - `docs(context): 更新上下文知识库`

## 注意事项

- 不要提交 `.env`、`credentials.json` 等敏感文件
- `sdkconfig` 纳入版本管理（用户已确认）
- 提交前确认分组合理，避免混合不相关改动
