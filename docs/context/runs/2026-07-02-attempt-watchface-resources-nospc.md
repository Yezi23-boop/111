---
id: 2026-07-02-attempt-watchface-resources-nospc
tags: context, runs, watchface, resources, littlefs, sdcard, build
summary: 修复表情表盘预览帧进入 resources 分区导致 LittleFS 镜像超过 4MiB 的构建失败。
last_reviewed: 2026-07-02
memory_type: attempt
scope: task
owners: resources, scripts/watchface, docs/context/plans/active/2026-06-30-watchface-emoji-root-ui-plan.md
triggers: LFS_ERR_NOSPC, resources.bin, resources/watchface, LittleFS, 表情表盘
evidence_level: observed
status: active
---

# 表情资源撑爆 LittleFS 修复记录

## 问题

`idf.py build` 在生成 `resources.bin` 时早早失败，错误为 `LFS_ERR_NOSPC`。根因是 `resources/watchface/frames` 中的表情预览 PNG 被纳入 `resources` LittleFS 镜像，而 `resources` 分区只有 4MiB。

## 修复

- 删除已跟踪的 `resources/watchface` 预览帧目录。
- `scripts/watchface/extract_watchface_frames.py` 默认输出改为 `sdcard/watchface/frames`。
- `scripts/watchface/pack_watchface_rawanim.py` 默认输入改为 `sdcard/watchface/frames`，默认临时 resources 输出改到 `tmp/watchface/resources`，避免默认写回固件 resources 分区。
- `.gitignore` 增加 `sdcard/watchface/frames/`，预览帧不再进入版本库和 LittleFS 镜像。
- 新增 `tests/test_watchface_resource_boundary_source.py`，锁定 `resources/watchface` 不存在且脚本默认路径不指向 `resources/watchface`。

## 验证

- `uv run pytest tests/test_watchface_resource_boundary_source.py`：通过，`2 passed`。
- `resources/` 当前文件总量约 `3.07 MiB`，低于 4MiB 分区。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：通过。
- LittleFS 创建日志只包含 `fonts` 和 `weather`，不再包含 `watchface`。
- `111.bin` 大小 `0xabdcf0`，14MiB app 分区剩余 `0x342310`，约 23%。

## 后续

表情运行资源继续走 SD 卡 `/sdcard/watchface/*.rawanim`，不要把表情预览帧或 rawanim 放回 `resources/`。
