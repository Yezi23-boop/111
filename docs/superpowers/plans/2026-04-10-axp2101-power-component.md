# AXP2101 Power Component Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为当前 `ESP32-S3-Touch-AMOLED-2.06` 项目落地第一阶段只读 `AXP2101` 电源管理基座，包括 `components/axp2101`、`board_power`、`power_service`、主链路接入与源码契约测试。

**Architecture:** 保持三层结构不变。`components/axp2101` 只做共享 I2C 下的 PMIC 寄存器访问和状态快照，`main/app/board_power.[ch]` 只做板级语义映射和缓存，`main/services/power_service.[ch]` 只做低频轮询、失败退避、日志节流和状态发布。`hardware_init.c` 只做非致命初始化接入，`app_main.c` 只负责服务启动，不提前进入 PMIC 控制寄存器写路径。

**Tech Stack:** ESP-IDF 5.5.3、`driver/i2c_master.h`、现有 `i2c_manager`、FreeRTOS 任务、Python `unittest` 源码契约测试、`uv`

---

## File Map

- `D:\esp32S3\111\components\axp2101\CMakeLists.txt`
  - 注册新的只读 PMIC 组件，依赖 `i2c_manager`
- `D:\esp32S3\111\components\axp2101\include\axp2101.h`
  - 对外暴露 `axp2101_snapshot_t`、`axp2101_irq_status_t` 和只读 API
- `D:\esp32S3\111\components\axp2101\axp2101_regs.h`
  - 保存当前阶段需要的寄存器地址和位定义
- `D:\esp32S3\111\components\axp2101\axp2101.c`
  - 复用共享 I2C，总线探测、寄存器读写、快照读取、IRQ 读清
- `D:\esp32S3\111\main\app\board_power.h`
  - 定义板级电源状态结构和查询接口
- `D:\esp32S3\111\main\app\board_power.c`
  - 将 PMIC 快照映射为板级状态，维护缓存和数据有效性
- `D:\esp32S3\111\main\services\power_service.h`
  - 暴露电源服务初始化、启动和状态读取接口
- `D:\esp32S3\111\main\services\power_service.c`
  - 低频轮询 `board_power`，失败退避，日志节流，状态变化回调
- `D:\esp32S3\111\main\app\hardware_init.c`
  - 在音频初始化后接入 `board_power_init()`，失败非致命
- `D:\esp32S3\111\main\app\app_main.c`
  - 在 `lvgl_task` 之后启动 `power_service`
- `D:\esp32S3\111\main\CMakeLists.txt`
  - 将 `board_power.c`、`power_service.c` 纳入编译，并添加 `axp2101` 依赖
- `D:\esp32S3\111\tests\main_paths.py`
  - 添加 `BOARD_POWER_*`、`POWER_SERVICE_*` 路径常量
- `D:\esp32S3\111\tests\test_axp2101_power_source.py`
  - 锁定 `AXP2101` 组件的只读 API、共享总线用法和寄存器覆盖范围
- `D:\esp32S3\111\tests\test_board_power_source.py`
  - 锁定 `board_power` 状态模型，特别是 `battery_data_valid` / `snapshot_stale`
- `D:\esp32S3\111\tests\test_power_service_source.py`
  - 锁定 `power_service` 的低频轮询、失败退避、日志节流行为
- `D:\esp32S3\111\tests\test_power_integration_source.py`
  - 锁定 `hardware_init.c`、`app_main.c` 和 `main/CMakeLists.txt` 的集成点
- `D:\esp32S3\111\docs\context\CHANGELOG.md`
  - 记录实现阶段的可复用结论

### Task 1: 建立源码契约测试和路径常量

**Files:**
- Modify: `D:\esp32S3\111\tests\main_paths.py`
- Create: `D:\esp32S3\111\tests\test_axp2101_power_source.py`
- Create: `D:\esp32S3\111\tests\test_board_power_source.py`
- Create: `D:\esp32S3\111\tests\test_power_service_source.py`
- Create: `D:\esp32S3\111\tests\test_power_integration_source.py`

- [ ] **Step 1: 在 `tests/main_paths.py` 里先补路径常量**

把下面这段加到 `D:\esp32S3\111\tests\main_paths.py` 末尾：

```python
BOARD_POWER_SOURCE = APP_DIR / "board_power.c"
BOARD_POWER_HEADER = APP_DIR / "board_power.h"

POWER_SERVICE_SOURCE = SERVICES_DIR / "power_service.c"
POWER_SERVICE_HEADER = SERVICES_DIR / "power_service.h"
```

