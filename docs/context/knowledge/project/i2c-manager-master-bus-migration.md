---
id: i2c-manager-master-bus-migration
tags: project, i2c, esp-idf, audio, touch
summary: 当前仓库 i2c_manager 切换到 master bus 接口后的调用约束与兼容边界。
last_reviewed: 2026-03-31
memory_type: procedural
scope: repo
owners: components/lvgl_port, components/touch_ft5x06, components/audio_codec, components/axp2101
triggers: i2c, manager, master, bus, migration
evidence_level: design
---

# i2c_manager master bus 迁移

- `i2c_manager` 在 `ESP-IDF >= 5.3` 下暴露 `i2c_manager_get_bus_handle()`，并通过 `driver/i2c_master.h` 创建共享 `master bus`。
- `audio_codec` 通过共享 `bus_handle` 创建 codec control interface，避免在 `ESP-IDF 5.5.x` 下继续停留在旧 I2C 接口契约。
- 必须保持 `sdkconfig` 中 `CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE` 为关闭状态；若该开关被打开，`esp_codec_dev` 会强制退回 legacy `i2c_master_cmd_begin()` 路径，即使上层已经传入 `bus_handle` 也不会使用。
- `touch_ft5x06` 本轮仍使用 legacy command-link 读寄存器，当前验证表明它可以与新版 `i2c_manager` 共存；后续若要统一 I2C 访问模型，再迁移到设备句柄模式。
- 共享总线仍为 `GPIO14/GPIO15`，触摸与音频控制面共用，总线问题仍会同时影响 `touch_ft5x06` 和 `audio_codec`。
- 构建验证需要先进入 ESP-IDF shell 环境；在当前 Windows 环境中可通过执行 `D:\esp-idf\v5.5.3\esp-idf\export.ps1` 后再运行 `idf.py build`。
