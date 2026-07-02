# DS2413 + PIOA 马达移植包

这个目录是从 PCB 测试代码中整理出来的可移植版本。主程序只需要调用
`board_ds2413_motor_*`，不需要直接处理 1-Wire bus、ROM 地址或 PIOA/PIOB 位定义。

## 已确认硬件事实

- 1-Wire 总线：`GPIO18`，原理图上 `R22=4.7k` 外部上拉。
- 当前板卡 DS2413 兼容器件 family code：`0xBA`。
- PIOA 控制马达驱动：
  - `PIOA release`：R9 上拉 Q1 基极，马达导通。
  - `PIOA pull-low`：DS2413 拉低 Q1 基极，马达关闭。
- PIOB 当前不接回主程序，封装中始终保持 `release`。
- DS2413 没有掉电保存锁存状态；上电后默认状态仍应尽量靠硬件保证，软件只能在初始化后立即关断马达。

## 迁移文件

把这些文件复制到主工程对应位置：

```text
components/ds2413/CMakeLists.txt
components/ds2413/ds2413.c
components/ds2413/include/ds2413.h
main/app/board_ds2413_motor.c
main/app/board_ds2413_motor.h
```

本目录已经按上述结构放好副本：

```text
scratch/ds2413_motor_migration/components/ds2413/
scratch/ds2413_motor_migration/main/app/
```

## 对外接口

底层组件接口在 `components/ds2413/include/ds2413.h`：

```c
esp_err_t ds2413_find_first(onewire_bus_handle_t bus, ds2413_device_t *device);
esp_err_t ds2413_find_by_index(onewire_bus_handle_t bus, size_t index, ds2413_device_t *device);
esp_err_t ds2413_read_state(const ds2413_device_t *device, ds2413_state_t *state);
esp_err_t ds2413_write_latch(const ds2413_device_t *device,
                             const ds2413_latch_state_t *latch_state,
                             ds2413_state_t *verified_state);
```

主程序建议只使用板级封装接口：

```c
esp_err_t board_ds2413_motor_init(void);
esp_err_t board_ds2413_motor_set_enabled(bool enabled);
esp_err_t board_ds2413_motor_pulse(uint32_t on_ms);
```

## main/CMakeLists.txt 接入点

恢复主程序模式后，在 `app_srcs` 中加入：

```cmake
${CMAKE_CURRENT_LIST_DIR}/app/board_ds2413_motor.c
```

在 `REQUIRES` 中保留或加入：

```cmake
ds2413
espressif__onewire_bus
driver
freertos
```

如果 `main` 已经通过 `ds2413` 间接拿到 `espressif__onewire_bus`，也可以只保留 `ds2413`；但 `board_ds2413_motor.c` 直接 include 了 `onewire_bus_impl_rmt.h` 和 `onewire_bus_impl_uart.h`，显式写上更清楚。

## hardware_init.c 建议接入

在 `hardware_init.c` 顶部加入：

```c
#include "board_ds2413_motor.h"
```

在 `hardware_nvs_init()` 成功后尽早调用，减少 R9 上拉后马达默认震动时间：

```c
ESP_LOGI(TAG, "Initializing DS2413 Motor...");
ret = board_ds2413_motor_init();
if (ret != ESP_OK)
{
    ESP_LOGW(TAG, "DS2413 motor init failed: %s", esp_err_to_name(ret));
}
```

建议放在音频、SD、显示等较慢初始化之前。

## 运行期调用

打开马达：

```c
board_ds2413_motor_set_enabled(true);
```

关闭马达：

```c
board_ds2413_motor_set_enabled(false);
```

输出一次脉冲：

```c
board_ds2413_motor_pulse(300);
```

## 验证标准

上电日志应看到：

```text
DS2413 ROM via RMT: BA ...
DS2413 motor default off: raw=0x78 PIOA(state=0 latch=0)
```

其中 `PIOA latch=0/state=0` 表示软件已拉低 PIOA，马达控制通路处于关闭命令状态。