- [ ] **Step 2: 写 `AXP2101` 组件失败测试**

创建 `D:\esp32S3\111\tests\test_axp2101_power_source.py`：

```python
import unittest

from tests.main_paths import REPO_ROOT


AXP2101_DIR = REPO_ROOT / "components" / "axp2101"
AXP2101_CMAKE = AXP2101_DIR / "CMakeLists.txt"
AXP2101_HEADER = AXP2101_DIR / "include" / "axp2101.h"
AXP2101_REGS = AXP2101_DIR / "axp2101_regs.h"
AXP2101_SOURCE = AXP2101_DIR / "axp2101.c"


class Axp2101PowerSourceTests(unittest.TestCase):
    def test_component_registers_shared_i2c_dependencies(self) -> None:
        self.assertTrue(AXP2101_CMAKE.exists())
        cmake = AXP2101_CMAKE.read_text(encoding="utf-8")
        self.assertIn('"axp2101.c"', cmake)
        self.assertIn("i2c_manager", cmake)

    def test_public_header_exposes_read_only_snapshot_contract(self) -> None:
        header = AXP2101_HEADER.read_text(encoding="utf-8")
        self.assertIn("typedef struct {", header)
        self.assertIn("axp2101_snapshot_t", header)
        self.assertIn("axp2101_irq_status_t", header)
        self.assertIn("esp_err_t axp2101_init(void);", header)
        self.assertIn("esp_err_t axp2101_probe(bool *present);", header)
        self.assertIn("esp_err_t axp2101_read_snapshot(axp2101_snapshot_t *snapshot);", header)
        self.assertIn("esp_err_t axp2101_read_irq_status(axp2101_irq_status_t *status);", header)
        self.assertIn("esp_err_t axp2101_clear_irq_status(const axp2101_irq_status_t *status);", header)

    def test_source_uses_master_bus_and_required_registers(self) -> None:
        source = AXP2101_SOURCE.read_text(encoding="utf-8")
        regs = AXP2101_REGS.read_text(encoding="utf-8")
        self.assertIn("i2c_manager_get_bus_handle()", source)
        self.assertIn("i2c_master_bus_add_device", source)
        self.assertIn("i2c_master_transmit_receive", source)
        self.assertIn("AXP2101_REG_STATUS0", regs)
        self.assertIn("AXP2101_REG_STATUS1", regs)
        self.assertIn("AXP2101_REG_BATTERY_H", regs)
        self.assertIn("AXP2101_REG_BATTERY_L", regs)
        self.assertIn("AXP2101_REG_VBUS_H", regs)
        self.assertIn("AXP2101_REG_VBUS_L", regs)
        self.assertIn("AXP2101_REG_VSYS_H", regs)
        self.assertIn("AXP2101_REG_VSYS_L", regs)
        self.assertIn("AXP2101_REG_BAT_PERCENT", regs)
        self.assertIn("AXP2101_REG_IRQ0", regs)
        self.assertIn("AXP2101_REG_IRQ1", regs)
        self.assertIn("AXP2101_REG_IRQ2", regs)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: 写 `board_power` 失败测试**

创建 `D:\esp32S3\111\tests\test_board_power_source.py`：

```python
import unittest

from tests.main_paths import BOARD_POWER_HEADER
from tests.main_paths import BOARD_POWER_SOURCE


class BoardPowerSourceTests(unittest.TestCase):
    def test_board_power_header_exposes_cached_state_contract(self) -> None:
        header = BOARD_POWER_HEADER.read_text(encoding="utf-8")
        self.assertIn("battery_data_valid", header)
        self.assertIn("snapshot_stale", header)
        self.assertNotIn("low_battery", header)
        self.assertIn("esp_err_t board_power_init(void);", header)
        self.assertIn("esp_err_t board_power_refresh(board_power_state_t *state);", header)
        self.assertIn("const board_power_state_t *board_power_get_cached_state(void);", header)

    def test_board_power_source_maps_axp2101_snapshot(self) -> None:
        source = BOARD_POWER_SOURCE.read_text(encoding="utf-8")
        self.assertIn('#include "axp2101.h"', source)
        self.assertIn("axp2101_read_snapshot", source)
        self.assertIn("battery_data_valid", source)
        self.assertIn("snapshot_stale", source)
        self.assertIn("board_power_get_cached_state", source)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 4: 写 `power_service` 和集成失败测试**

