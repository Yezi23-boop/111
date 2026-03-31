# I2C Manager New Driver Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将当前仓库的 `i2c_manager` 切换到 `idf-xiaozhi` 新版 master bus 接口，并同步让 `audio_codec` 使用共享 bus handle，更新测试和上下文记录。

**Architecture:** 保留当前共享 I2C 总线的板级参数不变，只替换 `i2c_manager` 的驱动模式和公开接口。`audio_codec` 在 `ESP-IDF >= 5.3` 下显式注入 `i2c_manager_get_bus_handle()`，`touch_ft5x06` 先不重构为设备句柄模式，仅在编译失败时做最小兼容修补。

**Tech Stack:** ESP-IDF 5.5.x、`driver/i2c.h`、`driver/i2c_master.h`、`esp_driver_i2c`、Python `unittest`

---

### Task 1: 替换 `i2c_manager` 组件为新版接口

**Files:**
- Modify: `D:\esp32S3\111\components\i2c_manager\CMakeLists.txt`
- Modify: `D:\esp32S3\111\components\i2c_manager\include\i2c_manager.h`
- Modify: `D:\esp32S3\111\components\i2c_manager\i2c_manager.c`
- Reference: `C:\Users\ye\Desktop\idf-xiaozhi\components\i2c_manager\CMakeLists.txt`
- Reference: `C:\Users\ye\Desktop\idf-xiaozhi\components\i2c_manager\include\i2c_manager.h`
- Reference: `C:\Users\ye\Desktop\idf-xiaozhi\components\i2c_manager\i2c_manager.c`

- [ ] **Step 1: 写失败测试，先锁定新版头文件契约**

在 `D:\esp32S3\111\tests\test_audio_codec_port_source.py` 里把旧断言改成下面的目标断言草稿，先不要改生产代码：

```python
    def test_audio_codec_uses_new_i2c_manager_bus_handle_contract(self) -> None:
        i2c_header = I2C_MANAGER_HEADER.read_text(encoding="utf-8")
        audio_codec = AUDIO_CODEC_SOURCE.read_text(encoding="utf-8")
        self.assertIn("i2c_manager_get_bus_handle", i2c_header)
        self.assertIn("i2c_manager_get_bus_handle()", audio_codec)
```

- [ ] **Step 2: 运行测试，确认它因旧接口缺失而失败**

Run:

```powershell
python -m unittest tests.test_audio_codec_port_source -v
```

Expected:
- `test_audio_codec_uses_new_i2c_manager_bus_handle_contract` 失败
- 失败信息包含 `i2c_manager_get_bus_handle` not found

- [ ] **Step 3: 最小实现新版 `i2c_manager` 头文件和依赖**

将 `CMakeLists.txt` 改成：

```cmake
idf_component_register(
    SRCS "i2c_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c
)
```

将 `include/i2c_manager.h` 改成：

```c
#pragma once

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_idf_version.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#include "driver/i2c_master.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define I2C_MANAGER_PORT I2C_NUM_0
#define I2C_MANAGER_SCL_GPIO 14
#define I2C_MANAGER_SDA_GPIO 15
#define I2C_MANAGER_FREQ_HZ 400000

esp_err_t i2c_manager_init(void);
i2c_port_t i2c_manager_get_port(void);
esp_err_t i2c_manager_deinit(void);
esp_err_t i2c_manager_scan(void);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
i2c_master_bus_handle_t i2c_manager_get_bus_handle(void);
#endif

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: 最小实现新版 `i2c_manager.c`**

将 `D:\esp32S3\111\components\i2c_manager\i2c_manager.c` 对齐为新版 master bus 方案，核心实现至少包含：

```c
static bool s_ready = false;
static const i2c_port_t s_i2c_port = I2C_MANAGER_PORT;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
static i2c_master_bus_handle_t s_bus_handle = NULL;
#endif

esp_err_t i2c_manager_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = s_i2c_port,
        .scl_io_num = I2C_MANAGER_SCL_GPIO,
        .sda_io_num = I2C_MANAGER_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus_handle), TAG,
                        "new master bus failed");
#else
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MANAGER_SDA_GPIO,
        .scl_io_num = I2C_MANAGER_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = I2C_MANAGER_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(s_i2c_port, &conf), TAG,
                        "legacy param config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(s_i2c_port, conf.mode, 0, 0, 0),
                        TAG, "legacy driver install failed");
