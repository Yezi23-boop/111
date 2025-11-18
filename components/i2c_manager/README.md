# I2C Manager 组件

## 概述

I2C Manager 是一个统一的 I2C 总线管理组件,提供共享的 I2C 总线供多个外设组件使用。

## 设计目的

- **统一管理**: 集中管理 I2C 总线的初始化和销毁
- **资源共享**: 多个组件共享同一条 I2C 总线(GPIO14/GPIO15)
- **避免冲突**: 防止多个组件重复初始化 I2C 总线
- **简化维护**: 统一配置 I2C 参数,便于后续调整

## 硬件配置

| 参数 | GPIO | 说明 |
|------|------|------|
| SCL | GPIO14 | I2C 时钟线 |
| SDA | GPIO15 | I2C 数据线 |
| 频率 | 400kHz | I2C 时钟频率 |
| 端口 | I2C_NUM_0 | ESP32-S3 I2C 端口 |

## 使用的外设设备

当前共享 I2C 总线的设备:

1. **FT5x06 触摸屏控制器** (地址: 0x38)
   - 组件: `touch_ft5x06`
   - 用途: 电容触摸屏坐标读取

2. **ES8311 音频编解码器** (地址: 0x18)
   - 组件: `audio_codec`
   - 用途: 音频播放 DAC

3. **ES7210 音频 ADC** (地址: 0x40)
   - 组件: `audio_codec`
   - 用途: 双麦克风录音

## API 使用

### 初始化

```c
#include "i2c_manager.h"

// 初始化 I2C 总线(多次调用安全)
esp_err_t ret = i2c_manager_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C manager init failed");
}
```

### 获取总线句柄

```c
// 获取 I2C 总线句柄供设备使用
i2c_master_bus_handle_t i2c_bus = i2c_manager_get_bus();
if (!i2c_bus) {
    ESP_LOGE(TAG, "I2C bus not initialized");
}

// 添加 I2C 设备到总线
i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = 0x38,  // 设备地址
    .scl_speed_hz = 400000,
};
i2c_master_dev_handle_t dev_handle;
i2c_master_bus_add_device(i2c_bus, &dev_cfg, &dev_handle);
```

### 反初始化

```c
// 删除 I2C 总线(所有设备必须先移除)
esp_err_t ret = i2c_manager_deinit();
```

## 组件依赖关系

```
i2c_manager
    └── driver (ESP-IDF I2C driver)

touch_ft5x06
    └── i2c_manager

audio_codec
    └── i2c_manager
```

## 注意事项

1. **初始化顺序**: 建议在 `app_main()` 开始时调用 `i2c_manager_init()`
2. **多次初始化**: 重复调用 `i2c_manager_init()` 是安全的,只会初始化一次
3. **设备移除**: 在调用 `i2c_manager_deinit()` 前必须先移除所有 I2C 设备
4. **线程安全**: I2C 总线操作是线程安全的(由 ESP-IDF 驱动保证)

## 修改历史

- **2025-11-17**: 创建 i2c_manager 组件,统一管理 I2C 总线
  - 替代之前由 `touch_ft5x06` 组件创建总线并导出的方式
  - 所有组件改为依赖 `i2c_manager` 而非互相依赖
