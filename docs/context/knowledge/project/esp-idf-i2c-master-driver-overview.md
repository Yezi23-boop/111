---
id: esp-idf-i2c-master-driver-overview
tags: project, esp-idf, i2c, driver, esp32-s3
summary: ESP-IDF 5.3+ 新版 I2C master bus 驱动的对象模型、常用 API 与共享总线注意事项。
last_reviewed: 2026-08-07
memory_type: semantic
scope: repo
owners: components/i2c_manager, tests/test_i2c_master_bus_sdkconfig.py
triggers: esp, idf, i2c, master, driver, overview
evidence_level: observed
status: active
---

# ESP-IDF 新版 I2C master 驱动概览

- `ESP-IDF 5.3+` 的官方 I2C master 驱动以 `driver/i2c_master.h` 为主入口，核心模型从 legacy 的“端口 + command link”切到“bus handle + device handle”。
- 总线先由 `i2c_new_master_bus()` 创建，再通过 `i2c_master_bus_add_device()` 把具体从设备挂到该 bus 上。
- 常规寄存器读写优先使用高级 API：
  - 写：`i2c_master_transmit()`
  - 读：`i2c_master_receive()`
  - 先写后读：`i2c_master_transmit_receive()`
  - 探测地址：`i2c_master_probe()`
- 需要精细控制时序或跳过驱动自动发地址时，可用 `i2c_master_execute_defined_operations()` 和 `I2C_DEVICE_ADDRESS_NOT_USED`。

# 对象模型

- `i2c_master_bus_config_t` 负责 bus 级配置：
  - `i2c_port`
  - `sda_io_num` / `scl_io_num`
  - `clk_source`
  - `glitch_ignore_cnt`
  - `trans_queue_depth`
  - `flags.enable_internal_pullup`
  - `flags.allow_pd`
- `i2c_device_config_t` 负责 device 级配置：
  - `dev_addr_length`
  - `device_address`
  - `scl_speed_hz`
  - `scl_wait_us`
  - `flags.disable_ack_check`
- 一个 bus 可以挂多个 device handle，适合触摸、PMIC、音频 codec 共用同一组 SDA/SCL。

# 与 legacy 驱动的关键差异

- legacy 路径以 `i2c_param_config()` + `i2c_driver_install()` 初始化，再手写 `i2c_cmd_link_create()` / `i2c_master_start()` / `i2c_master_write_byte()` / `i2c_master_cmd_begin()`。
- 新版驱动把“总线资源管理”和“设备寻址配置”前置到 handle 层，普通寄存器访问不再需要每次手拼 command link。
- 迁移关系：
  - `i2c_master_write_to_device()` -> `i2c_master_transmit()`
  - `i2c_master_read_from_device()` -> `i2c_master_receive()`
  - `i2c_master_write_read_device()` -> `i2c_master_transmit_receive()`

# 常见读写模式

- 寄存器写：
  - 把 `{reg, data...}` 组成连续 buffer，调用 `i2c_master_transmit()`
- 寄存器读：
  - 把寄存器地址放进 `write_buffer`
  - 调用 `i2c_master_transmit_receive()` 完成 `write + repeated start + read`
- 纯流式读：
  - 设备不要求先写寄存器地址时，直接用 `i2c_master_receive()`
- 16 位寄存器地址器件：
  - 先手动拼两个地址字节到 `write_buffer`，再 `transmit_receive`

# 共享总线注意事项

- 本仓库 `components/i2c_manager/i2c_manager.c` 已在 `ESP-IDF >= 5.3` 下统一切到 `master bus` 模型。
- 共享 bus 时，不要让不同模块各自重复初始化同一个 I2C 端口；推荐集中创建 bus，再向各模块分发 bus handle 或 device handle。
- 7 位地址和 8 位读写地址不要混用；`device_address` 填原始 7 位地址，不带 R/W bit。
- `flags.enable_internal_pullup` 仅适合低速或临时验证；`400 kHz` 及更复杂板级场景优先外部上拉。
- `ESP_ERR_TIMEOUT` 多数先查硬件：
  - SDA/SCL 上拉
  - 地址是否正确
  - 设备是否上电
  - 线序是否反
  - 共享总线是否被别的器件拉低

# 适用边界

- 高级 API 适合大多数寄存器型从设备。
- 若器件协议包含非标准起止条件、自定义地址阶段或特殊 ACK/NACK 序列，再考虑 `i2c_master_execute_defined_operations()`。
- 使用异步回调 `i2c_master_register_event_callbacks()` 时，缓冲区生命周期必须覆盖整个事务；同一 bus 上只允许一个 device 参与异步事务。

# 参考

- 官方示例：`D:/esp-idf/v5.5.3/esp-idf/examples/peripherals/i2c/i2c_basic/main/i2c_basic_example_main.c`
- 官方头文件：`D:/esp-idf/v5.5.3/esp-idf/components/esp_driver_i2c/include/driver/i2c_master.h`
- 仓库迁移说明：见本文末尾「i2c-manager-master-bus-migration」小节（2026-08-06 并入）

## i2c-manager-master-bus-migration



- `i2c_manager` 在 `ESP-IDF >= 5.3` 下暴露 `i2c_manager_get_bus_handle()`，并通过 `driver/i2c_master.h` 创建共享 `master bus`。
- `audio_codec` 通过共享 `bus_handle` 创建 codec control interface，避免在 `ESP-IDF 5.5.x` 下继续停留在旧 I2C 接口契约。
- 必须保持 `sdkconfig` 中 `CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE` 为关闭状态；若该开关被打开，`esp_codec_dev` 会强制退回 legacy `i2c_master_cmd_begin()` 路径，即使上层已经传入 `bus_handle` 也不会使用。
- `touch_ft5x06` 本轮仍使用 legacy command-link 读寄存器，当前验证表明它可以与新版 `i2c_manager` 共存；后续若要统一 I2C 访问模型，再迁移到设备句柄模式。
- 共享总线仍为 `GPIO14/GPIO15`，触摸与音频控制面共用，总线问题仍会同时影响 `touch_ft5x06` 和 `audio_codec`。
- 构建验证需要先进入 ESP-IDF shell 环境；在当前 Windows 环境中可通过执行 `D:\esp-idf\v5.5.3\esp-idf\export.ps1` 后再运行 `idf.py build`。