#endif

    s_ready = true;
    return ESP_OK;
}
```

并补齐：
- `i2c_manager_scan()` 在新驱动下使用 `i2c_master_probe(s_bus_handle, addr, 50)`
- `i2c_manager_deinit()` 在新驱动下调用 `i2c_del_master_bus(s_bus_handle)`
- `i2c_manager_get_bus_handle()` 在未初始化时返回 `NULL`

- [ ] **Step 5: 运行测试，确认头文件契约部分开始变绿或只剩 `audio_codec` 断言失败**

Run:

```powershell
python -m unittest tests.test_audio_codec_port_source -v
```

Expected:
- `i2c_manager_get_bus_handle` 已经能在头文件中找到
- 若仍失败，应主要集中在 `audio_codec.c` 尚未接入新接口

- [ ] **Step 6: 提交这个任务**

```bash
git add components/i2c_manager/CMakeLists.txt components/i2c_manager/include/i2c_manager.h components/i2c_manager/i2c_manager.c tests/test_audio_codec_port_source.py
git commit -m "feat: 切换 i2c_manager 到新版主总线接口"
```

### Task 2: 让 `audio_codec` 接入共享 I2C bus handle

**Files:**
- Modify: `D:\esp32S3\111\components\audio_codec\audio_codec.c`
- Test: `D:\esp32S3\111\tests\test_audio_codec_port_source.py`
- Reference: `C:\Users\ye\Desktop\idf-xiaozhi\components\audio_codec\audio_codec.c`

- [ ] **Step 1: 写失败测试，锁定 `audio_codec` 需要使用 bus handle**

在 `tests/test_audio_codec_port_source.py` 中保留下面断言：

```python
        self.assertIn("i2c_manager_get_bus_handle()", audio_codec)
```

如果当前测试文件中还存在旧的禁止断言，先删掉：

```python
        self.assertNotIn("i2c_manager_get_bus_handle", i2c_header)
        self.assertNotIn("i2c_manager_get_bus_handle", audio_codec)
```

- [ ] **Step 2: 运行测试，确认它因为 `audio_codec.c` 未使用新接口而失败**

Run:

```powershell
python -m unittest tests.test_audio_codec_port_source -v
```

Expected:
- 失败信息指向 `audio_codec.c` 缺少 `i2c_manager_get_bus_handle()`

- [ ] **Step 3: 在 `audio_codec.c` 中做最小实现，避免覆盖已有本地改动**

只修改 `audio_codec_new_shared_i2c_ctrl()`，目标形态如下：

```c
static const audio_codec_ctrl_if_t *audio_codec_new_shared_i2c_ctrl(uint8_t addr)
{
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_MANAGER_PORT,
        .addr = addr,
    };

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    i2c_cfg.bus_handle = i2c_manager_get_bus_handle();
    if (i2c_cfg.bus_handle == NULL)
    {
        ESP_LOGE(TAG, "I2C master bus handle is NULL");
        return NULL;
    }
#endif

    return audio_codec_new_i2c_ctrl(&i2c_cfg);
}
```

要求：
- 不重排整个文件
- 不覆盖现有 `audio_codec` 的其他用户改动
- 保持 `audio_i2c_init()` 仍然先调用 `i2c_manager_init()`

- [ ] **Step 4: 运行测试，确认源码契约通过**

Run:

```powershell
python -m unittest tests.test_audio_codec_port_source -v
```

Expected:
- `tests.test_audio_codec_port_source` 全绿

- [ ] **Step 5: 如测试失败，优先检查字符串和宏分支是否与断言一致**

重点检查：

```c
i2c_cfg.bus_handle = i2c_manager_get_bus_handle();
```

不要用其他命名或包装函数替代这行字面串，否则当前契约测试会继续失败。

- [ ] **Step 6: 提交这个任务**

```bash
git add components/audio_codec/audio_codec.c tests/test_audio_codec_port_source.py
git commit -m "feat: audio codec 接入共享 i2c bus handle"
```

### Task 3: 做最小编译验证并处理触摸兼容性

**Files:**
- Check: `D:\esp32S3\111\components\touch_ft5x06\touch_ft5x06.c`
- Check: `D:\esp32S3\111\main\hardware_init.c`
- Check: `D:\esp32S3\111\components\i2c_manager\include\i2c_manager.h`

- [ ] **Step 1: 先跑源码契约测试，确认 Python 侧已经绿**

Run:

```powershell
python -m unittest tests.test_audio_codec_port_source -v
```

Expected:
- 0 failures

- [ ] **Step 2: 运行完整构建，查 I2C 类型冲突或链接问题**

Run:

```powershell
idf.py build
```

Expected:
- 构建成功，退出码 `0`
- 没有 `i2c_master_bus_handle_t` 未定义、`esp_driver_i2c` 缺失、或新旧驱动冲突错误

- [ ] **Step 3: 如果构建失败且指向 `touch_ft5x06.c`，只做最小兼容修补**

允许的最小修补方向：

```c
#include "driver/i2c.h"