创建 `D:\esp32S3\111\tests\test_power_service_source.py`：

```python
import unittest

from tests.main_paths import POWER_SERVICE_HEADER
from tests.main_paths import POWER_SERVICE_SOURCE


class PowerServiceSourceTests(unittest.TestCase):
    def test_power_service_public_api_exists(self) -> None:
        header = POWER_SERVICE_HEADER.read_text(encoding="utf-8")
        self.assertIn("esp_err_t power_service_init(void);", header)
        self.assertIn("esp_err_t power_service_start(void);", header)
        self.assertIn("void power_service_register_callback(power_state_changed_cb_t cb);", header)
        self.assertIn("const board_power_state_t *power_service_get_state(void);", header)

    def test_power_service_source_uses_low_frequency_poll_and_backoff(self) -> None:
        source = POWER_SERVICE_SOURCE.read_text(encoding="utf-8")
        self.assertIn("board_power_refresh", source)
        self.assertIn("snapshot_stale", source)
        self.assertIn("pdMS_TO_TICKS(1000)", source)
        self.assertIn("pdMS_TO_TICKS(2000)", source)
        self.assertIn("pdMS_TO_TICKS(5000)", source)


if __name__ == "__main__":
    unittest.main()
```

创建 `D:\esp32S3\111\tests\test_power_integration_source.py`：

```python
import unittest

from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import HARDWARE_INIT_SOURCE
from tests.main_paths import REPO_ROOT


MAIN_CMAKE = REPO_ROOT / "main" / "CMakeLists.txt"


class PowerIntegrationSourceTests(unittest.TestCase):
    def test_hardware_init_initializes_board_power_after_audio_codec(self) -> None:
        source = HARDWARE_INIT_SOURCE.read_text(encoding="utf-8")
        self.assertIn('#include "board_power.h"', source)
        self.assertIn("audio_codec_init()", source)
        self.assertIn("board_power_init()", source)
        self.assertLess(source.index("audio_codec_init()"), source.index("board_power_init()"))

    def test_app_main_starts_power_service_after_lvgl_task(self) -> None:
        source = APP_MAIN_SOURCE.read_text(encoding="utf-8")
        self.assertIn('#include "services/power_service.h"', source)
        self.assertIn("xTaskCreatePinnedToCore(lvgl_task", source)
        self.assertIn("power_service_start()", source)
        self.assertLess(source.index("xTaskCreatePinnedToCore(lvgl_task"), source.index("power_service_start()"))

    def test_main_component_registers_new_sources_and_axp2101_dependency(self) -> None:
        source = MAIN_CMAKE.read_text(encoding="utf-8")
        self.assertIn('${CMAKE_CURRENT_LIST_DIR}/app/board_power.c', source)
        self.assertIn('${CMAKE_CURRENT_LIST_DIR}/services/power_service.c', source)
        self.assertIn("axp2101", source)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 5: 运行测试，确认它们因为文件不存在而失败**

Run:

```powershell
uv run python -m unittest `
  tests.test_axp2101_power_source `
  tests.test_board_power_source `
  tests.test_power_service_source `
  tests.test_power_integration_source -v
```

Expected:
- 四组测试失败
- 失败原因主要是新文件不存在，或现有集成点尚未加入

- [ ] **Step 6: 提交失败测试和路径常量**

```bash
git add tests/main_paths.py tests/test_axp2101_power_source.py tests/test_board_power_source.py tests/test_power_service_source.py tests/test_power_integration_source.py
git commit -m "测试: 增加 AXP2101 电源组件源码契约"
```

### Task 2: 实现 `components/axp2101` 只读 PMIC 组件

**Files:**
- Create: `D:\esp32S3\111\components\axp2101\CMakeLists.txt`
- Create: `D:\esp32S3\111\components\axp2101\include\axp2101.h`
- Create: `D:\esp32S3\111\components\axp2101\axp2101_regs.h`
- Create: `D:\esp32S3\111\components\axp2101\axp2101.c`
- Test: `D:\esp32S3\111\tests\test_axp2101_power_source.py`

- [ ] **Step 1: 创建组件注册文件**

创建 `D:\esp32S3\111\components\axp2101\CMakeLists.txt`：

```cmake
idf_component_register(
    SRCS "axp2101.c"
    INCLUDE_DIRS "include"
    REQUIRES driver i2c_manager
)
```

- [ ] **Step 2: 创建公共头文件**

创建 `D:\esp32S3\111\components\axp2101\include\axp2101.h`：

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool vbus_good;
    bool battery_present;
    bool battfet_on;
    bool charging;
    bool discharging;
    uint16_t battery_mv;
    uint16_t vbus_mv;
    uint16_t vsys_mv;
    int8_t battery_percent;
} axp2101_snapshot_t;

typedef struct {
    uint8_t irq0;
    uint8_t irq1;
    uint8_t irq2;
} axp2101_irq_status_t;

esp_err_t axp2101_init(void);
esp_err_t axp2101_probe(bool *present);
esp_err_t axp2101_read_snapshot(axp2101_snapshot_t *snapshot);
esp_err_t axp2101_read_irq_status(axp2101_irq_status_t *status);
esp_err_t axp2101_clear_irq_status(const axp2101_irq_status_t *status);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: 创建寄存器定义文件**

创建 `D:\esp32S3\111\components\axp2101\axp2101_regs.h`：

```c
#pragma once

