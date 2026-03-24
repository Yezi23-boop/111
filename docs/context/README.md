# 上下文库说明

该目录是 Codex 使用的项目长期上下文库。

## 目录结构

- `knowledge/`：可复用技术知识、排障手册、实践清单。
- `decisions/`：架构决策记录（ADR）。
- `CHANGELOG.md`：上下文库的变更流水。
- `knowledge-map.md`：知识导航入口。

## Frontmatter 约定

知识文件统一包含以下头部：

```yaml
---
id: 唯一标识
tags: 标签1, 标签2, 标签3
summary: 一句话摘要
last_reviewed: YYYY-MM-DD
---
```

## 常用命令

构建索引：

```bash
python scripts/context/build_index.py
```

校验文档：

```bash
python scripts/context/check.py
```

检索上下文：

```bash
python scripts/context/query.py --q "esp32 s3 lvgl 触摸漂移" --top 5
```

按标签过滤检索：

```bash
python scripts/context/query.py --q "待机电流过高" --tag power --tag esp32-s3
```

打包可复用上下文片段：

```bash
python scripts/context/pack_context.py --q "待机功耗优化" --top 5
```

## 维护规范

- 每个文档聚焦一个主题。
- 优先写清单、阈值和验收标准。
- 修改文档时同步更新 `last_reviewed`。
