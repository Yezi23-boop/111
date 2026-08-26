---
id: 2026-07-01-attempt-sd-manager-file-api
tags: context, runs, sd-card, storage, watchface, vfs, file-api
summary: 补齐 SD manager 头文件中已有声明但缺失实现的文件 API，为表盘 SD rawanim 缓存层提供稳定的 VFS 文件读写基础。
last_reviewed: 2026-07-01
memory_type: attempt
scope: task
owners: components/sd_card/sd_manager.c, components/sd_card/sd_manager.h, tests/test_sd_manager_source.py
triggers: sd_manager, SD 卡, /sdcard, watchface rawanim, fopen, fread, fwrite, stat
evidence_level: observed
---

# SD manager 文件 API 补齐记录

## 背景

表情表盘当前执行路线已经固定为 SD 卡 `/sdcard/watchface`。检查代码时发现：

- `components/sd_card/sd_manager.h` 声明了 `sd_manager_read_file()`、`sd_manager_write_file()`、`sd_manager_create_dir()`、`sd_manager_delete_file()`、`sd_manager_get_file_size()`。
- `components/sd_card/sd_manager.c` 只实现到 `sd_manager_file_exists()`。
- 后续 `watchface_anim_cache` 如果直接调用这些声明，会在链接阶段失败。

## 本次改动

- 补齐 `sd_manager_read_file()`：使用 VFS 标准 `fopen(..., "rb")` + `fread()` 读取到调用方缓冲区。
- 补齐 `sd_manager_write_file()`：使用 `fopen(..., "wb")` + `fwrite()` 写入文件。
- 补齐 `sd_manager_create_dir()`：使用 `mkdir()`，目录已存在且确实是目录时按成功处理。
- 补齐 `sd_manager_delete_file()`：使用 `remove()` 删除文件。
- 补齐 `sd_manager_get_file_size()`：使用 `stat()` 返回文件大小。
- 新增 source test `tests/test_sd_manager_source.py`，锁定头文件声明都有 `.c` 实现，并确认实现基于 VFS stdio/stat。

## 重要边界

- 本次不改 SD 引脚、SPI host、频率、挂载点或初始化顺序。
- 本次 API 是通用文件能力，不等同于表盘缓存层；`watchface_anim_cache` 仍应按帧 `fseek/fread`，不要整包加载 4-5MB rawanim。
- LVGL 仍不能直接使用 `/sdcard/watchface/*.rawanim` 作为 `lv_image_set_src()` 的文件路径。

## 验证

- `uv run pytest tests/test_sd_manager_source.py`：通过，`2 passed`。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：未进入编译阶段，CMake component manager 请求 `https://components-file.espressif.com/components/espressif/esp_codec_dev.json` 返回空/非法 JSON，失败点与本次 SD API 代码无关。

## 下一步

- 网络/组件仓库恢复后重跑 `idf.py build`。
- 表盘缓存层实现时可以复用 `sd_manager_get_file_size()` 做文件存在与大小检查，但帧级读取建议直接在 cache 层持有 `FILE *` 并使用 `fseek/fread`。