#define AXP2101_I2C_ADDRESS 0x34

#define AXP2101_REG_STATUS0 0x00
#define AXP2101_REG_STATUS1 0x01
#define AXP2101_REG_ADC_ENABLE0 0x30

#define AXP2101_REG_BATTERY_H 0x34
#define AXP2101_REG_BATTERY_L 0x35
#define AXP2101_REG_VBUS_H 0x38
#define AXP2101_REG_VBUS_L 0x39
#define AXP2101_REG_VSYS_H 0x3A
#define AXP2101_REG_VSYS_L 0x3B

#define AXP2101_REG_IRQ_ENABLE0 0x40
#define AXP2101_REG_IRQ_ENABLE1 0x41
#define AXP2101_REG_IRQ_ENABLE2 0x42

#define AXP2101_REG_IRQ0 0x48
#define AXP2101_REG_IRQ1 0x49
#define AXP2101_REG_IRQ2 0x4A

#define AXP2101_REG_BAT_PERCENT 0xA4

#define AXP2101_STATUS0_BAT_PRESENT BIT(3)
#define AXP2101_STATUS0_BATFET_ON BIT(4)
#define AXP2101_STATUS0_VBUS_GOOD BIT(5)

#define AXP2101_STATUS1_BAT_DIR_MASK 0x60
#define AXP2101_STATUS1_BAT_DIR_CHARGE 0x20
#define AXP2101_STATUS1_BAT_DIR_DISCHARGE 0x40
```

- [ ] **Step 4: 创建驱动实现**

创建 `D:\esp32S3\111\components\axp2101\axp2101.c`：

```c
#include "axp2101.h"

#include <string.h>

#include "axp2101_regs.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "i2c_manager.h"

static const char *TAG = "axp2101";

static bool s_initialized = false;
static i2c_master_dev_handle_t s_dev_handle = NULL;