static esp_err_t touch_ft5x06_i2c_read(...)
{
    i2c_port_t port = i2c_manager_get_port();
    ...
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(500));
    ...
}
```

规则：
- 不在本轮把触摸整体重构成 `i2c_master_dev_handle_t`
- 只修到“与新版 `i2c_manager` 共存且能编译”

- [ ] **Step 4: 重新构建，确认兼容修补有效**

Run:

```powershell
idf.py build
```

Expected:
- 构建成功

- [ ] **Step 5: 提交这个任务**

```bash
git add components/touch_ft5x06/touch_ft5x06.c main/hardware_init.c
git commit -m "fix: 兼容新版 i2c_manager 构建链路"
```

只在确实修改了这些文件时提交。

### Task 4: 更新上下文知识和变更记录

**Files:**
- Create: `D:\esp32S3\111\docs\context\knowledge\project\i2c-manager-master-bus-migration.md`
- Modify: `D:\esp32S3\111\docs\context\CHANGELOG.md`

- [ ] **Step 1: 写知识卡，固化这次迁移的适用条件和边界**

新建 `docs/context/knowledge/project/i2c-manager-master-bus-migration.md`，至少包含下面内容：

```markdown
---
id: i2c-manager-master-bus-migration
tags: project, i2c, esp-idf, audio, touch
summary: 当前仓库 i2c_manager 切换到 master bus 接口后的调用约束与兼容边界。
last_reviewed: 2026-03-31
---

# i2c_manager master bus 迁移

- `i2c_manager` 在 `ESP-IDF >= 5.3` 下暴露 `i2c_manager_get_bus_handle()`
- `audio_codec` 通过共享 `bus_handle` 创建 codec control interface
- `touch_ft5x06` 本轮仍使用 legacy command-link 读寄存器，后续如继续统一，再迁移到设备句柄模式
- 共享总线仍为 `GPIO14/GPIO15`，触摸与音频控制面共用
```

- [ ] **Step 2: 在 `CHANGELOG.md` 追加一行**

追加：

```markdown
- 2026-03-31：将 `i2c_manager` 迁移到新版 master bus 接口，同步让 `audio_codec` 使用共享 `bus_handle`，并更新 I2C 迁移知识卡。
```

- [ ] **Step 3: 运行上下文检查**

Run:

```powershell
uv run python scripts/context/check.py
```

Expected:
- `错误: 0`

- [ ] **Step 4: 提交文档**

```bash
git add docs/context/knowledge/project/i2c-manager-master-bus-migration.md docs/context/CHANGELOG.md
git commit -m "docs: 记录 i2c manager 主总线迁移约束"
```

### Task 5: 最终验证与收尾

**Files:**
- Check: `D:\esp32S3\111\components\i2c_manager\include\i2c_manager.h`
- Check: `D:\esp32S3\111\components\audio_codec\audio_codec.c`
- Check: `D:\esp32S3\111\tests\test_audio_codec_port_source.py`
- Check: `D:\esp32S3\111\docs\context\knowledge\project\i2c-manager-master-bus-migration.md`

- [ ] **Step 1: 跑最终源码契约测试**

Run:

```powershell
python -m unittest tests.test_audio_codec_port_source -v
```

Expected:
- 全部通过

- [ ] **Step 2: 跑最终构建**

Run:

```powershell
idf.py build
```

Expected:
- 构建成功

- [ ] **Step 3: 跑上下文检查**

Run:

```powershell
uv run python scripts/context/check.py
```

Expected:
- `错误: 0，警告:` 允许为 `0` 或已有已知非阻塞值

- [ ] **Step 4: 人工复核完成判据**

检查以下事实是否都成立：

```text
1. i2c_manager 已暴露 i2c_manager_get_bus_handle()
2. audio_codec 已调用 i2c_manager_get_bus_handle()
3. tests.test_audio_codec_port_source 全绿
4. idf.py build 成功
5. docs/context 已补充知识卡和 changelog
```

- [ ] **Step 5: 最终提交**

```bash
git add components/i2c_manager components/audio_codec/audio_codec.c tests/test_audio_codec_port_source.py docs/context/knowledge/project/i2c-manager-master-bus-migration.md docs/context/CHANGELOG.md
git commit -m "feat: 迁移共享 i2c 总线到新版主总线接口"
```