static esp_err_t axp2101_read_bytes(uint8_t reg, uint8_t *data, size_t len)
{
    if (s_dev_handle == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_transmit_receive(s_dev_handle, &reg, 1, data, len, 50);
}

static esp_err_t axp2101_read_u8(uint8_t reg, uint8_t *value)
{
    return axp2101_read_bytes(reg, value, 1);
}

static uint16_t axp2101_decode_14bit(uint8_t high, uint8_t low)
{
    return (uint16_t)(((high & 0x3F) << 8) | low);
}

esp_err_t axp2101_init(void)
{
    esp_err_t ret;
    i2c_master_bus_handle_t bus_handle;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDRESS,
        .scl_speed_hz = I2C_MANAGER_FREQ_HZ,
    };

    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(i2c_manager_init(), TAG, "shared i2c init failed");

    bus_handle = i2c_manager_get_bus_handle();
    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "shared i2c bus handle is null");
        return ESP_ERR_INVALID_STATE;
    }

    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "add AXP2101 device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t axp2101_probe(bool *present)
{
    uint8_t value = 0;
    esp_err_t ret;

    if (present == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *present = false;
    ESP_RETURN_ON_ERROR(axp2101_init(), TAG, "init failed before probe");

    ret = axp2101_read_u8(AXP2101_REG_STATUS0, &value);
    if (ret == ESP_OK) {
        *present = true;
    }
    return ret;
}

esp_err_t axp2101_read_snapshot(axp2101_snapshot_t *snapshot)
{
    uint8_t status0 = 0;
    uint8_t status1 = 0;
    uint8_t raw[2] = {0};

    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    ESP_RETURN_ON_ERROR(axp2101_init(), TAG, "init failed before snapshot");

    ESP_RETURN_ON_ERROR(axp2101_read_u8(AXP2101_REG_STATUS0, &status0), TAG, "read status0 failed");
    ESP_RETURN_ON_ERROR(axp2101_read_u8(AXP2101_REG_STATUS1, &status1), TAG, "read status1 failed");

    snapshot->vbus_good = (status0 & AXP2101_STATUS0_VBUS_GOOD) != 0;
    snapshot->battery_present = (status0 & AXP2101_STATUS0_BAT_PRESENT) != 0;
    snapshot->battfet_on = (status0 & AXP2101_STATUS0_BATFET_ON) != 0;
    snapshot->charging = (status1 & AXP2101_STATUS1_BAT_DIR_MASK) == AXP2101_STATUS1_BAT_DIR_CHARGE;
    snapshot->discharging = (status1 & AXP2101_STATUS1_BAT_DIR_MASK) == AXP2101_STATUS1_BAT_DIR_DISCHARGE;

    ESP_RETURN_ON_ERROR(axp2101_read_bytes(AXP2101_REG_BATTERY_H, raw, sizeof(raw)), TAG, "read battery failed");
    snapshot->battery_mv = axp2101_decode_14bit(raw[0], raw[1]);

    ESP_RETURN_ON_ERROR(axp2101_read_bytes(AXP2101_REG_VBUS_H, raw, sizeof(raw)), TAG, "read vbus failed");
    snapshot->vbus_mv = axp2101_decode_14bit(raw[0], raw[1]);

    ESP_RETURN_ON_ERROR(axp2101_read_bytes(AXP2101_REG_VSYS_H, raw, sizeof(raw)), TAG, "read vsys failed");
    snapshot->vsys_mv = axp2101_decode_14bit(raw[0], raw[1]);

    ESP_RETURN_ON_ERROR(axp2101_read_u8(AXP2101_REG_BAT_PERCENT, (uint8_t *)&snapshot->battery_percent), TAG, "read battery percent failed");
    if (snapshot->battery_percent > 100) {
        snapshot->battery_percent = -1;
    }

    return ESP_OK;
}

esp_err_t axp2101_read_irq_status(axp2101_irq_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(axp2101_init(), TAG, "init failed before irq read");
    ESP_RETURN_ON_ERROR(axp2101_read_u8(AXP2101_REG_IRQ0, &status->irq0), TAG, "read irq0 failed");
    ESP_RETURN_ON_ERROR(axp2101_read_u8(AXP2101_REG_IRQ1, &status->irq1), TAG, "read irq1 failed");
    ESP_RETURN_ON_ERROR(axp2101_read_u8(AXP2101_REG_IRQ2, &status->irq2), TAG, "read irq2 failed");
    return ESP_OK;
}

esp_err_t axp2101_clear_irq_status(const axp2101_irq_status_t *status)
{
    uint8_t buffer[2];

    if (status == NULL || s_dev_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    buffer[0] = AXP2101_REG_IRQ0;
    buffer[1] = status->irq0;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev_handle, buffer, sizeof(buffer), 50), TAG, "clear irq0 failed");

    buffer[0] = AXP2101_REG_IRQ1;
    buffer[1] = status->irq1;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev_handle, buffer, sizeof(buffer), 50), TAG, "clear irq1 failed");

    buffer[0] = AXP2101_REG_IRQ2;
    buffer[1] = status->irq2;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev_handle, buffer, sizeof(buffer), 50), TAG, "clear irq2 failed");

    return ESP_OK;
}
```

- [ ] **Step 5: 运行 `AXP2101` 组件测试，确认只读契约变绿**

Run:

```powershell
uv run python -m unittest tests.test_axp2101_power_source -v
```

Expected:
- `tests.test_axp2101_power_source` 全部通过

- [ ] **Step 6: 提交 `AXP2101` 组件**

```bash
git add components/axp2101 tests/test_axp2101_power_source.py
git commit -m "功能: 增加 AXP2101 只读电源组件"
```

### Task 3: 实现 `board_power` 板级状态映射

**Files:**
- Create: `D:\esp32S3\111\main\app\board_power.h`
- Create: `D:\esp32S3\111\main\app\board_power.c`
- Test: `D:\esp32S3\111\tests\test_board_power_source.py`

- [ ] **Step 1: 创建头文件并锁定状态模型**

创建 `D:\esp32S3\111\main\app\board_power.h`：

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool available;
    bool battery_data_valid;
    bool snapshot_stale;
    bool charging;
    bool discharging;
    bool external_power_present;
    bool battery_present;
    uint16_t battery_mv;
    uint16_t system_mv;
    uint8_t battery_percent;
} board_power_state_t;

esp_err_t board_power_init(void);
esp_err_t board_power_refresh(board_power_state_t *state);
const board_power_state_t *board_power_get_cached_state(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 创建源文件并实现状态映射**

创建 `D:\esp32S3\111\main\app\board_power.c`：

```c
#include "board_power.h"

#include <string.h>

#include "axp2101.h"
#include "esp_log.h"

static const char *TAG = "BOARD_POWER";

static bool s_initialized = false;
static board_power_state_t s_cached_state = {0};

static void board_power_apply_snapshot(const axp2101_snapshot_t *snapshot,
                                       board_power_state_t *state)
{
    memset(state, 0, sizeof(*state));

    state->available = true;
    state->snapshot_stale = false;
    state->charging = snapshot->charging;
    state->discharging = snapshot->discharging;
    state->external_power_present = snapshot->vbus_good;
    state->battery_present = snapshot->battery_present;
    state->battery_mv = snapshot->battery_mv;
    state->system_mv = snapshot->vsys_mv;

    if (snapshot->battery_present && snapshot->battery_percent >= 0 &&
        snapshot->battery_percent <= 100 && snapshot->battery_mv > 0) {
        state->battery_data_valid = true;
        state->battery_percent = (uint8_t)snapshot->battery_percent;
    } else {
        state->battery_data_valid = false;
        state->battery_percent = 0;
    }
}

esp_err_t board_power_init(void)
{
    esp_err_t ret;
    bool present = false;

    if (s_initialized) {
        return ESP_OK;
    }

    ret = axp2101_probe(&present);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 probe failed: %s", esp_err_to_name(ret));
        memset(&s_cached_state, 0, sizeof(s_cached_state));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "AXP2101 present: %d", present ? 1 : 0);
    return ESP_OK;
}

esp_err_t board_power_refresh(board_power_state_t *state)
{
    axp2101_snapshot_t snapshot = {0};
    esp_err_t ret;

    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = axp2101_read_snapshot(&snapshot);
    if (ret != ESP_OK) {
        if (s_cached_state.available) {
            s_cached_state.snapshot_stale = true;
            *state = s_cached_state;
        } else {
            memset(state, 0, sizeof(*state));
        }
        return ret;
    }

    board_power_apply_snapshot(&snapshot, &s_cached_state);
    *state = s_cached_state;
    return ESP_OK;
}

const board_power_state_t *board_power_get_cached_state(void)
{
    return &s_cached_state;
}
```

- [ ] **Step 3: 运行 `board_power` 测试，确认状态模型没有混入 `low_battery`**

Run:

```powershell
uv run python -m unittest tests.test_board_power_source -v
```

Expected:
- `tests.test_board_power_source` 全部通过

- [ ] **Step 4: 提交 `board_power`**

```bash
git add main/app/board_power.h main/app/board_power.c tests/test_board_power_source.py
git commit -m "功能: 增加板级电源状态映射"
```

### Task 4: 实现 `power_service` 低频轮询与失败退避

**Files:**
- Create: `D:\esp32S3\111\main\services\power_service.h`
- Create: `D:\esp32S3\111\main\services\power_service.c`
- Test: `D:\esp32S3\111\tests\test_power_service_source.py`

- [ ] **Step 1: 创建头文件**

创建 `D:\esp32S3\111\main\services\power_service.h`：

```c
#pragma once

#include "esp_err.h"
#include "app/board_power.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*power_state_changed_cb_t)(const board_power_state_t *state);

esp_err_t power_service_init(void);
esp_err_t power_service_start(void);
void power_service_register_callback(power_state_changed_cb_t cb);
const board_power_state_t *power_service_get_state(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 创建服务实现**

创建 `D:\esp32S3\111\main\services\power_service.c`：

```c
#include "services/power_service.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "POWER_SERVICE";

static TaskHandle_t s_power_task_handle = NULL;
static board_power_state_t s_service_state = {0};
static power_state_changed_cb_t s_callback = NULL;
static TickType_t s_last_error_log_tick = 0;
static uint32_t s_failure_count = 0;

static bool power_service_state_changed(const board_power_state_t *lhs,
                                        const board_power_state_t *rhs)
{
    return memcmp(lhs, rhs, sizeof(*lhs)) != 0;
}

static void power_service_task(void *arg)
{
    (void)arg;

    while (1) {
        board_power_state_t next_state = {0};
        esp_err_t ret = board_power_refresh(&next_state);
        TickType_t delay_ticks = pdMS_TO_TICKS(1000);

        if (ret == ESP_OK) {
            s_failure_count = 0;
            if (power_service_state_changed(&s_service_state, &next_state)) {
                s_service_state = next_state;
                if (s_callback != NULL) {
                    s_callback(&s_service_state);
                }
                ESP_LOGI(TAG,
                         "power state: ext=%d chg=%d dis=%d valid=%d stale=%d bat=%u%% bat_mv=%u vsys_mv=%u",
                         s_service_state.external_power_present ? 1 : 0,
                         s_service_state.charging ? 1 : 0,
                         s_service_state.discharging ? 1 : 0,
                         s_service_state.battery_data_valid ? 1 : 0,
                         s_service_state.snapshot_stale ? 1 : 0,
                         (unsigned)s_service_state.battery_percent,
                         (unsigned)s_service_state.battery_mv,
                         (unsigned)s_service_state.system_mv);
            } else {
                s_service_state = next_state;
            }
        } else {
            s_failure_count++;
            s_service_state = *board_power_get_cached_state();
            if ((xTaskGetTickCount() - s_last_error_log_tick) >= pdMS_TO_TICKS(5000)) {
                ESP_LOGW(TAG, "board power refresh failed: %s", esp_err_to_name(ret));
                s_last_error_log_tick = xTaskGetTickCount();
            }
            delay_ticks = s_failure_count >= 3 ? pdMS_TO_TICKS(5000) : pdMS_TO_TICKS(2000);
        }

        vTaskDelay(delay_ticks);
    }
}

esp_err_t power_service_init(void)
{
    s_service_state = *board_power_get_cached_state();
    return ESP_OK;
}

esp_err_t power_service_start(void)
{
    if (s_power_task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(power_service_task, "power_service",
                                            4096, NULL, 4, &s_power_task_handle, 0);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

void power_service_register_callback(power_state_changed_cb_t cb)
{
    s_callback = cb;
}

const board_power_state_t *power_service_get_state(void)
{
    return &s_service_state;
}
```

- [ ] **Step 3: 运行 `power_service` 测试，确认轮询周期和退避文本已经到位**

Run:

```powershell
uv run python -m unittest tests.test_power_service_source -v
```

Expected:
- `tests.test_power_service_source` 全部通过

- [ ] **Step 4: 提交 `power_service`**

```bash
git add main/services/power_service.h main/services/power_service.c tests/test_power_service_source.py
git commit -m "功能: 增加电源状态轮询服务"
```

### Task 5: 把新模块接入主工程并锁定集成点

**Files:**
- Modify: `D:\esp32S3\111\main\CMakeLists.txt`
- Modify: `D:\esp32S3\111\main\app\hardware_init.c`
- Modify: `D:\esp32S3\111\main\app\app_main.c`
- Test: `D:\esp32S3\111\tests\test_power_integration_source.py`

- [ ] **Step 1: 先运行集成测试，确认当前接缝还不存在**

Run:

```powershell
uv run python -m unittest tests.test_power_integration_source -v
```

Expected:
- 至少 1 个失败
- 失败信息指向 `board_power_init()`、`power_service_start()` 或 `main/CMakeLists.txt` 缺失

- [ ] **Step 2: 修改 `main/CMakeLists.txt` 注册新源码和依赖**

将 `D:\esp32S3\111\main\CMakeLists.txt` 改成下面这些关键块：

```cmake
set(app_srcs
    ${CMAKE_CURRENT_LIST_DIR}/app/app_main.c
    ${CMAKE_CURRENT_LIST_DIR}/app/hardware_init.c
    ${CMAKE_CURRENT_LIST_DIR}/app/board_power.c
)

set(service_srcs
    ${CMAKE_CURRENT_LIST_DIR}/services/network_service.c
    ${CMAKE_CURRENT_LIST_DIR}/services/official_chat_service.c
    ${CMAKE_CURRENT_LIST_DIR}/services/power_service.c
)
```

并在 `idf_component_register(... REQUIRES ...)` 中加入：

```cmake
        axp2101
```

- [ ] **Step 3: 在 `hardware_init.c` 中加入非致命的板级电源初始化**

在 `D:\esp32S3\111\main\app\hardware_init.c` 里加入：

```c
#include "board_power.h"
```

并在音频初始化成功或失败分支之后、`button_init()` 之前插入：

```c
    ESP_LOGI(TAG, "Initializing Board Power...");
    ret = board_power_init();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Board power init failed: %s", esp_err_to_name(ret));
    }
```

要求：
- 保持非致命
- 不把 `board_power_refresh()` 放进开机阻塞路径

- [ ] **Step 4: 在 `app_main.c` 中启动 `power_service`**

在 `D:\esp32S3\111\main\app\app_main.c` 顶部加入：

```c
#include "services/power_service.h"
```

并在 `lvgl_task` 创建之后、`network_service_start()` 之前插入：

```c
        if (power_service_init() != ESP_OK)
        {
            ESP_LOGW("MAIN", "Power service init failed");
        }
        else if (power_service_start() != ESP_OK)
        {
            ESP_LOGW("MAIN", "Power service start failed");
        }
```

- [ ] **Step 5: 重新运行集成测试，确认集成点全部变绿**

Run:

```powershell
uv run python -m unittest tests.test_power_integration_source -v
```

Expected:
- `tests.test_power_integration_source` 全部通过

- [ ] **Step 6: 提交主工程接入**

```bash
git add main/CMakeLists.txt main/app/hardware_init.c main/app/app_main.c tests/test_power_integration_source.py
git commit -m "功能: 接入 AXP2101 电源组件主链路"
```

### Task 6: 运行完整验证并更新上下文记录

**Files:**
- Modify: `D:\esp32S3\111\docs\context\CHANGELOG.md`
- Check: `D:\esp32S3\111\components\axp2101\axp2101.c`
- Check: `D:\esp32S3\111\main\app\board_power.c`
- Check: `D:\esp32S3\111\main\services\power_service.c`

- [ ] **Step 1: 跑新增源码契约测试全集**

Run:

```powershell
uv run python -m unittest `
  tests.test_axp2101_power_source `
  tests.test_board_power_source `
  tests.test_power_service_source `
  tests.test_power_integration_source -v
```

Expected:
- 四组测试全部通过

- [ ] **Step 2: 在 ESP-IDF 环境下做完整构建**

Run:

```powershell
& "D:\esp-idf\v5.5.3\esp-idf\export.ps1"
idf.py build
```

Expected:
- 构建成功
- 输出包含 `Project build complete.`

- [ ] **Step 3: 更新上下文变更记录**

在 `D:\esp32S3\111\docs\context\CHANGELOG.md` 追加：

```markdown
- 2026-04-10：落地 AXP2101 第一阶段只读电源基座，新增 `components/axp2101`、`board_power`、`power_service`，并将电源状态接入主启动链路。
```

- [ ] **Step 4: 重新跑上下文索引和质量检查**

Run:

```powershell
uv run python scripts/context/build_index.py
uv run python scripts/context/check.py
```

Expected:
- `build_index.py` 成功更新索引
- `check.py` 输出 `错误: 0`

- [ ] **Step 5: 做实现完成前的人工核对**

逐项核对：

```text
1. AXP2101 组件只读 API 已落地，没有开放电源轨控制、sleep/wakeup 或充电参数写接口
2. board_power 只保留事实层字段，包含 battery_data_valid 和 snapshot_stale，不包含 low_battery
3. power_service 使用 1s 轮询，失败后退避到 2s/5s，并做错误日志节流
4. hardware_init.c 里 board_power_init() 是非致命初始化
5. app_main.c 里 power_service_start() 在 lvgl_task 之后启动
6. idf.py build 成功
```

- [ ] **Step 6: 最终提交**

```bash
git add components/axp2101 main/CMakeLists.txt main/app/board_power.h main/app/board_power.c main/app/hardware_init.c main/app/app_main.c main/services/power_service.h main/services/power_service.c tests/main_paths.py tests/test_axp2101_power_source.py tests/test_board_power_source.py tests/test_power_service_source.py tests/test_power_integration_source.py docs/context/CHANGELOG.md
git commit -m "功能: 落地 AXP2101 第一阶段只读电源基座"
```
